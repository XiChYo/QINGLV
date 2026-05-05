// 测试 TrackerWorker 关键路径:首帧丢弃、IoU 关联、触发、已分拣池抑制。
// 为了直接调 private 方法/访问 private 成员,这里局部宏改访问控制。
// 仅此 TU 生效,不影响生产代码。
#define private public
#define protected public
#include "pipeline/tracker_worker.h"
#undef private
#undef protected

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <opencv2/imgproc.hpp>

#include "pipeline/pipeline_clock.h"

namespace {

RuntimeConfig makeCfg()
{
    RuntimeConfig c;
    c.realLengthMm          = 640.0f;
    c.realWidthMm           = 480.0f;
    c.maskRasterMmPerPx     = 2.0f;
    c.iouThreshold          = 0.3f;
    c.missFramesX           = 2;
    c.updateFramesY         = 2;   // 小 Y,便于用 2 帧即可触发
    c.dispatchedPoolClearMm = 100.0f;
    c.valveDistanceMm       = 50.0f;
    c.nominalSpeedMs        = 0.0f;  // 不想让物体在 belt 上滑,除非单独设置 lastSpeed
    return c;
}

// 构造一个像素空间上的 DetectedObject:bbox + 全 1 mask
DetectedObject makeDet(int classId, float conf,
                       int x, int y, int w, int h)
{
    DetectedObject d;
    d.classId    = classId;
    d.confidence = conf;
    d.bboxPx     = cv::Rect(x, y, w, h);
    d.maskPx     = cv::Mat(h, w, CV_8UC1, cv::Scalar(1));
    d.centerPx   = cv::Point2f(x + w * 0.5f, y + h * 0.5f);
    return d;
}

DetectedFrame makeFrame(qint64 tCapture,
                        int imgW, int imgH,
                        std::initializer_list<DetectedObject> dets)
{
    DetectedFrame f;
    f.tCaptureMs   = tCapture;
    f.tInferDoneMs = tCapture + 1;
    f.imgWidthPx   = imgW;
    f.imgHeightPx  = imgH;
    for (const auto& d : dets) f.objs.push_back(d);
    return f;
}

}  // namespace

class TrackerWorkerTest : public QObject
{
    Q_OBJECT
private slots:
    void firstFrame_registersAllAsGhosts();
    void newDet_createsActiveTrack();
    void firstFrame_lingeringObjectIsSuppressedNextFrame();
    void repeatedDet_increasesUpdateCountAndTriggersSortTask_whenClassEnabled();
    void repeatedDet_doesNotTrigger_whenClassDisabled();
    void missCount_reachesThresholdAndTriggers_ifUpdateAlreadyEnough();
    void ghostSuppression_preventsResortOfSameObject();
    void ghostSuppression_hit_updatesGhostByMaskUnion();
    void associatedDet_mergesMaskWithHistoricalTrack();
    void maskIoU_zeroOnDisjointBboxes();
    void maskIoU_oneOnIdentical();
    void maskCoverage_zeroOnDisjointBboxes();
    void maskCoverage_oneWhenAIsSubsetOfB();
    void rasterizeToBelt_scalesPixelToMmCorrectly();
    // R1 (§3.6):"残->全"场景下,IoU 低但 coverage 高,
    // 应被合并到同一 trackId,而非误新建第二条 track。
    void partialDet_mergesIntoFullTrack_viaCoverage();
    void partialDet_hitsGhostPool_viaCoverage();

    // R2 (§3.7):frameAnnotationReady 信号 —— 每帧无条件 emit,
    // bindings 与 frame.objs 一一对应,字段语义如 DetTrackBinding 所述。
    void r2_firstFrame_emitsBindingsWithFirstFrameGhostFlag();
    void r2_normalFrame_emptyDets_stillEmitsEmptyBindings();
    void r2_normalFrame_newAndAssociatedAndGhost_setsCorrectFields();
};

