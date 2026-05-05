// log_parser.cpp — 见 log_parser.h 头注释
#include "log_parser.h"

#include <QDateTime>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

namespace offline_sim {

namespace {

// ============================================================================
// 正则缓存:Qt5 的 QRegularExpression 默认线程安全,可静态共享。
// 所有正则都用 UTF-8 源码字面量 + QStringLiteral 编码到 UTF-16,匹配中文 OK。
// ============================================================================

const QRegularExpression& reLineHead()
{
    // logger 框架行首戳:"YYYY-MM-DD HH:MM:SS"(秒级)
    // 例: "2026-04-30 11:05:43 [INFO] [camerathread] ..."
    static const QRegularExpression r(QStringLiteral(
        R"(^(\d{4}-\d{2}-\d{2}) (\d{2}:\d{2}:\d{2}))"));
    return r;
}

const QRegularExpression& reCapture()
{
    // body: "--取图--YYYY-MM-DD HH:MM:SS.mmm || 第N次"
    static const QRegularExpression r(QStringLiteral(
        R"(--取图--(\d{4}-\d{2}-\d{2}) (\d{2}:\d{2}:\d{2})\.(\d{1,3}) \|\| 第(\d+)次)"));
    return r;
}

const QRegularExpression& reVelocity()
{
    // body: "--速度--YYYY-MM-DD HH:MM:SS.mmm线速度：Xm/min   转速：Yr/min"
    // "："是全角冒号 U+FF1A;m/min 与"转速"之间用 \s+ 宽松匹配(实际是 3 个空格)
    static const QRegularExpression r(QStringLiteral(
        R"(--速度--(\d{4}-\d{2}-\d{2}) (\d{2}:\d{2}:\d{2})\.(\d{1,3})线速度：(\d+)m/min\s+转速：(\d+)r/min)"));
    return r;
}

const QRegularExpression& reYoloInferStart()
{
    // body: "--开始识别--YYYY-MM-DD HH:MM:SS.mmm || 第N次"
    static const QRegularExpression r(QStringLiteral(
        R"(--开始识别--(\d{4}-\d{2}-\d{2}) (\d{2}:\d{2}:\d{2})\.(\d{1,3}) \|\| 第(\d+)次)"));
    return r;
}

const QRegularExpression& reYoloInferEnd()
{
    // body: "--识别完成--YYYY-MM-DD HH:MM:SS.mmm || 第N次"
    static const QRegularExpression r(QStringLiteral(
        R"(--识别完成--(\d{4}-\d{2}-\d{2}) (\d{2}:\d{2}:\d{2})\.(\d{1,3}) \|\| 第(\d+)次)"));
    return r;
}

}  // namespace

// ============================================================================
// toEpochMs
// ============================================================================
qint64 LogParser::toEpochMs(const QString& dateLocal,
                            const QString& timeLocal,
                            int msPart)
{
    // QDateTime::fromString 不接受空格分隔的"date time"格式,先拼成 ISO。
    QDateTime dt = QDateTime::fromString(dateLocal + QLatin1Char('T') + timeLocal,
                                         Qt::ISODate);
    if (!dt.isValid()) {
        return 0;
    }
    dt.setTimeSpec(Qt::LocalTime);
    const int clampedMs = qBound(0, msPart, 999);
    return dt.toMSecsSinceEpoch() + clampedMs;
}

// ============================================================================
// parseLine
// ============================================================================
LogEvent LogParser::parseLine(const QString& line)
{
    LogEvent ev;
    ev.raw = line;

    // 行首秒级戳 (作为没有内嵌 ms 戳时的 fallback)
    QString headDate, headTime;
    {
        auto m = reLineHead().match(line);
        if (m.hasMatch()) {
            headDate = m.captured(1);
            headTime = m.captured(2);
        }
    }

    // 1. 取图 (Capture)
    {
        auto m = reCapture().match(line);
        if (m.hasMatch()) {
            ev.type = LogEventType::Capture;
            ev.tMs  = toEpochMs(m.captured(1), m.captured(2), m.captured(3).toInt());
            ev.seq  = m.captured(4).toInt();
            return ev;
        }
    }

    // 2. 速度 (Velocity)
    {
        auto m = reVelocity().match(line);
        if (m.hasMatch()) {
            ev.type    = LogEventType::Velocity;
            ev.tMs     = toEpochMs(m.captured(1), m.captured(2), m.captured(3).toInt());
            ev.mPerMin = m.captured(4).toInt();
            ev.rpm     = m.captured(5).toInt();
            return ev;
        }
    }

    // 3. YOLO 推理开始 (YoloInferStart)
    {
        auto m = reYoloInferStart().match(line);
        if (m.hasMatch()) {
            ev.type = LogEventType::YoloInferStart;
            ev.tMs  = toEpochMs(m.captured(1), m.captured(2), m.captured(3).toInt());
            ev.seq  = m.captured(4).toInt();
            return ev;
        }
    }

    // 4. YOLO 推理完成 (YoloInferEnd)
    {
        auto m = reYoloInferEnd().match(line);
        if (m.hasMatch()) {
            ev.type = LogEventType::YoloInferEnd;
            ev.tMs  = toEpochMs(m.captured(1), m.captured(2), m.captured(3).toInt());
            ev.seq  = m.captured(4).toInt();
            return ev;
        }
    }

    // 5. SessionStart / SessionEnd
    if (line.contains(QStringLiteral("Start taking pictures"))) {
        ev.type = LogEventType::SessionStart;
        if (!headDate.isEmpty()) {
            ev.tMs = toEpochMs(headDate, headTime, 0);
        }
        return ev;
    }
    if (line.contains(QStringLiteral("Turn off camera"))) {
        ev.type = LogEventType::SessionEnd;
        if (!headDate.isEmpty()) {
            ev.tMs = toEpochMs(headDate, headTime, 0);
        }
        return ev;
    }

    // 6. Other:也带行首戳便于排序/调试
    ev.type = LogEventType::Other;
    if (!headDate.isEmpty()) {
        ev.tMs = toEpochMs(headDate, headTime, 0);
    }
    return ev;
}

// ============================================================================
// parseFile
// ============================================================================
QVector<LogEvent> LogParser::parseFile(const QString& filePath, QString* errMsg)
{
    QVector<LogEvent> out;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errMsg) {
            *errMsg = QStringLiteral("无法打开 %1: %2")
                          .arg(filePath, f.errorString());
        }
        return out;
    }
    QTextStream ts(&f);
    ts.setCodec("UTF-8");
    while (!ts.atEnd()) {
        const QString line = ts.readLine();
        if (line.isEmpty()) {
            continue;
        }
        out.push_back(parseLine(line));
    }
    return out;
}

// ============================================================================
// splitSessions
// ============================================================================
QVector<LogParser::SessionRange> LogParser::splitSessions(const QVector<LogEvent>& events)
{
    QVector<SessionRange> out;
    int begin = -1;
    for (int i = 0; i < events.size(); ++i) {
        const LogEventType t = events[i].type;
        if (t == LogEventType::SessionStart) {
            if (begin >= 0) {
                // 上一段没收到 SessionEnd,把上一段截到本 SessionStart 之前。
                // 这种情况不应该出现于 saveRawPic/log.txt,但容错。
                out.push_back({begin, i - 1});
            }
            begin = i;
        } else if (t == LogEventType::SessionEnd) {
            if (begin >= 0) {
                out.push_back({begin, i});
                begin = -1;
            }
            // 孤立的 SessionEnd:忽略(不会在常规日志里出现)
        }
    }
    if (begin >= 0) {
        // 最后一段开了没关:用最后一个事件作为 endIdx
        out.push_back({begin, events.size() - 1});
    }
    return out;
}

}  // namespace offline_sim
