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
    saveLocalpic(QObject* parent);

    void savelocalpicture(const QImage& img);

signals:
    void forOSSPathSig(const QString& FilePath, const int ImgClass);
};

#endif // SAVELOCALPIC_H
