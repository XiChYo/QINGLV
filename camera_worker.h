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

#include "runtime_config.h"

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

signals:
    // 新采到的一帧。tCaptureMs 来自 pipeline::nowMs(),统一时钟。
    // fileName 仅作日志/落盘使用(PR5 的 persistence 会用到)。
    void frameReadySig(const QImage& img, qint64 tCaptureMs, const QString& fileName);
    // 关键错误(相机无法打开、连续取帧失败等)。由 MainWindow 汇聚展示。
    void cameraError(const QString& msg);

private slots:
    void onTick();

private:
    bool openCameraLocked(const QString& ip);
    void closeCameraLocked();
    bool grabOneFrame(QImage& outImg, qint64& outCaptureMs);

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
};

#endif  // CAMERA_WORKER_H
