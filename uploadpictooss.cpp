#include "uploadpictooss.h"
#include "logger.h"
#include <QSettings>
#include <QCoreApplication>

// 目前用的是阿里云的测试样例代码，如果后面有不同的云端上传方式，则改变

uploadpictoOSS::uploadpictoOSS(QObject* parent)
{
    initializeOss();
}

uploadpictoOSS::~uploadpictoOSS()
{
//    ShutdownSdk(); // 关闭oss sdk
}

bool uploadpictoOSS::initializeOss()
{
    try{
////     初始化SDK
//    InitializeSdk();

//        //config.ini
//        QSettings settings("config.ini", QSettings::IniFormat);
//        settings.setIniCodec("UTF-8");

//        endpoint        = settings.value("OSS/endpoint").toString();
//        accessKeyId     = settings.value("OSS/accessKeyId").toString();
//        accessKeySecret = settings.value("OSS/accessKeySecret").toString();
//        bucketName              = settings.value("OSS/bucketName").toString();

//        //  校验
//        if (endpoint.isEmpty() ||
//            accessKeyId.isEmpty() ||
//            accessKeySecret.isEmpty() ||
//            bucketName.isEmpty())
//        {
//            LOG_ERROR("OSS config.ini missing required fields");
//            ShutdownSdk();
//            return false;
//        }

////     配置实例
//    ClientConfiguration conf;
//    OssClient client(endpoint.toStdString(), accessKeyId.toStdString(), accessKeySecret.toStdString(), conf);

////     创建API请求
//    ListBucketsRequest request;
//    auto outcome = client.ListBuckets(request);
//    if (!outcome.isSuccess()) {
//        // 异常处理
//        std::cout << "ListBuckets fail" <<
//            ",code:" << outcome.error().Code() <<
//            ",message:" << outcome.error().Message() <<
//            ",requestId:" << outcome.error().RequestId() << std::endl;
//        ShutdownSdk();
////         初始化云端oss失败
//        return false;
//    }

    // 初始化云端oss成功
        return true;
    }catch(std::exception& e)
    {
        logMsg = "Initialize OSS failed: " + QString::fromStdString(e.what());
        LOG_ERROR(logMsg);
        return false;
    }
}

bool uploadpictoOSS::uploadImage(const QString &localFilePath, const int imageClass)
{
    // OSS中的保存
    QFileInfo fileInfo(localFilePath);
    QString fileName = fileInfo.fileName();   // 例如：image_001.jpg
    if (imageClass == 1)
    {
        ossSaveRoad = "saveRawImage/" + fileName;
    }else if(imageClass == 2)
    {
        ossSaveRoad = "saveMarkImage/" + fileName;
    }else if(imageClass == 3)
    {
        ossSaveRoad = "saveLowConfidenceImage/" + fileName;
    }

    // 等后续接入oss云端上传后，将下面的代码解开注释，LN
    return true; // 测试用false


//    auto outcome = client.PutObject(
//        bucketName,
//        ossSaveRoad,
//        localFilePath.toStdString()
//    );

////  如果上传失败，打印失败原因
//    if (!outcome.isSuccess())
//    {
//        const auto &error = outcome.error();

//        QString logMsg = QString(
//            "OSS uploadImage failed | Code: %1 | Message: %2 | RequestId: %3 | HttpStatus: %4"
//        ).arg(
//            QString::fromStdString(error.Code()),
//            QString::fromStdString(error.Message()),
//            QString::fromStdString(error.RequestId())
//        ).arg(
//            error.HttpStatus()
//        );
//        LOG_ERROR(logMsg);
////         将上传云端失败的图片名称和类型保存到本地txt文件夹中
//        QFile file("failupLoadImage.txt");
//        if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
//        {
//            QTextStream out(&file);
//            out << localFilePath << "," << imageClass << "\n";
//            file.close();
//        }
//        return false;
//    }
//    else
//    {
//        logMsg = "upLoadImage success: " + ossSaveRoad;
//        LOG_INFO(logMsg);
//        return true;
//    }
}
