#include <QObject>
#include <QImage>
#include <QHash>
#include <QDebug>

#include "camerathread.h"
#include "models/yolothread.h"

/*
 * Minimal business integration example:
 * 1) Camera thread captures image
 * 2) YOLO thread receives image by queued signal
 * 3) Inference result is returned asynchronously with taskId
 *
 * This file is an integration reference only, and is intentionally
 * not wired into main business flow.
 */
class MinimalYoloIntegration : public QObject
{
    Q_OBJECT
public:
    explicit MinimalYoloIntegration(const QString& modelPath, QObject* parent = nullptr)
        : QObject(parent),
          m_camThread(new camerathread(this)),
          m_yoloThread(new yolothread(modelPath, this))
    {
        qRegisterMetaType<YoloTaskResult>("YoloTaskResult");

        // Camera frame -> enqueue YOLO async predict
        connect(m_camThread, &camerathread::frameReadySig,
                this, &MinimalYoloIntegration::onFrameReady, Qt::QueuedConnection);

        // YOLO model load status
        connect(m_yoloThread, &yolothread::modelReadySig,
                this, &MinimalYoloIntegration::onModelReady, Qt::QueuedConnection);

        // YOLO prediction callback
        connect(m_yoloThread, &yolothread::predictDoneSig,
                this, &MinimalYoloIntegration::onPredictDone, Qt::QueuedConnection);

        // Optional: unified error output
        connect(m_camThread, &camerathread::errorMegSig,
                this, [](const QString& err) { qWarning() << "[camera]" << err; });
        connect(m_yoloThread, &yolothread::errorMegSig,
                this, [](const QString& err) { qWarning() << "[yolo]" << err; });
    }

    void start()
    {
        // Start model thread first. It loads model in run().
        m_yoloThread->start();

        // Open and start camera.
        if (m_camThread->openCamera()) {
            m_camThread->start();
        } else {
            qWarning() << "camera open failed";
        }
    }

    void stop()
    {
        m_camThread->stop();
        m_yoloThread->stop();
    }

private slots:
    void onModelReady(bool ok, const QString& err)
    {
        if (!ok) {
            qWarning() << "YOLO model load failed:" << err;
            return;
        }

        // Keep only part of the 9 classes for prediction.
        const bool filterOk = m_yoloThread->setEnabledClasses({0, 2, 5});
        qInfo() << "setEnabledClasses result =" << filterOk;
    }

    void onFrameReady(const QImage& img, const QString& fileName)
    {
        if (img.isNull()) {
            return;
        }

        // Generate taskId and keep minimal request context.
        const quint64 taskId = ++m_taskIdSeed;
        m_taskSource.insert(taskId, fileName);

        // Cross-thread async inference call.
        QMetaObject::invokeMethod(m_yoloThread,
                                  "predictAsync",
                                  Qt::QueuedConnection,
                                  Q_ARG(quint64, taskId),
                                  Q_ARG(QImage, img.copy())); // copy to avoid buffer reuse
    }

    void onPredictDone(const YoloTaskResult& result)
    {
        const QString source = m_taskSource.take(result.taskId);
        if (!result.success) {
            qWarning() << "predict failed, taskId =" << result.taskId
                       << ", source =" << source
                       << ", error =" << result.errorMessage;
            return;
        }

        qInfo() << "predict ok, taskId =" << result.taskId
                << ", source =" << source
                << ", objectCount =" << result.objects.size();

        for (const auto& obj : result.objects) {
            qInfo() << " class =" << obj.classId
                    << " score =" << obj.score
                    << " center =" << obj.center
                    << " seg_points =" << obj.segmentation.size();
        }
    }

private:
    camerathread* m_camThread = nullptr;
    yolothread* m_yoloThread = nullptr;

    quint64 m_taskIdSeed = 0;
    QHash<quint64, QString> m_taskSource;
};

/*
Usage notes:
1) Construct MinimalYoloIntegration with model path.
2) Call start() after application startup.
3) Call stop() before application exit.
*/

#include "minimal_integration_example.moc"
