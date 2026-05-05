// ============================================================================
// offline_sim/main.cpp(M3)
// 离线仿真主程序:用 saveRawPic/log.txt 模拟相机 + 编码器,串接生产代码的
// YoloWorker / TrackerWorker / Dispatcher,把 enqueuePulses / sortTaskReady /
// frameAnnotationReady 等出口产物落到 offline_sim_outputs/<run_ts>/。
// 装配关系详见《测试需求.md》§4.2;CLI 参数详见 §7.5(本里程碑追加)。
// ============================================================================

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QThread>
#include <QTimer>

#include "config/runtime_config.h"
#include "infra/logger.h"
#include "pipeline/dispatcher.h"
#include "pipeline/pipeline_clock.h"
#include "pipeline/pipeline_types.h"
#include "pipeline/tracker_worker.h"
#include "pipeline/yolo_worker.h"

#include "annotated_frame_sink.h"
#include "log_parser.h"
#include "offline_camera_driver.h"
#include "offline_encoder_driver.h"
#include "sort_task_sink.h"
#include "valve_sink.h"

using offline_sim::AnnotatedFrameSink;
using offline_sim::CameraDriverOptions;
using offline_sim::EncoderDriverOptions;
using offline_sim::LogEvent;
using offline_sim::LogParser;
using offline_sim::OfflineCameraDriver;
using offline_sim::OfflineEncoderDriver;
using offline_sim::SortTaskSink;
using offline_sim::ValveSink;

namespace {

void registerMetaTypes()
{
    qRegisterMetaType<DetectedObject>("DetectedObject");
    qRegisterMetaType<DetectedFrame>("DetectedFrame");
    qRegisterMetaType<TrackedObject>("TrackedObject");
    qRegisterMetaType<DispatchedGhost>("DispatchedGhost");
    qRegisterMetaType<SortTask>("SortTask");
    qRegisterMetaType<SpeedSample>("SpeedSample");
    qRegisterMetaType<ValvePulse>("ValvePulse");
    qRegisterMetaType<QVector<ValvePulse>>("QVector<ValvePulse>");
    qRegisterMetaType<DetTrackBinding>("DetTrackBinding");
    qRegisterMetaType<QVector<DetTrackBinding>>("QVector<DetTrackBinding>");
    qRegisterMetaType<RuntimeConfig>("RuntimeConfig");
}

// 从 sim-ini 读 K(单位 mm/ms·per_rpm)。无 key 返回 0.0。
double loadKFromSimIni(const QString& iniPath)
{
    if (iniPath.isEmpty() || !QFile::exists(iniPath)) return 0.0;
    QSettings s(iniPath, QSettings::IniFormat);
    s.beginGroup("encoder");
    const double k = s.value("k_mm_per_ms_per_rpm", 0.0).toDouble();
    s.endGroup();
    return k;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("offline_sim");
    QCoreApplication::setApplicationVersion("1.0");

    pipeline::initClock();
    Logger::initialize();
    registerMetaTypes();

    // ------------------------------------------------------------------------
    // CLI
    // ------------------------------------------------------------------------
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "离线仿真:saveRawPic + log.txt -> 产物 offline_sim_outputs/<ts>/{annotated_frames,*.csv}");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption optDataDir(QStringList() << "data-dir",
        "saveRawPic 根目录(含 log.txt 与 *.jpg)。",
        "dir", "/home/pi/offline_sim_data/saveRawPic");
    QCommandLineOption optModel(QStringList() << "model",
        "RKNN 模型路径(仿真专用,与 §2.3 一致)。",
        "path", "/home/pi/code/QINGLV/models/model-n-1024-robotic-final.rknn");
    QCommandLineOption optConfig(QStringList() << "config",
        "config.ini 路径(空则用 QSettings 默认 cwd)。",
        "path", "");
    QCommandLineOption optSimIni(QStringList() << "sim-ini",
        "tests/offline_sim/offline_sim.ini(读 K 标定值)。",
        "path", "/home/pi/code/QINGLV/tests/offline_sim/offline_sim.ini");
    QCommandLineOption optOutput(QStringList() << "output",
        "产物输出根目录(实际落到 <output>/<run_ts>/)。",
        "dir", "/home/pi/offline_sim_outputs");
    QCommandLineOption optSession(QStringList() << "session",
        "选哪段 session(0=11:05 段, 1=11:26 段)。",
        "idx", "0");
    QCommandLineOption optSeqFrom(QStringList() << "seq-from",
        "起始 seq(闭区间;-1 不限制)。",
        "n", "2");
    QCommandLineOption optSeqTo(QStringList() << "seq-to",
        "结束 seq(闭区间;-1 不限制)。",
        "n", "334");
    QCommandLineOption optSoftFps(QStringList() << "soft-fps",
        "软节流 fps(0 等价 --no-throttle)。",
        "n", "2");
    QCommandLineOption optNoThrottle(QStringList() << "no-throttle",
        "标定/调试用:节流 + 反压全部短路,所有 Capture 都 emit。");
    QCommandLineOption optFastForward(QStringList() << "fast-forward",
        "时间轴加速因子(1=原速;>1 加速;0=瞬时派发,与 --no-throttle 配合)。",
        "f", "1.0");
    QCommandLineOption optK(QStringList() << "k",
        "K 直接覆盖 sim-ini(单位 mm/ms·per_rpm,§3.5)。",
        "f", "");
    QCommandLineOption optUseMperMin(QStringList() << "use-mpermin",
        "编码器换算用 log 里 mPerMin/60(对照基线;业务方告知不准,默认 useRpm)。");
    QCommandLineOption optConfThr(QStringList() << "conf-thr",
        "覆盖 cfg.confThreshold(默认按 config.ini 0.25;§3.4 AC19 跑 0.9 用)。",
        "f", "");
    QCommandLineOption optExitDelaySec(QStringList() << "exit-delay-sec",
        "cam.sessionEnded 后再等几秒让 in-flight 帧落地再退出。",
        "n", "5");