void TrackerWorkerTest::firstFrame_registersAllAsGhosts()
{
    // AC15 重定义:启动首帧里已经在视野的物体不能形成 track、不能触发分拣,
    // 但必须作为 ghost 抑制同一物体在后续帧再次被识别。
    TrackerWorker trk;
    trk.onSessionStart(makeCfg());
    QSignalSpy spy(&trk, &TrackerWorker::sortTaskReady);

    DetectedFrame f = makeFrame(100, 640, 480, {makeDet(1, 0.9f, 10, 10, 40, 40)});
    trk.onFrameInferred(f);

    QCOMPARE(trk.m_active.size(), 0);          // 首帧不入 active
    QCOMPARE(trk.m_ghosts.size(), 1);          // 首帧全进 ghost
    QCOMPARE(trk.m_firstFrame, false);
    QCOMPARE(spy.count(), 0);                  // 未触发分拣
}

void TrackerWorkerTest::firstFrame_lingeringObjectIsSuppressedNextFrame()
{
    // 首帧有一个物体 A,下一帧它还在视野同一位置,应被 ghost 抑制,
    // 不会新建 track,也不会触发 SortTask。
    RuntimeConfig cfg = makeCfg();
    cfg.enabledClassIds.insert(1);
    cfg.updateFramesY = 2;
    // valveLineYb = realWidthMm + valveDistanceMm = 480 + 50 = 530 mm;
    // ghost bbox.y = 10 raster => 20 mm。nominalSpeed=0,ghost 不会被 purge。
    cfg.dispatchedPoolClearMm = 10000.0f;

    TrackerWorker trk;
    trk.onSessionStart(cfg);
    QSignalSpy spy(&trk, &TrackerWorker::sortTaskReady);

    trk.onFrameInferred(makeFrame(0, 640, 480,
        {makeDet(1, 0.9f, 10, 10, 40, 40)}));  // 首帧入 ghost

    trk.onFrameInferred(makeFrame(100, 640, 480,
        {makeDet(1, 0.9f, 10, 10, 40, 40)}));  // 同位置 det,应被抑制
    QCOMPARE(trk.m_active.size(), 0);
    QCOMPARE(spy.count(), 0);
}

void TrackerWorkerTest::newDet_createsActiveTrack()
{
    TrackerWorker trk;
    trk.onSessionStart(makeCfg());
    // 先消费一个首帧(任意 det,会被丢弃),让后续帧开始正常处理
    trk.onFrameInferred(makeFrame(100, 640, 480, {}));

    DetectedFrame f = makeFrame(200, 640, 480, {makeDet(1, 0.9f, 10, 10, 40, 40)});
    trk.onFrameInferred(f);
    QCOMPARE(trk.m_active.size(), 1);
    QCOMPARE(trk.m_active[0].updateCount, 1);
    QCOMPARE(trk.m_active[0].missCount,   0);
}

void TrackerWorkerTest::repeatedDet_increasesUpdateCountAndTriggersSortTask_whenClassEnabled()
{
    RuntimeConfig cfg = makeCfg();
    cfg.enabledClassIds.insert(1);
    cfg.updateFramesY = 2;   // 触发阈值 Y=2

    TrackerWorker trk;
    trk.onSessionStart(cfg);
    QSignalSpy spy(&trk, &TrackerWorker::sortTaskReady);

    trk.onFrameInferred(makeFrame(0, 640, 480, {}));                      // 首帧丢弃
    trk.onFrameInferred(makeFrame(100, 640, 480,
        {makeDet(1, 0.9f, 10, 10, 40, 40)}));                             // update=1
    trk.onFrameInferred(makeFrame(200, 640, 480,
        {makeDet(1, 0.9f, 10, 10, 40, 40)}));                             // update=2 -> trigger

    QCOMPARE(spy.count(), 1);
    const auto args = spy.takeFirst();
    const SortTask task = args.at(0).value<SortTask>();
    QCOMPARE(task.finalClassId, 1);
    QVERIFY(task.trackId > 0);
    QVERIFY(!task.maskBeltRaster.empty());
    QCOMPARE(trk.m_active.size(), 0);   // 已被移出 active
    QCOMPARE(trk.m_ghosts.size(), 1);   // 入 ghost 池
}

