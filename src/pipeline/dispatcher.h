#ifndef DISPATCHER_H
#define DISPATCHER_H

// ============================================================================
// Dispatcher:SortTask -> ValvePulse 时间轴 + 机械臂 stub(CSV 日志)。
// 设计见 docs/design.md §3.6 / §4.4 / §4.5。
// ============================================================================

#include <QFile>
#include <QMap>
#include <QObject>
#include <QTextStream>
#include <QVector>
#include <opencv2/core.hpp>

#include "pipeline/pipeline_types.h"
#include "config/runtime_config.h"

class Dispatcher : public QObject
{
    Q_OBJECT
public:
    explicit Dispatcher(QObject* parent = nullptr);
    ~Dispatcher() override;

public slots:
    void onSessionStart(const RuntimeConfig& cfg);
    void onSessionStop();
    void onSortTask(const SortTask& task);
    void onSpeedSample(const SpeedSample& s);

signals:
    // -> BoardWorker
    void enqueuePulses(const QVector<ValvePulse>& pulses, int trackId);
    void cancelPulses(int trackId);
    // -> MainWindow(本期记日志 + CSV)
    void armStubDispatched(int trackId, int classId,
                           float posArmAXmm, float posArmAYmm,
                           float posArmBXmm, float posArmBYmm);
    void warning(const QString& msg);

private:
    // 计算 y_valve_line:与 TrackerWorker 同步口径。
    float valveLineYbMm() const;

    // 纯函数:按给定速度为某个 SortTask 计算 valve 时间轴。
    QVector<ValvePulse> computePulses(const SortTask& task, float speedMmPerMs) const;

    // 机械臂 stub:把 belt 系的 xy 换算成每条机械臂的本地 xy。
    void dispatchArmStub(const SortTask& task);

    bool          m_sessionActive = false;
    RuntimeConfig m_cfg;
    SpeedSample   m_lastSpeed;

    // 已派发但尚未被 BoardWorker 完全执行的任务,key=trackId。
    // 仅保留"当前还能产出 pulse"的任务:空 pulse(如物体已越过喷阀线)不入 pending。
    QMap<int, SortTask>                m_pending;

    // arm stub CSV 文件句柄
    QFile*       m_armCsvFile = nullptr;
    QTextStream* m_armCsvStream = nullptr;

    bool openArmCsv();
    void closeArmCsv();
};

#endif  // DISPATCHER_H
