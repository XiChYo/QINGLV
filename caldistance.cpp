#include "caldistance.h"

calDistance::calDistance(QObject* parent):QObject(parent)
{

}

void calDistance::distance(const QPoint& corPoint)
{
//    if (corPoint.x() == -1 && corPoint.y() == -1) return;
//    int picX = corPoint.x();
//    int picY = 2048 - corPoint.y();

//    float percX = picX / picLength;
//    float percY = picY / picWidth;

//    float realPosX = realLength * percX;
//    float realPosY = realWidth * percY + endToSpray - 0.32;

//    int index = getIndex(realPosX);

//    const std::vector<ValveCmd>& results = generateCommands(index);



    emit s_point("10,0");

    return;

}

int calDistance::getIndex(float realPosX)
{
//    double realLength = 1.52;
//    int parts = 72;

//    double step = realLength / parts;

    int index = std::round(realPosX / step);

    // 防止越界
    if (index < 0) index = 0;
    if (index >= 72) index = 72 - 1;

    return index;
}

std::vector<ValveCmd> calDistance::generateCommands(int index)
{
    std::map<int, uint16_t> valveMap;  // valveId -> mask

    std::vector<int> indices = {index - 4, index - 3, index - 2, index - 1, index, index + 1, index + 2, index + 3, index + 4};

    for (int idx : indices)
    {
        if (idx < 1 || idx > 72)
            continue;

        int valveId = (idx - 1) / 9 + 1;
        int channel = (idx - 1) % 9 + 1;

        // 设置 bit
        valveMap[valveId] |= (1 << (channel - 1));
    }

    // 转成 vector
    std::vector<ValveCmd> result;

    for (auto& [valveId, mask] : valveMap)
    {
        result.push_back({valveId, mask});
    }

    for (const auto& cmd : result)
    {
        uint8_t valve = cmd.valveId;
        uint8_t high = (cmd.mask >> 8) & 0xFF;
        uint8_t low  = cmd.mask & 0xFF;

        QString msg = QString("%1 %2 %3")
                .arg(valve, 2, 16, QChar('0'))
                .arg(high, 2, 16, QChar('0'))
                .arg(low, 2, 16, QChar('0'))
                .toUpper();

        qDebug() << "cmd : result Send:" << msg;
    }

    return result;
}
