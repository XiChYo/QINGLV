// annotated_frame_sink.cpp — 见头文件。
#include "annotated_frame_sink.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QtDebug>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "offline_camera_driver.h"

namespace offline_sim {

namespace {

QColor colorForTrack(int trackId)
{
    if (trackId < 0) return QColor(160, 160, 160);
    static const QColor palette[] = {
        QColor(220, 50,  50),  QColor(50,  180, 50),  QColor(50, 100, 220),
        QColor(220, 180, 50),  QColor(180, 50,  200), QColor(50, 200, 200),
        QColor(255, 130, 50),  QColor(120, 220, 50),  QColor(220, 80, 160),
        QColor(80,  220, 160), QColor(160, 80,  220),
    };
    const int n = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
    return palette[trackId % n];
}

// cv::Mat(BGR) → QImage(RGB888,自有数据);copy() 一份避免悬挂引用。
QImage cvMatBgrToQImage(const cv::Mat& bgr)
{
    if (bgr.empty()) return QImage();
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows,
                  static_cast<int>(rgb.step), QImage::Format_RGB888).copy();
}

// QImage → cv::Mat(BGR);先 toFormat(RGB888) 保证连续内存与字节序。
cv::Mat qImageToCvMatBgr(const QImage& img)
{
    QImage rgb = img.convertToFormat(QImage::Format_RGB888);
    cv::Mat tmp(rgb.height(), rgb.width(), CV_8UC3,
                const_cast<uchar*>(rgb.constBits()),
                static_cast<size_t>(rgb.bytesPerLine()));
    cv::Mat bgr;
    cv::cvtColor(tmp, bgr, cv::COLOR_RGB2BGR);
    return bgr;
}

// 在 BGR cv::Mat 上把每个 det 的 mask 半透明叠色。处理 bbox 越图像边界 &
// mask.size != bbox.size 的情况(YoloWorker 极少越界,但兜底)。
void overlayMasks(cv::Mat& bgr,
                  const QVector<DetectedObject>& objs,
                  const QVector<DetTrackBinding>& bindings)
{
    const cv::Rect imgRect(0, 0, bgr.cols, bgr.rows);
    for (int i = 0; i < objs.size(); ++i) {
        const DetectedObject& d = objs[i];
        if (d.maskPx.empty()) continue;

        const cv::Rect inter = d.bboxPx & imgRect;
        if (inter.empty()) continue;

        const int offX = inter.x - d.bboxPx.x;
        const int offY = inter.y - d.bboxPx.y;
        cv::Mat maskFull;
        if (d.maskPx.size() == d.bboxPx.size()) {
            maskFull = d.maskPx;
        } else {
            cv::resize(d.maskPx, maskFull, d.bboxPx.size(), 0, 0, cv::INTER_NEAREST);
        }
        if (offX < 0 || offY < 0 ||
            offX + inter.width  > maskFull.cols ||
            offY + inter.height > maskFull.rows) {
            continue;  // 守门防 ROI 越界
        }
        cv::Mat maskRoi = maskFull(cv::Rect(offX, offY, inter.width, inter.height));

        const int trackId = (i < bindings.size()) ? bindings[i].trackId : -1;
        const cv::Scalar bgrColor = AnnotatedFrameSink::colorForTrackBgr(trackId);

        cv::Mat roi    = bgr(inter);
        cv::Mat colored(inter.size(), CV_8UC3, bgrColor);
        cv::Mat blended;
        cv::addWeighted(roi, 0.55, colored, 0.45, 0, blended);
        blended.copyTo(roi, maskRoi);
    }
}

}  // namespace

cv::Scalar AnnotatedFrameSink::colorForTrackBgr(int trackId)
{
    const QColor c = colorForTrack(trackId);
    return cv::Scalar(c.blue(), c.green(), c.red());
}

// ============================================================================
AnnotatedFrameSink::AnnotatedFrameSink(OfflineCameraDriver* cam,
                                       const RuntimeConfig& cfg,
                                       const QString& outDir,
                                       const QString& assocCsvPath,
                                       QObject* parent)
    : QObject(parent), m_cam(cam), m_outDir(outDir), m_assocPath(assocCsvPath)
{
    for (const auto& cb : cfg.classButtons) {
        if (cb.classId >= 0) m_id2nameCn.insert(cb.classId, cb.name);
    }
}

AnnotatedFrameSink::~AnnotatedFrameSink() { close(); }

