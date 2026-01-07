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

    QMessageBox::information(nullptr, "Updater Arguments", text);


//    args[0]  程序自身路径（永远是 exe）
//    args[1]  m_zipPath
//    args[2]  m_targetDir
//    args[3]  m_mainPid

    for (int i = 0; i < args.size(); ++i)
    {
        if (args[i] == "--zip" && i + 1 < args.size())
            m_zipPath = args[i + 1];

        if (args[i] == "--target" && i + 1 < args.size())
            m_targetDir = args[i + 1];

        if (args[i] == "--pid" && i + 1 < args.size())
            m_mainPid = args[i + 1].toLongLong();
    }

    return !m_zipPath.isEmpty() &&
           !m_targetDir.isEmpty() &&
           m_mainPid > 0;
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
    m_tempDir = m_targetDir + "/update_tmp";

    QDir tempDir(m_tempDir);
    if (tempDir.exists())
        tempDir.removeRecursively();

    QDir().mkpath(m_tempDir);

    QProcess process;

#ifdef Q_OS_WIN
    // Windows 10+ 自带 tar
    QString program = "tar";
    QStringList args;
    args << "-xf" << m_zipPath
         << "-C" << m_tempDir;

#elif defined(Q_OS_LINUX)
    QString program = "unzip";
    QStringList args;
    args << m_zipPath
         << "-d" << m_tempDir;
#endif

    process.start(program, args);
    process.waitForFinished(-1);

    if (process.exitCode() != 0)
    {
        qCritical() << "Extract failed:" << process.readAllStandardError();
    }
}
void Updater::backupOldVersion()
{
    QString backupZip = m_targetDir + "/pastVersion.zip";

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

    process.setWorkingDirectory(m_targetDir);
    process.start(program, args);
    process.waitForFinished(-1);
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

    QDir srcDir(m_tempDir);
    QDir dstDir(m_targetDir);

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


void Updater::restartMainApp()
{
    QProcess::startDetached(m_targetDir + "/guangxuan.exe");
}