void TrackerWorkerTest::repeatedDet_doesNotTrigger_whenClassDisabled()
{
    RuntimeConfig cfg = makeCfg();
    cfg.enabledClassIds.insert(2);      // 只启用 cls=2,但 det 是 cls=1
    cfg.updateFramesY = 2;

    TrackerWorker trk;
    trk.onSessionStart(cfg);
    QSignalSpy spy(&trk, &TrackerWorker::sortTaskReady);

    trk.onFrameInferred(makeFrame(0, 640, 480, {}));
    trk.onFrameInferred(makeFrame(100, 640, 480,
        {makeDet(1, 0.9f, 10, 10, 40, 40)}));
    trk.onFrameInferred(makeFrame(200, 640, 480,
        {makeDet(1, 0.9f, 10, 10, 40, 40)}));

    QCOMPARE(spy.count(), 0);
}

void TrackerWorkerTest::missCount_reachesThresholdAndTriggers_ifUpdateAlreadyEnough()
{
    // 同时满足:update >= Y 或 miss >= X 都会触发,以 miss 达标为例。
    RuntimeConfig cfg = makeCfg();
    cfg.enabledClassIds.insert(1);
    cfg.updateFramesY = 10;  // 故意很高,让 update 不起作用
    cfg.missFramesX   = 2;

    TrackerWorker trk;
    trk.onSessionStart(cfg);
    QSignalSpy spy(&trk, &TrackerWorker::sortTaskReady);

    trk.onFrameInferred(makeFrame(0, 640, 480, {}));
    trk.onFrameInferred(makeFrame(100, 640, 480,
        {makeDet(1, 0.9f, 10, 10, 40, 40)}));                  // 新建 track
    trk.onFrameInferred(makeFrame(200, 640, 480, {}));          // miss=1
    trk.onFrameInferred(makeFrame(300, 640, 480, {}));          // miss=2 -> 触发

    QCOMPARE(spy.count(), 1);
    const SortTask task = spy.takeFirst().at(0).value<SortTask>();
    QCOMPARE(task.finalClassId, 1);
}

void TrackerWorkerTest::ghostSuppression_preventsResortOfSameObject()
{
    RuntimeConfig cfg = makeCfg();
    cfg.enabledClassIds.insert(1);
    cfg.updateFramesY = 2;
    // 保证 ghost 不会立即越过 valve 线被 purge:
    // valveLineYb = realWidthMm + valveDistanceMm = 480 + 50 = 530mm
    // ghost bbox.y = 10 (px in belt raster),mmPerPx=2 => 20mm,远小于 530mm。
    cfg.dispatchedPoolClearMm = 10000.0f;

    TrackerWorker trk;
    trk.onSessionStart(cfg);
    QSignalSpy spy(&trk, &TrackerWorker::sortTaskReady);

    // 触发一次分拣,入 ghost 池。
    trk.onFrameInferred(makeFrame(0, 640, 480, {}));
    trk.onFrameInferred(makeFrame(100, 640, 480,
        {makeDet(1, 0.9f, 10, 10, 40, 40)}));
    trk.onFrameInferred(makeFrame(200, 640, 480,
        {makeDet(1, 0.9f, 10, 10, 40, 40)}));
    QCOMPARE(spy.count(), 1);

    // 再来一帧同位置 det,应该被 ghost 抑制(不新建 track)。
    trk.onFrameInferred(makeFrame(300, 640, 480,
        {makeDet(1, 0.9f, 10, 10, 40, 40)}));
    QCOMPARE(trk.m_active.size(), 0);
    // 不会再触发新的 SortTask
    QCOMPARE(spy.count(), 1);
}

