#ifndef UPDATER_H
#define UPDATER_H

#pragma once

#include <QObject>
#include <QString>

class Updater : public QObject
{
    Q_OBJECT
public:
    explicit Updater(QObject* parent = nullptr);

    bool initFromArgs();
    void start();

signals:
    void progressChanged(int value);
    void statusChanged(const QString& text);
    void finished(bool success);

private:
    void waitForMainProcessExit();
    void extractZip();
    void replaceFiles();
    void restartMainApp();
    void backupOldVersion();
    bool copyRecursively(const QString& src, const QString& dst);

private:
    QString m_FzipPath;
    QString m_AzipPath;

    QString m_targetFirmDir;
    QString m_targetAlgorithmDir;

    qint64  m_mainPid;

    QString m_tempFirmDir;
    QString m_tempAlgorithmDir;

    qint64 m_updateFaA;

    enum UpdateFlag
    {
        UpdateNone      = 0x0,  // 00
        UpdateFirmware  = 0x1,  // 01
        UpdateAlgorithm = 0x2   // 10
    };

};


#endif // UPDATER_H
