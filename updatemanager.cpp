#include "updatemanager.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QDir>
#include "logger.h"

static const QString CURRENT_VERSION = "1.0.0";

UpdateManager::UpdateManager(QObject* parent)
    : QObject(parent)
{
    m_net = new QNetworkAccessManager(this);
}

void UpdateManager::checkForUpdate()
{
    // 假设你的服务器返回一个 json
//    QNetworkRequest req(QUrl("https://example.com/version.json"));
//    QNetworkReply* reply = m_net->get(req);

//    connect(reply, &QNetworkReply::finished,
//            this, &UpdateManager::onVersionReplyFinished);
    onVersionReplyFinished();
}

void UpdateManager::onVersionReplyFinished()
{
//    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
//    if (!reply)
//        return;

//    if (reply->error() != QNetworkReply::NoError)
//    {
//        emit updateError(reply->errorString());
//        reply->deleteLater();
//        return;
//    }

//    QByteArray data = reply->readAll();
//    reply->deleteLater();

    /*
        version.json 示例：
        {
            "version": "1.1.0",
            "url": "https://example.com/update.zip"
        }
    */

//    QJsonDocument doc = QJsonDocument::fromJson(data);
//    if (!doc.isObject())
//    {
//        emit updateError("Invalid version json");
//        return;
//    }

//    QJsonObject obj = doc.object();
//    m_remoteVersion = obj.value("version").toString();
//    m_downloadUrl  = obj.value("url").toString();

//    if (m_remoteVersion <= CURRENT_VERSION)
//    {
//        emit noUpdate();
//        return;
//    }

    downloadUpdatePackage();
}

void UpdateManager::downloadUpdatePackage()
{
    QString appDir = QCoreApplication::applicationDirPath();

    // ★ 核心：本地新版本 zip 路径

    // TODO 这里要增加校验，校验压缩包内的文件是否有那几个必要的文件，如果没有，则不更新
    m_localFirmZipPath = appDir + "/update.zip";
    m_localAlgorithmZipPath = appDir + "/algorithmupdate.zip";

    if (!QFile::exists(m_localFirmZipPath))
    {
        qCritical() << "软件更新包不存在:" << m_localFirmZipPath;
        emit updateError("本地软件更新包 guangxuan.zip 不存在");
        return;
    }


    qDebug() << "找到软件本地更新包:" << m_localFirmZipPath;
//    QDir dir(QCoreApplication::applicationDirPath());
//    m_localFirmZipPath = dir.filePath("update.zip");

//    QNetworkRequest req(QUrl(m_downloadUrl));

//    QNetworkRequest request;
//    request.setUrl(QUrl("https://example.com/version.json"));
//    QNetworkReply* reply = m_net->get(request);

//    connect(reply, &QNetworkReply::finished,
//            this, &UpdateManager::onPackageDownloadFinished);
    onPackageDownloadFinished();
}

void UpdateManager::onPackageDownloadFinished()
{
//    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
//    if (!reply)
//        return;

//    if (reply->error() != QNetworkReply::NoError)
//    {
//        emit updateError(reply->errorString());
//        reply->deleteLater();
//        return;
//    }

//    QByteArray zipData = reply->readAll();
//    reply->deleteLater();

//    QFile file(m_localFirmZipPath);
//    if (!file.open(QIODevice::WriteOnly))
//    {
//        emit updateError("Cannot write update.zip");
//        return;
//    }

//    file.write(zipData);
//    file.close();

    startUpdaterAndExit();
}

void UpdateManager::startUpdaterAndExit()
{
    QString appDir = QCoreApplication::applicationDirPath();

    QString updaterPath = appDir + "/updater/updater.exe";

    bool exists = QFile::exists(updaterPath);
    qDebug() << "updater.exe";

    if (!exists)
    {
        qDebug() << "updater.exe 不存在";
    }

    QStringList args;
    args << "--Fzip" << m_localFirmZipPath;
    args << "--Azip" << m_localAlgorithmZipPath;
    args << "--target" << appDir;
    args << "--pid" << QString::number(QCoreApplication::applicationPid());
    args << "--FaA" << QString::number(updateFaA);

    bool ok = QProcess::startDetached(updaterPath, args);
    if (!ok)
    {
        qCritical() << "启动 updater 失败";
        return;
    }

    // 关键：立刻退出自己
    QCoreApplication::quit();
}
