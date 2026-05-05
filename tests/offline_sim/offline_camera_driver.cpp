// offline_camera_driver.cpp — 见头文件。
#include "offline_camera_driver.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImage>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QTimer>
#include <QtDebug>
#include <algorithm>

#include "pipeline/pipeline_clock.h"

namespace offline_sim {

OfflineCameraDriver::OfflineCameraDriver(QObject* parent) : QObject(parent) {}

OfflineCameraDriver::~OfflineCameraDriver() = default;

// ============================================================================
// 文件名 -> seq 索引。saveRawPic 文件名形如:
//   img_20260430_110543_450.jpg_20.jpg
// 末段下划线后的整数即 seq(并与 log 里 "第N次" 一致)。
// ============================================================================
QHash<int, QString> OfflineCameraDriver::indexFilesBySeq(const QString& rawDir,
                                                          qint64 winStartMs,
                                                          qint64 winEndMs)
{
    QHash<int, QString> out;
    // 形如:img_20260430_110543_450.jpg_20.jpg
    //          ^^^^^^^^ ^^^^^^ ^^^         ^^
    //          YYYYMMDD HHMMSS mmm         seq
    static const QRegularExpression reFull(
        QStringLiteral(R"(^img_(\d{8})_(\d{6})_(\d{3})\.jpg_(\d+)\.jpg$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reSeqOnly(
        QStringLiteral(R"(_(\d+)\.jpg$)"),
        QRegularExpression::CaseInsensitiveOption);

    const bool hasWin = (winStartMs > 0 && winEndMs > 0 && winEndMs >= winStartMs);

    QDirIterator it(rawDir,
                    QStringList() << QStringLiteral("*.jpg"),
                    QDir::Files,
                    QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString base = QFileInfo(path).fileName();

        int seq = -1;
        bool inWindow = !hasWin;  // 没给窗时一律放行
        auto mFull = reFull.match(base);
        if (mFull.hasMatch()) {
            bool okSeq = false;
            seq = mFull.captured(4).toInt(&okSeq);
            if (!okSeq) continue;
            if (hasWin) {
                const QString d = mFull.captured(1);
                const QString t = mFull.captured(2);
                const int    ms = mFull.captured(3).toInt();
                // YYYYMMDD/HHMMSS 不带分隔符,LogParser::toEpochMs 期望带分隔符,
                // 这里手工拼一下。
                const QString dateLocal = QStringLiteral("%1-%2-%3").arg(
                    d.left(4), d.mid(4, 2), d.mid(6, 2));
                const QString timeLocal = QStringLiteral("%1:%2:%3").arg(
                    t.left(2), t.mid(2, 2), t.mid(4, 2));
                const qint64 fileTs = LogParser::toEpochMs(dateLocal, timeLocal, ms);
                if (fileTs <= 0) continue;
                inWindow = (fileTs >= winStartMs && fileTs <= winEndMs);
            }
        } else {
            // 文件名不符合预期格式时,退回老逻辑(只取末段 seq);此时无法做窗过滤,
            // 让它进 hash 兜底,避免完全丢图。
            auto mSeq = reSeqOnly.match(base);
            if (!mSeq.hasMatch()) continue;
            bool okSeq = false;
            seq = mSeq.captured(1).toInt(&okSeq);
            if (!okSeq) continue;
        }

        if (!inWindow) continue;
        // 同一 seq 出现多次时保留先出现的,后续靠时间窗过滤已基本去重;若依然
        // 撞 seq(同段相机重启 + 重置计数),保留先出现的并由调用方告警。
        if (!out.contains(seq)) {
            out.insert(seq, path);
        }
    }
    return out;
}

// ============================================================================
// initialize
// ============================================================================
bool OfflineCameraDriver::initialize(const CameraDriverOptions& opt,
                                     const QVector<LogEvent>& events,
                                     QString* errMsg)
{
    m_opt = opt;
    m_frames.clear();
    m_pathMap.clear();
    m_matchedFileCount = 0;
    m_lastEmitSimMs = -1;
    m_lastEmittedTCapture = -1;
    m_droppedSinceWarn = 0;
    m_lastWarnMs = -1;
    m_inFlight.store(0);

    // softFps -> 节流间隔
    if (m_opt.softFps > 0 && !m_opt.noThrottle) {
        m_intervalMs = 1000 / m_opt.softFps;
    } else {
        m_intervalMs = 0;
    }

    if (!QFileInfo(m_opt.rawDir).isDir()) {
        if (errMsg) *errMsg = QStringLiteral("rawDir 不存在或不是目录: %1").arg(m_opt.rawDir);
        return false;
    }

    // 1. session 切分 + 边界检查
    const auto sessions = LogParser::splitSessions(events);
    if (m_opt.sessionIdx < 0 || m_opt.sessionIdx >= sessions.size()) {
        if (errMsg) *errMsg = QStringLiteral(
            "sessionIdx 越界: %1, 总共 %2 段").arg(m_opt.sessionIdx).arg(sessions.size());
        return false;
    }
    const auto sr = sessions[m_opt.sessionIdx];

    // 2. 找该 session 第一条 Velocity 作为 t0_log
    qint64 t0 = 0;
    for (int i = sr.beginIdx; i <= sr.endIdx; ++i) {
        if (events[i].type == LogEventType::Velocity) {
            t0 = events[i].tMs;
            break;
        }
    }
    if (t0 == 0) {
        if (errMsg) *errMsg = QStringLiteral(
            "session %1 内无 Velocity 事件,无法定位 t0_log").arg(m_opt.sessionIdx);
        return false;
    }
    m_t0Log = t0;

    // 3. 索引 jpg —— 限定到当前 session 时间窗(±5s 容差),修复 F9:rawDir 同时
    //    含两段 session 的 jpg 时,不带时间窗会让两段同 seq jpg 互相覆盖,导致
    //    仿真选错图(YOLO 拿到本段时间戳,但喂的是另一段画面)。
    const qint64 winStart = events[sr.beginIdx].tMs - 5000;
    const qint64 winEnd   = events[sr.endIdx].tMs   + 5000;
    const QHash<int, QString> seq2path = indexFilesBySeq(m_opt.rawDir, winStart, winEnd);

    // 4. 选 Capture 事件:t_log >= t0_log + 在 [seqFrom, seqTo] 区间内
    int missingFile = 0;
    for (int i = sr.beginIdx; i <= sr.endIdx; ++i) {
        const auto& e = events[i];
        if (e.type != LogEventType::Capture) continue;
        if (e.tMs < t0) continue;
        if (m_opt.seqFrom >= 0 && e.seq < m_opt.seqFrom) continue;
        if (m_opt.seqTo   >= 0 && e.seq > m_opt.seqTo) continue;

        ScheduledFrame f;
        f.deltaMs = e.tMs - t0;
        f.seq     = e.seq;
        f.logTMs  = e.tMs;
        if (seq2path.contains(e.seq)) {
            f.filePath = seq2path.value(e.seq);
            f.fileName = QFileInfo(f.filePath).fileName();
            ++m_matchedFileCount;
        } else {
            ++missingFile;
            qWarning().noquote() << QStringLiteral(
                "[OfflineCameraDriver] Capture seq=%1 在 rawDir 中无对应 jpg,跳过").arg(e.seq);
        }
        m_frames.push_back(f);
    }

    // 5. 排个序兜底(events 已是 logger 出现顺序,理论上 deltaMs 已升序)
    std::sort(m_frames.begin(), m_frames.end(),
              [](const ScheduledFrame& a, const ScheduledFrame& b) {
                  return a.deltaMs < b.deltaMs;
              });

    if (m_frames.isEmpty()) {
        if (errMsg) *errMsg = QStringLiteral(
            "session %1 + seq=[%2,%3] 内 0 帧 Capture(过滤后)").arg(
            m_opt.sessionIdx).arg(m_opt.seqFrom).arg(m_opt.seqTo);
        return false;
    }
    return true;
}

int OfflineCameraDriver::scheduledCount() const { return m_frames.size(); }

int OfflineCameraDriver::matchedFileCount() const { return m_matchedFileCount; }

QString OfflineCameraDriver::filePathOf(qint64 tCaptureMs) const
{
    QMutexLocker lk(&m_pathMapMu);
    return m_pathMap.value(tCaptureMs);
}

// ============================================================================
// start / stop
// ============================================================================
void OfflineCameraDriver::start()
{
    if (m_running.load()) return;
    m_running.store(true);
    m_t0Sim = pipeline::nowMs();

    const double ff = (m_opt.fastForward > 0.0) ? m_opt.fastForward : 0.0;

    int lastDelay = 0;
    for (int i = 0; i < m_frames.size(); ++i) {
        const qint64 raw = m_frames[i].deltaMs;
        int delay = 0;
        if (ff > 0.0) {
            delay = static_cast<int>(static_cast<double>(raw) / ff);
        }
        if (delay < 0) delay = 0;
        QTimer::singleShot(delay, this, [this, i]() { onRawFrameArrived(i); });
        lastDelay = std::max(lastDelay, delay);
    }
    // session 末尾再多等 50ms,让最后一帧的反压告警(若有)落定后再 emit sessionEnded
    QTimer::singleShot(lastDelay + 50, this, [this]() {
        if (!m_running.load()) return;
        emit sessionEnded();
    });
}

void OfflineCameraDriver::stop() { m_running.store(false); }

void OfflineCameraDriver::onFrameConsumed()
{
    int v = m_inFlight.fetch_sub(1) - 1;
    if (v < 0) {
        // 配置上 driver-only/no-throttle 时不会用到反压,容许越界,clamp 到 0。
        m_inFlight.store(0);
    }
}

// ============================================================================
// onRawFrameArrived — 与《测试需求.md》§4.3 节流/反压伪代码同款
// ============================================================================
void OfflineCameraDriver::onRawFrameArrived(int eventIdx)
{
    if (!m_running.load()) return;
    if (eventIdx < 0 || eventIdx >= m_frames.size()) return;
    const ScheduledFrame& f = m_frames[eventIdx];

    // 文件缺失:不可能 emit 任何 QImage,直接跳。
    if (f.filePath.isEmpty()) return;

    const qint64 now = pipeline::nowMs();

    // 节流(墙钟)
    bool throttleSkip = false;
    if (!m_opt.noThrottle && m_intervalMs > 0 && m_lastEmitSimMs >= 0) {
        if (now - m_lastEmitSimMs < m_intervalMs) {
            throttleSkip = true;
        }
    }
    // 单元素反压
    bool backpressureSkip = false;
    if (!m_opt.noThrottle && m_inFlight.load() >= 1) {
        backpressureSkip = true;
    }

    if (throttleSkip || backpressureSkip) {
        if (backpressureSkip) {
            ++m_droppedSinceWarn;
            if (m_droppedSinceWarn >= m_opt.warnDropThreshold &&
                (m_lastWarnMs < 0 || now - m_lastWarnMs >= m_opt.warnDropQuietMs)) {
                emit warning(QStringLiteral(
                    "[OfflineCameraDriver] 已累计反压丢帧 %1 张").arg(m_droppedSinceWarn));
                m_lastWarnMs = now;
            }
        }
        return;
    }

    // 加载 QImage
    QImage img(f.filePath);
    if (img.isNull()) {
        qWarning().noquote() << QStringLiteral(
            "[OfflineCameraDriver] QImage 加载失败,跳过: %1").arg(f.filePath);
        return;
    }

    // 修复 F12:tCaptureMs 必须用 raw 文件本身的"物理拍摄时间"(对齐 log Capture
    // e.tMs - t0_log,与 raw 文件名 timestamp 一致),**不能**用墙钟 now。
    //
    // 根因:仿真侧 yolo 推理耗时 + 反压跳帧不均,会让"两次 emit 之间的墙钟差"
    // 与"两个 raw 文件物理拍摄时间差"严重不等(实测 case 3/4 墙钟 1000ms ↔ raw
    // 物理 504ms,case 6 墙钟 1000ms ↔ raw 物理 1185ms)。tracker 用 tCapture 差
    // 乘以 K*RPM 外推前帧 bbox 到当前时刻,墙钟口径会让外推位移系统性偏离 raw
    // 真实物理位移,关联打分 IoU/coverage 越过阈值。
    //
    // 改用 deltaMs 后,tCapture 语义重新对齐生产 CameraWorker:相机硬件 timestamp
    // 永远代表"快门按下的物理瞬间",与"该帧到达下游 worker 时的墙钟"无关。
    // m_lastEmittedTCapture 单调递增保护继续保留,防止 fastForward=0 时同 ms 撞。
    qint64 tCapture = f.deltaMs;
    if (tCapture <= m_lastEmittedTCapture) {
        tCapture = m_lastEmittedTCapture + 1;
    }
    m_lastEmittedTCapture = tCapture;
    {
        QMutexLocker lk(&m_pathMapMu);
        m_pathMap.insert(tCapture, f.filePath);
    }
    if (!m_opt.noThrottle) {
        m_inFlight.fetch_add(1);
    }
    m_lastEmitSimMs = now;

    emit frameReadySig(img, tCapture, f.fileName);
}

}  // namespace offline_sim
