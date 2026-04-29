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
    void rasterizeToBelt_scalesPixelToMmCorrectly();
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

QObject* makeTrackerWorkerTest() { return new TrackerWorkerTest; }

#include "test_tracker_worker.moc"
