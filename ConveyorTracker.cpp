#include "ConveyorTracker.h"
#include <QDebug>

ConveyorTracker::ConveyorTracker(QObject *parent)
    : QObject(parent)
{
    m_timer.start();
    m_lastTime = m_timer.elapsed();
}

// 生成物料任务，保存到vector结构中
int ConveyorTracker::addTrackerTask(const std::vector<ValveCmd>& cmds, float distance)
{
    QMutexLocker locker(&m_mutex);
    qDebug()<<"addTrackerTask: "<<distance;

    Task task;
    task.id = m_nextId++;
    task.targetDistance = distance;
    task.currentDistance = 0.0;
    task.finished = false;
    task.cmds = cmds;

    m_tasks.append(task);
    return task.id;
}

// 更新当前皮带速度, 并计算当前任务的剩余距离
void ConveyorTracker::updateSpeed(double speed)
{
    QMutexLocker locker(&m_mutex);

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
            for (const auto& cmd : task.cmds)
            {
                uint8_t valve = cmd.valveId;
                uint8_t high  = (cmd.mask >> 8) & 0xFF;
                uint8_t low   = cmd.mask & 0xFF;

                QString cmdStr = QString("%1 %2 %3")
                        .arg(valve, 2, 16, QChar('0'))
                        .arg(high, 2, 16, QChar('0'))
                        .arg(low, 2, 16, QChar('0'))
                        .toUpper();

            }

            // 当前任务剩余距离为0，发送对应任务的喷射许可信号
            emit trackerTaskFinishedSig(task);
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
