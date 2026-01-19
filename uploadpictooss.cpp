#include "uploadpictooss.h"
#include "logger.h"
#include <QSettings>
#include <QCoreApplication>

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonDocument>
#include <QEventLoop>
#include <QDebug>

#include <QHttpMultiPart>
#include <QHttpPart>
#include <QFile>
#include <QTimer>

uploadpictoOSS::uploadpictoOSS(QObject* parent)
{
//    initializeOss();
}

uploadpictoOSS::~uploadpictoOSS()
{
//    ShutdownSdk(); // 关闭oss sdk
}

bool uploadpictoOSS::initializeOss()
{
    try{
    // 构建 JSON
    LOG_INFO("Build Initialize OssJson");
    QJsonObject json;
    json["device_id"] = device_id;
    json["device_ip"] = device_ip;
    json["register_key"] = register_key;

    QJsonDocument doc(json);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    // 创建网络管理器
    QNetworkAccessManager manager;

    // 创建请求
    QNetworkRequest request{ QUrl(registerAPI) };
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // 发送 POST 请求
    QNetworkReply *reply = manager.post(request, jsonData);

    // 阻塞等待（控制台程序常用，UI 程序不要这么干）
//    QEventLoop loop;
//    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

//    loop.exec();

    // 处理结果
    if (reply->error() == QNetworkReply::NoError)
    {
        int httpCode = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();

        qDebug() << "httpCode: "<<httpCode;

        QByteArray response = reply->readAll();

        if (httpCode == 201)
        {
            LOG_INFO("Successfully register!");

            // 5️⃣ 解析 JSON
            QJsonParseError err;
            QJsonDocument docJson = QJsonDocument::fromJson(response, &err);

            if (err.error != QJsonParseError::NoError || !docJson.isObject()) {
                logMsg = "Illegally JSON" + err.errorString();
                LOG_ERROR(logMsg);
                reply->deleteLater();
                return -1;
            }

            QJsonObject obj = docJson.object();

            if (!obj.contains("device_key")) {
                logMsg = "Successfully register but no device_key.";
                LOG_ERROR(logMsg);
                reply->deleteLater();
                return -1;
            }

            QString deviceKey = obj.value("device_key").toString();
            logMsg = "deviceKey:" + deviceKey;
            LOG_INFO(logMsg);
            device_key = deviceKey;
        }
        else
        {
            logMsg = "Failed registration.httpCode: " + httpCode;
            LOG_ERROR(logMsg);
        }
    }
    else
    {
        logMsg = "Network request failed: " + reply->errorString();
        LOG_ERROR(logMsg);
    }

    reply->deleteLater();

    // 初始化云端oss成功
    return true;
    }catch(std::exception& e)
    {
        logMsg = "Initialize OSS failed: " + QString::fromStdString(e.what());
        LOG_ERROR(logMsg);
        return false;
    }
}

int uploadpictoOSS::send_heartbeat()
{
    // 设备信息
    device_id = "device-test";
    device_ip = "192.168.1.100";
    device_key = "d293b269-aa2b-4aad-91af-7d8ccca0e9f3";

    // 构建 JSON
   QJsonObject json;
   json["device_id"] = device_id;
   json["device_key"] = device_key;

   QJsonDocument doc(json);
   QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

   QNetworkAccessManager manager;

   QNetworkRequest request{ QUrl(heartbeatAPI) };
   request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

   QNetworkReply *reply = manager.post(request, jsonData);

   // 阻塞等待
   QEventLoop loop;
   QObject::connect(reply, &QNetworkReply::finished,
                    &loop, &QEventLoop::quit);
   loop.exec();

   // 处理响应
   int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
   QByteArray response = reply->readAll();

   if (reply->error() == QNetworkReply::NoError && httpCode == 200) {
       logMsg = "Successfully send heartbeat. response:" + response;
       LOG_INFO(logMsg);
   } else {
       logMsg = "Failed send heartbeat. HTTP:" + QString::number(httpCode) + " errorMeg:" + reply->errorString() + " response:" + response;
       LOG_ERROR(logMsg);
   }

   reply->deleteLater();
   return httpCode == 200 ? 0 : 1;
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

    QNetworkAccessManager manager;

    QUrl url(savepicAPI);
    QNetworkRequest request(url);

    QByteArray boundary = "----QtFormBoundary7MA4YWxkTrZu0gW";
    QByteArray body;

    // device_id
    body.append("--" + boundary + "\r\n");
    body.append("Content-Disposition: form-data; name=\"device_id\"\r\n\r\n");
    body.append(device_id.toUtf8() + "\r\n");

    // device_key
    body.append("--" + boundary + "\r\n");
    body.append("Content-Disposition: form-data; name=\"device_key\"\r\n\r\n");
    body.append(device_key.toUtf8() + "\r\n");

    // confidence
    body.append("--" + boundary + "\r\n");
    body.append("Content-Disposition: form-data; name=\"confidence\"\r\n\r\n");
    body.append("0.85\r\n");

    // file
    QFile file(localFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Failed to open file: ";
        logMsg = "Failed to open file: " + localFilePath;
        LOG_ERROR(logMsg);
        return -1;
    }

    body.append("--" + boundary + "\r\n");
    body.append(
        "Content-Disposition: form-data; name=\"file\"; filename=\"test_image.jpg\"\r\n");
    body.append("Content-Type: image/jpeg\r\n\r\n");
    body.append(file.readAll());
    body.append("\r\n");

    // end
    body.append("--" + boundary + "--\r\n");

    request.setHeader(
        QNetworkRequest::ContentTypeHeader,
        "multipart/form-data; boundary=" + boundary);
    request.setHeader(
        QNetworkRequest::ContentLengthHeader,
        QByteArray::number(body.size()));

    QNetworkReply *reply = manager.post(request, body);

    // 阻塞等待（仅限非 UI 线程）
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished,
                     &loop, &QEventLoop::quit);
    loop.exec();

    // 读取结果
    int httpCode = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray response = reply->readAll();

    if (reply->error() == QNetworkReply::NoError) {
        logMsg = "Successfully upload, HTTP:" + QString::number(httpCode) + " response:" + response;
        LOG_INFO(logMsg);
    } else {
        logMsg = "Failed upload, HTTP:" + QString::number(httpCode) + " errorMeg:" + reply->errorString() + " response:" + response;
        LOG_INFO(logMsg);
    }

    reply->deleteLater();
    return 0;

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
