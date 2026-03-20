#include "ConveyorTracker.h"
#include <QDebug>

ConveyorTracker::ConveyorTracker(QObject *parent)
    : QObject(parent)
{
    m_timer.start();
    m_lastTime = m_timer.elapsed();
}

int ConveyorTracker::addTask(const std::vector<ValveCmd>& cmds, float distance)
{
    QMutexLocker locker(&m_mutex);
    qDebug()<<"addTask: "<<distance;

    Task task;
    task.id = m_nextId++;
    task.targetDistance = distance;
    task.currentDistance = 0.0;
    task.finished = false;

    m_tasks.append(task);
    return task.id;
}

void ConveyorTracker::updateSpeed(double speed)
{
    QMutexLocker locker(&m_mutex);
//    qDebug()<<"updateSpeed:"<<speed;

    qint64 now = m_timer.elapsed();
    double dt = (now - m_lastTime) / 1000.0;
    m_lastTime = now;

    if (dt <= 0)
        return;

    // m/min → m/s
    double v1 = m_lastSpeed / 60.0;
    double v2 = speed / 60.0;

    double ds = (v1 + v2) * 0.5 * dt;

    for (int i = 0; i < m_tasks.size(); ++i)
    {
        Task &task = m_tasks[i];

        if (task.finished)
            continue;

        task.currentDistance += ds;

        if (task.currentDistance >= task.targetDistance)
        {
            task.finished = true;
            qDebug()<<"taskFinished task.id: "<<task.id;

            emit taskFinished(task);
        }
    }

    // 删除完成任务
    for (int i = m_tasks.size() - 1; i >= 0; --i)
    {
        if (m_tasks[i].finished)
        {
            m_tasks.removeAt(i);
        }
    }

    m_lastSpeed = speed;
}
