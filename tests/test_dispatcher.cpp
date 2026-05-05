// 见 test_tracker_worker.cpp 顶部说明:'#define private public' 必须放在
// system / 三方头之后,否则 OpenCV → <sstream> 的 __xfer_bufptrs 会因 access
// 替换而触发 redeclared 错误。
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <opencv2/core.hpp>

#include "pipeline/pipeline_types.h"

#define private public
#define protected public
#include "pipeline/dispatcher.h"
#undef private
#undef protected

namespace {

RuntimeConfig makeValveCfg(const QString& csv = QString())
{
    RuntimeConfig c;
    c.sorterMode            = RuntimeConfig::SorterMode::Valve;
    c.realWidthMm           = 480.0f;
    c.valveDistanceMm       = 100.0f;   // valve line yb = 580 mm
    c.maskRasterMmPerPx     = 2.0f;
    c.valveTotalChannels    = 72;
    c.valveBoards           = 8;
    c.valveChannelsPerBoard = 9;
    c.valveXMinMm           = 0.0f;
    c.valveXMaxMm           = 720.0f;   // 每通道 10 mm
    c.valveHeadSkipRatio    = 0.0f;
    c.valveOpenDurationMs   = 10;
    c.valveMinCmdIntervalMs = 10;
    c.valveSpeedRecalcPct   = 20;
    c.nominalSpeedMs        = 0.5f;     // mm/ms = m/s, fallback
    c.armStubCsv            = csv;
    return c;
}

RuntimeConfig makeArmCfg(const QString& csv)
{
    RuntimeConfig c = makeValveCfg(csv);
    c.sorterMode = RuntimeConfig::SorterMode::Arm;
    return c;
}

// 构造一个"全 1"的 mask 作 SortTask.maskBeltRaster。
// bbox 以"belt raster px"为单位(mm_per_px = 2.0),mask 大小 = bbox.size()。
SortTask makeTask(int trackId, int classId,
                  int bx_raster, int by_raster, int bw_raster, int bh_raster,
                  qint64 tCaptureMs,
                  float speedMmPerMs = 0.5f)
{
    SortTask t;
    t.trackId             = trackId;
    t.finalClassId        = classId;
    t.tCaptureMs          = tCaptureMs;
    t.bboxBeltRasterPx    = cv::Rect(bx_raster, by_raster, bw_raster, bh_raster);
    t.maskBeltRaster      = cv::Mat(bh_raster, bw_raster, CV_8UC1, cv::Scalar(1));
    t.currentSpeedMmPerMs = speedMmPerMs;
    return t;
}

}  // namespace

class DispatcherTest : public QObject
{
    Q_OBJECT
private slots:
    void valveMode_emitsEnqueuePulses_forReachableObject();
    void valveMode_returnsEmpty_whenObjectAlreadyPastValveLine();
    void valveMode_cancelPulses_onSessionStop();
    void armMode_writesCsvAndEmitsArmStubDispatched();
    void speedSample_triggersRecomputation_whenChangeExceedsThreshold();
    void speedSample_noRecomputation_whenChangeWithinThreshold();
};

void DispatcherTest::valveMode_emitsEnqueuePulses_forReachableObject()
{
    Dispatcher dsp;
    const RuntimeConfig cfg = makeValveCfg();
    dsp.onSessionStart(cfg);

    // valve line yb = 580 mm; 设一个 bbox.y = 50 raster => 100 mm (yb),远未到阀线。
    // bbox 宽度 5 raster = 10 mm,刚好覆盖第 0 条通道。
    QSignalSpy enq(&dsp, &Dispatcher::enqueuePulses);
    QSignalSpy warn(&dsp, &Dispatcher::warning);

    const SortTask task = makeTask(1, 3, /*bx*/0, /*by*/50, /*bw*/5, /*bh*/5,
                                   /*tCaptureMs*/1000, /*speed*/0.5f);
    dsp.onSortTask(task);

    QCOMPARE(enq.count(), 1);
    QCOMPARE(warn.count(), 0);

    const QVector<ValvePulse> pulses = enq.takeFirst().at(0).value<QVector<ValvePulse>>();
    QVERIFY(!pulses.isEmpty());
    QCOMPARE(static_cast<int>(pulses.front().boardId), 1);   // 第 0 通道 -> board 1
    QVERIFY(pulses.front().channelMask & 0x0001u);           // bit0
    // tOpen 应落在 tCapture 之后(物体到阀线才开),tClose > tOpen。
    QVERIFY(pulses.front().tOpenMs >= task.tCaptureMs);
    QVERIFY(pulses.front().tCloseMs > pulses.front().tOpenMs);
}

void DispatcherTest::valveMode_returnsEmpty_whenObjectAlreadyPastValveLine()
{
    Dispatcher dsp;
    RuntimeConfig cfg = makeValveCfg();
    dsp.onSessionStart(cfg);

    // 把 bbox.y 放到远超 valveLine 的位置:bbox.y = 400 raster => 800 mm > 580 mm。
    // mask 的后沿更在后方,computePulses 判定 dy_tail<0,返回空。
    QSignalSpy enq(&dsp, &Dispatcher::enqueuePulses);
    QSignalSpy warn(&dsp, &Dispatcher::warning);

    const SortTask task = makeTask(2, 1, 0, 400, 5, 5, 1000, 0.5f);
    dsp.onSortTask(task);

    QCOMPARE(enq.count(), 0);
    QCOMPARE(warn.count(), 1);              // 空脉冲 -> 发告警
    QCOMPARE(dsp.m_pending.size(), 0);      // 空脉冲不进 pending
}

