#include "pipeline/dispatcher.h"

#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <algorithm>
#include <cmath>

#include "infra/logger.h"
#include "pipeline/pipeline_clock.h"

namespace {

// 最小速度保护:避免除零或超大时间窗口。1 mm/s = 1e-3 mm/ms。
constexpr float kMinSpeedMmPerMs = 1e-3f;

}  // namespace

Dispatcher::Dispatcher(QObject* parent) : QObject(parent) {}
Dispatcher::~Dispatcher()
{
    closeArmCsv();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void Dispatcher::onSessionStart(const RuntimeConfig& cfg)
{
    m_cfg           = cfg;
    m_lastSpeed     = {};
    m_pending.clear();
    m_sessionActive = true;

    LOG_INFO(QString("Dispatcher sessionStart mode=%1 valve_distance=%2 min_cmd_interval=%3")
             .arg(cfg.sorterMode == RuntimeConfig::SorterMode::Valve ? "valve" : "arm")
             .arg(cfg.valveDistanceMm)
             .arg(cfg.valveMinCmdIntervalMs));
}

void Dispatcher::onSessionStop()
{
    // 取消所有尚在 BoardWorker 队列里的 pulse,避免 stop 后还弹阀。
    for (auto it = m_pending.constBegin(); it != m_pending.constEnd(); ++it) {
        emit cancelPulses(it.key());
    }
    m_pending.clear();
    m_lastSpeed = {};
    m_sessionActive = false;
    closeArmCsv();
}

void Dispatcher::onSpeedSample(const SpeedSample& s)
{
    const float oldSpeed = m_lastSpeed.valid ? m_lastSpeed.speedMmPerMs : s.speedMmPerMs;
    m_lastSpeed = s;
    if (!s.valid) return;

    // 速度重算判定:若偏差超过 cfg.valve.speed_recalc_threshold_pct,
    // 对 m_pending 里的每个任务 cancel + 重算 + enqueue。
    if (oldSpeed < kMinSpeedMmPerMs) return;
    const float delta    = std::fabs(s.speedMmPerMs - oldSpeed) / oldSpeed;
    const float thresh   = std::max(1, m_cfg.valveSpeedRecalcPct) / 100.0f;
    if (delta <= thresh) return;

    // 速度突变:对还有潜在未发完 pulse 的任务整体重算一次。
    // 重算后仍然为空(物体已彻底越过喷阀线)的任务,直接从 pending 移除。
    QVector<int> drop;
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        const int trackId = it.key();
        emit cancelPulses(trackId);
        const QVector<ValvePulse> pulses = computePulses(it.value(), s.speedMmPerMs);
        if (pulses.isEmpty()) {
            drop.push_back(trackId);
            continue;
        }
        emit enqueuePulses(pulses, trackId);
    }
    for (int id : drop) m_pending.remove(id);
}

// ---------------------------------------------------------------------------
// SortTask 路由
// ---------------------------------------------------------------------------
void Dispatcher::onSortTask(const SortTask& task)
{
    if (!m_sessionActive) return;

    if (m_cfg.sorterMode == RuntimeConfig::SorterMode::Arm) {
        dispatchArmStub(task);
        return;
    }

    // Valve 路径
    const float speed = m_lastSpeed.valid ? m_lastSpeed.speedMmPerMs
                                          : m_cfg.nominalSpeedMs;
    const QVector<ValvePulse> pulses = computePulses(task, speed);

    if (pulses.isEmpty()) {
        // 物体已越过喷阀线或 mask 为空 → 不入 pending,也不派发。
        emit warning(QString("Dispatcher: empty pulse list for trackId=%1 (already past or empty mask)")
                     .arg(task.trackId));
        return;
    }

    m_pending.insert(task.trackId, task);
    emit enqueuePulses(pulses, task.trackId);
}

// ---------------------------------------------------------------------------
// 算法:喷阀预计算(design.md §4.4)
// ---------------------------------------------------------------------------
float Dispatcher::valveLineYbMm() const
{
    // 必须与 TrackerWorker 保持一致。PR4 简化:realWidthMm + valveDistanceMm。
    return m_cfg.realWidthMm + m_cfg.valveDistanceMm;
}

