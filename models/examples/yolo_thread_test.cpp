#include <QCoreApplication>
#include <QEventLoop>
#include <QImage>
#include <QTimer>
#include <QObject>
#include <QDebug>

#include "models/yolothread.h"

class ThreadCaller : public QObject
{
    Q_OBJECT
public:
    explicit ThreadCaller(QObject* parent = nullptr) : QObject(parent) {}

    bool setClassResult = false;
    bool gotSetClassResult = false;
    YoloTaskResult predictResult;
    bool gotPredictResult = false;

signals:
    void requestSetClassesSig(const QList<int>& classIds);
    void requestPredictSig(quint64 taskId, const QImage& image);

public slots:
    void onSetClassesDone(bool ok)
    {
        setClassResult = ok;
        gotSetClassResult = true;
    }

    void onPredictDone(const YoloTaskResult& result)
    {
        predictResult = result;
        gotPredictResult = true;
    }
};

static bool waitForModelReady(yolothread& yolo, bool& modelReadyOk, int timeoutMs = 15000)
{
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        modelReadyOk = false;
        loop.quit();
    });

    QObject::connect(&yolo, &yolothread::modelReadySig, &loop, [&](bool ok, const QString& error) {
        if (!ok) {
            qWarning() << "Model load failed:" << error;
        }
        modelReadyOk = ok;
        loop.quit();
    });

    timer.start(timeoutMs);
    loop.exec();
    return modelReadyOk;
}

static bool testDirectApi(yolothread& yolo, const QImage& inputImage)
{
    qInfo() << "[Test A] direct API: set classes + predict";
    const bool setOk = yolo.setEnabledClasses({0, 2, 5});
    if (!setOk) {
        qWarning() << "setEnabledClasses failed in direct API";
        return false;
    }

    const YoloTaskResult result = yolo.predict(1001, inputImage);
    if (!result.success) {
        qWarning() << "predict failed in direct API:" << result.errorMessage;
        return false;
    }

    qInfo() << "TaskId:" << result.taskId << ", object count:" << result.objects.size();
    for (const auto& obj : result.objects) {
        qInfo() << " class=" << obj.classId
                << " score=" << obj.score
                << " center=(" << obj.center.x() << "," << obj.center.y() << ")"
                << " segmentation points=" << obj.segmentation.size();
    }
    return true;
}

static bool testThreadCommunication(yolothread& yolo, const QImage& inputImage)
{
    qInfo() << "[Test B] queued cross-thread calls";
    ThreadCaller caller;

    QObject::connect(&caller, &ThreadCaller::requestSetClassesSig,
                     &yolo, &yolothread::setEnabledClassesAsync, Qt::QueuedConnection);
    QObject::connect(&caller, &ThreadCaller::requestPredictSig,
                     &yolo, &yolothread::predictAsync, Qt::QueuedConnection);
    QObject::connect(&yolo, &yolothread::setEnabledClassesDoneSig,
                     &caller, &ThreadCaller::onSetClassesDone, Qt::QueuedConnection);
    QObject::connect(&yolo, &yolothread::predictDoneSig,
                     &caller, &ThreadCaller::onPredictDone, Qt::QueuedConnection);

    QEventLoop setLoop;
    QTimer setTimer;
    setTimer.setSingleShot(true);
    QObject::connect(&setTimer, &QTimer::timeout, &setLoop, &QEventLoop::quit);
    QObject::connect(&yolo, &yolothread::setEnabledClassesDoneSig, &setLoop, &QEventLoop::quit);
    setTimer.start(5000);
    emit caller.requestSetClassesSig({1, 3, 7});
    setLoop.exec();
    if (!caller.gotSetClassResult || !caller.setClassResult) {
        qWarning() << "Queued setEnabledClasses failed or timeout";
        return false;
    }

    QEventLoop predictLoop;
    QTimer predictTimer;
    predictTimer.setSingleShot(true);
    QObject::connect(&predictTimer, &QTimer::timeout, &predictLoop, &QEventLoop::quit);
    QObject::connect(&yolo, &yolothread::predictDoneSig, &predictLoop, &QEventLoop::quit);
    predictTimer.start(20000);
    emit caller.requestPredictSig(2002, inputImage);
    predictLoop.exec();
    if (!caller.gotPredictResult || !caller.predictResult.success) {
        qWarning() << "Queued predict failed or timeout:" << caller.predictResult.errorMessage;
        return false;
    }

    qInfo() << "Queued predict done, taskId:" << caller.predictResult.taskId
            << "objects:" << caller.predictResult.objects.size();
    return true;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (argc < 3) {
        qWarning() << "Usage:" << argv[0] << "<model_path> <image_path>";
        return -1;
    }

    const QString modelPath = QString::fromLocal8Bit(argv[1]);
    const QString imagePath = QString::fromLocal8Bit(argv[2]);

    QImage inputImage(imagePath);
    if (inputImage.isNull()) {
        qWarning() << "Read image failed:" << imagePath;
        return -1;
    }

    yolothread yolo(modelPath);
    yolo.start();

    bool modelReadyOk = false;
    if (!waitForModelReady(yolo, modelReadyOk)) {
        yolo.stop();
        return -1;
    }

    const bool passA = testDirectApi(yolo, inputImage);
    const bool passB = testThreadCommunication(yolo, inputImage);

    yolo.stop();
    qInfo() << "Test A:" << (passA ? "PASS" : "FAIL")
            << ", Test B:" << (passB ? "PASS" : "FAIL");
    return (passA && passB) ? 0 : 1;
}

#include "yolo_thread_test.moc"
