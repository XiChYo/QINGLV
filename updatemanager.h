#ifndef UPDATEMANAGER_H
#define UPDATEMANAGER_H


#pragma once

#include <QObject>
#include <QString>


class QNetworkAccessManager;
class QNetworkReply;

class UpdateManager : public QObject
{
    Q_OBJECT
public:
    explicit UpdateManager(QObject* parent = nullptr);

    // 对外唯一调用接口
    void checkForUpdate();

signals:
    void noUpdate();
    void updateError(const QString& msg);

private slots:
    void onVersionReplyFinished();
    void onPackageDownloadFinished();

private:
    void downloadUpdatePackage();
    void startUpdaterAndExit();

private:
    QNetworkAccessManager* m_net;

    QString m_remoteVersion;
    QString m_downloadUrl;

    QString m_localFirmZipPath;
    QString m_localAlgorithmZipPath;

    int updateFaA = 1; // =0：软件和算法都不需要更新；=1：仅软件需要更新；=2：仅算法要更新；=3：软件和算法都要更新
};


#endif // UPDATEMANAGER_H
