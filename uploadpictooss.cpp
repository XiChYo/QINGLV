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

#include <curl/curl.h>
#include <QFileInfo>

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
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    loop.exec();

    // 处理结果
    if (reply->error() == QNetworkReply::NoError)
    {
        int httpCode = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();


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
    return send_heartbeat();
    }catch(std::exception& e)
    {
        logMsg = "Initialize OSS failed: " + QString::fromStdString(e.what());
        LOG_ERROR(logMsg);
        return false;
    }
}

int uploadpictoOSS::send_heartbeat()
{
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
    qDebug()<<localFilePath;
    QFileInfo fileInfo(localFilePath);
    QString fileName = fileInfo.fileName();

    if (imageClass == 1)
        ossSaveRoad = "saveRawImage/" + fileName;
    else if (imageClass == 2)
        ossSaveRoad = "saveMarkImage/" + fileName;
    else if (imageClass == 3)
        ossSaveRoad = "saveLowConfidenceImage/" + fileName;

    CURL *curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("curl_easy_init failed");
        return false;
    }

    curl_mime *mime = curl_mime_init(curl);
    curl_mimepart *part = nullptr;

    /* device_id */
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "device_id");
    curl_mime_data(part, device_id.toUtf8().constData(), CURL_ZERO_TERMINATED);

    /* device_key */
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "device_key");
    curl_mime_data(part, device_key.toUtf8().constData(), CURL_ZERO_TERMINATED);

    /* confidence */
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "confidence");
    curl_mime_data(part, "0.85", CURL_ZERO_TERMINATED);

    /* file */
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    curl_mime_filedata(part, localFilePath.toUtf8().constData());
    curl_mime_type(part, "image/jpg");

    /* curl options */
    curl_easy_setopt(curl, CURLOPT_URL, savepicAPI.toUtf8().constData());
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);

    /* HTTPS 场景（调试用，正式环境应开启校验） */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    if (res == CURLE_OK) {
        logMsg = QString("Successfully upload, HTTP:%1").arg(httpCode);
        LOG_INFO(logMsg);
        qDebug() << "Successfully upload";
    } else {
        logMsg = QString("Failed upload, HTTP:%1 error:%2")
                     .arg(httpCode)
                     .arg(curl_easy_strerror(res));
        LOG_ERROR(logMsg);
        qDebug() << "Failed upload";
    }

    curl_mime_free(mime);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK);
}
