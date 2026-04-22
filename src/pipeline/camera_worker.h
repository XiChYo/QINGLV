#ifndef CAMERA_WORKER_H
#define CAMERA_WORKER_H

// ============================================================================
// CameraWorker:相机采集 + 软件节流 + 反压。
// 线程模型:QObject,通过 moveToThread 挂到一个独立 QThread;内部用 QTimer
// 轮询 MVS 缓冲,每 tick 尝试取一帧。采图瞬间打 tCaptureMs。
// 详细设计见 docs/design.md §1.1 / §3 (CameraWorker)。
// ============================================================================

#include <QObject>
#include <QImage>
#include <QString>
#include <QTimer>
#include <atomic>

#include "config/runtime_config.h"

class CameraWorker : public QObject
{
    Q_OBJECT
public:
    explicit CameraWorker(QObject* parent = nullptr);
    ~CameraWorker() override;

public slots:
    // 下发一次性配置并启动相机采集。
    void sessionStart(const RuntimeConfig& cfg);
    // 停止采集,释放相机句柄,清空在途计数。
    void sessionStop();
    // 下游处理完一帧后回调,用于简单反压(避免积压)。
    void onFrameConsumed();
    // F4 硬检查:枚举 MVS 设备,看目标 IP 是否在线。
    // 主线程通过 BlockingQueuedConnection 调用,直接拿 bool 返回值;
    // 调用期间阻塞 camera 线程事件循环,但时间极短(仅枚举)。
    bool probeConnection(const QString& ip);

signals:
    // 新采到的一帧。tCaptureMs 来自 pipeline::nowMs(),统一时钟。
    // fileName 非空表示本帧已被 raw 采样命中并落盘,YoloWorker 据此同名落盘 result;
    // fileName 空表示本帧不落盘。
    void frameReadySig(const QImage& img, qint64 tCaptureMs, const QString& fileName);
    // 关键错误(相机无法打开、连续取帧失败等)。由 MainWindow 汇聚展示。
    void cameraError(const QString& msg);
    // F10 反压告警:连续丢帧累计到一定数量时通知。msg 含已丢帧数。
    void warning(const QString& msg);

private slots:
    void onTick();

private:
    bool openCameraLocked(const QString& ip);
    void closeCameraLocked();
    bool grabOneFrame(QImage& outImg, qint64& outCaptureMs);
    // 按 rawSampleRatio 决定当前帧是否落盘;返回落盘的绝对路径,未落盘返回空串。
    QString trySaveRawFrame(const QImage& img, qint64 tCaptureMs);

    QTimer*        m_tickTimer     = nullptr;
    void*          m_hCam          = nullptr;
    unsigned char* m_rgbBuffer     = nullptr;
    int            m_rgbBufferSize = 0;
    qint64         m_lastCaptureMs = -1;
    int            m_intervalMs    = 500;  // 1000/soft_fps
    int            m_frameCounter  = 0;
    // 反压:允许的在途帧数上限(设计里简化为 1)。
    std::atomic<int> m_inFlight {0};
    static constexpr int kInFlightMax = 1;
    bool           m_sessionActive = false;

    // F16 persistence 配置快照
    bool           m_saveRaw         = false;
    float          m_rawSampleRatio  = 0.1f;
    QString        m_saveDir;

    // F10 丢帧统计(反压引起的跳帧)
    int            m_droppedSinceWarn = 0;
    qint64         m_lastWarnMs       = -1;
};

#endif  // CAMERA_WORKER_H
