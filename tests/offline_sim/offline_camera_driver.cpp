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
QHash<int, QString> OfflineCameraDriver::indexFilesBySeq(const QString& rawDir)
{
    QHash<int, QString> out;
    static const QRegularExpression reSeq(
        QStringLiteral(R"(_(\d+)\.jpg$)"),
        QRegularExpression::CaseInsensitiveOption);

    QDirIterator it(rawDir,
                    QStringList() << QStringLiteral("*.jpg"),
                    QDir::Files,
                    QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString base = QFileInfo(path).fileName();
        auto m = reSeq.match(base);
        if (!m.hasMatch()) continue;
        bool ok = false;
        const int seq = m.captured(1).toInt(&ok);
        if (!ok) continue;
        // 同一 seq 出现多次时(理论不该)保留先出现的,后续告警。
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

    // 3. 索引 jpg
    const QHash<int, QString> seq2path = indexFilesBySeq(m_opt.rawDir);

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

    // 严格递增的 tCaptureMs:即便 noThrottle=true / fastForward=0 让多帧
    // 在同一 ms 命中,emit 出去的字段也不会撞,m_pathMap 不会自我覆盖,
    // 下游(AnnotatedFrameSink M3)按 tCaptureMs 反查就一定能取回源图。
    qint64 tCapture = now;
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
