#include "tracker_worker.h"

#include <QDebug>
#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>

#include "logger.h"
#include "pipeline_clock.h"

namespace {

struct AssocCandidate {
    float iou;
    int   detIdx;
    int   trkIdx;
};

}  // namespace

TrackerWorker::TrackerWorker(QObject* parent) : QObject(parent) {}
TrackerWorker::~TrackerWorker() = default;

void TrackerWorker::onSessionStart(const RuntimeConfig& cfg)
{
    m_cfg           = cfg;
    m_mmPerPx       = cfg.maskRasterMmPerPx > 0.1f ? cfg.maskRasterMmPerPx : 2.0f;
    m_active.clear();
    m_ghosts.clear();
    m_lastSpeed     = {};
    m_nextTrackId   = 1;
    m_firstFrame    = true;
    m_tOriginMs     = -1;
    m_sessionActive = true;
    LOG_INFO(QString("TrackerWorker sessionStart mm_per_px=%1 iou_th=%2 miss_x=%3 upd_y=%4")
             .arg(m_mmPerPx).arg(cfg.iouThreshold)
             .arg(cfg.missFramesX).arg(cfg.updateFramesY));
}

void TrackerWorker::onSessionStop()
{
    m_sessionActive = false;
    m_active.clear();
    m_ghosts.clear();
    m_firstFrame = true;
}

void TrackerWorker::onSpeedSample(const SpeedSample& s)
{
    if (s.valid) m_lastSpeed = s;
}

float TrackerWorker::currentSpeedMmPerMs() const
{
    if (m_lastSpeed.valid) return m_lastSpeed.speedMmPerMs;
    // 兜底:采用 nominal_speed_m/s。m/s 与 mm/ms 数值相同。
    return m_cfg.nominalSpeedMs;
}

float TrackerWorker::valveLineYb() const
{
    // y_valve_line = img_bottom_yb + valve_distance_mm
    // img_bottom_yb = 原点前端 + realWidthMm (图像最底端在 belt 系 y 方向的大小)
    // 简化:tOrigin 时相机底边 = real_width_mm;之后 +valve_distance_mm
    return m_cfg.realWidthMm + m_cfg.valveDistanceMm;
}

void TrackerWorker::rasterizeToBelt(const DetectedObject& det,
                                    int imgWidthPx, int imgHeightPx,
                                    qint64 tCaptureMs,
                                    cv::Mat& outMask,
                                    cv::Rect& outBbox,
                                    float& outAreaMm2)
{
    // 1. 像素 bbox -> 物理 bbox(mm)
    const float pxToMmX = m_cfg.realLengthMm / static_cast<float>(std::max(1, imgWidthPx));
    const float pxToMmY = m_cfg.realWidthMm  / static_cast<float>(std::max(1, imgHeightPx));

    const float bbox_xb_mm = det.bboxPx.x * pxToMmX;
    const float bbox_yb0   = det.bboxPx.y * pxToMmY;  // 快照时刻的 y
    const float bbox_w_mm  = det.bboxPx.width  * pxToMmX;
    const float bbox_h_mm  = det.bboxPx.height * pxToMmY;

    // 2. belt 位移外推:把 yb0 推到 t_now(这里 t_now = tCaptureMs,
    //    因为栅格化时我们要的是"该帧采图瞬间"的 belt 坐标;t_capture 就是当时刻)
    //    另外,首帧时 tOrigin 记下来用作相对零点,以免 t_offset 过大。
    if (m_tOriginMs < 0) m_tOriginMs = tCaptureMs;
    const float speed    = currentSpeedMmPerMs();
    const float dy_mm    = speed * static_cast<float>(tCaptureMs - m_tOriginMs);
    const float bbox_yb  = bbox_yb0 + dy_mm;

    // 3. 栅格化 bbox
    const int gx = static_cast<int>(std::round(bbox_xb_mm / m_mmPerPx));
    const int gy = static_cast<int>(std::round(bbox_yb    / m_mmPerPx));
    const int gw = std::max(1, static_cast<int>(std::round(bbox_w_mm / m_mmPerPx)));
    const int gh = std::max(1, static_cast<int>(std::round(bbox_h_mm / m_mmPerPx)));
    outBbox = cv::Rect(gx, gy, gw, gh);

    // 4. mask 栅格化:把原 mask(bbox 尺寸)按物理比例 resize 到 (gw, gh)
    if (det.maskPx.empty()) {
        // 没 mask 时,全 1 填充 bbox
        outMask = cv::Mat(gh, gw, CV_8UC1, cv::Scalar(1));
    } else {
        cv::Mat resized;
        cv::resize(det.maskPx, resized, cv::Size(gw, gh), 0, 0, cv::INTER_NEAREST);
        // 保证二值
        cv::threshold(resized, outMask, 0, 1, cv::THRESH_BINARY);
    }

    outAreaMm2 = cv::countNonZero(outMask) * (m_mmPerPx * m_mmPerPx);
}

