#include "robotcontrol.h"

robotControl::robotControl(QObject *parent): QObject(parent)
{

}

bool robotControl::initRobot()
{
//    int nRet = -1;
    // 连接机器人
//    const char* hostname = "192.168.0.10";
//    unsigned short nPort = 10003;
    qDebug()<<"start initrobot";
    nRet = HRIF_Connect(0,hostname,nPort);
    // 机器人本体上电
    nRet = HRIF_Electrify(0);
    qDebug()<<"连接控制器(启动主站，连接从站)";
    while(1)
    {
        int nCurFSM = -1;
        HRIF_ReadCurFSMFromCPS(0,0, nCurFSM);
        if(nCurFSM==14)
        {
           // 连接控制器(启动主站，连接从站)
           nRet = HRIF_Connect2Controller(0);
           break;
        }
        QThread::sleep(0.2);
    }
    qDebug()<<"判断控制器是否启动完成";
    while(1)
    {
        // 判断控制器是否启动完成
        int nStarted = -1;
        nRet = HRIF_IsControllerStarted(0,nStarted);
        if(nStarted==1)
        {
            break;
        }
        QThread::sleep(0.2);
    }
    qDebug()<<"使能机器人";
    while(1)
    {
        int nCurFSM = -1;
        HRIF_ReadCurFSMFromCPS(0,0, nCurFSM);
        if(nCurFSM==24)
        {
            // 使能机器人
            nRet = HRIF_GrpEnable(0,0);
            break;
        }
        QThread::sleep(0.2);
    }
    qDebug() << "finish.";
    return true;
}

void robotControl::testRobotControl()
{
    // 连接机器人
//    const char* hostname = "10.20.60.144";
//    unsigned short nPort = 10003;
    qDebug()<<"start testrobotcontrol";
    nRet = HRIF_Connect(0,hostname,nPort);

    // 设置力控坐标系控制方向为 Tool 坐标系方向 1：Tool 0：当前 ucs
    qDebug()<<"设置力控坐标系控制方向为 Tool 坐标系方向 1：Tool 0：当前 ucs";
    int nMode = 1;
    nRet = HRIF_SetForceToolCoordinateMotion(0,0,nMode);

    // 设置力控策略为恒力模式 0:恒力模式 2：越障模式
    qDebug()<<"设置力控策略为恒力模式";
    int nStrategy = 0;
    nRet = HRIF_SetForceControlStrategy(0,0,nStrategy);

    // 设置质量/惯量参数
    double dXMass = 40; double dYMass = 40; double dZMass = 40;
    double dRxMass = 10; double dRyMass = 10; double dRzMass = 10;
    nRet = HRIF_SetMassParams(0,0,dXMass,dYMass,dZMass,dRxMass,dRyMass,dRzMass);

    // 设置阻尼参数
    double dXDamp = 800; double dYDamp = 800; double dZDamp = 800;
    double dRxDamp = 40; double dRyDamp = 40; double dRzDamp = 40;
    nRet = HRIF_SetDampParams(0,0,dXDamp,dYDamp,dZDamp,dRxDamp,dRyDamp,dRzDamp);

    // 设置刚度参数
    double dXStiff = 1000; double dYStiff = 1000; double dZStiff = 1000;
    double dRxStiff = 100; double dRyStiff = 100; double dRzStiff = 100;
    nRet = HRIF_SetStiffParams(0,0,dXStiff,dYStiff,dZStiff,dRxStiff,dRyStiff,dRzStiff);

    // 设置力控探寻自由度状态 Z 方向
    int nX = 0; int nY = 0; int nZ = 1;
    int nRx = 0; int nRy = 0; int nRz = 0;
    nRet = HRIF_SetControlFreedom(0,0, nX, nY, nZ, nRx, nRy, nRz);

    // 设置力控目标力大小 Z 方向 20N
    double dXForce = 0; double dYForce = 0; double dZForce = 20;
    double dRxForce = 0; double dRyForce = 0; double dRzForce = 0;
    nRet = HRIF_SetForceControlGoal(0,0,dXForce,dYForce,dZForce,dRxForce,dRyForce,dRzForce);

    // 力控探寻直线速度
    double dMaxLinearVelocity = 15;

    // 力控探寻姿态角速度
    double dMaxAngularVelocity = 5;

    // 设置力控探寻最大直线速度及角速度
    nRet = HRIF_SetMaxSearchVelocities(0,0,dMaxLinearVelocity,dMaxAngularVelocity);

    // 设置各自由度（X/Y/Z/RX/RY/RZ）力控探寻最大距离
    double Dis_X = 300; double Dis_Y = 300; double Dis_Z = 300;
    double Dis_RX = 20; double Dis_RY = 20; double Dis_RZ = 20;
    nRet = HRIF_SetMaxSearchDistance(0,0,Dis_X,Dis_Y,Dis_Z,Dis_RX,Dis_RY,Dis_RZ);

    // 设置恒力控稳定阶段边界
    double Pos_X = 101; double Pos_Y = 100; double Pos_Z = 100;
    double Pos_RX = 20; double Pos_RY = 20; double Pos_RZ = 20;

    double Neg_X = -100; double Neg_Y = -100; double Neg_Z = -100;
    double Neg_RX = -20; double Neg_RY = -20; double Neg_RZ = -20;
    HRIF_SetSteadyContactDeviationRange(0,0,Pos_X,Pos_Y,Pos_Z,Pos_RX,Pos_RY,Pos_RZ,Neg_X,Neg_Y,Neg_Z,Neg_RX,Neg_RY,Neg_RZ);

    // 探寻起点
    HRIF_WayPoint(0,0, 1, 420,0,445,180,0,180, 0,0,0,0,0,0,"TCP","Base",100,500,0, 0,0,0,0,"ID0");
    qDebug()<<"判断运动是否完成";
    while(1)
    {
        bool bDone = false;
        // 判断运动是否完成
        int nRet = HRIF_IsMotionDone(0,0,bDone);
        if (bDone==true) break;
    }
    // 开启力控探寻
    int nState = 1;
    nRet = HRIF_SetForceControlState(0,0, nState);
    qDebug()<<"读取当前力控状态";
    while(1)
    {
        qDebug() << "Start force search.";
        // 读取当前力控状态
        // 0：关闭状态 1：开力控探寻状态 2：力控探寻完成状态 3：力控自由驱动状态
        int nState1 = -1;
        nRet = HRIF_ReadForceControlState(0,0, nState1);
        if(nState1==2) break;
        QThread::sleep(0.2);
    }
    // MoveL 路点运动
    HRIF_WayPoint(0,0, 1, 520,0,345,180,0,180, 0,0,0,0,0,0,"TCP","Base",60,500,30, 0,0,0,0,"ID0");
    qDebug()<<"判断路点是否完成";
    while(1)
    {
        bool bDone = false;
        // 判断路点是否完成
        HRIF_IsBlendingDone(0,0,bDone);
        if (bDone==true) break;
    }
    HRIF_WayPoint(0,0, 1, 520,200,345,180,0,180, 0,0,0,0,0,0,"TCP","Base",60,500,30, 0,0,0,0,"ID1");
    while(1)
    {
        bool bDone = false;
        // 判断路点是否完成
        HRIF_IsBlendingDone(0,0,bDone);
        if (bDone==true) break;
    }
    // 关闭力控
    qDebug()<<"关闭力控";
    int nState2 = 0;
    nRet = HRIF_SetForceControlState(0,0, nState2);
    qDebug() << "finish.";

}