void TrackerWorkerTest::ghostSuppression_hit_updatesGhostByMaskUnion()
{
    RuntimeConfig cfg = makeCfg();
    cfg.enabledClassIds.insert(1);
    cfg.updateFramesY = 2;
    cfg.dispatchedPoolClearMm = 10000.0f;

    TrackerWorker trk;
    trk.onSessionStart(cfg);

    // 先让一个 track 触发分拣,进入 ghost 池
    trk.onFrameInferred(makeFrame(0, 640, 480, {}));
    trk.onFrameInferred(makeFrame(100, 640, 480,
        {makeDet(1, 0.9f, 10, 10, 40, 40)}));
    trk.onFrameInferred(makeFrame(200, 640, 480,
        {makeDet(1, 0.9f, 10, 10, 40, 40)}));
    QCOMPARE(trk.m_ghosts.size(), 1);
    QCOMPARE(trk.m_ghosts[0].bboxBeltRasterPx.x, 5);
    QCOMPARE(trk.m_ghosts[0].bboxBeltRasterPx.width, 20);

    // 后续命中 ghost 的检测向右偏移,应被抑制且把 ghost 融合扩展
    trk.onFrameInferred(makeFrame(300, 640, 480,
        {makeDet(1, 0.9f, 20, 10, 40, 40)}));
    QCOMPARE(trk.m_active.size(), 0);   // 仍被抑制,不新建 track
    QCOMPARE(trk.m_ghosts.size(), 1);
    QCOMPARE(trk.m_ghosts[0].bboxBeltRasterPx.x, 5);
    QCOMPARE(trk.m_ghosts[0].bboxBeltRasterPx.width, 25);
    QCOMPARE(cv::countNonZero(trk.m_ghosts[0].maskBeltRaster), 25 * 20);
    QCOMPARE(trk.m_ghosts[0].tCaptureMs, static_cast<qint64>(300));
}

void TrackerWorkerTest::associatedDet_mergesMaskWithHistoricalTrack()
{
    RuntimeConfig cfg = makeCfg();
    cfg.updateFramesY = 10;  // 避免第二次更新后立刻触发出轨
    cfg.iouThreshold  = 0.3f;

    TrackerWorker trk;
    trk.onSessionStart(cfg);

    // 首帧丢弃
    trk.onFrameInferred(makeFrame(0, 640, 480, {}));

    // 第一次出现:新建 track
    trk.onFrameInferred(makeFrame(100, 640, 480,
        {makeDet(1, 0.9f, 10, 10, 40, 40)}));
    QCOMPARE(trk.m_active.size(), 1);
    QCOMPARE(trk.m_active[0].bboxBeltRasterPx.x, 5);
    QCOMPARE(trk.m_active[0].bboxBeltRasterPx.width, 20);

    // 第二次出现:同一目标向右偏移,应与历史 track 关联并做 union 融合
    trk.onFrameInferred(makeFrame(200, 640, 480,
        {makeDet(1, 0.9f, 20, 10, 40, 40)}));
    QCOMPARE(trk.m_active.size(), 1);
    QCOMPARE(trk.m_active[0].updateCount, 2);

    // mm_per_px=2, 且 realLength/Width 与图像像素一一对应:
    // x: [10,50) -> [5,25), [20,60) -> [10,30), union => [5,30), width=25
    QCOMPARE(trk.m_active[0].bboxBeltRasterPx.x, 5);
    QCOMPARE(trk.m_active[0].bboxBeltRasterPx.width, 25);
    QCOMPARE(cv::countNonZero(trk.m_active[0].maskBeltRaster), 25 * 20);
}

void TrackerWorkerTest::maskIoU_zeroOnDisjointBboxes()
{
    cv::Mat a(10, 10, CV_8UC1, cv::Scalar(1));
    cv::Mat b(10, 10, CV_8UC1, cv::Scalar(1));
    cv::Rect ra(0, 0, 10, 10);
    cv::Rect rb(100, 100, 10, 10);
    QCOMPARE(TrackerWorker::maskIoU(a, ra, b, rb), 0.0f);
}

