#include "updater.h"

#include <QCoreApplication>
#include <QProcess>
#include <QThread>
#include <QDir>
#include <QMessageBox>
#include <QFile>
#include <QDebug>


Updater::Updater(QObject* parent)
    : QObject(parent), m_mainPid(-1)
{
}

bool Updater::initFromArgs()
{
    QStringList args = QCoreApplication::arguments();
    QString text;
    text += "arguments count = " + QString::number(args.size()) + "\n\n";

    for (int i = 0; i < args.size(); ++i)
    {
        text += QString("args[%1] = %2\n").arg(i).arg(args[i]);
    }



//    args[0]  程序自身路径（永远是 exe）
//    args[2]  m_FzipPath
//    args[4]  m_targetFirmDir
//    args[6]  m_mainPid
//    args[8]  updateFaA
    bool ok = false;

    for (int i = 0; i < args.size(); ++i)
    {
        if (args[i] == "--Fzip" && i + 1 < args.size())
            m_FzipPath = args[i + 1];

        if (args[i] == "--Azip" && i + 1 < args.size())
            m_AzipPath = args[i + 1];

        if (args[i] == "--target" && i + 1 < args.size())
            m_targetFirmDir = args[i + 1];

        if (args[i] == "--pid" && i + 1 < args.size())
            m_mainPid = args[i + 1].toLongLong();

        if (args[i] == "--FaA" && i + 1 < args.size())
        {
            m_updateFaA = args[i + 1].toLongLong();
            ok = true;
        }
    }
    if(ok)
    {
        if (m_updateFaA == 0) // 软件和算法都不需要更新
        {
            return 0;
        }else if (m_updateFaA == 1) // 仅更新软件
        {
            return !m_FzipPath.isEmpty() &&
                   !m_targetFirmDir.isEmpty() &&
                   m_mainPid > 0;
        }else if(m_updateFaA == 2) // 仅更新算法
        {
            return !m_AzipPath.isEmpty() &&
                   !m_targetFirmDir.isEmpty() &&
                   m_mainPid > 0;
        }else if(m_updateFaA == 3) // 更新软件和算法
        {
            return !m_FzipPath.isEmpty() &&
                   !m_AzipPath.isEmpty() &&
                   !m_targetFirmDir.isEmpty() &&
                   m_mainPid > 0;
        }
    }
}

void Updater::start()
{
    emit statusChanged("Waiting for main program to exit...");
    emit progressChanged(10);
    waitForMainProcessExit();

    emit statusChanged("Extracting update package...");
    emit progressChanged(40);
    extractZip();

    emit statusChanged("Replacing files...");
    emit progressChanged(80);
    replaceFiles();

    emit statusChanged("Restarting application...");
    emit progressChanged(100);
    restartMainApp();

    emit finished(true);
}

void Updater::waitForMainProcessExit()
{
    while (true)
    {
        QProcess process;
        process.start("tasklist");
        process.waitForFinished();

        QByteArray output = process.readAllStandardOutput();

        if (!output.contains(QByteArray::number(m_mainPid)))
        {
            break;
        }


        QThread::msleep(500);
    }
}

