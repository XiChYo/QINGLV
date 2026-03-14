#ifndef YOLO_THREAD_H
#define YOLO_THREAD_H

#include <QImage>
#include <QMutex>
#include <QSet>
#include <QThread>
#include <QString>
#include <vector>

#include "rknn_api.h"
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
    bool loadModelLocked(QString& errorMessage);
    void releaseModelLocked();
    bool setEnabledClassesLocked(const QList<int>& classIds);
    YoloTaskResult predictLocked(quint64 taskId, const QImage& image);

    static unsigned char* loadModelData(const char* filename, int* modelSize);

private:
    QString m_modelPath;
    QMutex m_mutex;
    bool m_modelReady = false;
    QSet<int> m_enabledClasses;

    rknn_context m_ctx = 0;
    std::vector<unsigned char> m_modelBytes;
    rknn_input_output_num m_ioNum {};
    rknn_tensor_attr m_inputAttr {};
    std::vector<rknn_tensor_attr> m_outputAttrs;
};

#endif // YOLO_THREAD_H