float TrackerWorker::maskIoU(const cv::Mat& maskA, const cv::Rect& bboxA,
                             const cv::Mat& maskB, const cv::Rect& bboxB)
{
    const cv::Rect inter = bboxA & bboxB;
    if (inter.area() == 0) return 0.0f;

    // 取各自 mask 里对应 inter 的子区域
    const cv::Rect localA(inter.x - bboxA.x, inter.y - bboxA.y,
                          inter.width, inter.height);
    const cv::Rect localB(inter.x - bboxB.x, inter.y - bboxB.y,
                          inter.width, inter.height);
    if (localA.x < 0 || localA.y < 0 ||
        localA.x + localA.width  > maskA.cols ||
        localA.y + localA.height > maskA.rows) return 0.0f;
    if (localB.x < 0 || localB.y < 0 ||
        localB.x + localB.width  > maskB.cols ||
        localB.y + localB.height > maskB.rows) return 0.0f;

    cv::Mat subA = maskA(localA);
    cv::Mat subB = maskB(localB);
    cv::Mat andMat;
    cv::bitwise_and(subA, subB, andMat);
    const int interArea = cv::countNonZero(andMat);
    if (interArea == 0) return 0.0f;

    const int areaA = cv::countNonZero(maskA);
    const int areaB = cv::countNonZero(maskB);
    const int uni   = areaA + areaB - interArea;
    if (uni <= 0) return 0.0f;
    return static_cast<float>(interArea) / static_cast<float>(uni);
}

cv::Rect TrackerWorker::extrapolateBbox(const cv::Rect& bbox, qint64 fromT, qint64 toT) const
{
    const float speed = currentSpeedMmPerMs();
    const float dy_mm = speed * static_cast<float>(toT - fromT);
    const int dy_px   = static_cast<int>(std::round(dy_mm / m_mmPerPx));
    return cv::Rect(bbox.x, bbox.y + dy_px, bbox.width, bbox.height);
}

SortTask TrackerWorker::makeSortTask(const TrackedObject& trk, int finalClassId) const
{
    SortTask t;
    t.trackId             = trk.trackId;
    t.finalClassId        = finalClassId;
    t.tCaptureMs          = trk.tCaptureMs;
    t.maskBeltRaster      = trk.maskBeltRaster.clone();
    t.bboxBeltRasterPx    = trk.bboxBeltRasterPx;
    t.currentSpeedMmPerMs = currentSpeedMmPerMs();
    return t;
}

void TrackerWorker::purgeGhosts(qint64 nowMs)
{
    const float sortLineYb = valveLineYb();
    const float clearExtra = m_cfg.dispatchedPoolClearMm;

    for (int i = m_ghosts.size() - 1; i >= 0; --i) {
        const cv::Rect nowBbox = extrapolateBbox(m_ghosts[i].bboxBeltRasterPx,
                                                 m_ghosts[i].tCaptureMs, nowMs);
        const float yb_front_mm = nowBbox.y * m_mmPerPx;
        if (yb_front_mm > sortLineYb + clearExtra) {
            m_ghosts.removeAt(i);
        }
    }
}