    parser.addOption(optDataDir);
    parser.addOption(optModel);
    parser.addOption(optConfig);
    parser.addOption(optSimIni);
    parser.addOption(optOutput);
    parser.addOption(optSession);
    parser.addOption(optSeqFrom);
    parser.addOption(optSeqTo);
    parser.addOption(optSoftFps);
    parser.addOption(optNoThrottle);
    parser.addOption(optFastForward);
    parser.addOption(optK);
    parser.addOption(optUseMperMin);
    parser.addOption(optConfThr);
    parser.addOption(optExitDelaySec);
    parser.process(app);

    const QString dataDir   = parser.value(optDataDir);
    const QString modelPath = parser.value(optModel);
    const QString cfgPath   = parser.value(optConfig);
    const QString simIni    = parser.value(optSimIni);
    const QString outRoot   = parser.value(optOutput);
    const int     sessionIdx= parser.value(optSession).toInt();
    const int     seqFrom   = parser.value(optSeqFrom).toInt();
    const int     seqTo     = parser.value(optSeqTo).toInt();
    const int     softFps   = parser.value(optSoftFps).toInt();
    const bool    noThrottle= parser.isSet(optNoThrottle);
    const double  ff        = parser.value(optFastForward).toDouble();
    const bool    useMperMin= parser.isSet(optUseMperMin);
    const int     exitDelay = parser.value(optExitDelaySec).toInt();

    // K 优先级:--k > sim-ini > 0.0(若 useRpm 必要则报错)
    double K = 0.0;
    if (parser.isSet(optK)) {
        K = parser.value(optK).toDouble();
    } else {
        K = loadKFromSimIni(simIni);
    }

    // ------------------------------------------------------------------------
    // 解析 log
    // ------------------------------------------------------------------------
    const QString logPath = dataDir + "/log.txt";
    QString err;
    QVector<LogEvent> events = LogParser::parseFile(logPath, &err);
    if (events.isEmpty()) {
        qCritical().noquote() << QStringLiteral("[main] LogParser 解析失败: %1").arg(err);
        return 2;
    }
    qInfo().noquote() << QStringLiteral("[main] 解析到 %1 条事件 (log=%2)")
                             .arg(events.size()).arg(logPath);

    // ------------------------------------------------------------------------
    // 加载 RuntimeConfig + 仿真覆盖(§2.1 物理尺寸 / §3.4 关掉 YOLO 自落盘)
    // ------------------------------------------------------------------------
    RuntimeConfig cfg = loadRuntimeConfig(cfgPath);
    cfg.realLengthMm  = 1520.0f;
    cfg.realWidthMm   = 1270.0f;
    cfg.modelPath     = modelPath;
    cfg.softFps       = softFps;
    cfg.saveRaw       = false;
    cfg.saveResult    = false;          // 关掉 YoloWorker::result_*.jpg(§4.4)
    cfg.drawOverlay   = false;          // 我们自己出 annotated;不要 yolo 内部 overlay
    if (parser.isSet(optConfThr)) {
        cfg.confThreshold = parser.value(optConfThr).toFloat();
    }

