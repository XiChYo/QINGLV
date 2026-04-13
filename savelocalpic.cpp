#include "savelocalpic.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QImage>
#include <QDebug>

bool saveLocalpic::dirInited = false;

saveLocalpic::saveLocalpic(QObject* parent)
{
}

void saveLocalpic::savelocalpicture(const QImage& img, const QString& fileName)
{

//    if (testint == 1)
//    {
    times += 1;
    QImage imgReal = img;  // 把图像复制的代码放到这里来，不要放在相机线程
    imgReal.detach();  // 转换深拷贝
//    qDebug() << "savePic times:" << QString::number(times);
    // 保存图片到本地saveRawPic文件夹
    QString baseDir = QCoreApplication::applicationDirPath();

    QString saveDirPath = baseDir + "/saveRawPic";
    QDir saveDir(saveDirPath);
    if (!dirInited)
    {
        if (!saveDir.exists())
        {
            saveDir.mkpath(".");
        }
        dirInited = true;
    }
    QString filePath = saveDirPath + "/" + fileName;
    bool ok = imgReal.save(filePath, "JPG", 60);
    emit forOSSPathSig(filePath, 1); // 保存到本地的图片将命名发送到云端上传线程
    testint = 0;
//    }
}