// ============================================================================
// 主流程
// ============================================================================
void TrackerWorker::onFrameInferred(const DetectedFrame& frame)
{
    if (!m_sessionActive) return;

    // F4:启动后第一帧全量丢弃
    if (m_firstFrame) {
        m_firstFrame = false;
        m_tOriginMs  = frame.tCaptureMs;
        return;
    }

    const qint64 tNow = frame.tCaptureMs;

    // 1. 栅格化所有 det
    struct DetRaster {
        int         srcIdx;
        cv::Mat     mask;
        cv::Rect    bbox;
        float       areaMm2;
    };
    QVector<DetRaster> dets;
    dets.reserve(frame.objs.size());
    for (int i = 0; i < frame.objs.size(); ++i) {
        DetRaster dr;
        dr.srcIdx = i;
        rasterizeToBelt(frame.objs[i],
                        frame.imgWidthPx, frame.imgHeightPx,
                        tNow, dr.mask, dr.bbox, dr.areaMm2);
        dets.push_back(dr);
    }

    // 2. 把每个活跃 track 的 bbox 外推到 tNow,构造候选集
    QVector<AssocCandidate> candidates;
    for (int di = 0; di < dets.size(); ++di) {
        for (int ti = 0; ti < m_active.size(); ++ti) {
            const cv::Rect trkBboxAtNow = extrapolateBbox(
                m_active[ti].bboxBeltRasterPx, m_active[ti].tCaptureMs, tNow);
            const float iou = maskIoU(dets[di].mask, dets[di].bbox,
                                      m_active[ti].maskBeltRaster, trkBboxAtNow);
            if (iou >= m_cfg.iouThreshold) {
                candidates.push_back({iou, di, ti});
            }
        }
    }

    // 3. 贪心关联
    std::sort(candidates.begin(), candidates.end(),
              [](const AssocCandidate& a, const AssocCandidate& b) { return a.iou > b.iou; });
    QVector<bool> detUsed(dets.size(), false);
    QVector<bool> trkUsed(m_active.size(), false);
    for (const auto& c : candidates) {
        if (detUsed[c.detIdx] || trkUsed[c.trkIdx]) continue;
        auto& trk = m_active[c.trkIdx];
        const auto& det = frame.objs[dets[c.detIdx].srcIdx];
        trk.maskBeltRaster   = dets[c.detIdx].mask;
        trk.bboxBeltRasterPx = dets[c.detIdx].bbox;
        trk.tCaptureMs       = tNow;
        trk.updateCount     += 1;
        trk.missCount        = 0;
        trk.latestAreaMm2    = dets[c.detIdx].areaMm2;
        // 类别投票:按面积加权
        trk.classAreaAcc[det.classId] = trk.classAreaAcc.value(det.classId, 0.0f)
                                      + dets[c.detIdx].areaMm2;
        detUsed[c.detIdx] = true;
        trkUsed[c.trkIdx] = true;
    }

    // 4. 未匹配 det:查已分拣池,否则新增 track
    for (int di = 0; di < dets.size(); ++di) {
        if (detUsed[di]) continue;
        const auto& det = frame.objs[dets[di].srcIdx];

        bool suppressed = false;
        for (const auto& g : m_ghosts) {
            const cv::Rect gNow = extrapolateBbox(g.bboxBeltRasterPx, g.tCaptureMs, tNow);
            if (maskIoU(dets[di].mask, dets[di].bbox, g.maskBeltRaster, gNow)
                >= m_cfg.iouThreshold) {
                suppressed = true;
                break;
            }
        }
        if (suppressed) continue;

        TrackedObject trk;
        trk.trackId          = m_nextTrackId++;
        trk.maskBeltRaster   = dets[di].mask;
        trk.bboxBeltRasterPx = dets[di].bbox;
        trk.tCaptureMs       = tNow;
        trk.updateCount      = 1;
        trk.missCount        = 0;
        trk.latestAreaMm2    = dets[di].areaMm2;
        trk.classAreaAcc[det.classId] = dets[di].areaMm2;
        m_active.push_back(trk);
    }

    // 5. 未匹配 track:miss++
    for (int ti = 0; ti < m_active.size(); ++ti) {
        if (!trkUsed[ti]) m_active[ti].missCount += 1;
    }

    // 6. 触发判定(反向遍历以便删除)
    for (int ti = m_active.size() - 1; ti >= 0; --ti) {
        auto& trk = m_active[ti];
        if (trk.missCount < m_cfg.missFramesX &&
            trk.updateCount < m_cfg.updateFramesY) continue;

        // 选 class:累计面积最大
        int   bestClass = -1;
        float bestArea  = 0.0f;
        for (auto it = trk.classAreaAcc.constBegin(); it != trk.classAreaAcc.constEnd(); ++it) {
            if (it.value() > bestArea) { bestArea = it.value(); bestClass = it.key(); }
        }

        // 品类过滤
        if (bestClass < 0 ||
            (!m_cfg.enabledClassIds.isEmpty() && !m_cfg.enabledClassIds.contains(bestClass))) {
            m_active.removeAt(ti);
            continue;
        }

        emit sortTaskReady(makeSortTask(trk, bestClass));

        DispatchedGhost g;
        g.maskBeltRaster   = trk.maskBeltRaster.clone();
        g.bboxBeltRasterPx = trk.bboxBeltRasterPx;
        g.tCaptureMs       = trk.tCaptureMs;
        g.finalClassId     = bestClass;
        m_ghosts.push_back(g);

        m_active.removeAt(ti);
    }

    // 7. 清理已分拣池
    purgeGhosts(tNow);
}
