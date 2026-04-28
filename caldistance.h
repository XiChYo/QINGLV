#ifndef CALDISTANCE_H
#define CALDISTANCE_H
#include <QObject>
#include <QPoint>
#include <map>
#include <cmath>
#include <QDebug>
#include <valvecmd.h>
#include <QThread>
#include <QTimer>
class calDistance : public QObject
{
    Q_OBJECT
public:
    explicit calDistance(QObject* parent = nullptr);

    float pixelLength_X = 2448; // 总像素长度
    float pixelWidth_Y = 2048;  // 总像素宽度
    float realLength = 1.52;    // 实际长度, m
    float realWidth = 1.27;  // 实际宽度, m

    float endToSpray = 0.7; // 相机视野末端到喷阀的距离 m

    int getIndex(float realPositionX);
    std::vector<ValveCmd> generateCommands(int index, int numsOfNozzles);

    double step = realLength / 72;



public slots:
    void distance(const QPoint& corPoint, int objlength);
signals:
    void readyPoint(const std::vector<ValveCmd>& cmds, float realPositionY);

};

#endif // CALDISTANCE_H
