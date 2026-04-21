#ifndef RUNTIME_CONFIG_H
#define RUNTIME_CONFIG_H

// ============================================================================
// 运行时配置:由 MainWindow 在任务启动时从 config.ini 读取一次,
// 通过 sessionStart(cfg) 信号下发给所有 worker。worker 本地保存副本,
// 运行中不再回读文件。
// 字段命名/分组与 docs/design.md §5 一致。
// ============================================================================

#include <QString>
#include <QVector>
#include <QSet>
#include <QMetaType>

struct ClassButton
{
    QString name;   // 中文可读
    int     classId = -1;
};

struct RuntimeConfig
{
    // ---- camera ----
    QString cameraIp       = "192.168.1.30";
    int     cameraHwFps    = 60;
    bool    cameraYFlip    = false;
    float   realLengthMm   = 560.0f;   // 图像 x 方向对应物理宽度
    float   realWidthMm    = 470.0f;   // 图像 y 方向对应物理长度

    // ---- model & yolo ----
    QString modelPath             = "/opt/models/yolov8seg.rknn";
    int     modelInputSize        = 1024;
    int     modelTopkClassCount   = 80;     // 兼容 top-k 布局的兜底
    float   confThreshold         = 0.25f;
    float   nmsThreshold          = 0.45f;
    bool    drawOverlay           = true;   // 是否给 UI 发带 mask/box/label 的 overlay 图
    QVector<ClassButton> classButtons;

    // ---- pipeline ----
    int     softFps                 = 2;
    float   iouThreshold            = 0.30f;
    int     missFramesX             = 2;
    int     updateFramesY           = 10;
    float   dispatchedPoolClearMm   = 200.0f;
    float   maskRasterMmPerPx       = 2.0f;
    int     tickIntervalMs          = 5;

    // ---- belt & encoder ----
    float   nominalSpeedMs              = 0.5f;  // m/s
    float   encoderPulseToMm            = 0.1f;
    int     encoderRequestIntervalMs    = 500;

    // ---- sorter ----
    enum class SorterMode { Valve, Arm };
    SorterMode sorterMode        = SorterMode::Valve;
    float      armDistanceMm     = 500.0f;
    float      valveDistanceMm   = 800.0f;

    // ---- valve ----
    int     valveTotalChannels   = 72;
    int     valveBoards          = 8;
    int     valveChannelsPerBoard = 9;
    float   valveXMinMm          = 0.0f;
    float   valveXMaxMm          = 560.0f;
    float   valveHeadSkipRatio   = 0.125f;
    int     valveOpenDurationMs  = 50;
    int     valveMinCmdIntervalMs = 50;
    int     valveSpeedRecalcPct  = 20;

    // ---- arm stub (本期仅日志) ----
    float   armAOriginXMm = 0.0f;
    float   armAOriginYMm = 0.0f;
    int     armAXSign     = 1;
    int     armAYSign     = 1;
    float   armBOriginXMm = 0.0f;
    float   armBOriginYMm = 0.0f;
    int     armBXSign     = 1;
    int     armBYSign     = 1;

    // ---- persistence ----
    bool    saveRaw           = false;
    float   rawSampleRatio    = 0.1f;
    bool    saveResult        = false;
    QString saveDir           = "/data/captures";
    QString armStubCsv        = "/data/logs/arm_stub.csv";

    // ---- 运行期 session 选项(从 UI 合成) ----
    // 前端"品类"按钮勾选集合对应的 class_id
    QSet<int> enabledClassIds;
};

// 从 config.ini 加载一份配置(复用 QSettings)。
// path 为 ini 文件路径,传空时使用 "config.ini"(工作目录)。
RuntimeConfig loadRuntimeConfig(const QString& iniPath = QString());

Q_DECLARE_METATYPE(RuntimeConfig)

#endif // RUNTIME_CONFIG_H
