#ifndef SAVELOCALPIC_H
#define SAVELOCALPIC_H

#include <exception>
#include <QString>
#include <QObject>
#include <QFileInfo>
#include <QThread>

class saveLocalpic: public QThread
{
    Q_OBJECT
public:
    saveLocalpic(QObject* parent = nullptr);

    void savelocalpicture(const QImage& img, const QString& fileName);

    void saveresultpicture(const QImage& img, const QString& fileName);

    int testint = 0;
    int times = 0;
private:
    static bool dirInited;

signals:
    void forOSSPathSig(const QString& FilePath, const int ImgClass);
};

#endif // SAVELOCALPIC_H