void TrackerWorkerTest::maskIoU_oneOnIdentical()
{
    cv::Mat a(10, 10, CV_8UC1, cv::Scalar(1));
    cv::Rect r(0, 0, 10, 10);
    QCOMPARE(TrackerWorker::maskIoU(a, r, a, r), 1.0f);
}

void TrackerWorkerTest::maskCoverage_zeroOnDisjointBboxes()
{
    cv::Mat a(10, 10, CV_8UC1, cv::Scalar(1));
    cv::Mat b(10, 10, CV_8UC1, cv::Scalar(1));
    cv::Rect ra(0, 0, 10, 10);
    cv::Rect rb(100, 100, 10, 10);
    QCOMPARE(TrackerWorker::maskCoverage(a, ra, b, rb), 0.0f);
}

void TrackerWorkerTest::maskCoverage_oneWhenAIsSubsetOfB()
{
    // A: 10x10 全 1,放在原点
    // B: 40x40 全 1,放在原点(把 A 完全包住)
    // |A∩B| = 100, min(|A|,|B|) = min(100, 1600) = 100, coverage = 1.0
    // |A∪B| = 1600, IoU = 100/1600 = 0.0625
    cv::Mat a(10, 10, CV_8UC1, cv::Scalar(1));
    cv::Mat b(40, 40, CV_8UC1, cv::Scalar(1));
    cv::Rect ra(0, 0, 10, 10);
    cv::Rect rb(0, 0, 40, 40);
    QCOMPARE(TrackerWorker::maskCoverage(a, ra, b, rb), 1.0f);
    // 同时确认 IoU 远低于阈值,体现"R1 必要性"
    QVERIFY(TrackerWorker::maskIoU(a, ra, b, rb) < 0.1f);
}

void TrackerWorkerTest::rasterizeToBelt_scalesPixelToMmCorrectly()
{
    RuntimeConfig cfg = makeCfg();
    cfg.realLengthMm      = 640.0f;    // pxToMmX = 1 mm/px
    cfg.realWidthMm       = 480.0f;    // pxToMmY = 1 mm/px
    cfg.maskRasterMmPerPx = 2.0f;

    TrackerWorker trk;
    trk.onSessionStart(cfg);

    // 伪首帧以设 tOrigin
    trk.onFrameInferred(makeFrame(0, 640, 480, {}));

    DetectedObject det = makeDet(5, 0.8f, 40, 60, 80, 40);  // 40x60 像素 → 40x60 mm
    cv::Mat mask;
    cv::Rect bbox;
    float areaMm2 = 0.f;
    trk.rasterizeToBelt(det, 640, 480, /*tCaptureMs=*/0,
                        mask, bbox, areaMm2);
    // gx = round(40 / 2) = 20, gy = round(60 / 2) = 30,
    // gw = round(80 / 2) = 40, gh = round(40 / 2) = 20
    QCOMPARE(bbox.x,  20);
    QCOMPARE(bbox.y,  30);
    QCOMPARE(bbox.width,  40);
    QCOMPARE(bbox.height, 20);
    QCOMPARE(static_cast<int>(mask.rows), 20);
    QCOMPARE(static_cast<int>(mask.cols), 40);
    // 40 * 20 像素,每像素 4mm²
    QCOMPARE(areaMm2, 40.0f * 20.0f * 4.0f);
}