QVector<ValvePulse> Dispatcher::computePulses(const SortTask& task, float speedMmPerMs) const
{
    QVector<ValvePulse> result;
    if (speedMmPerMs < kMinSpeedMmPerMs) return result;
    if (task.maskBeltRaster.empty()) return result;
    if (m_cfg.valveTotalChannels <= 0 || m_cfg.valveBoards <= 0 ||
        m_cfg.valveChannelsPerBoard <= 0) return result;

    const float m       = (m_cfg.maskRasterMmPerPx > 0.1f) ? m_cfg.maskRasterMmPerPx : 2.0f;
    const float valveY  = valveLineYbMm();
    const float chanW   = (m_cfg.valveXMaxMm - m_cfg.valveXMinMm)
                          / static_cast<float>(std::max(1, m_cfg.valveTotalChannels));
    if (chanW <= 0.0f) return result;

    // bbox 在 belt 系 mm。belt 的 y 轴沿运动方向递增,所以 bbox 里
    // yb_max 一侧(bb.y + bb.height) 是物体"前沿"(先到喷阀线),
    // yb_min 一侧(bb.y) 是"后沿"(最后到达)。
    const cv::Rect& bb      = task.bboxBeltRasterPx;
    const float bbox_yb_min = bb.y * m;                        // 后沿
    const float bbox_yb_max = (bb.y + bb.height) * m;          // 前沿
    const float bbox_xb_min = bb.x * m;

    // 前沿 / 后沿到喷阀线的剩余距离(mm)。
    const float dy_head = valveY - bbox_yb_max;    // 前沿剩余
    const float dy_tail = valveY - bbox_yb_min;    // 后沿剩余
    if (dy_tail < 0) {
        // 后沿已过喷阀线 → 物体全部越过,不再出 pulse。
        return result;
    }

    const qint64 tHead = task.tCaptureMs + static_cast<qint64>(std::round(dy_head / speedMmPerMs));
    const qint64 tTail = task.tCaptureMs + static_cast<qint64>(std::round(dy_tail / speedMmPerMs));
    if (tTail <= tHead) return result;

    // 头部留白面积(按 "前沿侧" 的行往后累积)
    const cv::Mat& mask = task.maskBeltRaster;
    const int totalArea = cv::countNonZero(mask);
    if (totalArea <= 0) return result;
    const int skipArea  = static_cast<int>(std::round(totalArea * m_cfg.valveHeadSkipRatio));

    // 沿 mask 从前沿(最后一行,mask.rows-1)向后沿扫描,
    // 累积到 >= skipArea 时该行作为 "开阀起始行"。
    int row_open_start = mask.rows - 1;
    int cum = 0;
    for (int r = mask.rows - 1; r >= 0; --r) {
        cum += cv::countNonZero(mask.row(r));
        if (cum >= skipArea) { row_open_start = r; break; }
    }
    // 该行对应的 belt yb(mask 在 bbox 局部,行 r=0 对应 bb.y = 后沿最近一侧)
    const float yb_open = (bb.y + row_open_start) * m;
    const float dy_open = valveY - yb_open;
    if (dy_open < 0) {
        // 留白后已越过喷阀线,物体太小或算不出 pulse
        return result;
    }
    const qint64 tOpenStart = task.tCaptureMs + static_cast<qint64>(std::round(dy_open / speedMmPerMs));

    // 滚动投影
    const int openDurMs = std::max(1, m_cfg.valveOpenDurationMs);
    const int minInterv = std::max(openDurMs, m_cfg.valveMinCmdIntervalMs);

    // 每个 board 上一次 tOpenMs,用于合并过于密集的 pulse
    QMap<int, qint64> lastOpenOnBoard;

    for (qint64 t = tOpenStart; t < tTail; t += openDurMs) {
        // 该时刻 belt 上"喷阀线对应物体的位置":valveY - 物体前沿已走过的距离。
        // 因为前沿初始在 bbox_yb_max,经过 dt 后前沿到 bbox_yb_max + speed*dt。
        // 喷阀线 y = valveY 对应 mask 内的行 = valveY - (bbox_yb_min + speed*dt - 0)
        const float obj_tail_yb  = bbox_yb_min + speedMmPerMs * (t - task.tCaptureMs);
        const float local_y_mm   = valveY - obj_tail_yb;  // 相对 mask 底部的 y
        const int   local_row    = static_cast<int>(std::round(local_y_mm / m));
        if (local_row < 0 || local_row >= mask.rows) continue;

        // 该行非零列 -> xb -> channel -> board/mask
        QMap<int, quint16> boardMask;
        const uchar* rowPtr = mask.ptr<uchar>(local_row);
        for (int c = 0; c < mask.cols; ++c) {
            if (!rowPtr[c]) continue;
            const float xb_mm = bbox_xb_min + c * m;
            if (xb_mm < m_cfg.valveXMinMm || xb_mm > m_cfg.valveXMaxMm) continue;
            const int chanIdx = static_cast<int>(
                std::floor((xb_mm - m_cfg.valveXMinMm) / chanW));
            if (chanIdx < 0 || chanIdx >= m_cfg.valveTotalChannels) continue;

            // boardId: 1..boards
            const int boardId = chanIdx / m_cfg.valveChannelsPerBoard + 1;
            const int bitPos  = chanIdx % m_cfg.valveChannelsPerBoard;
            if (boardId < 1 || boardId > m_cfg.valveBoards) continue;
            boardMask[boardId] |= static_cast<quint16>(1u << bitPos);
        }

        for (auto it = boardMask.constBegin(); it != boardMask.constEnd(); ++it) {
            const int boardId = it.key();
            // 合并:与上条同 board 的 pulse 间距 < min_cmd_interval_ms 时,
            // 把新 pulse 的 mask OR 进上一条。
            auto itLast = lastOpenOnBoard.find(boardId);
            if (itLast != lastOpenOnBoard.end() &&
                (t - itLast.value()) < minInterv &&
                !result.isEmpty()) {
                // 往回找该 board 的最后一条 pulse
                for (int k = result.size() - 1; k >= 0; --k) {
                    if (result[k].boardId == boardId) {
                        result[k].channelMask |= it.value();
                        result[k].tCloseMs = std::max(result[k].tCloseMs, t + openDurMs);
                        break;
                    }
                }
                continue;
            }
            ValvePulse p;
            p.tOpenMs     = t;
            p.tCloseMs    = t + openDurMs;
            p.boardId     = static_cast<quint8>(boardId);
            p.channelMask = it.value();
            result.push_back(p);
            lastOpenOnBoard[boardId] = t;
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Arm stub
// ---------------------------------------------------------------------------
void Dispatcher::dispatchArmStub(const SortTask& task)
{
    const float m       = (m_cfg.maskRasterMmPerPx > 0.1f) ? m_cfg.maskRasterMmPerPx : 2.0f;
    const cv::Rect& bb  = task.bboxBeltRasterPx;
    const float cx_mm   = (bb.x + bb.width  * 0.5f) * m;
    const float cy_mm   = (bb.y + bb.height * 0.5f) * m;

    auto toArm = [](float xb, float yb, float ox, float oy, int sx, int sy,
                    float& outX, float& outY) {
        outX = sx * (xb - ox);
        outY = sy * (yb - oy);
    };
    float aX = 0.0f, aY = 0.0f, bX = 0.0f, bY = 0.0f;
    toArm(cx_mm, cy_mm, m_cfg.armAOriginXMm, m_cfg.armAOriginYMm,
          m_cfg.armAXSign, m_cfg.armAYSign, aX, aY);
    toArm(cx_mm, cy_mm, m_cfg.armBOriginXMm, m_cfg.armBOriginYMm,
          m_cfg.armBXSign, m_cfg.armBYSign, bX, bY);

    if (!m_armCsvStream || m_armCsvDate != QDate::currentDate()) {
        if (!openArmCsv()) {
            LOG_ERROR(QString("Dispatcher: arm_stub_csv open failed path=%1")
                      .arg(m_cfg.armStubCsv));
        }
    }

    if (m_armCsvStream) {
        *m_armCsvStream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
                        << ',' << task.trackId
                        << ',' << task.finalClassId
                        << ',' << cx_mm << ',' << cy_mm
                        << ',' << aX    << ',' << aY
                        << ',' << bX    << ',' << bY
                        << '\n';
        m_armCsvStream->flush();
    }
    emit armStubDispatched(task.trackId, task.finalClassId, aX, aY, bX, bY);
}

// ---------------------------------------------------------------------------
// arm_stub.csv
// ---------------------------------------------------------------------------
bool Dispatcher::openArmCsv()
{
    closeArmCsv();
    if (m_cfg.armStubCsv.isEmpty()) return false;

    QFileInfo info(m_cfg.armStubCsv);
    const QDate today = QDate::currentDate();
    QDir dir(info.dir().filePath(today.toString("yyyyMMdd")));
    if (!dir.exists()) dir.mkpath(".");

    const QString fileName = info.fileName().isEmpty()
                                 ? QStringLiteral("arm_stub.csv")
                                 : info.fileName();
    m_armCsvFile = new QFile(dir.filePath(fileName));
    if (!m_armCsvFile->open(QIODevice::Append | QIODevice::Text)) {
        delete m_armCsvFile;
        m_armCsvFile = nullptr;
        return false;
    }
    m_armCsvDate = today;
    m_armCsvStream = new QTextStream(m_armCsvFile);
    // 若文件为空,写表头
    if (m_armCsvFile->size() == 0) {
        *m_armCsvStream << "ts_iso,trackId,classId,cx_mm,cy_mm,armA_x,armA_y,armB_x,armB_y\n";
        m_armCsvStream->flush();
    }
    return true;
}

void Dispatcher::closeArmCsv()
{
    if (m_armCsvStream) {
        m_armCsvStream->flush();
        delete m_armCsvStream;
        m_armCsvStream = nullptr;
    }
    if (m_armCsvFile) {
        m_armCsvFile->close();
        delete m_armCsvFile;
        m_armCsvFile = nullptr;
    }
    m_armCsvDate = QDate();
}
