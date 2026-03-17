#ifndef YOLO_THREAD_H
#define YOLO_THREAD_H

#include <QImage>
#include <QThread>
#include <QString>
#include "yoloresulttypes.h"

class yolothread : public QThread
{
    Q_OBJECT
public:
    explicit yolothread(const QString& modelPath, QObject* parent = nullptr);
    ~yolothread() override;

    void stop();

    // Function 1: update enabled classes (subset of 0..8)
    bool setEnabledClasses(const QList<int>& classIds);

    // Function 2: run prediction for task id + image
    YoloTaskResult predict(quint64 taskId, const QImage& image);

protected:
    void run() override;

public slots:
    bool setEnabledClassesInThread(const QList<int>& classIds);
    YoloTaskResult predictInThread(quint64 taskId, const QImage& image);
    void setEnabledClassesAsync(const QList<int>& classIds);
    void predictAsync(quint64 taskId, const QImage& image);

signals:
    void modelReadySig(bool success, const QString& errorMessage);
    void setEnabledClassesDoneSig(bool success);
    void predictDoneSig(const YoloTaskResult& result);
    void errorMegSig(const QString& errMeg);

private:
    class YoloWorker;
    YoloWorker* m_worker = nullptr;
};

#endif // YOLO_THREAD_H
