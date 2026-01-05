<<<<<<< Updated upstream
#include "savelocalpic.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QImage>

saveLocalpic::saveLocalpic(QObject* parent)
{

}

void saveLocalpic::savelocalpicture(const QImage& img)
{
    // 保存图片到本地saveRawPic文件夹
    QString baseDir = QCoreApplication::applicationDirPath();

    QString saveDirPath = baseDir + "/saveRawPic";
    QDir saveDir(saveDirPath);
    if (!saveDir.exists())
    {
        saveDir.mkpath(".");
    }
    QString fileName = QString("img_%1.png")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz"));
    QString filePath = saveDirPath + "/" + fileName;
    img.save(filePath);
    emit forOSSPathSig(filePath, 1); // 保存到本地的图片将命名发送到云端上传线程
}
=======
#include "savelocalpic.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QImage>

saveLocalpic::saveLocalpic(QObject* parent)
{

}

void saveLocalpic::savelocalpicture(const QImage& img)
{
    // 保存图片到本地saveRawPic文件夹
    QString baseDir = QCoreApplication::applicationDirPath();

    QString saveDirPath = baseDir + "/saveRawPic";
    QDir saveDir(saveDirPath);
    if (!saveDir.exists())
    {
        saveDir.mkpath(".");
    }
    QString fileName = QString("img_%1.png")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz"));
    QString filePath = saveDirPath + "/" + fileName;
    img.save(filePath);
    emit forOSSPathSig(filePath, 1); // 保存到本地的图片将命名发送到云端上传线程
}
>>>>>>> Stashed changes
