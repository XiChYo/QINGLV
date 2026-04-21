#include "yolo_worker.h"

#include <QByteArray>
#include <QDebug>
#include <opencv2/imgproc.hpp>

#include "logger.h"
#include "pipeline_clock.h"
#include "postprocess_ex.h"

namespace {

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
    sessionStop();
}

const char* YoloWorker::defaultLabelFromId(int id)
{
    (void)id;
    return nullptr;  // 留白。真实标签(中文)由 UI 层在 overlay 前注入或由 TrackerWorker 带出。
}

void YoloWorker::sessionStart(const RuntimeConfig& cfg)
{
    if (m_sessionValid) {
        LOG_INFO("YoloWorker::sessionStart called while session already valid; recreating");
        sessionStop();
    }

    m_modelPath      = cfg.modelPath;
    m_confThreshold  = cfg.confThreshold;
    m_nmsThreshold   = cfg.nmsThreshold;
    m_topkClassCount = cfg.modelTopkClassCount;
    m_drawOverlay    = cfg.drawOverlay;

    const QByteArray pathUtf8 = m_modelPath.toUtf8();
    if (!yolo_session_init(pathUtf8.constData(), m_session)) {
        LOG_ERROR(QString("YoloWorker: yolo_session_init fail path=%1").arg(m_modelPath));
        m_sessionValid = false;
        return;
    }
    m_sessionValid = true;
    LOG_INFO(QString("YoloWorker started model=%1 conf=%2 nms=%3")
             .arg(m_modelPath).arg(m_confThreshold).arg(m_nmsThreshold));
}

void YoloWorker::sessionStop()
{
    if (m_sessionValid) {
        yolo_session_release(m_session);
        m_sessionValid = false;
    }
}

void YoloWorker::onFrame(const QImage& img, qint64 tCaptureMs, const QString& fileName)
{
    (void)fileName;

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

    if (m_drawOverlay) {
        cv::Mat overlay = orig.clone();
        if (ok) draw_results_ex(overlay, segs, &YoloWorker::defaultLabelFromId);
        emit resultImgReady(bgrToQImage(overlay));
    } else {
        emit resultImgReady(bgrToQImage(orig));
    }

    emit frameConsumed();
}
