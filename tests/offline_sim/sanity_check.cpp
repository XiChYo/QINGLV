// ============================================================================
// sanity_check.cpp — 一次性 smoke,把 LogParser 跑在真实 saveRawPic/log.txt 上,
// 把事件计数和 session 边界与《测试需求.md》§2 数据资产盘点对核。
//
// 用法:
//     qmake sanity_check.pro && make
//     ./sanity_check /Users/fangjin/Downloads/saveRawPic/log.txt
//
// 这个程序不会进 commit;只是 M0.5 阶段开发者验真用。
// ============================================================================

#include <QCoreApplication>
#include <QHash>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QtCore/QDateTime>

#include <algorithm>
#include <limits>

#include "log_parser.h"

using offline_sim::LogEvent;
using offline_sim::LogEventType;
using offline_sim::LogParser;

static const char* tname(LogEventType t)
{
    switch (t) {
        case LogEventType::Capture:        return "Capture";
        case LogEventType::Velocity:       return "Velocity";
        case LogEventType::YoloInferStart: return "YoloInferStart";
        case LogEventType::YoloInferEnd:   return "YoloInferEnd";
        case LogEventType::SessionStart:   return "SessionStart";
        case LogEventType::SessionEnd:     return "SessionEnd";
        case LogEventType::Other:          return "Other";
    }
    return "?";
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    if (argc < 2) {
        out << "用法: " << argv[0] << " <log.txt 绝对路径>\n";
        return 2;
    }
    const QString path = QString::fromLocal8Bit(argv[1]);

    QString err;
    auto evs = LogParser::parseFile(path, &err);
    if (!err.isEmpty()) {
        out << "解析失败: " << err << "\n";
        return 1;
    }
    out << "总事件数: " << evs.size() << "\n";

    // 按类型计数
    int counts[7] = {0,0,0,0,0,0,0};
    for (const auto& e : evs) {
        switch (e.type) {
            case LogEventType::Capture:        counts[0]++; break;
            case LogEventType::Velocity:       counts[1]++; break;
            case LogEventType::YoloInferStart: counts[2]++; break;
            case LogEventType::YoloInferEnd:   counts[3]++; break;
            case LogEventType::SessionStart:   counts[4]++; break;
            case LogEventType::SessionEnd:     counts[5]++; break;
            case LogEventType::Other:          counts[6]++; break;
        }
    }
    out << "  Capture        = " << counts[0] << "  (期望 10771)\n";
    out << "  Velocity       = " << counts[1] << "  (期望 1739)\n";
    out << "  YoloInferStart = " << counts[2] << "  (期望 2027)\n";
    out << "  YoloInferEnd   = " << counts[3] << "  (期望 1981, 比 Start 少 46)\n";
    out << "  SessionStart   = " << counts[4] << "  (期望 2)\n";
    out << "  SessionEnd     = " << counts[5] << "  (期望 2)\n";
    out << "  Other          = " << counts[6] << "  (期望 8)\n";

    // YOLO 推理耗时统计:把 Start/End 按 (seq, 段) 配对,计算 ms 差。
    // 由于 End 比 Start 少 46 条,这里只数能配上的对,不强制 1:1。
    {
        QHash<QPair<int,int>, qint64> startMs;   // key = (sessionIdx, seq), 取最后一次 Start
        // 先把整张 sessions 表算出来,后面要给每个 YOLO 事件分段
        const auto sessions = LogParser::splitSessions(evs);
        auto sessionOf = [&](int idx) -> int {
            for (int i = 0; i < sessions.size(); ++i) {
                if (idx >= sessions[i].beginIdx && idx <= sessions[i].endIdx) return i;
            }
            return -1;
        };
        for (int k = 0; k < evs.size(); ++k) {
            if (evs[k].type != LogEventType::YoloInferStart) continue;
            const int s = sessionOf(k);
            if (s < 0) continue;
            startMs.insert(qMakePair(s, evs[k].seq), evs[k].tMs);
        }
        int  pairs = 0;
        qint64 sumMs = 0, minMs = std::numeric_limits<qint64>::max(), maxMs = 0;
        for (int k = 0; k < evs.size(); ++k) {
            if (evs[k].type != LogEventType::YoloInferEnd) continue;
            const int s = sessionOf(k);
            if (s < 0) continue;
            const auto key = qMakePair(s, evs[k].seq);
            if (!startMs.contains(key)) continue;
            const qint64 dt = evs[k].tMs - startMs.value(key);
            if (dt < 0) continue;
            ++pairs;
            sumMs += dt;
            minMs = std::min(minMs, dt);
            maxMs = std::max(maxMs, dt);
        }
        out << "\nYOLO 推理耗时统计:配对成功 " << pairs << " 对  ";
        if (pairs > 0) {
            out << "min=" << minMs << "ms  "
                << "avg=" << (sumMs / pairs) << "ms  "
                << "max=" << maxMs << "ms\n";
        } else {
            out << "(无可配对样本)\n";
        }
    }

    // session 切分
    auto sessions = LogParser::splitSessions(evs);
    out << "\nsessions = " << sessions.size() << " (期望 2)\n";
    for (int i = 0; i < sessions.size(); ++i) {
        const auto& s = sessions[i];
        const qint64 tBegin = evs[s.beginIdx].tMs;
        const qint64 tEnd   = evs[s.endIdx].tMs;
        QString tBeginStr = QDateTime::fromMSecsSinceEpoch(tBegin).toString("yyyy-MM-dd HH:mm:ss.zzz");
        QString tEndStr   = QDateTime::fromMSecsSinceEpoch(tEnd).toString("yyyy-MM-dd HH:mm:ss.zzz");

        // 段内首条速度作为 t0_log
        qint64 t0_log = 0;
        for (int k = s.beginIdx; k <= s.endIdx; ++k) {
            if (evs[k].type == LogEventType::Velocity) {
                t0_log = evs[k].tMs;
                break;
            }
        }
        QString t0_logStr = QDateTime::fromMSecsSinceEpoch(t0_log).toString("yyyy-MM-dd HH:mm:ss.zzz");

        // 段内 [2, 334] 范围内的 Capture 数(原始流)
        int rawCaps_2_334 = 0;
        // 段内 t0_log 之后 [2,334] 的 Capture 数(应进 OfflineCameraDriver 待发列表)
        int afterT0_2_334 = 0;
        for (int k = s.beginIdx; k <= s.endIdx; ++k) {
            if (evs[k].type != LogEventType::Capture) continue;
            if (evs[k].seq < 2 || evs[k].seq > 334) continue;
            if (evs[k].seq % 2 != 0) continue;  // 仅偶数 seq
            ++rawCaps_2_334;
            if (t0_log > 0 && evs[k].tMs >= t0_log) {
                ++afterT0_2_334;
            }
        }

        out << "\n  session #" << (i+1) << "\n"
            << "    beginIdx=" << s.beginIdx << " (" << tBeginStr << " " << tname(evs[s.beginIdx].type) << ")\n"
            << "    endIdx="   << s.endIdx   << " (" << tEndStr   << " " << tname(evs[s.endIdx].type)   << ")\n"
            << "    t0_log=  " << t0_logStr  << "  (段内首条速度)\n"
            << "    [2..334] 偶数 seq 原始流 = " << rawCaps_2_334
            << "  (期望 167)\n"
            << "    [2..334] t0_log 之后    = " << afterT0_2_334
            << "  (期望 162)\n";
    }
    return 0;
}
