#ifndef VALVECMD_H
#define VALVECMD_H

#include <cstdint>
#include <QMetaType>

struct ValveCmd
{
    int valveId;
    uint16_t mask;
};
Q_DECLARE_METATYPE(ValveCmd)
Q_DECLARE_METATYPE(std::vector<ValveCmd>)

#endif // VALVECMD_H
