#ifndef UPLOADPICTOOSS_H
#define UPLOADPICTOOSS_H

//#include <alibabacloud/oss/OssClient.h>
//using namespace AlibabaCloud::OSS;
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


private:
    bool initializeOss();

    QString logMsg;

    // 云端端点、id、密钥、桶名称
    QString endpoint;
    QString accessKeyId;
    QString accessKeySecret;
    QString bucketName;

    // OSS中的保存路径
    QString ossSaveRoad;

};

#endif // UPLOADPICTOOSS_H
