#ifndef BOARDCONTROL_H
#define BOARDCONTROL_H

// ============================================================================
// boardControl(兼 BoardWorker,见 docs/design.md §3.4)
//
// 职责:
//   1. 打开 /dev/ttyUSB0(失败尝试 /dev/ttyUSB1),115200 8N1,RS485 单线总线
//   2. 周期性发编码器请求帧并解析返回,以 SpeedSample 上报(PR3 新增)
//   3. 接收 Dispatcher 下发的 ValvePulse 时间轴,5 ms 轮询按 tOpenMs/tCloseMs
//      触发开/关阀帧(PR3 新增)
//   4. 保留原 singleBoardControl/batchBoardControl 两条 legacy 通道供 Qt Action
//      菜单触发(PR5 才会决定去留),但它们经由同一把 m_busMutex。
//
// 并发/时序保证:
//   - 单一 m_busMutex 保护所有串口写入;粒度=单帧,不跨帧持锁。
//   - 阀脉冲和编码器请求在同一 QThread 事件循环中串行执行,QTimer 即可。
// ============================================================================

#include <QByteArray>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVector>

#include "pipeline/pipeline_types.h"
#include "config/runtime_config.h"

class boardControl : public QObject
{
    Q_OBJECT
public:
    explicit boardControl(QObject *parent = nullptr);
    ~boardControl() override;

public slots:
    // 构造后、线程 started 时调用;打开串口并启动 encoderTick。
    void initSerial();

    // ---- 新管道接口(PR3) ----
    void onSessionStart(const RuntimeConfig& cfg);
    void onSessionStop();
    void onEnqueuePulses(const QVector<ValvePulse>& pulses, int trackId);
    void onCancelPulses(int trackId);

    // ---- Legacy(PR5 再决定去留) ----
    void singleBoardControl(QString order);
    void batchBoardControl(QString order);
    void requestEncoderSpeed();
    void stopWork();

signals:
    // Raw 帧(保留给 MainWindow::onEncoderSpeed 旧路径)。
    void encoderSpeedReceived(QByteArray frame);
    // PR3 新增:解析后的速度样本,供 Tracker / Dispatcher 使用。
    void speedSample(const SpeedSample& s);
    void errorOccured(QString msg);

private slots:
    // 阀轮询。pipeline.tick_interval_ms 缺省 5 ms。
    void onValveTick();
    // 编码器轮询。belt.encoder_request_interval_ms 缺省 500 ms。
    void onEncoderTick();

private:
    // ---- 串口底层 ----
    bool openSerial();
    void closeSerial();
    bool writeFrame(const QByteArray& frame);
    bool readFrame(QByteArray& frame, int timeoutMs);

    // ---- 帧构造 ----
    QByteArray buildControlFrame(const QByteArray& field);
    QByteArray buildBatchOpenFrame(const QByteArray& field);
    QByteArray buildBatchCloseFrame(const QByteArray& field);
    QByteArray buildBatchFrameCore(quint8 b6, quint8 b7, quint8 b8, quint8 b9);
    static quint8 countBits(quint8 v);

    // ---- Valve pulse 轨道内部记录 ----
    struct PulseSlot {
        ValvePulse pulse;
        bool       sentOpen  = false;
        bool       sentClose = false;
    };

    QByteArray buildPulseOpenFrame(const ValvePulse& p);
    QByteArray buildPulseCloseFrame(const ValvePulse& p);

    // ---- 成员 ----
    QMutex                           m_busMutex;      // 保护所有串口读写
    QByteArray                       m_rxBuffer;
    int                              m_fd = -1;
    QString                          m_dev = "/dev/ttyUSB0";
    bool                             m_sessionActive = false;

    QTimer*                          m_valveTick   = nullptr; // 5 ms 默认
    QTimer*                          m_encoderTick = nullptr; // 500 ms 默认

    // trackId -> 按 tOpenMs 升序的脉冲列表
    QMap<int, QVector<PulseSlot>>    m_pending;

    // 从 cfg 缓存的运行期参数
    int     m_valveTickIntervalMs       = 5;
    int     m_encoderRequestIntervalMs  = 500;
    // master 口径: raw * encoderRawToMPerMin = m/min;无需差分 / 窗口除法。
    float   m_encoderRawToMPerMin       = 0.502f;
    int     m_encoderConsecutiveFailures = 0;
};

#endif // BOARDCONTROL_H