bool AnnotatedFrameSink::open(QString* errMsg)
{
    close();
    QDir d(m_outDir);
    if (!d.exists() && !d.mkpath(".")) {
        if (errMsg) *errMsg = QStringLiteral("AnnotatedFrameSink: 无法创建 %1").arg(m_outDir);
        return false;
    }
    QFileInfo ai(m_assocPath);
    QDir adir = ai.dir();
    if (!adir.exists() && !adir.mkpath(".")) {
        if (errMsg) *errMsg = QStringLiteral(
            "AnnotatedFrameSink: 无法创建 frame_assoc 目录 %1").arg(adir.absolutePath());
        return false;
    }
    m_assocFile = new QFile(m_assocPath);
    if (!m_assocFile->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errMsg) *errMsg = QStringLiteral("AnnotatedFrameSink: 打开 %1 失败: %2").arg(
            m_assocPath, m_assocFile->errorString());
        delete m_assocFile;
        m_assocFile = nullptr;
        return false;
    }
    m_assocStream = new QTextStream(m_assocFile);
    m_assocStream->setCodec("UTF-8");
    writeHeader();
    return true;
}

void AnnotatedFrameSink::close()
{
    if (m_assocStream) {
        m_assocStream->flush();
        delete m_assocStream;
        m_assocStream = nullptr;
    }
    if (m_assocFile) {
        m_assocFile->close();
        delete m_assocFile;
        m_assocFile = nullptr;
    }
}

void AnnotatedFrameSink::writeHeader()
{
    if (!m_assocStream) return;
    *m_assocStream << "seqInWindow,tCaptureMs,detIndex,trackId,bestClassId,"
                   << "detClassId,confidence,bbox_x,bbox_y,bbox_w,bbox_h,"
                   << "suppressedByGhost,isNewTrack,firstFrameGhost\n";
    m_assocStream->flush();
}

QString AnnotatedFrameSink::classNameOf(int classId) const
{
    if (classId < 0) return QStringLiteral("?");
    const auto it = m_id2nameCn.find(classId);
    if (it == m_id2nameCn.end()) {
        return QStringLiteral("cls_%1").arg(classId);
    }
    return it.value();
}

// ============================================================================
// 双信号 join
// ============================================================================
void AnnotatedFrameSink::onDetectedFrame(const DetectedFrame& f)
{
    auto& p = m_pending[f.tCaptureMs];
    p.frame = f;
    p.detReceived = true;
    if (p.annReceived) tryRender(f.tCaptureMs);
}

void AnnotatedFrameSink::onFrameAnnotation(qint64 tCaptureMs,
                                           const QVector<DetTrackBinding>& bindings)
{
    auto& p = m_pending[tCaptureMs];
    p.bindings = bindings;
    p.annReceived = true;
    if (p.detReceived) tryRender(tCaptureMs);
}

void AnnotatedFrameSink::tryRender(qint64 tCaptureMs)
{
    auto it = m_pending.find(tCaptureMs);
    if (it == m_pending.end()) return;
    const Pending p = it.value();
    m_pending.erase(it);

    const int seqInWindow = m_seqInWindow++;
    renderOne(seqInWindow, tCaptureMs, p);
    writeAssocRows(seqInWindow, tCaptureMs, p);
}

