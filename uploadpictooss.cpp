#include "uploadpictooss.h"
#include "logger.h"
#include <QSettings>


#include <curl/curl.h>
#include <QFileInfo>

uploadpictoOSS::uploadpictoOSS(QObject* parent)
{
     initializeOss();
}

uploadpictoOSS::~uploadpictoOSS()
{
}
bool uploadpictoOSS::initializeOss()
{

    // ===== 2. 构建 JSON =====
    QJsonObject json;
    json["device_id"] = device_id;
    json["device_ip"] = device_ip;
    json["register_key"] = register_key;

    QJsonDocument doc(json);
    QByteArray postData = doc.toJson(QJsonDocument::Compact);

    // ===== 3. 构建请求 =====
    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(registerAPI)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // 如果你测试 HTTPS 很慢，可以临时打开（正式环境别用）
    /*
    QSslConfiguration sslConfig = request.sslConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(sslConfig);
    */

    // ===== 4. 发送请求 =====
    QNetworkReply* reply = manager.post(request, postData);

    // ===== 5. 同步等待 =====
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished,
                     &loop, &QEventLoop::quit);
    loop.exec();

    // ===== 6. 解析结果 =====
    int httpCode = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray responseData = reply->readAll();

    bool success = false;


    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "JSON parse error:" << parseError.errorString();
        return false;
    }

    if (!jsonDoc.isObject()) {
        qWarning() << "Response is not a JSON object";
        return false;
    }

    QJsonObject obj = jsonDoc.object();
    if (reply->error() == QNetworkReply::NoError && httpCode == 201) {
        logMsg = QString("Successfully Initial, Response:%1").arg(QString::fromUtf8(responseData));
        LOG_INFO(logMsg);
        success = true;
        if (obj.contains("device_key") && obj.value("device_key").isString()) {
            device_key = obj.value("device_key").toString();
            qDebug() << "device_key parsed:" << device_key;
        } else {
            qWarning() << "device_key not found in response";
            return false;
        }
    } else {
        logMsg = QString("Failed Initial, HTTP Code:%1  Error:%2  Response:%3").arg(httpCode).arg(reply->errorString()).arg(QString::fromUtf8(responseData));
        LOG_ERROR(logMsg);
    }

    reply->deleteLater();
    int sH = send_heartbeat();
    qDebug() << "sH = " << sH;
    return sH;
}

int uploadpictoOSS::send_heartbeat()
{
    CURL* curl = curl_easy_init();
    if (!curl) return 1;
    QString status = "online";
    QString qjson = QString(
        "{\"device_id\":\"%1\",\"device_key\":\"%2\",\"status\":\"%3\"}"
    ).arg(device_id, device_key, status);

    std::string json = qjson.toUtf8().constData();

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, heartbeatAPI);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, json.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // ⭐ 核心：不接收响应体
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return 1;

    return http_code == 200 ? 0 : 1;
}

bool uploadpictoOSS::uploadImage(const QString &localFilePath, const int imageClass)
{
    QFileInfo fileInfo(localFilePath);
    qDebug()<<localFilePath;
    QString fileName = fileInfo.fileName();
    qDebug()<<fileName;

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
    curl_mime_filename(part, fileName.toUtf8().constData());
//    curl_mime_type(part, "image/jpeg");

    /* curl options */
    curl_easy_setopt(curl, CURLOPT_URL, savepicAPI.toUtf8().constData());
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);

    CURLcode res = curl_easy_perform(curl);

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    if (res == CURLE_OK) {
        logMsg = QString("Successfully upload, HTTP:%1 error:%2").arg(httpCode).arg(curl_easy_strerror(res));
        LOG_INFO(logMsg);
        qDebug() << logMsg;
    } else {
        logMsg = QString("Failed upload, HTTP:%1 error:%2")
                     .arg(httpCode)
                     .arg(curl_easy_strerror(res));
        LOG_ERROR(logMsg);
        qDebug() << logMsg;
    }

    curl_mime_free(mime);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK);
}