void DispatcherTest::valveMode_cancelPulses_onSessionStop()
{
    Dispatcher dsp;
    const RuntimeConfig cfg = makeValveCfg();
    dsp.onSessionStart(cfg);

    QSignalSpy enq(&dsp, &Dispatcher::enqueuePulses);
    QSignalSpy cancel(&dsp, &Dispatcher::cancelPulses);

    dsp.onSortTask(makeTask(10, 3, 0, 50, 5, 5, 1000, 0.5f));
    dsp.onSortTask(makeTask(11, 3, 0, 60, 5, 5, 1000, 0.5f));
    QCOMPARE(enq.count(), 2);

    dsp.onSessionStop();
    QCOMPARE(cancel.count(), 2);            // 两个 pending 都被 cancel
    QCOMPARE(dsp.m_pending.size(), 0);
    QCOMPARE(dsp.m_sessionActive, false);
}

void DispatcherTest::armMode_writesCsvAndEmitsArmStubDispatched()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString csv = dir.path() + "/arm.csv";

    Dispatcher dsp;
    RuntimeConfig cfg = makeArmCfg(csv);
    cfg.armAOriginXMm = 0.0f;
    cfg.armAOriginYMm = 0.0f;
    cfg.armAXSign     = 1;
    cfg.armAYSign     = 1;
    dsp.onSessionStart(cfg);

    QSignalSpy emitted(&dsp, &Dispatcher::armStubDispatched);

    // bbox 中心: (bx + bw/2) * mm_per_px = (10+5)*2 = 30mm;(by + bh/2)*2 = (20+5)*2 = 50mm
    dsp.onSortTask(makeTask(5, 7, 10, 20, 10, 10, /*tCapture*/1000, 0.5f));
    QCOMPARE(emitted.count(), 1);
    const auto args = emitted.takeFirst();
    QCOMPARE(args.at(0).toInt(), 5);   // trackId
    QCOMPARE(args.at(1).toInt(), 7);   // classId
    QCOMPARE(args.at(2).toFloat(), 30.0f);  // armA_x
    QCOMPARE(args.at(3).toFloat(), 50.0f);  // armA_y

    // 文件必然已经创建、含表头 + 一行。
    dsp.onSessionStop();  // flush + close
    QFile f(csv);
    QVERIFY(f.exists());
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray body = f.readAll();
    QVERIFY(body.startsWith("ts_iso,trackId,classId,cx_mm,cy_mm,armA_x,armA_y,armB_x,armB_y"));
    QVERIFY(body.contains(",5,7,"));   // trackId/classId 正确写入
}

void DispatcherTest::speedSample_triggersRecomputation_whenChangeExceedsThreshold()
{
    Dispatcher dsp;
    RuntimeConfig cfg = makeValveCfg();
    cfg.valveSpeedRecalcPct = 20;   // 20%
    dsp.onSessionStart(cfg);

    QSignalSpy enq(&dsp, &Dispatcher::enqueuePulses);
    QSignalSpy cancel(&dsp, &Dispatcher::cancelPulses);

    // 先产生一个 pending task:onSortTask 使用 lastSpeed.valid=false 走 cfg.nominalSpeedMs=0.5
    dsp.onSortTask(makeTask(42, 3, 0, 50, 5, 5, 1000, 0.5f));
    QCOMPARE(enq.count(), 1);

    // 第一次速度样本:设定 baseline = 0.5
    SpeedSample s;
    s.valid = true; s.tMs = 1200; s.speedMmPerMs = 0.5f;
    dsp.onSpeedSample(s);
    QCOMPARE(cancel.count(), 0);   // 与 baseline 一致,不触发

    // 第二次速度样本:0.7 vs 0.5 = 40% 偏差,超过 20% -> 重算
    s.tMs = 1400; s.speedMmPerMs = 0.7f;
    dsp.onSpeedSample(s);
    QCOMPARE(cancel.count(), 1);
    QCOMPARE(enq.count(), 2);
}

void DispatcherTest::speedSample_noRecomputation_whenChangeWithinThreshold()
{
    Dispatcher dsp;
    RuntimeConfig cfg = makeValveCfg();
    cfg.valveSpeedRecalcPct = 20;
    dsp.onSessionStart(cfg);

    QSignalSpy cancel(&dsp, &Dispatcher::cancelPulses);

    dsp.onSortTask(makeTask(42, 3, 0, 50, 5, 5, 1000, 0.5f));

    SpeedSample s; s.valid = true; s.tMs = 1200; s.speedMmPerMs = 0.5f;
    dsp.onSpeedSample(s);
    s.tMs = 1400; s.speedMmPerMs = 0.55f;    // 10%,低于 20%
    dsp.onSpeedSample(s);

    QCOMPARE(cancel.count(), 0);
}

QObject* makeDispatcherTest() { return new DispatcherTest; }

#include "test_dispatcher.moc"
