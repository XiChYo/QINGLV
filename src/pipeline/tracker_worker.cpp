#include "pipeline/tracker_worker.h"

#include <QDebug>
#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>

#include "infra/logger.h"
#include "pipeline/pipeline_clock.h"

namespace {

// 关联候选打分:
//   score = max(iou, coverage),既兼顾"两帧大致一致 (IoU 高)",也兼顾
//   "一帧残缺一帧完整,A 几乎是 B 的子集 (coverage 高)"。详见 §3.6 R1。
//   贪心阶段也按 score 降序排,保证"高度相似 (高 IoU 或高 coverage) 的对"
//   优先抢占 trackId,避免被 IoU 中等但 coverage 弱的对抢先。
struct AssocCandidate {
    float score;     // = max(iou, coverage),用于阈值判断 + 贪心排序
    float iou;       // 保留诊断用
    float coverage;  // 同上
    int   detIdx;
    int   trkIdx;
};

// 在同一全局栅格坐标系下合并两个二值 mask，输出 union mask + union bbox。
static void mergeMasksUnion(const cv::Mat& maskA, const cv::Rect& bboxA,
                            const cv::Mat& maskB, const cv::Rect& bboxB,
                            cv::Mat& outMask, cv::Rect& outBbox)
{
    if (bboxA.area() <= 0 && bboxB.area() <= 0) {
        outMask.release();
        outBbox = cv::Rect();
        return;
    }
    if (bboxA.area() <= 0 || maskA.empty()) {
        outMask = maskB.clone();
        outBbox = bboxB;
        return;
    }
    if (bboxB.area() <= 0 || maskB.empty()) {
        outMask = maskA.clone();
        outBbox = bboxA;
        return;
    }

    const cv::Rect uni = bboxA | bboxB;
    if (uni.area() <= 0) {
        outMask.release();
        outBbox = cv::Rect();
        return;
    }

    cv::Mat merged(uni.height, uni.width, CV_8UC1, cv::Scalar(0));

    const cv::Rect dstA(bboxA.x - uni.x, bboxA.y - uni.y, bboxA.width, bboxA.height);
    const cv::Rect dstB(bboxB.x - uni.x, bboxB.y - uni.y, bboxB.width, bboxB.height);

    cv::bitwise_or(merged(dstA), maskA, merged(dstA));
    cv::bitwise_or(merged(dstB), maskB, merged(dstB));

    cv::threshold(merged, outMask, 0, 1, cv::THRESH_BINARY);
    outBbox = uni;
}

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
    m_lastSpeed = s;
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
    (void)tCaptureMs;  // 栅格化出来的 bbox 语义 = "tCapture 时刻相机视野内的物理坐标 (image-lab mm)"

    // 1. 像素 bbox -> 物理 bbox(mm,image-lab 系)
    const float pxToMmX = m_cfg.realLengthMm / static_cast<float>(std::max(1, imgWidthPx));
    const float pxToMmY = m_cfg.realWidthMm  / static_cast<float>(std::max(1, imgHeightPx));

    const float bbox_xb_mm = det.bboxPx.x * pxToMmX;
    const float bbox_yb_mm = det.bboxPx.y * pxToMmY;
    const float bbox_w_mm  = det.bboxPx.width  * pxToMmX;
    const float bbox_h_mm  = det.bboxPx.height * pxToMmY;

    // 关键约定:栅格化只把像素换算为"该帧采图瞬间的 image-lab mm",不再叠加
    // "tCapture - tOrigin" 的 belt 位移。原实现会在这里预先加上一次位移,
    // 随后 extrapolateBbox 又按 (toT - fromT) 再加一次,造成跨帧 IoU 对不齐。
    // 本处与 extrapolateBbox 必须保持"单一位移口径":
    //   bbox_at(t) = rasterize(det, tCapture) + speed * (t - tCapture)

