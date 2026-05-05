#ifndef OFFLINE_SIM_CAMERA_DRIVER_H
#define OFFLINE_SIM_CAMERA_DRIVER_H

// ============================================================================
// OfflineCameraDriver:用 log.txt 解析出的 Capture 事件 + saveRawPic/*.jpg
// 模拟生产 CameraWorker 的行为(M2)。
//
// 与生产 CameraWorker 的对应关系(《测试需求.md》§4.2 / §4.3):
//   - 同一 frameReadySig 签名(QImage, tCaptureMs, fileName)
//   - 同款"软节流(softFps,默认 2 → 500ms 间隔)+ 单元素反压(m_inFlight ≤ 1)"
//   - 同款 onFrameConsumed slot,被下游 YoloWorker::frameConsumed 触发
//   - 同款 warning(msg) 信号(累计丢帧 ≥ N 且距上次告警 ≥ 2s)
//
// 与生产的差异:
//   - 这里的"原始流"是按 t0_log 对齐的 log Capture 事件,而非 MVS 推流;
//     用 QTimer::singleShot 在 driver 所在线程的事件循环里逐个派发。
//   - 提供 filePathOf(tCaptureMs) 给 AnnotatedFrameSink 反查源图,避免在两路
//     信号里都拷贝 QImage。
//   - 提供两个标定/调试入口:noThrottle(软节流 + 反压全部短路)、
//     fastForward(整条时间轴线性加速)。
//
// 时间轴(《测试需求.md》§4.3):
//   t0_log = 该 session 内第一条 Velocity 事件 t_log;**忽略所有 t < t0_log**
//   t0_sim = start() 调用瞬间 pipeline::nowMs()
//   每个 Capture e: singleShot(max(0,(e.t_log - t0_log) / fastForward), ...)
//
// 线程模型(与生产 CameraWorker 一致):
//   driver 通过 moveToThread 挂到独立 QThread;start()/stop()/onFrameConsumed()
//   都是 slot,通过 QueuedConnection 投递安全。filePathOf 是只读 const 方法,
//   可在任意线程调用(内部 QMutex 保护小 hash)。
// ============================================================================

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QVector>
#include <atomic>

#include "log_parser.h"

namespace offline_sim {

struct CameraDriverOptions {
    // saveRawPic 根目录(内含 *.jpg + log.txt)。driver 自己 indexFilesBySeq 一次。
    QString rawDir;
    // 仿真选哪段 session(splitSessions 后的下标,0=11:05 段,1=11:26 段)。
    int     sessionIdx = 0;
    // 仿真窗口 [seqFrom, seqTo](闭区间);任一 = -1 表示该侧不限制。
    int     seqFrom    = -1;
    int     seqTo      = -1;
    // 软节流目标 fps;<= 0 等价 noThrottle。生产 RuntimeConfig 默认 2(500ms)。
    int     softFps    = 2;
    // 标定模式:软节流 + 反压全部短路,所有 Capture 都让 emit。
    bool    noThrottle = false;
    // 时间轴线性加速因子;1.0=按真实时间轴跑,>1 加速,=0 全部 0ms 立刻派出。
    // 用于:(a) 标定模式快速跑完;(b) 单测里把节流间隔以"墙钟"为单位卡死。
    double  fastForward = 1.0;
    // 反压告警:累计丢 ≥ N 帧且距上次告警 ≥ quietMs 时 emit warning(msg)。
    int     warnDropThreshold = 5;
    int     warnDropQuietMs   = 2000;
};

class OfflineCameraDriver : public QObject
{
    Q_OBJECT
public:
    explicit OfflineCameraDriver(QObject* parent = nullptr);
    ~OfflineCameraDriver() override;

    // 一次性配置 + 索引文件 + 选 Capture 事件 + 计算 t0_log。
    // 必须在 start() 之前调用;driver 已 moveToThread 时,从 driver 所在线程
    // 调用(或显式 QueuedConnection)。
    // 失败原因写到 errMsg(非空时):rawDir 不存在 / sessionIdx 越界 / 该 session
    // 内没有 Velocity 事件(没法定位 t0_log) / seq 范围内 0 帧文件等。
    bool initialize(const CameraDriverOptions& opt,
                    const QVector<LogEvent>& parsedEvents,
                    QString* errMsg = nullptr);

    // 选中的 Capture 事件数(initialize 后稳定;用于日志和单测断言)。
    int scheduledCount() const;

    // 选中事件中实际索引到 jpg 文件的数量。两者相等说明文件齐;否则差额表
    // 示有 Capture 事件对应的 jpg 不在 rawDir(已被 [WARN] 记录)。
    int matchedFileCount() const;

    // 给 AnnotatedFrameSink 用:tCaptureMs 来自 frameReadySig 第二参数;
    // 返回该帧的源 jpg 绝对路径,找不到返回空串。线程安全。
    QString filePathOf(qint64 tCaptureMs) const;

    // 单测专用:把 t0_log / 已加载的事件读出来检验。生产代码不依赖。
    qint64 t0LogMs() const { return m_t0Log; }

public slots:
    // 启动 session 时间轴回放。重复调用是无害的(内部判 m_running)。
    void start();
    // 停止回放:置 m_running=false,后续到点的 singleShot 全部 early return。
    // 已派发但还没触发的 singleShot 无法撤销(Qt 限制),靠 m_running 拦住。
    void stop();
    // 下游 YoloWorker::frameConsumed -> 这里。可在任意线程调用(原子计数)。
    void onFrameConsumed();

signals:
    // 与生产 CameraWorker::frameReadySig 同签名(《测试需求.md》§4.2)。
    void frameReadySig(const QImage& img, qint64 tCaptureMs, const QString& fileName);
    // 反压累计丢帧告警。
    void warning(const QString& msg);
    // 该 session 时间轴上所有 Capture 事件都"过过手"了(无论 emit 还是被
    // 节流/反压跳过)。主程序据此 stop YOLO/Tracker/Dispatcher。
    void sessionEnded();

private:
    void onRawFrameArrived(int eventIdx);
    static QHash<int, QString> indexFilesBySeq(const QString& rawDir);

    struct ScheduledFrame {
        qint64  deltaMs  = 0;   // 相对 t0_log
        int     seq      = -1;
        qint64  logTMs   = 0;   // 原始日志时间(秒+ms)
        QString filePath;       // 索引到的 jpg 绝对路径(可能为空 = 文件缺失)
        QString fileName;       // basename
    };

    CameraDriverOptions       m_opt;
    QVector<ScheduledFrame>   m_frames;
    qint64                    m_t0Log = 0;
    qint64                    m_t0Sim = 0;
    int                       m_matchedFileCount = 0;

    qint64                    m_lastEmitSimMs    = -1;
    // 保证 emit 出的 tCaptureMs 严格单调递增 —— 生产 CameraWorker 帧间隔
    // ≥几十 ms 自然不撞 ms,但仿真模式 fastForward=0 / 高加速时多帧瞬时派
    // 发,pipeline::nowMs() 会撞同一 ms,导致 m_pathMap 后写覆盖前写。
    qint64                    m_lastEmittedTCapture = -1;
    int                       m_intervalMs       = 500;  // 1000/softFps
    std::atomic<int>          m_inFlight{0};
    int                       m_droppedSinceWarn = 0;
    qint64                    m_lastWarnMs       = -1;
    std::atomic<bool>         m_running{false};

    mutable QMutex            m_pathMapMu;
    QHash<qint64, QString>    m_pathMap;
};

}  // namespace offline_sim

#endif  // OFFLINE_SIM_CAMERA_DRIVER_H
