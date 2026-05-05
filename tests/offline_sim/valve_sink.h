#ifndef OFFLINE_SIM_VALVE_SINK_H
#define OFFLINE_SIM_VALVE_SINK_H

// ============================================================================
// ValveSink:把 Dispatcher::enqueuePulses / cancelPulses 信号原样落到 CSV(M3)。
// 作用是替代生产路径 BoardWorker 收到信号后再下发到串口/继电器板;离线仿真里
// 没有任何硬件,我们只把"应该开几号阀、tOpen/tClose"完整记录下来,便于人工
// 抽查 §3.4 通过标准 #5 / AC8(速度 Δ → cancel 后接 enqueue)。
//
// CSV 列(每条 ValvePulse 一行):
//   nowMs, event, trackId, boardId, channelMaskHex, tOpenMs, tCloseMs
//   - event = "enqueue" | "cancel"
//   - cancel 行:trackId 有值,其它列空
//   - enqueue 行:每条 ValvePulse 占一行(同一 trackId 可有多行)
//   - channelMaskHex:0xNNNN(quint16,小端)便于人眼一眼看出哪几路通道开
//   - nowMs:落 csv 那一刻 pipeline::nowMs(),与 driver / yolo / tracker 同钟
//
// 线程模型:sink 自身无状态共享 → 跑在调用线程(主线程)即可;CSV 写入用 flush
// 保证进程崩溃时不丢前面的行。
// ============================================================================

#include <QFile>
#include <QObject>
#include <QString>
#include <QTextStream>
#include <QVector>

#include "pipeline/pipeline_types.h"

namespace offline_sim {

class ValveSink : public QObject
{
    Q_OBJECT
public:
    explicit ValveSink(const QString& csvPath, QObject* parent = nullptr);
    ~ValveSink() override;

    // 打开 csv 文件,失败返回 false。errMsg 非空时填错误。
    bool open(QString* errMsg = nullptr);
    void close();

    int enqueueRowCount() const { return m_enqueueRows; }
    int cancelRowCount()  const { return m_cancelRows;  }

public slots:
    void onEnqueue(const QVector<ValvePulse>& pulses, int trackId);
    void onCancel(int trackId);

private:
    void writeHeader();

    QString       m_path;
    QFile*        m_file   = nullptr;
    QTextStream*  m_stream = nullptr;
    int           m_enqueueRows = 0;
    int           m_cancelRows  = 0;
};

}  // namespace offline_sim

#endif  // OFFLINE_SIM_VALVE_SINK_H
