#ifndef VALVECMD_H
#define VALVECMD_H

#include <cstdint>
#include <QMetaType>

struct ValveCmd
{
    int valveId;
    uint16_t mask;
};

struct Task
{
    int id;
    float targetDistance;
    float currentDistance;
    std::vector<ValveCmd> cmds;
    bool finished;
};

Q_DECLARE_METATYPE(ValveCmd)
Q_DECLARE_METATYPE(std::vector<ValveCmd>)
Q_DECLARE_METATYPE(Task)

#endif // VALVECMD_H