    // 2. 栅格化 bbox
    const int gx = static_cast<int>(std::round(bbox_xb_mm / m_mmPerPx));
    const int gy = static_cast<int>(std::round(bbox_yb_mm / m_mmPerPx));
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

float TrackerWorker::maskCoverage(const cv::Mat& maskA, const cv::Rect& bboxA,
                                  const cv::Mat& maskB, const cv::Rect& bboxB)
{
    const cv::Rect inter = bboxA & bboxB;
    if (inter.area() == 0) return 0.0f;

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
    const int smaller = std::min(areaA, areaB);
    if (smaller <= 0) return 0.0f;
    return static_cast<float>(interArea) / static_cast<float>(smaller);
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
    // 分拣线在 belt 坐标系里的 y:
    //   valve 模式 -> img_bottom + valve_distance
    //   arm   模式 -> img_bottom + arm_distance
    // img_bottom 的 yb = real_width_mm(栅格化时 tOrigin 相机底边对齐)。
    const float dispatchLineYb =
        (m_cfg.sorterMode == RuntimeConfig::SorterMode::Arm)
            ? (m_cfg.realWidthMm + m_cfg.armDistanceMm)
            : valveLineYb();
    const float clearExtra = m_cfg.dispatchedPoolClearMm;

    for (int i = m_ghosts.size() - 1; i >= 0; --i) {
        const cv::Rect nowBbox = extrapolateBbox(m_ghosts[i].bboxBeltRasterPx,
                                                 m_ghosts[i].tCaptureMs, nowMs);
        // bbox 在 belt 系里:y = trailing edge(远离分拣线),y+height = leading edge(先过线)。
        // ghost 只有在 trailing edge 也越过分拣线 + 清除距离后,才安全回收,
        // 保守多保留一会儿可抑制跨帧检测抖动引起的二次派发。
        const float yb_trail_mm = nowBbox.y * m_mmPerPx;
        if (yb_trail_mm > dispatchLineYb + clearExtra) {
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

    // F4 / AC15:启动首帧里已经在视野的物体一律按"已分拣幽灵"处理,
    // 后续帧里它们被 IoU 抑制,不会形成新 track、也不会触发分拣。
    // 幽灵会随 extrapolate 自然推过喷阀线 + 清除距离,再由 purgeGhosts 回收。
    if (m_firstFrame) {
        m_firstFrame = false;
        for (int i = 0; i < frame.objs.size(); ++i) {
            cv::Mat mask;
            cv::Rect bbox;
            float areaMm2 = 0.0f;
            rasterizeToBelt(frame.objs[i],
                            frame.imgWidthPx, frame.imgHeightPx,
                            frame.tCaptureMs, mask, bbox, areaMm2);
            DispatchedGhost g;
            g.maskBeltRaster   = mask;
            g.bboxBeltRasterPx = bbox;
            g.tCaptureMs       = frame.tCaptureMs;
            g.finalClassId     = frame.objs[i].classId;  // 仅为日志可读
            m_ghosts.push_back(g);
        }
        LOG_INFO(QString("TrackerWorker first frame suppressed %1 objs as ghosts")
                 .arg(frame.objs.size()));
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
    //    打分用 max(IoU, coverage),阈值复用 cfg.iouThreshold(详见 §3.6 R1)
    QVector<AssocCandidate> candidates;
    for (int di = 0; di < dets.size(); ++di) {
        for (int ti = 0; ti < m_active.size(); ++ti) {
            const cv::Rect trkBboxAtNow = extrapolateBbox(
                m_active[ti].bboxBeltRasterPx, m_active[ti].tCaptureMs, tNow);
            const float iou = maskIoU(dets[di].mask, dets[di].bbox,
                                      m_active[ti].maskBeltRaster, trkBboxAtNow);
            const float cov = maskCoverage(dets[di].mask, dets[di].bbox,
                                           m_active[ti].maskBeltRaster, trkBboxAtNow);
            const float score = std::max(iou, cov);
            if (score >= m_cfg.iouThreshold) {
                candidates.push_back({score, iou, cov, di, ti});
            }
        }
    }

    // 3. 贪心关联(按 score 降序,避免 coverage 高但 IoU 低的"残→全"对被抢)
    std::sort(candidates.begin(), candidates.end(),
              [](const AssocCandidate& a, const AssocCandidate& b) { return a.score > b.score; });
    QVector<bool> detUsed(dets.size(), false);
    QVector<bool> trkUsed(m_active.size(), false);
    for (const auto& c : candidates) {
        if (detUsed[c.detIdx] || trkUsed[c.trkIdx]) continue;
        auto& trk = m_active[c.trkIdx];
        const auto& det = frame.objs[dets[c.detIdx].srcIdx];

        // 关联成功后做历史 union 累积:把"旧 track 外推到当前时刻"与当前 det 融合。
        const cv::Rect trkBboxAtNow = extrapolateBbox(
            trk.bboxBeltRasterPx, trk.tCaptureMs, tNow);
        cv::Mat mergedMask;
        cv::Rect mergedBbox;
        mergeMasksUnion(dets[c.detIdx].mask, dets[c.detIdx].bbox,
                        trk.maskBeltRaster, trkBboxAtNow,
                        mergedMask, mergedBbox);
        trk.maskBeltRaster   = mergedMask;
        trk.bboxBeltRasterPx = mergedBbox;
        trk.tCaptureMs       = tNow;
        trk.updateCount     += 1;
        trk.missCount        = 0;
        trk.latestAreaMm2    = cv::countNonZero(trk.maskBeltRaster) * (m_mmPerPx * m_mmPerPx);
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

        // ghost 抑制命中口径同步升级为 max(IoU, coverage),
        // 与上面候选打分对称,避免"关联放过、抑制漏掉"造成的二次派发(§3.6 R1)。
        bool suppressed = false;
        for (int gi = 0; gi < m_ghosts.size(); ++gi) {
            auto& g = m_ghosts[gi];
            const cv::Rect gNow = extrapolateBbox(g.bboxBeltRasterPx, g.tCaptureMs, tNow);
            const float ghostIou = maskIoU(dets[di].mask, dets[di].bbox,
                                           g.maskBeltRaster, gNow);
            const float ghostCov = maskCoverage(dets[di].mask, dets[di].bbox,
                                                g.maskBeltRaster, gNow);
            if (std::max(ghostIou, ghostCov) >= m_cfg.iouThreshold) {
                // 与 ghost 命中时也做 union 累积,让抑制池跟随观测持续更新。
                cv::Mat mergedMask;
                cv::Rect mergedBbox;
                mergeMasksUnion(dets[di].mask, dets[di].bbox,
                                g.maskBeltRaster, gNow,
                                mergedMask, mergedBbox);
                g.maskBeltRaster   = mergedMask;
                g.bboxBeltRasterPx = mergedBbox;
                g.tCaptureMs       = tNow;
                suppressed         = true;
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

    // 5. 未匹配 track:miss++。
    // 注意:此时 m_active 可能已经 push_back 过"未匹配 det 新建 track",
    // 导致 m_active.size() > trkUsed.size(),不能用 m_active.size() 作边界。
    // 新增 track 天然视为"刚匹配过",missCount 已初始化为 0,这里不再处理它们。
    for (int ti = 0; ti < trkUsed.size(); ++ti) {
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