void TrackerWorkerTest::partialDet_mergesIntoFullTrack_viaCoverage()
{
    // R1 关键回归(§3.6):
    //   第 1 个真实帧:小 det 进入,新建 track。栅格 mask 10x10 全 1。
    //   第 2 个真实帧:大 det 把上一帧那块完全包住。栅格 mask 40x40 全 1。
    //   IoU(小,大) = 100/1600 = 0.0625 < iouThreshold(0.3)
    //   coverage  = 100/min(100,1600) = 1.0 >= 0.3
    //   修复前(只看 IoU)会被当作新物体新建第 2 条 track;
    //   修复后(max(IoU,coverage))应合并到同一 trackId。
    RuntimeConfig cfg = makeCfg();
    cfg.realLengthMm      = 640.0f;
    cfg.realWidthMm       = 480.0f;
    cfg.maskRasterMmPerPx = 2.0f;     // pxToMm=1, 栅格 = 像素/2
    cfg.iouThreshold      = 0.3f;
    cfg.updateFramesY     = 10;       // 不让第 2 帧立刻触发
    cfg.nominalSpeedMs    = 0.0f;     // 不外推位移,简化几何

    TrackerWorker trk;
    trk.onSessionStart(cfg);

    trk.onFrameInferred(makeFrame(0, 640, 480, {}));   // 伪首帧

    // 第 1 真实帧:像素 (30,30,20,20) -> 栅格 (15,15,10,10),area=100
    trk.onFrameInferred(makeFrame(100, 640, 480,
        {makeDet(/*classId*/1, 0.9f, 30, 30, 20, 20)}));
    QCOMPARE(trk.m_active.size(), 1);
    const int tidFirst = trk.m_active[0].trackId;
    QCOMPARE(trk.m_active[0].updateCount, 1);

    // 第 2 真实帧:像素 (10,10,80,80) -> 栅格 (5,5,40,40),area=1600,完全包住前者
    trk.onFrameInferred(makeFrame(200, 640, 480,
        {makeDet(/*classId*/1, 0.9f, 10, 10, 80, 80)}));

    // 关键断言:m_active 仍是 1,没有被拆成 2(R1 修复点);trackId 不变;updateCount=2
    QCOMPARE(trk.m_active.size(), 1);
    QCOMPARE(trk.m_active[0].trackId, tidFirst);
    QCOMPARE(trk.m_active[0].updateCount, 2);
    // 合并后的 bbox 应该是大那块的并集 (=大 det 自身,因为小是子集)
    QCOMPARE(trk.m_active[0].bboxBeltRasterPx, cv::Rect(5, 5, 40, 40));
}

void TrackerWorkerTest::partialDet_hitsGhostPool_viaCoverage()
{
    // R1 同步升级 ghost 抑制(§3.6):
    //   先让一个大 det 触发分拣,进入 ghost 池(40x40 全 1)。
    //   再来一个小 det(10x10),被大 ghost 完全包住:
    //   IoU=0.0625 < 0.3,但 coverage=1.0,应被 ghost 抑制,不新建 track,
    //   也不再触发新的 SortTask。
    //
    //   修复前(ghost 检查只看 IoU)会被当作新物体进 active,后续若达阈值会
    //   触发第 2 次 SortTask -> 重复分拣 bug。
    RuntimeConfig cfg = makeCfg();
    cfg.realLengthMm          = 640.0f;
    cfg.realWidthMm           = 480.0f;
    cfg.maskRasterMmPerPx     = 2.0f;
    cfg.iouThreshold          = 0.3f;
    cfg.updateFramesY         = 2;
    cfg.nominalSpeedMs        = 0.0f;
    cfg.dispatchedPoolClearMm = 10000.0f;  // 不让 ghost 被 purge
    cfg.enabledClassIds.insert(1);

    TrackerWorker trk;
    trk.onSessionStart(cfg);
    QSignalSpy spy(&trk, &TrackerWorker::sortTaskReady);

    trk.onFrameInferred(makeFrame(0, 640, 480, {}));   // 伪首帧

    // 用大 det 连续两帧触发分拣 (像素 (10,10,80,80) -> 栅格 (5,5,40,40))
    trk.onFrameInferred(makeFrame(100, 640, 480,
        {makeDet(1, 0.9f, 10, 10, 80, 80)}));
    trk.onFrameInferred(makeFrame(200, 640, 480,
        {makeDet(1, 0.9f, 10, 10, 80, 80)}));
    QCOMPARE(spy.count(), 1);            // 第 1 次分拣
    QCOMPARE(trk.m_active.size(), 0);
    QCOMPARE(trk.m_ghosts.size(), 1);

    // 现在来一个小 det,栅格 10x10 完全包在 ghost 内
    trk.onFrameInferred(makeFrame(300, 640, 480,
        {makeDet(1, 0.9f, 30, 30, 20, 20)}));

    // 关键断言:不新建 track、不触发新分拣
    QCOMPARE(trk.m_active.size(), 0);
    QCOMPARE(spy.count(), 1);
    // ghost 时间戳应被刷新到本帧 (§3.6 R1: 命中时也走 union 路径)
    QCOMPARE(trk.m_ghosts[0].tCaptureMs, static_cast<qint64>(300));
}

