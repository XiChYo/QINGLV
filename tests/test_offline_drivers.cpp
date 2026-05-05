// 测试 OfflineCameraDriver / OfflineEncoderDriver(M2)。
// 关键覆盖:
//   - initialize:rawDir / sessionIdx / Velocity 缺失等错误路径
//   - 时间轴选 Capture / Velocity 事件,t0_log 取自首条 Velocity
//   - 文件名 _<seq>.jpg 索引,匹配/缺失计数
//   - noThrottle:全部派发 + 不需要 onFrameConsumed
//   - 节流 + 反压:不调 onFrameConsumed 时,首帧后被反压跳并最终触发 warning
//   - filePathOf:emit 后能反查源 jpg 路径
//   - SpeedSample 两种模式:useRpm=true(K*rpm) vs false(mPerMin/60.0)

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QImage>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QVector>

#include "offline_sim/offline_camera_driver.h"
#include "offline_sim/offline_encoder_driver.h"
#include "offline_sim/log_parser.h"
#include "pipeline/pipeline_clock.h"
#include "pipeline/pipeline_types.h"

using offline_sim::CameraDriverOptions;
using offline_sim::EncoderDriverOptions;
using offline_sim::LogEvent;
using offline_sim::LogEventType;
using offline_sim::OfflineCameraDriver;
using offline_sim::OfflineEncoderDriver;

namespace {

// 构造 mock LogEvent 序列。t0_log 取首条 Velocity,这里把 Capture 都安排在
// Velocity 之后。
//
//   SessionStart  @ t=1000
//   Velocity      @ t=1100  rpm=20 mPerMin=15  (== t0_log)
//   Capture seq=10 @ t=1100
//   Capture seq=12 @ t=1200
//   Capture seq=14 @ t=1300
//   Capture seq=16 @ t=1400
//   Capture seq=18 @ t=1500
//   SessionEnd    @ t=2000
QVector<LogEvent> makeMockEvents()
{
    QVector<LogEvent> v;
    auto push = [&](LogEventType t, qint64 ms, int seq=-1, int mpm=0, int rpm=0) {
        LogEvent e;
        e.type = t;
        e.tMs  = ms;
        e.seq  = seq;
        e.mPerMin = mpm;
        e.rpm     = rpm;
        v.push_back(e);
    };
    push(LogEventType::SessionStart, 1000);
    push(LogEventType::Velocity,     1100, -1, 15, 20);
    push(LogEventType::Capture,      1100, 10);
    push(LogEventType::Capture,      1200, 12);
    push(LogEventType::Capture,      1300, 14);
    push(LogEventType::Capture,      1400, 16);
    push(LogEventType::Capture,      1500, 18);
    push(LogEventType::SessionEnd,   2000);
    return v;
}

// 在 dir 下写一个最小可读 jpg(8x8 白图),文件名形如 img_x_<seq>.jpg。
// driver 的 indexFilesBySeq 用 R"(_(\d+)\.jpg$)" 末段匹配。
bool writeMockJpg(const QString& dir, int seq)
{
    QImage img(8, 8, QImage::Format_RGB888);
    img.fill(Qt::white);
    const QString p = QStringLiteral("%1/img_x_%2.jpg").arg(dir).arg(seq);
    return img.save(p, "JPG", 80);
}

}  // namespace

class OfflineDriversTest : public QObject
{
    Q_OBJECT
private slots:
    // CameraDriver
    void cam_initialize_rejectsBadRawDir();
    void cam_initialize_rejectsBadSessionIdx();
    void cam_initialize_rejectsSessionWithoutVelocity();
    void cam_initialize_filtersBeforeT0Log();
    void cam_initialize_seqRangeTrim();
    void cam_initialize_missingFileCounted();
    void cam_noThrottle_emitsAllMatchedFrames();
    void cam_filePathOf_returnsSourcePath();
    void cam_tCaptureEqualsRawPhysicalTime();   // F12 回归
    void cam_throttleAndBackpressure_skipsAndWarns();

