// offline_encoder_driver.cpp — 见头文件。
#include "offline_encoder_driver.h"

#include <QTimer>
#include <QtDebug>
#include <algorithm>

#include "pipeline/pipeline_clock.h"

namespace offline_sim {

OfflineEncoderDriver::OfflineEncoderDriver(QObject* parent) : QObject(parent) {}

OfflineEncoderDriver::~OfflineEncoderDriver() = default;

// ============================================================================
// initialize
// ============================================================================
bool OfflineEncoderDriver::initialize(const EncoderDriverOptions& opt,
                                      const QVector<LogEvent>& events,
                                      QString* errMsg)
{
    m_opt = opt;
    m_vels.clear();

    if (m_opt.useRpm && m_opt.k_mmPerMs_perRpm <= 0.0) {
        if (errMsg) *errMsg = QStringLiteral(
            "useRpm=true 但 k_mmPerMs_perRpm <= 0(=%1),拒绝初始化").arg(
            m_opt.k_mmPerMs_perRpm);
        return false;
    }

    const auto sessions = LogParser::splitSessions(events);
    if (m_opt.sessionIdx < 0 || m_opt.sessionIdx >= sessions.size()) {
        if (errMsg) *errMsg = QStringLiteral(
            "sessionIdx 越界: %1, 总共 %2 段").arg(m_opt.sessionIdx).arg(sessions.size());
        return false;
    }
    const auto sr = sessions[m_opt.sessionIdx];

    // 第一条 Velocity 当 t0_log
    qint64 t0 = 0;
    for (int i = sr.beginIdx; i <= sr.endIdx; ++i) {
        if (events[i].type == LogEventType::Velocity) {
            t0 = events[i].tMs;
            break;
        }
    }
    if (t0 == 0) {
        if (errMsg) *errMsg = QStringLiteral(
            "session %1 内无 Velocity 事件").arg(m_opt.sessionIdx);
        return false;
    }
    m_t0Log = t0;

    for (int i = sr.beginIdx; i <= sr.endIdx; ++i) {
        const auto& e = events[i];
        if (e.type != LogEventType::Velocity) continue;
        if (e.tMs < t0) continue;  // 不应出现,但兜底
        ScheduledVel v;
        v.deltaMs = e.tMs - t0;
        v.mPerMin = e.mPerMin;
        v.rpm     = e.rpm;
        m_vels.push_back(v);
    }
    std::sort(m_vels.begin(), m_vels.end(),
              [](const ScheduledVel& a, const ScheduledVel& b) {
                  return a.deltaMs < b.deltaMs;
              });

    if (m_vels.isEmpty()) {
        if (errMsg) *errMsg = QStringLiteral(
            "session %1 过滤后 0 条 Velocity").arg(m_opt.sessionIdx);
        return false;
    }
    return true;
}

int OfflineEncoderDriver::scheduledCount() const { return m_vels.size(); }

// ============================================================================
// start / stop
// ============================================================================
void OfflineEncoderDriver::start()
{
    if (m_running.load()) return;
    m_running.store(true);

    const double ff = (m_opt.fastForward > 0.0) ? m_opt.fastForward : 0.0;

    int lastDelay = 0;
    for (int i = 0; i < m_vels.size(); ++i) {
        const qint64 raw = m_vels[i].deltaMs;
        int delay = 0;
        if (ff > 0.0) {
            delay = static_cast<int>(static_cast<double>(raw) / ff);
        }
        if (delay < 0) delay = 0;
        QTimer::singleShot(delay, this, [this, i]() { onVelocityArrived(i); });
        lastDelay = std::max(lastDelay, delay);
    }
    QTimer::singleShot(lastDelay + 50, this, [this]() {
        if (!m_running.load()) return;
        emit sessionEnded();
    });
}

void OfflineEncoderDriver::stop() { m_running.store(false); }

// ============================================================================
// onVelocityArrived
// ============================================================================
void OfflineEncoderDriver::onVelocityArrived(int idx)
{
    if (!m_running.load()) return;
    if (idx < 0 || idx >= m_vels.size()) return;
    const auto& v = m_vels[idx];

    SpeedSample s;
    s.tMs   = pipeline::nowMs();
    s.valid = true;
    if (m_opt.useRpm) {
        s.speedMmPerMs = static_cast<float>(m_opt.k_mmPerMs_perRpm * v.rpm);
    } else {
        // m/min → mm/ms 的换算:
        //   1 m/min = 1000 mm / 60000 ms = 1/60 mm/ms
        s.speedMmPerMs = static_cast<float>(v.mPerMin / 60.0);
    }
    emit speedSample(s);
}

}  // namespace offline_sim
