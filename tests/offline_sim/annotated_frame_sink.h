#ifndef OFFLINE_SIM_ANNOTATED_FRAME_SINK_H
#define OFFLINE_SIM_ANNOTATED_FRAME_SINK_H

// ============================================================================
// AnnotatedFrameSink:把"过 YOLO 的每一帧"叠加 mask + bbox + 类别中文 + trackId
// 后落盘,作为业务方人工抽查的主入口(M3,《测试需求.md》§1.3 #4 / §3.4 #4-#5)。
//
// 双信号 join 设计(R2 + 原 YoloWorker 信号):
//   入口 1:YoloWorker::detectedFrameReady(DetectedFrame) —— det 列表(maskPx /
//          bboxPx / classId / confidence)
//   入口 2:TrackerWorker::frameAnnotationReady(qint64 tCaptureMs,
//          QVector<DetTrackBinding>) —— 每个 det 的 trackId / bestClassId 等
//   两路用 tCaptureMs 当 key 缓存,到齐才出图;OfflineCameraDriver 的 R3 修复
//   保证 tCaptureMs 严格唯一不撞键。
//
// 出口产物:
//   1. annotated_frames/<seqInWindow>_<tCaptureMs>.jpg
//      - 每个 mask 半透明叠色(同一 trackId 同色,trackId=-1 灰色)
//      - bbox 矩形框(同色)
//      - 两行文字:行 1 = "T<id> | best=<bestNameCn>",行 2 = "yolo=<detNameCn>
//        conf=<f.2>";trackId=-1 时行 1 = "T? | best=<...>"
//   2. frame_assoc.csv 一行/det,列:
//      seqInWindow, tCaptureMs, detIndex, trackId, bestClassId, detClassId,
//      confidence, bbox_x, bbox_y, bbox_w, bbox_h,
//      suppressedByGhost, isNewTrack, firstFrameGhost
//
// 渲染管线(为了支持中文):
//   cv::imread 拿原图(BGR) → mask 半透明叠加在 cv::Mat 上(cv::addWeighted)
//   → cvMatToQImage → QPainter 画 bbox + 中文文字(Qt 自带字体引擎,无需 OpenCV
//   freetype 模块) → qImageToCvMat → cv::imwrite。
//
// 线程模型:driver / yolo / tracker 在各自线程,sink 通过 QueuedConnection 接两
// 路信号,sink 自己有独立线程或主线程都行(取决于 main.cpp 装配)。filePathOf
// 在 driver 内有 mutex 保护,sink 跨线程调用安全。
// ============================================================================

#include <QFile>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTextStream>
#include <opencv2/core.hpp>

#include "pipeline/pipeline_types.h"
#include "config/runtime_config.h"

namespace offline_sim {

class OfflineCameraDriver;

class AnnotatedFrameSink : public QObject
{
    Q_OBJECT
public:
    AnnotatedFrameSink(OfflineCameraDriver* cam,
                       const RuntimeConfig& cfg,
                       const QString& outDir,
                       const QString& assocCsvPath,
                       QObject* parent = nullptr);
    ~AnnotatedFrameSink() override;

    bool open(QString* errMsg = nullptr);
    void close();

    int renderedCount() const { return m_seqInWindow; }
    int pendingCount()  const { return m_pending.size(); }

    // public:供同 TU 匿名 namespace 的 overlayMasks() 复用同套调色板。
    static cv::Scalar colorForTrackBgr(int trackId);

public slots:
    void onDetectedFrame(const DetectedFrame& frame);
    void onFrameAnnotation(qint64 tCaptureMs, const QVector<DetTrackBinding>& bindings);

private:
    struct Pending {
        DetectedFrame             frame;
        QVector<DetTrackBinding>  bindings;
        bool                      detReceived = false;
        bool                      annReceived = false;
    };

    void writeHeader();
    void tryRender(qint64 tCaptureMs);
    void renderOne(int seqInWindow, qint64 tCaptureMs, const Pending& p);
    void writeAssocRows(int seqInWindow, qint64 tCaptureMs, const Pending& p);
    QString classNameOf(int classId) const;

    OfflineCameraDriver*     m_cam = nullptr;
    QHash<int, QString>      m_id2nameCn;
    QString                  m_outDir;
    QString                  m_assocPath;
    QFile*                   m_assocFile   = nullptr;
    QTextStream*             m_assocStream = nullptr;
    int                      m_seqInWindow = 0;
    QHash<qint64, Pending>   m_pending;
};

}  // namespace offline_sim

#endif  // OFFLINE_SIM_ANNOTATED_FRAME_SINK_H