    // EncoderDriver
    void enc_initialize_rejectsKLeqZeroWhenUseRpm();
    void enc_initialize_rejectsBadSessionIdx();
    void enc_useRpm_emitsKTimesRpm();
    void enc_useMPerMin_emitsLineSpeedDividedBy60();
    void enc_emitsAllVelocitiesScheduled();
};

// ============================================================================
// CameraDriver — initialize 错误路径
// ============================================================================
void OfflineDriversTest::cam_initialize_rejectsBadRawDir()
{
    OfflineCameraDriver drv;
    CameraDriverOptions opt;
    opt.rawDir = QStringLiteral("/__definitely_not_a_dir__");
    QString err;
    QVERIFY(!drv.initialize(opt, makeMockEvents(), &err));
    QVERIFY(!err.isEmpty());
}

void OfflineDriversTest::cam_initialize_rejectsBadSessionIdx()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    OfflineCameraDriver drv;
    CameraDriverOptions opt;
    opt.rawDir     = tmp.path();
    opt.sessionIdx = 7;
    QString err;
    QVERIFY(!drv.initialize(opt, makeMockEvents(), &err));
    QVERIFY(err.contains(QStringLiteral("sessionIdx")));
}

void OfflineDriversTest::cam_initialize_rejectsSessionWithoutVelocity()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QVector<LogEvent> v;
    {
        LogEvent e; e.type = LogEventType::SessionStart; e.tMs = 1000; v.push_back(e);
    }
    {
        LogEvent e; e.type = LogEventType::Capture; e.tMs = 1100; e.seq = 10; v.push_back(e);
    }
    {
        LogEvent e; e.type = LogEventType::SessionEnd; e.tMs = 2000; v.push_back(e);
    }

    OfflineCameraDriver drv;
    CameraDriverOptions opt;
    opt.rawDir     = tmp.path();
    opt.sessionIdx = 0;
    QString err;
    QVERIFY(!drv.initialize(opt, v, &err));
    QVERIFY(err.contains(QStringLiteral("Velocity")));
}

void OfflineDriversTest::cam_initialize_filtersBeforeT0Log()
{
    // 在 t0_log(=1100)之前再放一条 Capture(seq=8 @ t=900),应该被过滤掉。
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QVERIFY(writeMockJpg(tmp.path(), 8));
    for (int s : {10, 12, 14, 16, 18}) QVERIFY(writeMockJpg(tmp.path(), s));

    QVector<LogEvent> v = makeMockEvents();
    {
        LogEvent e; e.type = LogEventType::Capture; e.tMs = 900; e.seq = 8;
        // 插到 SessionStart 后、Velocity 前
        v.insert(1, e);
    }

    OfflineCameraDriver drv;
    CameraDriverOptions opt;
    opt.rawDir = tmp.path();
    QVERIFY(drv.initialize(opt, v, nullptr));
    QCOMPARE(drv.scheduledCount(), 5);     // 5 个 Capture(8 被滤掉)
    QCOMPARE(drv.t0LogMs(), static_cast<qint64>(1100));
}

void OfflineDriversTest::cam_initialize_seqRangeTrim()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    for (int s : {10, 12, 14, 16, 18}) QVERIFY(writeMockJpg(tmp.path(), s));

    OfflineCameraDriver drv;
    CameraDriverOptions opt;
    opt.rawDir  = tmp.path();
    opt.seqFrom = 12;
    opt.seqTo   = 16;
    QVERIFY(drv.initialize(opt, makeMockEvents(), nullptr));
    QCOMPARE(drv.scheduledCount(), 3);     // 12,14,16
    QCOMPARE(drv.matchedFileCount(), 3);
}