    // ------------------------------------------------------------------------
    // 输出目录
    // ------------------------------------------------------------------------
    const QString runTs = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString outDir = outRoot + "/" + runTs;
    if (!QDir().mkpath(outDir + "/annotated_frames")) {
        qCritical().noquote() << QStringLiteral("[main] 创建输出目录失败: %1").arg(outDir);
        return 2;
    }
    qInfo().noquote() << QStringLiteral("[main] 产物落到: %1").arg(outDir);

    // ------------------------------------------------------------------------
    // Driver / Worker / Sink 构造
    // ------------------------------------------------------------------------
    OfflineCameraDriver  cam;
    OfflineEncoderDriver enc;
    YoloWorker           yolo;
    TrackerWorker        tracker;
    Dispatcher           dispatcher;

    ValveSink            valveSink (outDir + "/valve_commands.csv");
    SortTaskSink         stSink    (outDir + "/sort_tasks.csv", cfg);
    AnnotatedFrameSink   annSink   (&cam, cfg,
                                    outDir + "/annotated_frames",
                                    outDir + "/frame_assoc.csv");

    if (!valveSink.open(&err)) { qCritical().noquote() << err; return 2; }
    if (!stSink.open(&err))    { qCritical().noquote() << err; return 2; }
    if (!annSink.open(&err))   { qCritical().noquote() << err; return 2; }

    // ------------------------------------------------------------------------
    // Driver initialize
    // ------------------------------------------------------------------------
    {
        CameraDriverOptions opt;
        opt.rawDir       = dataDir;
        opt.sessionIdx   = sessionIdx;
        opt.seqFrom      = seqFrom;
        opt.seqTo        = seqTo;
        opt.softFps      = softFps;
        opt.noThrottle   = noThrottle;
        opt.fastForward  = ff;
        if (!cam.initialize(opt, events, &err)) {
            qCritical().noquote() << QStringLiteral("[main] CameraDriver init: %1").arg(err);
            return 2;
        }
        qInfo().noquote() << QStringLiteral(
            "[main] CameraDriver: scheduledCount=%1 matchedFile=%2 t0Log=%3")
            .arg(cam.scheduledCount()).arg(cam.matchedFileCount()).arg(cam.t0LogMs());
    }
    {
        EncoderDriverOptions opt;
        opt.sessionIdx        = sessionIdx;
        opt.useRpm            = !useMperMin;
        opt.k_mmPerMs_perRpm  = K;
        opt.fastForward       = ff;
        if (opt.useRpm && K <= 0.0) {
            qCritical().noquote() << QStringLiteral(
                "[main] EncoderDriver useRpm=true 但 K<=0(=%1)。"
                "请先跑 calibrate_rpm_to_speed 标定 K,或显式 --use-mpermin 用 log 线速度对照基线。")
                .arg(K);
            return 2;
        }
        if (!enc.initialize(opt, events, &err)) {
            qCritical().noquote() << QStringLiteral("[main] EncoderDriver init: %1").arg(err);
            return 2;
        }
        qInfo().noquote() << QStringLiteral(
            "[main] EncoderDriver: scheduledCount=%1 useRpm=%2 K=%3")
            .arg(enc.scheduledCount()).arg(opt.useRpm ? "true" : "false")
            .arg(QString::number(K, 'g', 6));
    }

    // ------------------------------------------------------------------------
    // 线程模型(与 mainwindow.cpp 一致):每个 worker 一个 QThread
    // ------------------------------------------------------------------------
    QThread tCam, tEnc, tYolo, tTrk, tDp, tAnn;
    cam.moveToThread(&tCam);
    enc.moveToThread(&tEnc);
    yolo.moveToThread(&tYolo);
    tracker.moveToThread(&tTrk);
    dispatcher.moveToThread(&tDp);
    annSink.moveToThread(&tAnn);

    tCam.start();
    tEnc.start();
    tYolo.start();
    tTrk.start();
    tDp.start();
    tAnn.start();

