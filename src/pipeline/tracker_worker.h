#ifndef TRACKER_WORKER_H
#define TRACKER_WORKER_H

// ============================================================================
// TrackerWorker:跨帧 mask IoU 关联 + 触发分拣判定。
// 设计见 docs/design.md §3.5 / §4。
//
// 线程模型:QObject + moveToThread。所有跨线程槽通过 QueuedConnection。
// ============================================================================

#include <QList>
#include <QObject>
#include <QVector>

#include "pipeline/pipeline_types.h"
#include "config/runtime_config.h"

class TrackerWorker : public QObject
{
    Q_OBJECT
public:
    explicit TrackerWorker(QObject* parent = nullptr);
    ~TrackerWorker() override;

public slots:
    void onSessionStart(const RuntimeConfig& cfg);
    void onSessionStop();
    void onFrameInferred(const DetectedFrame& frame);
    void onSpeedSample(const SpeedSample& s);

signals:
    void sortTaskReady(const SortTask& task);
    void warning(const QString& msg);

private:
    // 把一个 DetectedObject 栅格化到 belt 系。
    // 返回值与 TrackedObject 的 maskBeltRaster/bboxBeltRasterPx 同语义。
    // 内部会读取 cfg 里的 realLength_mm / realWidth_mm / imgW / imgH / mmPerPx。
    void rasterizeToBelt(const DetectedObject& det,
                         int imgWidthPx, int imgHeightPx,
                         qint64 tCaptureMs,
                         cv::Mat& outMask,
                         cv::Rect& outBbox,
                         float& outAreaMm2);

    // 计算 mask IoU(均为 belt 栅格系,bbox 为全局栅格坐标)。
    static float maskIoU(const cv::Mat& maskA, const cv::Rect& bboxA,
                         const cv::Mat& maskB, const cv::Rect& bboxB);

    // 计算 mask coverage(containment ratio):|A∩B| / min(|A|,|B|)。
    // 物理含义:"较小那一侧 mask 有多大比例落在另一侧 mask 内"。
    // 与 maskIoU 配合用于"残缺帧 vs 完整帧"的关联场景(参见 §3.6 R1):
    //   - A 是 B 的子集 → coverage 接近 1,IoU 可能很低;
    //   - 用 max(IoU, coverage) 作打分,避免漏关联导致同物体被拆 track。
    // |A∩B|=0 时返回 0;退化输入(空 mask)返回 0。
    static float maskCoverage(const cv::Mat& maskA, const cv::Rect& bboxA,
                              const cv::Mat& maskB, const cv::Rect& bboxB);

    // 把一个 tracked / ghost 的 bbox 从其 tCaptureMs 外推到目标时间,
    // 只移动 bbox.y(belt 运动方向);mask 不动。
    cv::Rect extrapolateBbox(const cv::Rect& bbox, qint64 fromT, qint64 toT) const;

    // 根据配置构造 SortTask 并返回。
    SortTask makeSortTask(const TrackedObject& trk, int finalClassId) const;

    // 做一次已分拣池清理:把超过 valve 线 + pool_clear 的 ghost 丢弃。
    void purgeGhosts(qint64 nowMs);

    // 取当前速度(mm/ms);如果最近速度无效,返回 cfg 的 nominal。
    float currentSpeedMmPerMs() const;

    // 求 "y_valve_line_mm":喷阀线在 belt 系的 y 坐标。
    float valveLineYb() const;

    RuntimeConfig m_cfg;
    bool          m_sessionActive = false;

    QList<TrackedObject>   m_active;
    QList<DispatchedGhost> m_ghosts;
    SpeedSample            m_lastSpeed;          // valid=false 表示还没拿到过速度
    int                    m_nextTrackId   = 1;
    bool                   m_firstFrame    = true;
    float                  m_mmPerPx       = 2.0f;
};

#endif  // TRACKER_WORKER_H