// ============================================================================
// 渲染:cv::imread → mask 叠加 → QPainter 画 bbox + 中文文字 → cv::imwrite
// ============================================================================
void AnnotatedFrameSink::renderOne(int seqInWindow,
                                   qint64 tCaptureMs,
                                   const Pending& p)
{
    if (!m_cam) {
        qWarning() << "[AnnotatedFrameSink] m_cam is null";
        return;
    }
    const QString filePath = m_cam->filePathOf(tCaptureMs);
    if (filePath.isEmpty()) {
        qWarning().noquote() << QStringLiteral(
            "[AnnotatedFrameSink] filePathOf 空,tCap=%1 (driver R3 应已避免)")
            .arg(tCaptureMs);
        return;
    }
    cv::Mat bgr = cv::imread(filePath.toStdString(), cv::IMREAD_COLOR);
    if (bgr.empty()) {
        qWarning().noquote() << QStringLiteral(
            "[AnnotatedFrameSink] cv::imread 失败: %1").arg(filePath);
        return;
    }

    // 1. mask 半透明叠加(BGR cv::Mat 上做)
    overlayMasks(bgr, p.frame.objs, p.bindings);

    // 2. 转 QImage,QPainter 画 bbox + 文字(中文用 Qt 自带字体引擎)
    QImage qimg = cvMatBgrToQImage(bgr);
    {
        QPainter painter(&qimg);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        QFont font = painter.font();
        // 1024 输入图相对来说,28 px 字号读起来够大;如果原图分辨率更高,人眼也 OK。
        const int basePx = std::max(20, qimg.width() / 50);
        font.setPixelSize(basePx);
        painter.setFont(font);
        const QFontMetrics fm(font);

        for (int i = 0; i < p.frame.objs.size(); ++i) {
            const DetectedObject& d = p.frame.objs[i];
            const DetTrackBinding b = (i < p.bindings.size())
                                          ? p.bindings[i]
                                          : DetTrackBinding{};

            const QColor c = colorForTrack(b.trackId);
            QRect bbox(d.bboxPx.x, d.bboxPx.y, d.bboxPx.width, d.bboxPx.height);

            // bbox
            painter.setPen(QPen(c, 4));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(bbox);

            // 文字两行
            const QString trackTag = (b.trackId >= 0)
                                         ? QStringLiteral("T%1").arg(b.trackId)
                                         : QStringLiteral("T?");
            const QString line1 = QStringLiteral("%1 | best=%2").arg(
                trackTag, classNameOf(b.bestClassId));
            const QString line2 = QStringLiteral("yolo=%1 conf=%2").arg(
                classNameOf(d.classId),
                QString::number(d.confidence, 'f', 2));
            const int line1W = fm.horizontalAdvance(line1);
            const int line2W = fm.horizontalAdvance(line2);
            const int textW  = std::max({bbox.width(), line1W + 16, line2W + 16});
            const int lineH  = fm.height();
            const int blockH = lineH * 2 + 8;

            // 默认放在 bbox 上方;不够空间就放在 bbox 内顶部
            int blockY = bbox.top() - blockH - 4;
            if (blockY < 4) blockY = bbox.top() + 4;

            painter.fillRect(QRect(bbox.left(), blockY, textW, blockH),
                             QColor(0, 0, 0, 160));
            painter.setPen(c);
            painter.drawText(bbox.left() + 8, blockY + lineH - fm.descent(), line1);
            painter.drawText(bbox.left() + 8, blockY + lineH * 2 - fm.descent() + 4, line2);
        }
    }

    cv::Mat outBgr = qImageToCvMatBgr(qimg);
    const QString outPath = QStringLiteral("%1/%2_%3.jpg").arg(
        m_outDir,
        QString::number(seqInWindow).rightJustified(4, '0'),
        QString::number(tCaptureMs));
    if (!cv::imwrite(outPath.toStdString(), outBgr)) {
        qWarning().noquote() << QStringLiteral(
            "[AnnotatedFrameSink] cv::imwrite 失败: %1").arg(outPath);
    }
}

// ============================================================================
// frame_assoc.csv 写一帧的所有 det 行
// ============================================================================
void AnnotatedFrameSink::writeAssocRows(int seqInWindow,
                                        qint64 tCaptureMs,
                                        const Pending& p)
{
    if (!m_assocStream) return;
    const int n = p.frame.objs.size();
    for (int i = 0; i < n; ++i) {
        const DetectedObject& d = p.frame.objs[i];
        const DetTrackBinding b = (i < p.bindings.size()) ? p.bindings[i] : DetTrackBinding{};
        *m_assocStream << seqInWindow
                       << ',' << tCaptureMs
                       << ',' << i
                       << ',' << b.trackId
                       << ',' << b.bestClassId
                       << ',' << d.classId
                       << ',' << QString::number(d.confidence, 'f', 4)
                       << ',' << d.bboxPx.x
                       << ',' << d.bboxPx.y
                       << ',' << d.bboxPx.width
                       << ',' << d.bboxPx.height
                       << ',' << (b.suppressedByGhost ? 1 : 0)
                       << ',' << (b.isNewTrack       ? 1 : 0)
                       << ',' << (b.firstFrameGhost  ? 1 : 0)
                       << '\n';
    }
    // 该帧 0 det 也写一行哨兵,方便 N_yolo 计数(detIndex=-1)
    if (n == 0) {
        *m_assocStream << seqInWindow << ',' << tCaptureMs
                       << ",-1,-1,-1,-1,0.0000,0,0,0,0,0,0,0\n";
    }
    m_assocStream->flush();
}

}  // namespace offline_sim
