#ifndef OFFLINE_SIM_ENCODER_DRIVER_H
#define OFFLINE_SIM_ENCODER_DRIVER_H

// ============================================================================
// OfflineEncoderDriver:用 log.txt 解析出的 Velocity 事件模拟编码器(M2)。
//
// 与生产侧 BoardWorker 抛 SpeedSample 的关系(《测试需求.md》§4.2):
//   - 同款 SpeedSample 结构(tMs / speedMmPerMs / valid)
//   - 同款 emit 频率(真机 BoardControl 约每 500 ms 推一次,这里 log 也是 500ms)
//   - tMs 取自 pipeline::nowMs(),与下游 TrackerWorker / Dispatcher 完全等价
//
// 速度换算两种模式(《测试需求.md》§3.5):
//   useRpm = false → speedMmPerMs = mPerMin / 60.0       (m/min → mm/ms)
//                    业务方告知 mPerMin 不准,该模式仅作对照基线
//   useRpm = true  → speedMmPerMs = K * rpm
//                    K = §3.5 标定产物(单位 mm/ms·per_rpm),来自
//                    tests/offline_sim/offline_sim.ini [encoder]/k_mm_per_ms_per_rpm
//
// 时间轴对齐:与 OfflineCameraDriver 共用 t0_log = 该 session 第一条 Velocity
// 事件的 t_log;**首条速度事件本身在 t0_log 时刻 emit(deltaMs=0)**,
// 后续按 (t_log - t0_log) / fastForward 派发(《测试需求.md》§4.3 注)。
//
// 线程模型:与 driver 同款,QObject + moveToThread,start/stop 走 QueuedConnection。
// ============================================================================

#include <QObject>
#include <QString>
#include <QVector>
#include <atomic>

#include "log_parser.h"
#include "pipeline/pipeline_types.h"

namespace offline_sim {

struct EncoderDriverOptions {
    int    sessionIdx = 0;
    bool   useRpm     = true;       // true = K*rpm(线上首选);false = mPerMin/60.0
    double k_mmPerMs_perRpm = 0.0;  // useRpm=true 时必须 > 0
    double fastForward = 1.0;       // 与 CameraDriver 同节奏
};

class OfflineEncoderDriver : public QObject
{
    Q_OBJECT
public:
    explicit OfflineEncoderDriver(QObject* parent = nullptr);
    ~OfflineEncoderDriver() override;

    // 一次性配置 + 选 Velocity 事件 + 计算 t0_log。
    // 失败:sessionIdx 越界 / 该 session 无 Velocity / useRpm=true 但 K<=0。
    bool initialize(const EncoderDriverOptions& opt,
                    const QVector<LogEvent>& parsedEvents,
                    QString* errMsg = nullptr);

    int  scheduledCount() const;

    // 单测专用:t0_log 与 cam driver 应该一致。
    qint64 t0LogMs() const { return m_t0Log; }

public slots:
    void start();
    void stop();

signals:
    void speedSample(SpeedSample s);
    // 时间轴上所有 Velocity 事件都已派发(无论是否真 emit)。
    void sessionEnded();

private:
    void onVelocityArrived(int idx);

    struct ScheduledVel {
        qint64 deltaMs = 0;
        int    mPerMin = 0;
        int    rpm     = 0;
    };

    EncoderDriverOptions m_opt;
    QVector<ScheduledVel> m_vels;
    qint64               m_t0Log = 0;
    std::atomic<bool>    m_running{false};
};

}  // namespace offline_sim

#endif  // OFFLINE_SIM_ENCODER_DRIVER_H
