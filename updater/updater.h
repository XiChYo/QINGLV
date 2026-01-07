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
    QString m_zipPath;
    QString m_targetDir;
    qint64  m_mainPid;
    QString m_tempDir;
};


#endif // UPDATER_H
