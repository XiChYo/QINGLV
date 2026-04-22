#include "config/runtime_config.h"

#include <QSettings>
#include <QStringList>

RuntimeConfig loadRuntimeConfig(const QString& iniPath)
{
    const QString path = iniPath.isEmpty() ? QStringLiteral("config.ini") : iniPath;

    QSettings s(path, QSettings::IniFormat);
    s.setIniCodec("UTF-8");

    RuntimeConfig cfg;

    // [camera]
    s.beginGroup("camera");
    cfg.cameraIp      = s.value("ip", cfg.cameraIp).toString();
    cfg.cameraHwFps   = s.value("hw_fps", cfg.cameraHwFps).toInt();
    cfg.cameraYFlip   = s.value("y_flip", cfg.cameraYFlip).toBool();
    cfg.realLengthMm  = s.value("real_length_mm", cfg.realLengthMm).toFloat();
    cfg.realWidthMm   = s.value("real_width_mm", cfg.realWidthMm).toFloat();
    s.endGroup();

    // [model]
    s.beginGroup("model");
    cfg.modelPath             = s.value("path", cfg.modelPath).toString();
    cfg.modelInputSize        = s.value("input_size", cfg.modelInputSize).toInt();
    cfg.modelTopkClassCount   = s.value("topk_class_count",
                                        cfg.modelTopkClassCount).toInt();
    // 品类按钮:model/class_btn/size, model/class_btn/N/name|id
    const int btnCount = s.value("class_btn/size", 0).toInt();
    for (int i = 1; i <= btnCount; ++i) {
        ClassButton b;
        b.name    = s.value(QString("class_btn/%1/name").arg(i)).toString();
        b.classId = s.value(QString("class_btn/%1/id").arg(i), -1).toInt();
        if (!b.name.isEmpty() && b.classId >= 0) {
            cfg.classButtons.push_back(b);
        }
    }
    s.endGroup();

    // [yolo]
    s.beginGroup("yolo");
    cfg.confThreshold = s.value("conf_threshold", cfg.confThreshold).toFloat();
    cfg.nmsThreshold  = s.value("nms_threshold", cfg.nmsThreshold).toFloat();
    cfg.drawOverlay   = s.value("draw_overlay", cfg.drawOverlay).toBool();
    s.endGroup();

    // [pipeline]
    s.beginGroup("pipeline");
    cfg.softFps               = s.value("soft_fps", cfg.softFps).toInt();
    cfg.iouThreshold          = s.value("iou_threshold", cfg.iouThreshold).toFloat();
    cfg.missFramesX           = s.value("miss_frames_x", cfg.missFramesX).toInt();
    cfg.updateFramesY         = s.value("update_frames_y", cfg.updateFramesY).toInt();
    cfg.dispatchedPoolClearMm = s.value("dispatched_pool_clear_distance_mm",
                                        cfg.dispatchedPoolClearMm).toFloat();
    cfg.maskRasterMmPerPx     = s.value("mask_raster_mm_per_px",
                                        cfg.maskRasterMmPerPx).toFloat();
    cfg.tickIntervalMs        = s.value("tick_interval_ms", cfg.tickIntervalMs).toInt();
    s.endGroup();

    // [belt]
    s.beginGroup("belt");
    cfg.nominalSpeedMs           = s.value("nominal_speed_m_s",
                                           cfg.nominalSpeedMs).toFloat();
    // 新键(master 口径): encoder_raw_to_m_per_min,默认 0.502。
    // 兼容旧键 encoder_pulse_to_mm:以 raw=单位的 "脉冲→mm/采样窗口" 口径已废弃,
    // 若 ini 仍出现会被静默忽略(硬件实测证明 raw 是转速代理,不是脉冲计数)。
    cfg.encoderRawToMPerMin      = s.value("encoder_raw_to_m_per_min",
                                           cfg.encoderRawToMPerMin).toFloat();
    cfg.encoderRequestIntervalMs = s.value("encoder_request_interval_ms",
                                           cfg.encoderRequestIntervalMs).toInt();
    s.endGroup();

    // [sorter]
    s.beginGroup("sorter");
    const QString mode = s.value("mode", "valve").toString().toLower();
    cfg.sorterMode = (mode == "arm") ? RuntimeConfig::SorterMode::Arm
                                     : RuntimeConfig::SorterMode::Valve;
    cfg.armDistanceMm   = s.value("arm_distance_mm", cfg.armDistanceMm).toFloat();
    cfg.valveDistanceMm = s.value("valve_distance_mm", cfg.valveDistanceMm).toFloat();
    s.endGroup();

    // [valve]
    s.beginGroup("valve");
    cfg.valveTotalChannels    = s.value("total_channels", cfg.valveTotalChannels).toInt();
    cfg.valveBoards           = s.value("boards", cfg.valveBoards).toInt();
    cfg.valveChannelsPerBoard = s.value("channels_per_board", cfg.valveChannelsPerBoard).toInt();
    cfg.valveXMinMm           = s.value("x_min_mm", cfg.valveXMinMm).toFloat();
    cfg.valveXMaxMm           = s.value("x_max_mm", cfg.valveXMaxMm).toFloat();
    cfg.valveHeadSkipRatio    = s.value("head_skip_ratio", cfg.valveHeadSkipRatio).toFloat();
    cfg.valveOpenDurationMs   = s.value("open_duration_ms", cfg.valveOpenDurationMs).toInt();
    cfg.valveMinCmdIntervalMs = s.value("min_cmd_interval_ms", cfg.valveMinCmdIntervalMs).toInt();
    cfg.valveSpeedRecalcPct   = s.value("speed_recalc_threshold_pct",
                                        cfg.valveSpeedRecalcPct).toInt();
    s.endGroup();

    // [arm_stub]
    s.beginGroup("arm_stub");
    cfg.armAOriginXMm = s.value("a_origin_x_mm", cfg.armAOriginXMm).toFloat();
    cfg.armAOriginYMm = s.value("a_origin_y_mm", cfg.armAOriginYMm).toFloat();
    cfg.armAXSign     = s.value("a_x_sign", cfg.armAXSign).toInt();
    cfg.armAYSign     = s.value("a_y_sign", cfg.armAYSign).toInt();
    cfg.armBOriginXMm = s.value("b_origin_x_mm", cfg.armBOriginXMm).toFloat();
    cfg.armBOriginYMm = s.value("b_origin_y_mm", cfg.armBOriginYMm).toFloat();
    cfg.armBXSign     = s.value("b_x_sign", cfg.armBXSign).toInt();
    cfg.armBYSign     = s.value("b_y_sign", cfg.armBYSign).toInt();
    s.endGroup();

    // [persistence]
    s.beginGroup("persistence");
    cfg.saveRaw         = s.value("save_raw", cfg.saveRaw).toBool();
    cfg.rawSampleRatio  = s.value("raw_sample_ratio", cfg.rawSampleRatio).toFloat();
    cfg.saveResult      = s.value("save_result", cfg.saveResult).toBool();
    cfg.saveDir         = s.value("save_dir", cfg.saveDir).toString();
    cfg.armStubCsv      = s.value("arm_stub_csv", cfg.armStubCsv).toString();
    s.endGroup();

    return cfg;
}
