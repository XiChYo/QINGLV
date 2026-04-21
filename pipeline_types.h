#ifndef PIPELINE_TYPES_H
#define PIPELINE_TYPES_H

// ============================================================================
// Pipeline 通用数据结构
// 详细语义见 docs/design.md §2
// 所有跨线程信号的参数必须 Q_DECLARE_METATYPE,并在 main() 里 qRegisterMetaType
// ============================================================================

#include <QMetaType>
#include <QVector>
#include <QHash>
#include <QString>
#include <QPoint>
#include <opencv2/core.hpp>
#include <cstdint>

// ----------------------------------------------------------------------------
// YOLO 阶段:单个识别目标
// ----------------------------------------------------------------------------
struct DetectedObject
{
    int        classId       = -1;
    float      confidence    = 0.0f;
    cv::Rect   bboxPx;
    // mask 尺寸 = bboxPx.size(),CV_8UC1,非零表示命中
    cv::Mat    maskPx;
    // 像素坐标下的重心(mask 主轮廓矩;没有 mask 则退化成 bbox 中心)
    cv::Point2f centerPx {0.f, 0.f};
};

// ----------------------------------------------------------------------------
// YOLO 阶段:一帧完整输出
// ----------------------------------------------------------------------------
struct DetectedFrame
{
    qint64    tCaptureMs    = 0;
    qint64    tInferDoneMs  = 0;
    int       imgWidthPx    = 0;
    int       imgHeightPx   = 0;
    QVector<DetectedObject> objs;
};

// ----------------------------------------------------------------------------
// Tracker 阶段:追踪物体
// mask 与 bbox 均以"皮带固定坐标系栅格"为参考系,分辨率 mm_per_px 全局一致
// ----------------------------------------------------------------------------
struct TrackedObject
{
    int       trackId       = 0;
    cv::Mat   maskBeltRaster;       // CV_8UC1
    cv::Rect  bboxBeltRasterPx;     // 栅格图全局坐标
    qint64    tCaptureMs    = 0;    // 该快照对应的采图时间戳
    float     latestAreaMm2 = 0.0f;
    int       missCount     = 0;
    int       updateCount   = 0;
    QHash<int, float> classAreaAcc; // classId -> 累计加权面积(mm^2)
};

// ----------------------------------------------------------------------------
// Tracker 阶段:已分拣池(派发后继续随皮带平移,用于抑制二次分拣)
// ----------------------------------------------------------------------------
struct DispatchedGhost
{
    cv::Mat   maskBeltRaster;
    cv::Rect  bboxBeltRasterPx;
    qint64    tCaptureMs    = 0;
    int       finalClassId  = -1;
};

// ----------------------------------------------------------------------------
// Tracker -> Dispatcher:一次触发分拣的任务
// ----------------------------------------------------------------------------
struct SortTask
{
    int       trackId       = 0;
    int       finalClassId  = -1;
    qint64    tCaptureMs    = 0;
    cv::Mat   maskBeltRaster;
    cv::Rect  bboxBeltRasterPx;
    float     currentSpeedMmPerMs = 0.0f;
};

// ----------------------------------------------------------------------------
// BoardWorker -> 其他模块:编码器速度样本
// ----------------------------------------------------------------------------
struct SpeedSample
{
    qint64    tMs           = 0;
    float     speedMmPerMs  = 0.0f;
    bool      valid         = false;
};

// ----------------------------------------------------------------------------
// Dispatcher -> BoardWorker:一条阀脉冲(绝对时间)
// ----------------------------------------------------------------------------
struct ValvePulse
{
    qint64    tOpenMs       = 0;
    qint64    tCloseMs      = 0;
    quint8    boardId       = 0;   // 1..8
    quint16   channelMask   = 0;   // 每板 9 通道:bit0..bit8
};

Q_DECLARE_METATYPE(DetectedObject)
Q_DECLARE_METATYPE(DetectedFrame)
Q_DECLARE_METATYPE(TrackedObject)
Q_DECLARE_METATYPE(DispatchedGhost)
Q_DECLARE_METATYPE(SortTask)
Q_DECLARE_METATYPE(SpeedSample)
Q_DECLARE_METATYPE(ValvePulse)
Q_DECLARE_METATYPE(QVector<ValvePulse>)

#endif // PIPELINE_TYPES_H
