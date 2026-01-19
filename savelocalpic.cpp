#include "savelocalpic.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QImage>
#include <QDebug>

saveLocalpic::saveLocalpic(QObject* parent)
{

}

void saveLocalpic::savelocalpicture(const QImage& img, const QString& fileName)
{
    // 保存图片到本地saveRawPic文件夹
    QString baseDir = QCoreApplication::applicationDirPath();

    QString saveDirPath = baseDir + "/saveRawPic";
    QDir saveDir(saveDirPath);
    if (!saveDir.exists())
    {
        saveDir.mkpath(".");
    }
    QString filePath = saveDirPath + "/" + fileName;
    bool ok = img.save(filePath, "JPG", 60);
    emit forOSSPathSig(filePath, 1); // 保存到本地的图片将命名发送到云端上传线程
}
