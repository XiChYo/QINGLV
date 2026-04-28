#include "boardcontrol.h"

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <string.h>
#include <QDebug>
#include <QThread>
#include <QElapsedTimer>
#include <QTimer>
#include <QThread>

boardControl::boardControl(QObject *parent)
    : QObject(parent)
{
}

boardControl::~boardControl()
{
    closeSerial();
}

// 串口底层

// 打开控制板串口
bool boardControl::openSerial()
{
    int jud = 0;
    while(1)
    {
    m_fd = ::open(m_dev.toLocal8Bit().data(),
                  O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (m_fd < 0 && jud == 0) {
            jud++;
            m_dev = "/dev/ttyUSB1";
            continue;
    }
    if(m_fd < 0 && jud == 1)
    {
        qDebug()<<"open serial failed";
        emit errorOccured("open serial failed");
        return false;
    }
    break;
    }

    termios tty{};
    tcgetattr(m_fd, &tty);

    cfmakeraw(&tty);

    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;

    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    tcflush(m_fd, TCIOFLUSH);
    tcsetattr(m_fd, TCSANOW, &tty);


    return true;
}

void boardControl::closeSerial()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

// 写串口
bool boardControl::writeFrame(const QByteArray& frame, int speedOrjet)
{
    if (speedRead == false && speedOrjet == 3){
        return false;
    }

    if (m_fd < 0)
    {
        qDebug()<<"writeFrame false";
        return false;
    }
    m_writeMutex.lock();
    ssize_t ret = ::write(m_fd, frame.constData(), frame.size());
    tcdrain(m_fd);
    m_writeMutex.unlock();

    return ret;
}

// 读串口
bool boardControl::readFrame(QByteArray& frame, int timeoutMs)
{
    const QByteArray head("\xAA\x55\x11\x05\x03\x0A", 6);
    const QByteArray tail("\x55\xAA", 2);
    const int frameLen = 11;

    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs)
    {
        // select 等待
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(m_fd, &rfds);

        timeval tv{};
        tv.tv_sec  = 0;
        tv.tv_usec = 20 * 1000; // 20ms 小轮询

        int ret = select(m_fd + 1, &rfds, nullptr, nullptr, &tv);
        if (ret > 0)
        {
            char buf[64];
            int n = ::read(m_fd, buf, sizeof(buf));
            if (n > 0)
            {
                m_rxBuffer.append(buf, n);
            }else if (errno != EAGAIN)
            {
                qDebug()<<"read error"<<strerror(errno);
            }

        }

        // 尝试拆帧
        while (true)
        {
            int headPos = m_rxBuffer.indexOf(head);
            if (headPos < 0)
            {
                // 没找到帧头，防止缓存无限长
                if (m_rxBuffer.size() > 64)
                    m_rxBuffer.clear();
                break;
            }

            // 数据不够一帧
            if (m_rxBuffer.size() < headPos + frameLen)
                break;

            QByteArray oneFrame = m_rxBuffer.mid(headPos, frameLen);

            // 校验帧尾
            if (!oneFrame.endsWith(tail))
            {
                // 错位 → 丢1字节继续找
                m_rxBuffer.remove(0, headPos + 1);
                continue;
            }

            // ✅ 成功拿到完整帧
            frame = oneFrame;

            // 移除已处理数据
            m_rxBuffer.remove(0, headPos + frameLen);
            return true;
        }
    }
    return false; // 超时
}

void boardControl::initSerial()
{
    if (!openSerial())
    {
        qDebug()<<"openSerial failed";
        return;
    }
    m_speedTimer = new QTimer(this);
    connect(m_speedTimer, &QTimer::timeout,
            this, &boardControl::requestEncoderSpeed);

    m_speedTimer->start(500);
}

// 单通道控制，不常用
void boardControl::singleBoardControl(QString order)
{
//    qDebug() << "speedRead:false";
    QMutexLocker locker(&m_serialMutex);
    speedRead = false;
    m_speedTimer->stop();
    qDebug() << "order:" << order;

    QByteArray field = QByteArray::fromHex(order.toLatin1());
    if (field.size() != 3) {
        qWarning() << "order format error!";
        return;
    }

    // 解析喷阀号（第一个字节）
    int valveId = static_cast<quint8>(field[0]);

    // 忙碌检测
    if (m_valveBusySet.contains(valveId))
    {
        qDebug() << "valve" << valveId << "is busy, drop command";
        return;
    }

    // 标记忙碌
    m_valveBusySet.insert(valveId);

    //  发送开启帧
    QByteArray openFrame = buildControlFrame(field);
    writeFrame(openFrame, 1);

    // 0.5 秒后发送关闭
    QByteArray closeField = field;
    closeField[2] = 0x00;  // 最后一位改为关闭

    QByteArray closeFrame = buildControlFrame(closeField);

    QThread::msleep(50);

    writeFrame(closeFrame, 1);
    // 释放忙碌（极其关键）
    m_valveBusySet.remove(valveId);

    qDebug() << "valve" << valveId << "released";

    speedRead = true;
    m_speedTimer->start();
//    qDebug() << "speedRead:true";
}