// ============================================================================
// R2 frameAnnotationReady 单测(§3.7)
// ============================================================================

void TrackerWorkerTest::r2_firstFrame_emitsBindingsWithFirstFrameGhostFlag()
{
    TrackerWorker trk;
    trk.onSessionStart(makeCfg());
    QSignalSpy spy(&trk, &TrackerWorker::frameAnnotationReady);

    DetectedFrame f = makeFrame(100, 640, 480, {
        makeDet(/*classId*/3, 0.9f, 10, 10, 40, 40),
        makeDet(/*classId*/7, 0.8f, 200, 100, 30, 30),
    });
    trk.onFrameInferred(f);

    QCOMPARE(spy.count(), 1);
    const auto args = spy.takeFirst();
    QCOMPARE(args.at(0).value<qint64>(), static_cast<qint64>(100));
    const auto bindings = args.at(1).value<QVector<DetTrackBinding>>();
    QCOMPARE(bindings.size(), 2);

    for (int i = 0; i < bindings.size(); ++i) {
        QCOMPARE(bindings[i].detIndex,          i);
        QCOMPARE(bindings[i].trackId,           -1);
        QCOMPARE(bindings[i].firstFrameGhost,   true);
        QCOMPARE(bindings[i].suppressedByGhost, false);
        QCOMPARE(bindings[i].isNewTrack,        false);
    }
    // bestClassId 应等于该 det 的 classId
    QCOMPARE(bindings[0].bestClassId, 3);
    QCOMPARE(bindings[1].bestClassId, 7);
}

void TrackerWorkerTest::r2_normalFrame_emptyDets_stillEmitsEmptyBindings()
{
    TrackerWorker trk;
    trk.onSessionStart(makeCfg());
    QSignalSpy spy(&trk, &TrackerWorker::frameAnnotationReady);

    // 首帧 dets 为空 -> emit 空 bindings(走首帧分支)
    trk.onFrameInferred(makeFrame(0, 640, 480, {}));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(1).value<QVector<DetTrackBinding>>().size(), 0);

    // 后续 dets 为空 -> 主流程也 emit 空 bindings
    trk.onFrameInferred(makeFrame(100, 640, 480, {}));
    QCOMPARE(spy.count(), 1);
    const auto args = spy.takeFirst();
    QCOMPARE(args.at(0).value<qint64>(), static_cast<qint64>(100));
    QCOMPARE(args.at(1).value<QVector<DetTrackBinding>>().size(), 0);
}

