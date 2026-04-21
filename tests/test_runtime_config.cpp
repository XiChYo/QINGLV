#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>

#include "runtime_config.h"

class RuntimeConfigTest : public QObject
{
    Q_OBJECT
private slots:
    void loadRuntimeConfig_missingFileFallsBackToDefaults();
    void loadRuntimeConfig_populatesAllSections();
    void loadRuntimeConfig_armSorterModeParsed();
    void loadRuntimeConfig_classButtonsParsedInOrder();

private:
    void writeIni(const QString& path, const QString& body);
};

void RuntimeConfigTest::writeIni(const QString& path, const QString& body)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream ts(&f);
    ts.setCodec("UTF-8");
    ts << body;
    f.close();
}

void RuntimeConfigTest::loadRuntimeConfig_missingFileFallsBackToDefaults()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/no_such.ini";
    const RuntimeConfig cfg = loadRuntimeConfig(path);

    // 默认值必须与 runtime_config.h 的成员初始化一致。
    QCOMPARE(cfg.cameraIp, QStringLiteral("192.168.1.30"));
    QCOMPARE(cfg.softFps, 2);
    QCOMPARE(cfg.valveTotalChannels, 72);
    QCOMPARE(cfg.valveOpenDurationMs, 50);
    QCOMPARE(cfg.sorterMode, RuntimeConfig::SorterMode::Valve);
}

void RuntimeConfigTest::loadRuntimeConfig_populatesAllSections()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/cfg.ini";
    const QString body = QStringLiteral(R"INI(
[camera]
ip=10.0.0.7
hw_fps=30
real_length_mm=600
real_width_mm=400

[model]
path=/tmp/m.rknn
input_size=1280
topk_class_count=5

[yolo]
conf_threshold=0.35
nms_threshold=0.5
draw_overlay=false

[pipeline]
soft_fps=4
iou_threshold=0.2
miss_frames_x=3
update_frames_y=7
mask_raster_mm_per_px=1.5
tick_interval_ms=10
dispatched_pool_clear_distance_mm=300

[belt]
nominal_speed_m_s=0.8
encoder_pulse_to_mm=0.2
encoder_request_interval_ms=250

[sorter]
mode=valve
arm_distance_mm=400
valve_distance_mm=700

[valve]
total_channels=36
boards=4
channels_per_board=9
x_min_mm=10
x_max_mm=490
head_skip_ratio=0.2
open_duration_ms=60
min_cmd_interval_ms=80
speed_recalc_threshold_pct=25

[arm_stub]
a_origin_x_mm=100
a_origin_y_mm=200
a_x_sign=-1
a_y_sign=1
b_origin_x_mm=300
b_origin_y_mm=400
b_x_sign=1
b_y_sign=-1

[persistence]
save_raw=true
raw_sample_ratio=0.25
save_result=true
save_dir=/tmp/caps
arm_stub_csv=/tmp/arm.csv
)INI");
    writeIni(path, body);

    const RuntimeConfig cfg = loadRuntimeConfig(path);

    QCOMPARE(cfg.cameraIp, QStringLiteral("10.0.0.7"));
    QCOMPARE(cfg.cameraHwFps, 30);
    QCOMPARE(cfg.realLengthMm, 600.0f);
    QCOMPARE(cfg.realWidthMm,  400.0f);

    QCOMPARE(cfg.modelPath, QStringLiteral("/tmp/m.rknn"));
    QCOMPARE(cfg.modelInputSize, 1280);
    QCOMPARE(cfg.modelTopkClassCount, 5);
    QCOMPARE(cfg.confThreshold, 0.35f);
    QCOMPARE(cfg.nmsThreshold, 0.5f);
    QCOMPARE(cfg.drawOverlay, false);

    QCOMPARE(cfg.softFps, 4);
    QCOMPARE(cfg.iouThreshold, 0.2f);
    QCOMPARE(cfg.missFramesX, 3);
    QCOMPARE(cfg.updateFramesY, 7);
    QCOMPARE(cfg.maskRasterMmPerPx, 1.5f);
    QCOMPARE(cfg.tickIntervalMs, 10);
    QCOMPARE(cfg.dispatchedPoolClearMm, 300.0f);

    QCOMPARE(cfg.nominalSpeedMs, 0.8f);
    QCOMPARE(cfg.encoderPulseToMm, 0.2f);
    QCOMPARE(cfg.encoderRequestIntervalMs, 250);

    QCOMPARE(cfg.sorterMode, RuntimeConfig::SorterMode::Valve);
    QCOMPARE(cfg.valveDistanceMm, 700.0f);

    QCOMPARE(cfg.valveTotalChannels, 36);
    QCOMPARE(cfg.valveBoards, 4);
    QCOMPARE(cfg.valveChannelsPerBoard, 9);
    QCOMPARE(cfg.valveXMinMm, 10.0f);
    QCOMPARE(cfg.valveXMaxMm, 490.0f);
    QCOMPARE(cfg.valveHeadSkipRatio, 0.2f);
    QCOMPARE(cfg.valveOpenDurationMs, 60);
    QCOMPARE(cfg.valveMinCmdIntervalMs, 80);
    QCOMPARE(cfg.valveSpeedRecalcPct, 25);

    QCOMPARE(cfg.armAOriginXMm, 100.0f);
    QCOMPARE(cfg.armAXSign, -1);
    QCOMPARE(cfg.armBYSign, -1);

    QCOMPARE(cfg.saveRaw, true);
    QCOMPARE(cfg.rawSampleRatio, 0.25f);
    QCOMPARE(cfg.armStubCsv, QStringLiteral("/tmp/arm.csv"));
}

void RuntimeConfigTest::loadRuntimeConfig_armSorterModeParsed()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/cfg.ini";
    writeIni(path, "[sorter]\nmode=arm\n");
    RuntimeConfig cfg = loadRuntimeConfig(path);
    QCOMPARE(cfg.sorterMode, RuntimeConfig::SorterMode::Arm);

    writeIni(path, "[sorter]\nmode=ARM\n");
    cfg = loadRuntimeConfig(path);
    QCOMPARE(cfg.sorterMode, RuntimeConfig::SorterMode::Arm);

    writeIni(path, "[sorter]\nmode=other\n");
    cfg = loadRuntimeConfig(path);
    QCOMPARE(cfg.sorterMode, RuntimeConfig::SorterMode::Valve);
}

void RuntimeConfigTest::loadRuntimeConfig_classButtonsParsedInOrder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/cfg.ini";
    // class_btn 的 key 实际是 "class_btn/N/name" 形式,runtime_config 通过
    // s.value(QString("class_btn/%1/name").arg(i)) 读取。
    writeIni(path, QStringLiteral(R"INI(
[model]
class_btn/size=3
class_btn/1/name=Apple
class_btn/1/id=0
class_btn/2/name=Banana
class_btn/2/id=7
class_btn/3/name=Rotten
class_btn/3/id=9
)INI"));
    const RuntimeConfig cfg = loadRuntimeConfig(path);
    QCOMPARE(cfg.classButtons.size(), 3);
    QCOMPARE(cfg.classButtons[0].name, QStringLiteral("Apple"));
    QCOMPARE(cfg.classButtons[0].classId, 0);
    QCOMPARE(cfg.classButtons[1].name, QStringLiteral("Banana"));
    QCOMPARE(cfg.classButtons[1].classId, 7);
    QCOMPARE(cfg.classButtons[2].classId, 9);
}

QObject* makeRuntimeConfigTest() { return new RuntimeConfigTest; }

#include "test_runtime_config.moc"
