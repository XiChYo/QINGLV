#ifndef CAMERA_THREAD_H
#define CAMERA_THREAD_H
#include <QThread>
#include <QImage>
#include <QElapsedTimer>
#include "bin/include/MvCameraControl.h"

class camerathread: public QThread
{
    Q_OBJECT
public:
    camerathread(QObject* parent = nullptr);
    ~camerathread();

    bool openCamera();
    void stop();

    float captureIntervalMs  = 150; // 采样周期，单位毫秒，皮带速度按照1m/s计算

protected:
    void run() override;

signals:
    void frameReadySig(const QImage& img, const QString& fileName);
    void errorMegSig(const QString& errMeg);

private:
    void* m_hCam = nullptr;
    bool m_running = false;
    unsigned char* rgbBuffer = nullptr;
};

#endif // CAMERA_THREAD_H
