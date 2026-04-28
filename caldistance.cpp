#include "caldistance.h"

calDistance::calDistance(QObject* parent):QObject(parent)
{
}

// 计算喷阀指令和计算物料到喷阀的距离
void calDistance::distance(const QPoint& corPoint, int objlength)
{
    if (corPoint.x() == -1 && corPoint.y() == -1) return;
    int pixelX = corPoint.x();
    int pixelY = 2048 - corPoint.y();


    // 计算物料位置占相机视野的百分比
    float percentageX = pixelX / pixelLength_X;
    float percentageY = pixelY / pixelWidth_Y;

    // 计算物料现实的位置，单位m
    float realPositionX = realLength * percentageX;  // real cor x
    float realPositionY = realWidth * percentageY + endToSpray; // m

    // 计算物理宽度，后面求该宽度需要多少喷孔进行喷射
    float realobjwidth = realLength * (objlength / pixelLength_X);

    // 计算物理中心点位置对应哪一个喷孔
    int nozzleIndex = getIndex(realPositionX);

    // 计算物料宽度对应多少个喷孔
    int numsOfNozzles = getIndex(realobjwidth);

    // 中心点对应喷孔的经验值补偿
    if (nozzleIndex > 58)
    {
        nozzleIndex += 1;
    }else if(nozzleIndex < 14 && nozzleIndex >7)
    {
        nozzleIndex -= 1;
    }else if(nozzleIndex < 7 && nozzleIndex >= 2)
    {
        nozzleIndex -= 2;
    }

    // 根据中心喷孔和喷孔数量生成喷阀指令
    const std::vector<ValveCmd>& results = generateCommands(nozzleIndex, numsOfNozzles);
    qDebug()<<"numsOfNozzles:"<<numsOfNozzles;

    // 发送当前物料喷射相关信息到主处理线程
    emit readyPoint(results, realPositionY);

    return;

}

// 计算当前点位对应的喷孔位置
int calDistance::getIndex(float realPositionX)
{
    int nozzleIndex = std::round(realPositionX / step);

    // 防止越界
    if (nozzleIndex < 0) nozzleIndex = 0;
    if (nozzleIndex >= 72) nozzleIndex = 72 - 1;

    return nozzleIndex;
}

// 生成喷射指令
std::vector<ValveCmd> calDistance::generateCommands(int nozzleIndex, int numsOfNozzles)
{
    std::map<int, uint16_t> valveMap;  // valveId -> mask

    if (numsOfNozzles % 2 == 1)
    {
        numsOfNozzles += 1;
    }
    std::vector<int> nozzleIndices;

    int nozzleStart = nozzleIndex - numsOfNozzles;
    int nozzleEnd   = nozzleIndex + numsOfNozzles;

    // 边界限制：最小为 1，最大为 72
    if (nozzleStart <= 0)
    {
        nozzleStart = 1;
    }

    if (nozzleEnd >= 73)
    {
        nozzleEnd = 72;
    }

    // 生成 nozzleIndices
    for (int i = nozzleStart; i <= nozzleEnd; ++i)
    {
        nozzleIndices.push_back(i);
    }

    for (int idx : nozzleIndices)
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

//        qDebug() << "cmd : result Send:" << msg;
    }

    return result;
}
