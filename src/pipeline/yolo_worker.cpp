#include "pipeline/yolo_worker.h"

#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <string>
#include <unordered_map>

#include "infra/logger.h"
#include "pipeline/pipeline_clock.h"
#include "pipeline/postprocess_ex.h"

namespace {

// 类别名注册表(轻量版):只存 ini 里实际配置的 classId → 中文名,
// 不再为未配置的 id 预留槽位。配置一般 <20 条,O(n) 哈希访问足够。
// 生命周期:与 YoloWorker 同线程,sessionStart 写,onFrame 读,同线程串行 → 无需锁。
// .c_str() 的有效期仅保证到下一次 setClassLabels 被调用之前;draw_results_ex
// 是同线程同步调用,不存在跨线程或跨帧持有指针的情况,安全。
std::unordered_map<int, std::string> g_labelTable;

void setClassLabels(const QVector<ClassButton>& classButtons)
{
    g_labelTable.clear();
    for (const auto& cb : classButtons) {
        if (cb.classId < 0) continue;
        g_labelTable[cb.classId] = cb.name.toUtf8().toStdString();
    }
}

cv::Mat qimageToBgr(const QImage& img)
{
    // QImage 深拷贝后再取指针,避免被上游复用的 buffer 改写。
    QImage conv = img.convertToFormat(QImage::Format_RGB888);
    cv::Mat mat(conv.height(), conv.width(), CV_8UC3,
                const_cast<uchar*>(conv.bits()), conv.bytesPerLine());
    cv::Mat out = mat.clone();
    cv::cvtColor(out, out, cv::COLOR_RGB2BGR);
    return out;
}

QImage bgrToQImage(const cv::Mat& bgr)
{
    if (bgr.type() != CV_8UC3) return QImage();
    return QImage(bgr.data, bgr.cols, bgr.rows, bgr.step,
                  QImage::Format_RGB888).rgbSwapped().copy();
}

// 用二值 mask 近似求分割重心(像素坐标)。
cv::Point2f segCenterPx(const SegObject& obj)
{
    if (!obj.mask.empty()) {
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(obj.mask.clone(), contours,
                         cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        double best_area = 0.0;
        int    best_idx  = -1;
        for (size_t i = 0; i < contours.size(); ++i) {
            const double a = cv::contourArea(contours[i]);
            if (a > best_area) { best_area = a; best_idx = (int)i; }
        }
        if (best_idx >= 0) {
            const cv::Moments m = cv::moments(contours[best_idx]);
            if (std::abs(m.m00) > 1e-6) {
                return cv::Point2f(
                    (float)(m.m10 / m.m00) + obj.box.x,
                    (float)(m.m01 / m.m00) + obj.box.y);
            }
        }
    }
    return cv::Point2f(obj.box.x + obj.box.width  * 0.5f,
                       obj.box.y + obj.box.height * 0.5f);
}

}  // namespace

YoloWorker::YoloWorker(QObject* parent) : QObject(parent) {}

YoloWorker::~YoloWorker()
{
    // 进程退出时释放常驻模型。
    if (m_sessionValid) {
        yolo_session_release(m_session);
        m_sessionValid = false;
    }
}

const char* YoloWorker::defaultLabelFromId(int id)
{
    const auto it = g_labelTable.find(id);
    if (it == g_labelTable.end() || it->second.empty()) return nullptr;
    return it->second.c_str();
}

void YoloWorker::preloadModel(const QString& modelPath)
{
    if (modelPath.isEmpty()) {
        LOG_ERROR("YoloWorker::preloadModel: empty path");
        return;
    }
    m_modelPath = modelPath;
    ensureModelLoaded();
}

void YoloWorker::ensureModelLoaded()
{
    // 设计 F8:模型进程级常驻。第一次 session 前懒加载一次,之后复用。
    if (m_sessionValid) return;
    if (m_modelPath.isEmpty()) return;

    const QByteArray pathUtf8 = m_modelPath.toUtf8();
    if (!yolo_session_init(pathUtf8.constData(), m_session)) {
        LOG_ERROR(QString("YoloWorker: yolo_session_init fail path=%1").arg(m_modelPath));
        m_sessionValid = false;
        return;
    }
    m_sessionValid = true;
    LOG_INFO(QString("YoloWorker model loaded (persistent) path=%1").arg(m_modelPath));
}

void YoloWorker::sessionStart(const RuntimeConfig& cfg)
{
    // 运行期参数:每次 session 都刷新。
    m_confThreshold  = cfg.confThreshold;
    m_nmsThreshold   = cfg.nmsThreshold;
    m_topkClassCount = cfg.modelTopkClassCount;
    m_drawOverlay    = cfg.drawOverlay;
    m_saveResult     = cfg.saveResult;
    m_saveDir        = cfg.saveDir;

    // 类别中文名:写进 overlay 查找表,替代数字 id。
    setClassLabels(cfg.classButtons);

    // 模型路径变化时才重载(设计上明确"不支持热切换",但这里仍做一次防御)。
    if (m_modelPath != cfg.modelPath) {
        if (m_sessionValid) {
            yolo_session_release(m_session);
            m_sessionValid = false;
            LOG_INFO("YoloWorker model path changed, reloading");
        }
        m_modelPath = cfg.modelPath;
    }
    ensureModelLoaded();

    LOG_INFO(QString("YoloWorker sessionStart model=%1 conf=%2 nms=%3 save_result=%4")
             .arg(m_modelPath).arg(m_confThreshold).arg(m_nmsThreshold)
             .arg(m_saveResult ? "1" : "0"));
}

void YoloWorker::sessionStop()
{
    // 仅停止会话级副作用(落盘等),模型保持常驻直到析构。
    m_saveResult = false;
    LOG_INFO("YoloWorker sessionStop (model stays resident)");
}

void YoloWorker::onFrame(const QImage& img, qint64 tCaptureMs, const QString& fileName)
{
    if (!m_sessionValid) {
        // 不丢消息,但也要解锁上游反压。
        emit frameConsumed();
        return;
    }
    if (img.isNull()) {
        emit frameConsumed();
        return;
    }

    const cv::Mat orig = qimageToBgr(img);
    if (orig.empty()) {
        emit frameConsumed();
        return;
    }

    std::vector<SegObject> segs;
    const bool ok = yolo_session_infer(m_session, orig,
                                       m_topkClassCount,
                                       m_confThreshold, m_nmsThreshold,
                                       segs);
    const qint64 tInferDone = pipeline::nowMs();

    DetectedFrame frame;
    frame.tCaptureMs   = tCaptureMs;
    frame.tInferDoneMs = tInferDone;
    frame.imgWidthPx   = orig.cols;
    frame.imgHeightPx  = orig.rows;
    frame.objs.reserve((int)segs.size());

    if (ok) {
        for (const auto& s : segs) {
            DetectedObject obj;
            obj.classId    = s.label;
            obj.confidence = s.prob;
            obj.bboxPx     = s.box;
            obj.maskPx     = s.mask;
            obj.centerPx   = segCenterPx(s);
            frame.objs.push_back(std::move(obj));
        }
    }

    emit detectedFrameReady(frame);

    cv::Mat overlay;
    if (m_drawOverlay) {
        overlay = orig.clone();
        if (ok) draw_results_ex(overlay, segs, &YoloWorker::defaultLabelFromId);
        emit resultImgReady(bgrToQImage(overlay));
    } else {
        emit resultImgReady(bgrToQImage(orig));
    }

    // F16 / AC12:fileName 非空表示 CameraWorker 本帧已落盘 raw;此处按同名落盘 result。
    // 命名约定:raw 文件名含扩展,result 文件改前缀 "result_"。
    if (m_saveResult && !fileName.isEmpty()) {
        QFileInfo fi(fileName);
        const QString dir  = fi.absolutePath();
        const QString base = fi.completeBaseName();
        const QString suffix = fi.suffix().isEmpty() ? QStringLiteral("jpg") : fi.suffix();
        QDir().mkpath(dir.isEmpty() ? m_saveDir : dir);
        const QString outPath = QStringLiteral("%1/result_%2.%3")
                                    .arg(dir.isEmpty() ? m_saveDir : dir)
                                    .arg(base).arg(suffix);
        const cv::Mat& toSave = m_drawOverlay ? overlay : orig;
        if (!toSave.empty()) {
            try {
                cv::imwrite(outPath.toStdString(), toSave);
            } catch (const cv::Exception& e) {
                LOG_ERROR(QString("YoloWorker: imwrite %1 failed: %2")
                          .arg(outPath).arg(e.what()));
            }
        }
    }

    emit frameConsumed();
}
