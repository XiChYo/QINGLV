#ifndef YOLO_WORKER_H
#define YOLO_WORKER_H

// ============================================================================
// YoloWorker:常驻 RKNN session 的检测 worker。
//   - sessionStart(cfg):一次性加载 rknn 模型(常驻)。
//   - onFrame(img, tCaptureMs):推理并发布 DetectedFrame + overlay 图。
//   - sessionStop:释放 session。
// 设计见 docs/design.md §1.2 / §3 / §4。
// ============================================================================

#include <QImage>
#include <QObject>
#include <QString>
#include <opencv2/core.hpp>
#include <vector>

#include "pipeline_types.h"
#include "runtime_config.h"
#include "yolo_session.h"

class YoloWorker : public QObject
{
    Q_OBJECT
public:
    explicit YoloWorker(QObject* parent = nullptr);
    ~YoloWorker() override;

public slots:
    void sessionStart(const RuntimeConfig& cfg);
    void sessionStop();
    void onFrame(const QImage& img, qint64 tCaptureMs, const QString& fileName);

signals:
    // 结构化结果给 Tracker(PR4 接入)
    void detectedFrameReady(const DetectedFrame& frame);
    // 叠加图给 UI,MainWindow 在主线程换 pixmap。
    void resultImgReady(const QImage& img);
    // 反压:通知 CameraWorker 解锁下一帧(MainWindow 中转)。
    void frameConsumed();

private:
    YoloSession m_session;
    bool        m_sessionValid  = false;
    float       m_confThreshold = 0.25f;
    float       m_nmsThreshold  = 0.45f;
    int         m_topkClassCount = 80;
    bool        m_drawOverlay    = true;
    QString     m_modelPath;

    // 从类别 id 反查中文/英文名,用于 overlay 标注。这里保留最简实现:
    // id→index in enabledClasses,没有 label 则打印 id。真正的中文字典在 PR5 整合 UI 时一并做。
    static const char* defaultLabelFromId(int id);
};

#endif  // YOLO_WORKER_H