// 构建指令帧
QByteArray boardControl::buildControlFrame(const QByteArray& field)
{
    // 固定头
    QByteArray frame;
    frame.append(char(0xAA));
    frame.append(char(0x55));

    // 固定字段
    frame.append(char(0x11));
    frame.append(char(0x05));
    frame.append(char(0x01));

    // 可变三字节
    frame.append(field);

    // 计算校验
    quint8 sum = 0;
    sum += 0x11;
    sum += 0x05;
    sum += 0x01;
    sum += static_cast<quint8>(field[0]);
    sum += static_cast<quint8>(field[1]);
    sum += static_cast<quint8>(field[2]);

    quint8 checksum = sum & 0xFF;
    frame.append(char(checksum));

    // 帧尾
    frame.append(char(0x55));
    frame.append(char(0xAA));

//    qDebug() << "send:" << frame.toHex(' ').toUpper();

    return frame;
}

// 批量控制
void boardControl::batchBoardControl(QString order)
{
    if (busy) return;
    busy = true;
//    qDebug() << "speedRead:false";
    QMutexLocker locker(&m_serialMutex);
    speedRead = false;
    m_speedTimer->stop();

    QByteArray field = QByteArray::fromHex(order.toLatin1());
    if (field.size() != 3) {
        qWarning() << "order format error!";
        return;
    }

    // 解析喷阀号（第一个字节）
    int valveId = static_cast<quint8>(field[0]);

    // 忙碌检测
    if (m_valveBusySet.contains(valveId))
    {
        qDebug() << "valve" << valveId << "is busy, drop command";
        return;
    }

    // 标记忙碌
    m_valveBusySet.insert(valveId);

    QByteArray openFrame = buildBatchOpenFrame(field);
//    qDebug()<<"openframe:"<<openFrame.toHex(' ').toUpper();

    writeFrame(openFrame, 2);

    QByteArray closeFrame = buildBatchCloseFrame(field);
//    qDebug()<<"closeFrame:"<<closeFrame.toHex(' ').toUpper();

    QThread::msleep(50);


    writeFrame(closeFrame, 2);
    m_valveBusySet.remove(valveId);

    speedRead = true;
    busy = false;
    m_speedTimer->start();
//    qDebug() << "speedRead:true";
}

// 构建批量控制开启指令帧
QByteArray boardControl::buildBatchOpenFrame(const QByteArray& field)
{
    quint8 b6 = static_cast<quint8>(field[0]);
    quint8 b8 = static_cast<quint8>(field[1]);
    quint8 b9 = static_cast<quint8>(field[2]);

    // 第7位 = bit1数量
    quint8 bitCount = countBits(b8) + countBits(b9);

    return buildBatchFrameCore(b6, bitCount, b8, b9);
}

// 构建批量控制关闭指令帧
QByteArray boardControl::buildBatchCloseFrame(const QByteArray& field)
{
    quint8 b6 = static_cast<quint8>(field[0]);

    quint8 b7 = 0x09; //  固定 1111b
    quint8 b8 = 0x00;
    quint8 b9 = 0x00;

    return buildBatchFrameCore(b6, b7, b8, b9);
}

// 构建批量控制指令全帧
QByteArray boardControl::buildBatchFrameCore(
    quint8 b6,
    quint8 b7,
    quint8 b8,
    quint8 b9)
{
    QByteArray frame;

    // 帧头
    frame.append(char(0xAA));
    frame.append(char(0x55));

    // 固定
    frame.append(char(0x11));
    frame.append(char(0x06));
    frame.append(char(0x02));

    // 数据区
    frame.append(char(b6));
    frame.append(char(b7));
    frame.append(char(b8));
    frame.append(char(b9));

    // 校验
    quint8 sum = 0;
    sum += 0x11;
    sum += 0x06;
    sum += 0x02;
    sum += b6;
    sum += b7;
    sum += b8;
    sum += b9;

    quint8 checksum = sum & 0xFF;
    frame.append(char(checksum));

    // 帧尾
    frame.append(char(0x55));
    frame.append(char(0xAA));

//    qDebug() << "batch send:" << frame.toHex(' ').toUpper();

    return frame;
}


quint8 boardControl::countBits(quint8 v)
{
    quint8 cnt = 0;
    while (v) {
        cnt += (v & 1);
        v >>= 1;
    }
    return cnt;
}

// 请求速度
void boardControl::requestEncoderSpeed()
{

    QMutexLocker locker(&m_serialMutex);
    if(!speedRead) return;

    QByteArray cmd = QByteArray::fromHex(
        "AA 55 11 03 03 0A 21 55 AA");
    writeFrame(cmd, 3);

    QByteArray rx;

    if (readFrame(rx, 50)) {
        emit encoderSpeedReceived(rx);
    }

}

void boardControl::stopWork()
{
    closeSerial();
}