void OfflineDriversTest::cam_initialize_missingFileCounted()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    // 只写 4 张 jpg(故意缺 18),5 个 Capture 全保留但 matchedFileCount=4
    for (int s : {10, 12, 14, 16}) QVERIFY(writeMockJpg(tmp.path(), s));

    OfflineCameraDriver drv;
    CameraDriverOptions opt;
    opt.rawDir = tmp.path();
    QVERIFY(drv.initialize(opt, makeMockEvents(), nullptr));
    QCOMPARE(drv.scheduledCount(), 5);
    QCOMPARE(drv.matchedFileCount(), 4);
}

// ============================================================================
// CameraDriver — 运行期(noThrottle:全部派发)
// ============================================================================
void OfflineDriversTest::cam_noThrottle_emitsAllMatchedFrames()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    for (int s : {10, 12, 14, 16}) QVERIFY(writeMockJpg(tmp.path(), s));
    // seq=18 故意缺文件:不会 emit,但 sessionEnded 仍会到。

    OfflineCameraDriver drv;
    CameraDriverOptions opt;
    opt.rawDir      = tmp.path();
    opt.noThrottle  = true;
    opt.fastForward = 0.0;     // 全部 0 ms 派发,跑得快
    QVERIFY(drv.initialize(opt, makeMockEvents(), nullptr));

    QSignalSpy spyFrame(&drv, &OfflineCameraDriver::frameReadySig);
    QSignalSpy spyEnd  (&drv, &OfflineCameraDriver::sessionEnded);
    drv.start();

    QVERIFY(spyEnd.wait(5000));
    QCOMPARE(spyFrame.count(), 4);     // 仅 4 张有文件
}

void OfflineDriversTest::cam_filePathOf_returnsSourcePath()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    for (int s : {10, 12, 14, 16, 18}) QVERIFY(writeMockJpg(tmp.path(), s));

    OfflineCameraDriver drv;
    CameraDriverOptions opt;
    opt.rawDir      = tmp.path();
    opt.noThrottle  = true;
    opt.fastForward = 0.0;
    QVERIFY(drv.initialize(opt, makeMockEvents(), nullptr));

    QSignalSpy spy(&drv, &OfflineCameraDriver::frameReadySig);
    drv.start();
    QVERIFY(QTest::qWaitFor([&] { return spy.count() >= 5; }, 5000));

    // 取一条 frameReadySig 的 tCaptureMs,反查 filePathOf
    const auto args = spy.first();
    const qint64 tCap = args.at(1).value<qint64>();
    const QString fileName = args.at(2).value<QString>();
    const QString p = drv.filePathOf(tCap);
    QVERIFY(!p.isEmpty());
    QVERIFY2(p.endsWith(fileName),
             qPrintable(QStringLiteral("filePathOf=%1 fileName=%2").arg(p, fileName)));
    QVERIFY2(QFile::exists(p), qPrintable(p));
}

