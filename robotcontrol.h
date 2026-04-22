#ifndef ROBOTCONTROL_H
#define ROBOTCONTROL_H

#include "robotcontrol/include/HR_Pro.h"
#include <QObject>
#include <QThread>
#include <QDebug>

class robotControl : public QObject
{
    Q_OBJECT
public:
    explicit robotControl(QObject *parent = nullptr);

    bool initRobot();

    void testRobotControl();

    const char* hostname = "192.168.0.20";
    unsigned short nPort = 10003;

    int nRet = -1;
};

#endif // ROBOTCONTROL_H