    // ------------------------------------------------------------------------
    // 信号连线(与 mainwindow.cpp 同口径,《测试需求.md》§4.2)
    // ------------------------------------------------------------------------
    QObject::connect(&cam,  &OfflineCameraDriver::frameReadySig,
                     &yolo, &YoloWorker::onFrame);
    QObject::connect(&yolo, &YoloWorker::frameConsumed,
                     &cam,  &OfflineCameraDriver::onFrameConsumed);
    QObject::connect(&yolo, &YoloWorker::detectedFrameReady,
                     &tracker, &TrackerWorker::onFrameInferred);
    QObject::connect(&enc,  &OfflineEncoderDriver::speedSample,
                     &tracker, &TrackerWorker::onSpeedSample);
    QObject::connect(&enc,  &OfflineEncoderDriver::speedSample,
                     &dispatcher, &Dispatcher::onSpeedSample);
    QObject::connect(&tracker, &TrackerWorker::sortTaskReady,
                     &dispatcher, &Dispatcher::onSortTask);
    QObject::connect(&tracker, &TrackerWorker::sortTaskReady,
                     &stSink,    &SortTaskSink::onSortTask);
    QObject::connect(&dispatcher, &Dispatcher::enqueuePulses,
                     &valveSink,  &ValveSink::onEnqueue);
    QObject::connect(&dispatcher, &Dispatcher::cancelPulses,
                     &valveSink,  &ValveSink::onCancel);
    // AnnotatedFrameSink 双信号 join
    QObject::connect(&yolo,    &YoloWorker::detectedFrameReady,
                     &annSink, &AnnotatedFrameSink::onDetectedFrame);
    QObject::connect(&tracker, &TrackerWorker::frameAnnotationReady,
                     &annSink, &AnnotatedFrameSink::onFrameAnnotation);

    // 警告/错误汇总:打到 stderr;后续 §1.3 warnings.log 由 logger 自身落
    QObject::connect(&cam, &OfflineCameraDriver::warning,
                     [](const QString& m){ qWarning().noquote() << "[CAM]" << m; });
    QObject::connect(&dispatcher, &Dispatcher::warning,
                     [](const QString& m){ qWarning().noquote() << "[DSP]" << m; });

    // ------------------------------------------------------------------------
    // session 启动:preloadModel → sessionStart → driver.start
    // ------------------------------------------------------------------------
    QMetaObject::invokeMethod(&yolo, "preloadModel", Qt::QueuedConnection,
                              Q_ARG(QString, cfg.modelPath));
    QMetaObject::invokeMethod(&yolo, "sessionStart", Qt::QueuedConnection,
                              Q_ARG(RuntimeConfig, cfg));
    QMetaObject::invokeMethod(&tracker, "onSessionStart", Qt::QueuedConnection,
                              Q_ARG(RuntimeConfig, cfg));
    QMetaObject::invokeMethod(&dispatcher, "onSessionStart", Qt::QueuedConnection,
                              Q_ARG(RuntimeConfig, cfg));

    QMetaObject::invokeMethod(&cam, "start", Qt::QueuedConnection);
    QMetaObject::invokeMethod(&enc, "start", Qt::QueuedConnection);

    // cam.sessionEnded -> 等 exitDelay 秒让最后几帧 flush -> quit
    QObject::connect(&cam, &OfflineCameraDriver::sessionEnded,
                     [exitDelay]() {
                         qInfo().noquote() << QStringLiteral(
                             "[main] cam.sessionEnded,等 %1 秒后退出").arg(exitDelay);
                         QTimer::singleShot(exitDelay * 1000, []() {
                             QCoreApplication::quit();
                         });
                     });

    const int rc = app.exec();

    // ------------------------------------------------------------------------
    // teardown(顺序很关键:先停 driver 不再产帧 -> 再关 worker -> 关 sink -> 退线程)
    // ------------------------------------------------------------------------
    QMetaObject::invokeMethod(&cam, "stop", Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(&enc, "stop", Qt::BlockingQueuedConnection);

    QMetaObject::invokeMethod(&dispatcher, "onSessionStop", Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(&yolo,       "sessionStop",   Qt::BlockingQueuedConnection);

    valveSink.close();
    stSink.close();
    annSink.close();

    qInfo().noquote() << QStringLiteral(
        "[main] 收尾统计: valveSink enqueue=%1 cancel=%2; sortTaskSink rows=%3; "
        "annSink rendered=%4 pending=%5")
        .arg(valveSink.enqueueRowCount()).arg(valveSink.cancelRowCount())
        .arg(stSink.rowCount())
        .arg(annSink.renderedCount()).arg(annSink.pendingCount());

    tCam.quit();  tCam.wait();
    tEnc.quit();  tEnc.wait();
    tYolo.quit(); tYolo.wait();
    tTrk.quit();  tTrk.wait();
    tDp.quit();   tDp.wait();
    tAnn.quit();  tAnn.wait();

    return rc;
}