// F12 回归:tCapture 必须 = raw 文件物理时间(log Capture e.tMs - t0_log = deltaMs),
// 不能用墙钟。仿真模式下 fastForward=0 让 5 帧在同一墙钟 ms 内瞬时派发,墙钟版本
// 会全部踩同一 ms(靠单调递增兜底变成 0,1,2,3,4),物理版本则严格保留 raw 真实间隔
// 0,100,200,300,400。tracker 用 tCapture 差外推 dy = speed * Δt,只有物理时间口径
// 才与 raw 文件画面里的物体真实位移一致,墙钟口径会让外推位移系统性偏离。
void OfflineDriversTest::cam_tCaptureEqualsRawPhysicalTime()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    for (int s : {10, 12, 14, 16, 18}) QVERIFY(writeMockJpg(tmp.path(), s));

    OfflineCameraDriver drv;
    CameraDriverOptions opt;
    opt.rawDir      = tmp.path();
    opt.noThrottle  = true;     // 关掉节流和反压,5 帧全部 emit
    opt.fastForward = 0.0;      // 全部立刻派发 -> 同一墙钟 ms 命中
    QVERIFY(drv.initialize(opt, makeMockEvents(), nullptr));

    QSignalSpy spy(&drv, &OfflineCameraDriver::frameReadySig);
    QSignalSpy spyEnd(&drv, &OfflineCameraDriver::sessionEnded);
    drv.start();
    QVERIFY(spyEnd.wait(5000));
    QCOMPARE(spy.count(), 5);

    // mock LogEvents Capture seq=10/12/14/16/18 分别在 t=1100/1200/1300/1400/1500,
    // t0_log=1100(首条 Velocity 时刻),所以 deltaMs 期望 0/100/200/300/400。
    QVector<qint64> expected = {0, 100, 200, 300, 400};
    for (int i = 0; i < spy.count(); ++i) {
        const qint64 tCap = spy.at(i).at(1).value<qint64>();
        QVERIFY2(tCap == expected[i],
                 qPrintable(QStringLiteral(
                     "frame %1: tCapture=%2 expected raw deltaMs=%3 (F12)")
                     .arg(i).arg(tCap).arg(expected[i])));
    }
    // 同一墙钟 ms 派发不会让 tCapture 撞:严格单调递增。
    for (int i = 1; i < spy.count(); ++i) {
        const qint64 a = spy.at(i-1).at(1).value<qint64>();
        const qint64 b = spy.at(i).at(1).value<qint64>();
        QVERIFY(b > a);
    }
}

void OfflineDriversTest::cam_throttleAndBackpressure_skipsAndWarns()
{
    // softFps + 反压:不调 onFrameConsumed,首帧 emit 后 m_inFlight=1,
    // 后续 4 帧全反压跳;warnDropThreshold 调到 2 + quietMs 调到 0,
    // 应至少 emit 1 条 warning。
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    for (int s : {10, 12, 14, 16, 18}) QVERIFY(writeMockJpg(tmp.path(), s));

    OfflineCameraDriver drv;
    CameraDriverOptions opt;
    opt.rawDir      = tmp.path();
    opt.noThrottle  = false;
    opt.softFps     = 2;       // 500 ms 节流
    opt.fastForward = 0.0;     // 5 帧瞬间到事件循环 -> 拼反压
    opt.warnDropThreshold = 2;
    opt.warnDropQuietMs   = 0;
    QVERIFY(drv.initialize(opt, makeMockEvents(), nullptr));

    QSignalSpy spyFrame(&drv, &OfflineCameraDriver::frameReadySig);
    QSignalSpy spyWarn (&drv, &OfflineCameraDriver::warning);
    QSignalSpy spyEnd  (&drv, &OfflineCameraDriver::sessionEnded);
    drv.start();
    QVERIFY(spyEnd.wait(5000));

    QCOMPARE(spyFrame.count(), 1);              // 只首帧 emit
    QVERIFY2(spyWarn.count() >= 1,
             qPrintable(QStringLiteral("warn count=%1").arg(spyWarn.count())));
}

// ============================================================================
// EncoderDriver
// ============================================================================
void OfflineDriversTest::enc_initialize_rejectsKLeqZeroWhenUseRpm()
{
    OfflineEncoderDriver drv;
    EncoderDriverOptions opt;
    opt.useRpm = true;
    opt.k_mmPerMs_perRpm = 0.0;
    QString err;
    QVERIFY(!drv.initialize(opt, makeMockEvents(), &err));
    QVERIFY(err.contains(QStringLiteral("k_mmPerMs_perRpm")));
}

void OfflineDriversTest::enc_initialize_rejectsBadSessionIdx()
{
    OfflineEncoderDriver drv;
    EncoderDriverOptions opt;
    opt.useRpm = false;
    opt.sessionIdx = 9;
    QString err;
    QVERIFY(!drv.initialize(opt, makeMockEvents(), &err));
    QVERIFY(err.contains(QStringLiteral("sessionIdx")));
}

