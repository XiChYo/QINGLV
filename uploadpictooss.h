#ifndef UPLOADPICTOOSS_H
#define UPLOADPICTOOSS_H

#include <exception>
#include <QString>
#include <QObject>
#include <QFileInfo>
#include <QThread>


class uploadpictoOSS: public QThread
{
    Q_OBJECT
public:
    uploadpictoOSS(QObject* parent);
    ~uploadpictoOSS();

    // 相机读取的照片先保存本地，然后再由本地上传到云端，必须返回true才算上传成功
    // imageClass = 1：将照片上传到 原图路径； 2：将照片上传到 图中有方框标记的路径； 3：上传低置信度照片
    bool uploadImage(const QString& localFilePath, const int imageClass);

    int send_heartbeat();


private:
    bool initializeOss();

    QString logMsg;

    // 设备id、ip、注册密钥、设备密钥、三大api_url
    QString device_id = "device-test";
    QString device_ip = "192.168.1.100";
    QString register_key = "register-key-2026";
    QString device_key;
    QString bucketName;
    QString registerAPI = "http://120.79.183.234:18000/api/devices/register";
    QString heartbeatAPI = "http://120.79.183.234:18000/api/devices/heartbeat";
    QString savepicAPI = "http://120.79.183.234:18000/api/data/images";


    // OSS中的保存路径
    QString ossSaveRoad;

};

#endif // UPLOADPICTOOSS_H
