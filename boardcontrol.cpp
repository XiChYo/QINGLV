#include "boardcontrol.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QThread>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include "logger.h"
#include "pipeline_clock.h"

// ============================================================================
// ctor / dtor
// ============================================================================
boardControl::boardControl(QObject *parent)
    : QObject(parent)
{
}

boardControl::~boardControl()
{
    closeSerial();
}

// ============================================================================
// 串口底层
// ============================================================================
bool boardControl::openSerial()
{
    int jud = 0;
    while (true) {
        m_fd = ::open(m_dev.toLocal8Bit().data(),
                      O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (m_fd < 0 && jud == 0) {
            jud++;
            m_dev = "/dev/ttyUSB1";
            continue;
        }
        if (m_fd < 0 && jud == 1) {
            LOG_ERROR("boardControl: open serial failed for ttyUSB0/ttyUSB1");
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

bool boardControl::writeFrame(const QByteArray& frame)
{
    if (m_fd < 0) return false;

    // 单一 busMutex:粒度=一帧,不跨帧持锁(替代原 m_writeMutex+m_serialMutex)。
    QMutexLocker locker(&m_busMutex);
    const ssize_t ret = ::write(m_fd, frame.constData(), frame.size());
    tcdrain(m_fd);
    return ret == frame.size();
}

bool boardControl::readFrame(QByteArray& frame, int timeoutMs)
{
    static const QByteArray kHead("\xAA\x55\x11\x05\x03\x0A", 6);
    static const QByteArray kTail("\x55\xAA", 2);
    static constexpr int kFrameLen = 11;

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(m_fd, &rfds);
        timeval tv{};
        tv.tv_sec  = 0;
        tv.tv_usec = 20 * 1000;
        int sel = select(m_fd + 1, &rfds, nullptr, nullptr, &tv);
        if (sel > 0) {
            char buf[64];
            int n = ::read(m_fd, buf, sizeof(buf));
            if (n > 0) {
                m_rxBuffer.append(buf, n);
            } else if (errno != EAGAIN) {
                LOG_ERROR(QString("boardControl::readFrame read error %1")
                          .arg(std::strerror(errno)));
            }
        }

        while (true) {
            int headPos = m_rxBuffer.indexOf(kHead);
            if (headPos < 0) {
                if (m_rxBuffer.size() > 64) m_rxBuffer.clear();
                break;
            }
            if (m_rxBuffer.size() < headPos + kFrameLen) break;
            QByteArray one = m_rxBuffer.mid(headPos, kFrameLen);
            if (!one.endsWith(kTail)) {
                m_rxBuffer.remove(0, headPos + 1);
                continue;
            }
            frame = one;
            m_rxBuffer.remove(0, headPos + kFrameLen);
            return true;
        }
    }
    return false;
}

// ============================================================================
// 初始化 / Session
// ============================================================================
void boardControl::initSerial()
{
    if (!openSerial()) {
        LOG_ERROR("boardControl: initSerial failed");
        return;
    }

    m_valveTick = new QTimer(this);
    m_valveTick->setTimerType(Qt::PreciseTimer);
    connect(m_valveTick, &QTimer::timeout, this, &boardControl::onValveTick);

    m_encoderTick = new QTimer(this);
    connect(m_encoderTick, &QTimer::timeout, this, &boardControl::onEncoderTick);
    // 启动即开始轮询编码器(与 session 解耦,便于 UI 侧速度显示始终生效)。
    m_encoderTick->start(m_encoderRequestIntervalMs);
}

void boardControl::onSessionStart(const RuntimeConfig& cfg)
{
    m_valveTickIntervalMs      = std::max(1, cfg.tickIntervalMs);
    m_encoderRequestIntervalMs = std::max(50, cfg.encoderRequestIntervalMs);
    m_encoderRawToMPerMin      = cfg.encoderRawToMPerMin;

    if (m_encoderTick) {
        if (m_encoderTick->interval() != m_encoderRequestIntervalMs) {
            m_encoderTick->start(m_encoderRequestIntervalMs);
        }
    }
    if (m_valveTick) {
        m_valveTick->start(m_valveTickIntervalMs);
    }
    m_sessionActive = true;
    m_pending.clear();
    LOG_INFO(QString("boardControl sessionStart tick=%1ms enc=%2ms raw2mPerMin=%3")
             .arg(m_valveTickIntervalMs)
             .arg(m_encoderRequestIntervalMs)
             .arg(m_encoderRawToMPerMin));
}

void boardControl::onSessionStop()
{
    m_sessionActive = false;
    if (m_valveTick) m_valveTick->stop();
    // 强制关闭所有已开未关的阀,避免漏卡关阀。
    QVector<ValvePulse> forceClose;
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        for (auto& slot : it.value()) {
            if (slot.sentOpen && !slot.sentClose) {
                forceClose.push_back(slot.pulse);
                slot.sentClose = true;
            }
        }
    }
    for (const auto& p : forceClose) {
        writeFrame(buildPulseCloseFrame(p));
    }
    m_pending.clear();
}

void boardControl::onEnqueuePulses(const QVector<ValvePulse>& pulses, int trackId)
{
    if (!m_sessionActive) return;
    QVector<PulseSlot> slots;
    slots.reserve(pulses.size());
    for (const auto& p : pulses) {
        slots.push_back({p, false, false});
    }
    m_pending[trackId] = std::move(slots);
}

void boardControl::onCancelPulses(int trackId)
{
    auto it = m_pending.find(trackId);
    if (it == m_pending.end()) return;
    // 保留已发 open 未发 close 的,避免漏关;剩余未 open 的丢弃。
    QVector<PulseSlot> keep;
    for (const auto& slot : it.value()) {
        if (slot.sentOpen && !slot.sentClose) {
            keep.push_back(slot);
        }
    }
    if (keep.isEmpty()) {
        m_pending.erase(it);
    } else {
        it.value() = std::move(keep);
    }
}

// ============================================================================
// Tick 处理
// ============================================================================
void boardControl::onValveTick()
{
    if (!m_sessionActive) return;
    const qint64 now = pipeline::nowMs();

    auto it = m_pending.begin();
    while (it != m_pending.end()) {
        auto& slots = it.value();
        bool allClosed = true;
        for (auto& slot : slots) {
            if (!slot.sentOpen && slot.pulse.tOpenMs <= now) {
                writeFrame(buildPulseOpenFrame(slot.pulse));
                slot.sentOpen = true;
            }
            if (slot.sentOpen && !slot.sentClose && slot.pulse.tCloseMs <= now) {
                writeFrame(buildPulseCloseFrame(slot.pulse));
                slot.sentClose = true;
            }
            if (!slot.sentClose) allClosed = false;
        }
        if (allClosed) it = m_pending.erase(it);
        else           ++it;
    }
}

void boardControl::onEncoderTick()
{
    requestEncoderSpeed();
}

// ============================================================================
// 帧构造:阀脉冲
// ============================================================================
QByteArray boardControl::buildPulseOpenFrame(const ValvePulse& p)
{
    // 协议:b6 = boardId(1..8);b7 = 开启通道数;b8/b9 = 两字节通道位图
    const quint16 mask    = p.channelMask;
    const quint8  b8      = (quint8)(mask & 0xFF);
    const quint8  b9      = (quint8)((mask >> 8) & 0xFF);
    const quint8  bitCnt  = countBits(b8) + countBits(b9);
    return buildBatchFrameCore(p.boardId, bitCnt, b8, b9);
}

QByteArray boardControl::buildPulseCloseFrame(const ValvePulse& p)
{
    // 关阀:b7 固定 0x09(协议约定),b8/b9 全 0
    return buildBatchFrameCore(p.boardId, 0x09, 0x00, 0x00);
}

// ============================================================================
// 帧构造:legacy single/batch
// ============================================================================
QByteArray boardControl::buildControlFrame(const QByteArray& field)
{
    QByteArray frame;
    frame.append(char(0xAA));
    frame.append(char(0x55));
    frame.append(char(0x11));
    frame.append(char(0x05));
    frame.append(char(0x01));
    frame.append(field);

    quint8 sum = 0x11 + 0x05 + 0x01
               + static_cast<quint8>(field[0])
               + static_cast<quint8>(field[1])
               + static_cast<quint8>(field[2]);
    frame.append(char(sum & 0xFF));
    frame.append(char(0x55));
    frame.append(char(0xAA));
    return frame;
}

QByteArray boardControl::buildBatchOpenFrame(const QByteArray& field)
{
    const quint8 b6 = static_cast<quint8>(field[0]);
    const quint8 b8 = static_cast<quint8>(field[1]);
    const quint8 b9 = static_cast<quint8>(field[2]);
    const quint8 bitCnt = countBits(b8) + countBits(b9);
    return buildBatchFrameCore(b6, bitCnt, b8, b9);
}

QByteArray boardControl::buildBatchCloseFrame(const QByteArray& field)
{
    const quint8 b6 = static_cast<quint8>(field[0]);
    return buildBatchFrameCore(b6, 0x09, 0x00, 0x00);
}

QByteArray boardControl::buildBatchFrameCore(quint8 b6, quint8 b7, quint8 b8, quint8 b9)
{
    QByteArray frame;
    frame.append(char(0xAA));
    frame.append(char(0x55));
    frame.append(char(0x11));
    frame.append(char(0x06));
    frame.append(char(0x02));
    frame.append(char(b6));
    frame.append(char(b7));
    frame.append(char(b8));
    frame.append(char(b9));
    const quint8 sum = 0x11 + 0x06 + 0x02 + b6 + b7 + b8 + b9;
    frame.append(char(sum & 0xFF));
    frame.append(char(0x55));
    frame.append(char(0xAA));
    return frame;
}

quint8 boardControl::countBits(quint8 v)
{
    quint8 cnt = 0;
    while (v) { cnt += (v & 1); v >>= 1; }
    return cnt;
}

// ============================================================================
// Legacy 单板/批量控制(UI Action 菜单触发)
// ============================================================================
void boardControl::singleBoardControl(QString order)
{
    QByteArray field = QByteArray::fromHex(order.toLatin1());
    if (field.size() != 3) {
        LOG_ERROR(QString("singleBoardControl format error: %1").arg(order));
        return;
    }
    writeFrame(buildControlFrame(field));

    // 50ms 后手动发关阀帧。原实现用 QThread::msleep 阻塞线程,现改为 QTimer 单触。
    QByteArray closeField = field;
    closeField[2] = 0x00;
    QTimer::singleShot(50, this, [this, closeField]() {
        writeFrame(buildControlFrame(closeField));
    });
}

void boardControl::batchBoardControl(QString order)
{
    QByteArray field = QByteArray::fromHex(order.toLatin1());
    if (field.size() != 3) {
        LOG_ERROR(QString("batchBoardControl format error: %1").arg(order));
        return;
    }
    writeFrame(buildBatchOpenFrame(field));
    QTimer::singleShot(50, this, [this, field]() {
        writeFrame(buildBatchCloseFrame(field));
    });
}

// ============================================================================
// 编码器请求
// ============================================================================
void boardControl::requestEncoderSpeed()
{
    static const QByteArray kEncReq =
        QByteArray::fromHex("AA 55 11 03 03 0A 21 55 AA");
    writeFrame(kEncReq);

    QByteArray rx;
    if (!readFrame(rx, 50)) return;

    emit encoderSpeedReceived(rx);

    // ---- PR3:解析为 SpeedSample ----
    if (rx.size() < 8) return;

    // 协议(master 实测口径): 第 6、7 字节为 u16 big-endian 的"转速代理",
    //   rotation      = (rx[6] << 8) | rx[7];
    //   speed(m/min) = rotation * encoderRawToMPerMin;   // master 默认 0.502
    //   speed(m/s)   = speed(m/min) / 60;
    //   speed(mm/ms) = speed(m/s)   // 数值相等
    // 因此 raw 既不是累计脉冲,也不是窗口增量脉冲,不需要差分或除以采样窗口。
    // 若后续硬件确认其实是累计脉冲,再改此段为 (raw - lastRaw)/(tNow - lastTMs) 口径。
    const quint16 raw =
        (static_cast<quint8>(rx[6]) << 8) |
         static_cast<quint8>(rx[7]);
    const qint64 tNow = pipeline::nowMs();

    SpeedSample s;
    s.tMs           = tNow;
    s.valid         = true;
    const float mPerMin = raw * m_encoderRawToMPerMin;
    s.speedMmPerMs  = mPerMin / 60.0f;
    emit speedSample(s);
}

void boardControl::stopWork()
{
    closeSerial();
}