void OfflineDriversTest::enc_useRpm_emitsKTimesRpm()
{
    // K=0.025, rpm=20 -> speedMmPerMs = 0.5
    OfflineEncoderDriver drv;
    EncoderDriverOptions opt;
    opt.useRpm = true;
    opt.k_mmPerMs_perRpm = 0.025;
    opt.fastForward = 0.0;
    QVERIFY(drv.initialize(opt, makeMockEvents(), nullptr));

    QSignalSpy spy(&drv, &OfflineEncoderDriver::speedSample);
    QSignalSpy spyEnd(&drv, &OfflineEncoderDriver::sessionEnded);
    drv.start();
    QVERIFY(spyEnd.wait(2000));

    QVERIFY(spy.count() >= 1);
    const SpeedSample s = spy.first().at(0).value<SpeedSample>();
    QVERIFY(s.valid);
    QVERIFY2(qFuzzyCompare(s.speedMmPerMs, 0.5f),
             qPrintable(QString::number(s.speedMmPerMs)));
}

void OfflineDriversTest::enc_useMPerMin_emitsLineSpeedDividedBy60()
{
    // mPerMin=15, 不用 K -> speedMmPerMs = 15/60 = 0.25
    OfflineEncoderDriver drv;
    EncoderDriverOptions opt;
    opt.useRpm = false;
    opt.fastForward = 0.0;
    QVERIFY(drv.initialize(opt, makeMockEvents(), nullptr));

    QSignalSpy spy(&drv, &OfflineEncoderDriver::speedSample);
    QSignalSpy spyEnd(&drv, &OfflineEncoderDriver::sessionEnded);
    drv.start();
    QVERIFY(spyEnd.wait(2000));

    QVERIFY(spy.count() >= 1);
    const SpeedSample s = spy.first().at(0).value<SpeedSample>();
    QVERIFY(s.valid);
    QVERIFY2(qFuzzyCompare(s.speedMmPerMs, 0.25f),
             qPrintable(QString::number(s.speedMmPerMs)));
}

void OfflineDriversTest::enc_emitsAllVelocitiesScheduled()
{
    // 我们 mock 里只 1 条 Velocity;再加 2 条做多样性。
    // 注意:必须插在 SessionEnd(makeMockEvents 的最后一项) 之前,否则
    // splitSessions 会把它们切到 session 外,scheduledCount 还是 1。
    QVector<LogEvent> v = makeMockEvents();
    QCOMPARE(v.last().type, LogEventType::SessionEnd);
    v.removeLast();   // 弹掉 SessionEnd
    {
        LogEvent e; e.type = LogEventType::Velocity;
        e.tMs = 1600; e.mPerMin = 12; e.rpm = 18;
        v.push_back(e);
    }
    {
        LogEvent e; e.type = LogEventType::Velocity;
        e.tMs = 1700; e.mPerMin = 18; e.rpm = 24;
        v.push_back(e);
    }
    {
        LogEvent e; e.type = LogEventType::SessionEnd; e.tMs = 2000;
        v.push_back(e);
    }

    OfflineEncoderDriver drv;
    EncoderDriverOptions opt;
    opt.useRpm = true;
    opt.k_mmPerMs_perRpm = 0.025;
    opt.fastForward = 0.0;
    QVERIFY(drv.initialize(opt, v, nullptr));
    QCOMPARE(drv.scheduledCount(), 3);

    QSignalSpy spy(&drv, &OfflineEncoderDriver::speedSample);
    QSignalSpy spyEnd(&drv, &OfflineEncoderDriver::sessionEnded);
    drv.start();
    QVERIFY(spyEnd.wait(2000));
    QCOMPARE(spy.count(), 3);
}

QObject* makeOfflineDriversTest() { return new OfflineDriversTest; }

#include "test_offline_drivers.moc"
