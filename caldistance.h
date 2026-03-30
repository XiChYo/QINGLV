#ifndef CALDISTANCE_H
#define CALDISTANCE_H
#include <QObject>
#include <QPoint>
#include <map>
#include <cmath>
#include <QDebug>
#include <valvecmd.h>

class calDistance : public QObject
{
    Q_OBJECT
public:
    explicit calDistance(QObject* parent = nullptr);



    float picLength = 2448;
    float picWidth = 2048;
    float realLength = 1.52;    // m
    float realWidth = 1.27;  // m

    float endToSpray = 0.7; // 相机视野末端到喷阀的距离 m

    int getIndex(float realPosX);
    std::vector<ValveCmd> generateCommands(int index);

    double step = realLength / 72;




public slots:
    void distance(const QPoint& corPoint);

signals:
//    void s_point(const std::vector<ValveCmd>& cmds, float realPosY);
    void s_point(QByteArray robotOrder);

};

#endif // CALDISTANCE_H
