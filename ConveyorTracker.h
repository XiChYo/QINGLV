#ifndef CONVEYORTRACKER_H
#define CONVEYORTRACKER_H

#include <QObject>
#include <QElapsedTimer>
#include <QList>
#include <QMutex>
#include <valvecmd.h>

//struct Task
//{
//    int id;
//    float targetDistance;
//    float currentDistance;
//    std::vector<ValveCmd> cmds;
//    bool finished;
//};

class ConveyorTracker : public QObject
{
    Q_OBJECT

public:
    explicit ConveyorTracker(QObject *parent = nullptr);

    // 添加任务，返回任务ID
    int addTask(const std::vector<ValveCmd>& cmds, float distance);

    // 更新输送带速度 m/min
    void updateSpeed(double speed);

signals:
    // 当某个任务完成
    void taskFinished(Task task);

private:



    QList<Task> m_tasks;

    QElapsedTimer m_timer;
    qint64 m_lastTime = 0;

    double m_lastSpeed = 0.0;

    int m_nextId = 1;

    QMutex m_mutex;
};

#endif