void Updater::extractZip()
{
    if(m_updateFaA & UpdateFirmware)
    {
        m_tempFirmDir = m_targetFirmDir + "/update_tmp";  // 这里是存储更新包解压文件的地方

        QDir tempDir(m_tempFirmDir);
        if (tempDir.exists())
            tempDir.removeRecursively();

        QDir().mkpath(m_tempFirmDir);

        QProcess process;

    #ifdef Q_OS_WIN
        // Windows 10+ 自带 tar
        QString program = "tar";
        QStringList args;
        args << "-xf" << m_FzipPath
             << "-C" << m_tempFirmDir;

    #elif defined(Q_OS_LINUX)
        QString program = "unzip";
        QStringList args;
        args << m_FzipPath
             << "-d" << m_tempFirmDir;
    #endif

        process.start(program, args);
        process.waitForFinished(-1);

        if (process.exitCode() != 0)
        {
            qCritical() << "Extract failed:" << process.readAllStandardError();
        }
    }
    if(m_updateFaA & UpdateAlgorithm)
    {

        m_tempAlgorithmDir = m_targetAlgorithmDir + "/Algorithm_update_tmp";

        QDir tempDir(m_tempAlgorithmDir);
        if (tempDir.exists())
            tempDir.removeRecursively();

        QDir().mkpath(m_tempAlgorithmDir);

        QProcess process;

    #ifdef Q_OS_WIN
        // Windows 10+ 自带 tar
        QString program = "tar";
        QStringList args;
        args << "-xf" << m_AzipPath
             << "-C" << m_tempAlgorithmDir;

    #elif defined(Q_OS_LINUX)
        QString program = "unzip";
        QStringList args;
        args << m_AzipPath
             << "-d" << m_tempAlgorithmDir;
    #endif

        process.start(program, args);
        process.waitForFinished(-1);

        if (process.exitCode() != 0)
        {
            qCritical() << "Extract failed:" << process.readAllStandardError();
        }
    }
}
void Updater::backupOldVersion()
{
    if (m_updateFaA & UpdateFirmware)   // 更新软件
    {
        QString backupZip = m_targetFirmDir + "/pastFirmVersion.zip";

        if (QFile::exists(backupZip))
            QFile::remove(backupZip);

        QProcess process;

    #ifdef Q_OS_WIN
        // Windows 使用 tar
        QString program = "tar";
        QStringList args;

        args << "-cf" << backupZip
             << "guangxuan.exe"
             << "guangxuan_zh_CN.qm"
             << "guangxuan_zh_EN.qm";

    #elif defined(Q_OS_LINUX)
        // Linux 使用 zip
        QString program = "zip";
        QStringList args;

        args << "-r" << backupZip
             << "guangxuan"
             << "guangxuan_zh_CN.qm"
             << "guangxuan_zh_EN.qm";
    #endif

        process.setWorkingDirectory(m_targetFirmDir);
        process.start(program, args);
        process.waitForFinished(-1);
    }

    if(m_updateFaA & UpdateAlgorithm)
    {

        QString backupZip = m_targetAlgorithmDir + "/pastAlgorithmVersion.zip";

        if (QFile::exists(backupZip))
            QFile::remove(backupZip);

        QProcess process;

    // TODO 这下面要改成算法专用的更新
    #ifdef Q_OS_WIN
        QString program = "tar";
        QStringList args;

        args << "-cf" << backupZip
             << "guangxuan.exe"
             << "guangxuan_zh_CN.qm"
             << "guangxuan_zh_EN.qm";

    #elif defined(Q_OS_LINUX)
        // Linux 使用 zip
        QString program = "zip";
        QStringList args;

        args << "-r" << backupZip
             << "guangxuan"
             << "guangxuan_zh_CN.qm"
             << "guangxuan_zh_EN.qm";
    #endif

        process.setWorkingDirectory(m_targetAlgorithmDir);
        process.start(program, args);
        process.waitForFinished(-1);
    }
}

bool Updater::copyRecursively(const QString& src, const QString& dst)
{
    QDir srcDir(src);
    if (!srcDir.exists())
        return false;

    QDir().mkpath(dst);

    QFileInfoList entries = srcDir.entryInfoList(
        QDir::NoDotAndDotDot | QDir::AllEntries);

    for (const QFileInfo& entry : entries)
    {
        QString srcPath = entry.absoluteFilePath();
        QString dstPath = dst + "/" + entry.fileName();

        if (entry.isDir())
        {
            copyRecursively(srcPath, dstPath);
        }
        else
        {
            QFile::remove(dstPath);
            QFile::copy(srcPath, dstPath);
        }
    }
    return true;
}

void Updater::replaceFiles()
{
    backupOldVersion();

    if(m_updateFaA & UpdateFirmware)
    {
        QDir srcDir(m_tempFirmDir);
        QDir dstDir(m_targetFirmDir);

        QFileInfoList entries = srcDir.entryInfoList(
            QDir::NoDotAndDotDot | QDir::AllEntries);

        for (const QFileInfo& entry : entries)
        {
            QString srcPath = entry.absoluteFilePath();
            QString dstPath = dstDir.filePath(entry.fileName());

            if (entry.isDir())
            {
                QDir().mkpath(dstPath);
                copyRecursively(srcPath, dstPath);
            }
            else
            {
                QFile::remove(dstPath);
                QFile::copy(srcPath, dstPath);
            }
        }
    }

    if(m_updateFaA & UpdateAlgorithm)
    {
        QDir srcDir(m_tempAlgorithmDir);
        QDir dstDir(m_targetAlgorithmDir);

        QFileInfoList entries = srcDir.entryInfoList(
            QDir::NoDotAndDotDot | QDir::AllEntries);

        for (const QFileInfo& entry : entries)
        {
            QString srcPath = entry.absoluteFilePath();
            QString dstPath = dstDir.filePath(entry.fileName());

            if (entry.isDir())
            {
                QDir().mkpath(dstPath);
                copyRecursively(srcPath, dstPath);
            }
            else
            {
                QFile::remove(dstPath);
                QFile::copy(srcPath, dstPath);
            }
        }
    }
}


void Updater::restartMainApp()
{
    if (m_updateFaA & UpdateFirmware)
    {
        QProcess::startDetached(m_targetFirmDir + "/guangxuan.exe");
    }

    if (m_updateFaA & UpdateAlgorithm)
    {
        QProcess::startDetached(m_targetAlgorithmDir + "/guangxuan.exe");  // 更换算法模型的路径
    }
}
