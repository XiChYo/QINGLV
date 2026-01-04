#ifndef CAMERA_THREAD_H
#define CAMERA_THREAD_H
#include <QThread>
#include <QImage>
#include <QElapsedTimer>
//#include "library/mvs/includes/MvCameraControl.h"
#include "bin/include/MvCameraControl.h"
//#include "/../../../../opt/MVS/include/MvCameraControl.h"

class camerathread: public QThread
{
    Q_OBJECT
public:
    camerathread(QObject* parent = nullptr);
    ~camerathread();

    bool openCamera();
    void stop();

    float captureIntervalMs  = 1000; // 采样周期，单位毫秒

protected:
    void run() override;

signals:
    void frameReadySig(const QImage& img);
    void errorMegSig(const QString& errMeg);

private:
    void* m_hCam = nullptr;
    bool m_running = false;
    unsigned char* rgbBuffer = nullptr;
};

#endif // CAMERA_THREAD_H