void TrackerWorkerTest::r2_normalFrame_newAndAssociatedAndGhost_setsCorrectFields()
{
    // 这一用例覆盖 R2 三种状态位的设置:
    //   - isNewTrack:第一次进入 active
    //   - 关联成功:bestClassId 取累计面积 argmax
    //   - suppressedByGhost:命中已分拣池
    //
    // 时间线:
    //   t=0      : 伪首帧(无 det)
    //   t=100    : 大物体 A 在 (10,10,80,80)        -> 新建 track1 (isNewTrack)
    //   t=200    : 同 A 再次出现                    -> 关联成功 + 触发分拣 (Y=2)
    //              (此时 track1 进 ghost 池)
    //   t=300    : A 同位置再次出现 +
    //              新物体 B 在 (300,300,40,40)       -> A: suppressedByGhost
    //                                                  -> B: isNewTrack(track2)
    RuntimeConfig cfg = makeCfg();
    cfg.realLengthMm          = 640.0f;
    cfg.realWidthMm           = 480.0f;
    cfg.maskRasterMmPerPx     = 2.0f;
    cfg.iouThreshold          = 0.3f;
    cfg.updateFramesY         = 2;
    cfg.nominalSpeedMs        = 0.0f;
    cfg.dispatchedPoolClearMm = 10000.0f;
    cfg.enabledClassIds.insert(1);

    TrackerWorker trk;
    trk.onSessionStart(cfg);
    QSignalSpy spy(&trk, &TrackerWorker::frameAnnotationReady);

    trk.onFrameInferred(makeFrame(0, 640, 480, {}));   // 伪首帧 -> emit#1
    trk.onFrameInferred(makeFrame(100, 640, 480,
        {makeDet(1, 0.9f, 10, 10, 80, 80)}));          // A 新建 -> emit#2
    trk.onFrameInferred(makeFrame(200, 640, 480,
        {makeDet(1, 0.9f, 10, 10, 80, 80)}));          // A 关联触发 -> emit#3
    trk.onFrameInferred(makeFrame(300, 640, 480, {
        makeDet(1, 0.9f, 10, 10, 80, 80),               // A 命中 ghost
        makeDet(1, 0.9f, 300, 300, 40, 40),             // B 新建
    }));                                                // -> emit#4

    QCOMPARE(spy.count(), 4);

    // 取第 2 次:A 新建
    const auto frameNew = spy.at(1).at(1).value<QVector<DetTrackBinding>>();
    QCOMPARE(frameNew.size(), 1);
    QCOMPARE(frameNew[0].detIndex,          0);
    QVERIFY  (frameNew[0].trackId           > 0);
    QCOMPARE(frameNew[0].bestClassId,       1);
    QCOMPARE(frameNew[0].isNewTrack,        true);
    QCOMPARE(frameNew[0].suppressedByGhost, false);
    QCOMPARE(frameNew[0].firstFrameGhost,   false);
    const int trackId_A = frameNew[0].trackId;

    // 取第 3 次:A 关联(触发但 binding 在触发判定前就写好了,trackId 仍是 A)
    const auto frameAssoc = spy.at(2).at(1).value<QVector<DetTrackBinding>>();
    QCOMPARE(frameAssoc.size(), 1);
    QCOMPARE(frameAssoc[0].detIndex,          0);
    QCOMPARE(frameAssoc[0].trackId,           trackId_A);
    QCOMPARE(frameAssoc[0].bestClassId,       1);
    QCOMPARE(frameAssoc[0].isNewTrack,        false);
    QCOMPARE(frameAssoc[0].suppressedByGhost, false);
    QCOMPARE(frameAssoc[0].firstFrameGhost,   false);

    // 取第 4 次:A 被 ghost 抑制 + B 新建
    const auto frameMix = spy.at(3).at(1).value<QVector<DetTrackBinding>>();
    QCOMPARE(frameMix.size(), 2);

    // det0 = A,suppressedByGhost
    QCOMPARE(frameMix[0].detIndex,          0);
    QCOMPARE(frameMix[0].trackId,           -1);
    QCOMPARE(frameMix[0].bestClassId,       1);   // ghost.finalClassId
    QCOMPARE(frameMix[0].isNewTrack,        false);
    QCOMPARE(frameMix[0].suppressedByGhost, true);
    QCOMPARE(frameMix[0].firstFrameGhost,   false);

    // det1 = B,新建 track,trackId 不同于 A
    QCOMPARE(frameMix[1].detIndex,          1);
    QVERIFY  (frameMix[1].trackId           > 0);
    QVERIFY  (frameMix[1].trackId           != trackId_A);
    QCOMPARE(frameMix[1].bestClassId,       1);
    QCOMPARE(frameMix[1].isNewTrack,        true);
    QCOMPARE(frameMix[1].suppressedByGhost, false);
    QCOMPARE(frameMix[1].firstFrameGhost,   false);
}

QObject* makeTrackerWorkerTest() { return new TrackerWorkerTest; }

#include "test_tracker_worker.moc"
