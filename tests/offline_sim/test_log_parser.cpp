// ============================================================================
// test_log_parser.cpp — log_parser 雏形单测 (M0.5)
//
// 覆盖点:
//   1. parseLine 4 类已知事件 + Other 兜底
//   2. toEpochMs 日期/时间/ms 三段拼装
//   3. parseFile 跑 mini fixture,事件计数与时间戳真值核对
//   4. splitSessions 在标准两段 session 上的切分结果
//
// 不依赖外部文件;fixture 都是字符串字面量。运行:
//     ./test_log_parser
//   或在 IDE 里直接挂 QTest::qExec。
// ============================================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryFile>
#include <QtTest/QtTest>

#include "log_parser.h"

using offline_sim::LogEvent;
using offline_sim::LogEventType;
using offline_sim::LogParser;

namespace {

// 与 saveRawPic/log.txt 同口径的 mini fixture (UTF-8 中文)。
// 含两段 mini session:
//   #A: 11:05:43 Start ~ 11:05:46 Turn off (1 取图 + 1 速度)
//   #B: 11:26:18 Start ~ 11:26:20 Turn off (1 取图 + 1 速度)
const char* kMiniFixture =
    "2026-04-30 11:05:42 [INFO] [MainWindow] AI Optical Sorter Software Starting\n"
    "2026-04-30 11:05:43 [INFO] [camerathread] Start taking pictures\n"
    "2026-04-30 11:05:43 [INFO] [camerathread] --取图--2026-04-30 11:05:43.450 || 第2次\n"
    "2026-04-30 11:05:44 [INFO] [MainWindow] --速度--2026-04-30 11:05:44.171线速度：10m/min   转速：20r/min\n"
    "2026-04-30 11:05:46 [INFO] [camerathread] Turn off camera\n"
    "2026-04-30 11:26:18 [INFO] [MainWindow] AI Optical Sorter Software Starting\n"
    "2026-04-30 11:26:19 [INFO] [camerathread] Start taking pictures\n"
    "2026-04-30 11:26:19 [INFO] [camerathread] --取图--2026-04-30 11:26:19.180 || 第2次\n"
    "2026-04-30 11:26:19 [INFO] [MainWindow] --速度--2026-04-30 11:26:19.954线速度：5m/min   转速：10r/min\n"
    "2026-04-30 11:26:20 [INFO] [camerathread] Turn off camera\n";

}  // namespace

class LogParserTest : public QObject
{
    Q_OBJECT

private slots:
    // 1. 单行解析:Capture
    void parseLine_capture_ok()
    {
        const QString line =
            QStringLiteral("2026-04-30 11:05:43 [INFO] [camerathread] "
                           "--取图--2026-04-30 11:05:43.450 || 第2次");
        LogEvent ev = LogParser::parseLine(line);
        QCOMPARE(int(ev.type), int(LogEventType::Capture));
        QCOMPARE(ev.seq, 2);
        // 时间戳应取 body 内嵌的 ms 级戳 (.450) 而非行首 (.000)
        const qint64 expect = LogParser::toEpochMs(QStringLiteral("2026-04-30"),
                                                   QStringLiteral("11:05:43"),
                                                   450);
        QCOMPARE(ev.tMs, expect);
        QCOMPARE(ev.mPerMin, 0);
        QCOMPARE(ev.rpm, 0);
    }

    // 2. 单行解析:Velocity
    void parseLine_velocity_ok()
    {
        const QString line =
            QStringLiteral("2026-04-30 11:05:44 [INFO] [MainWindow] "
                           "--速度--2026-04-30 11:05:44.171线速度：10m/min   转速：20r/min");
        LogEvent ev = LogParser::parseLine(line);
        QCOMPARE(int(ev.type), int(LogEventType::Velocity));
        QCOMPARE(ev.mPerMin, 10);
        QCOMPARE(ev.rpm, 20);
        const qint64 expect = LogParser::toEpochMs(QStringLiteral("2026-04-30"),
                                                   QStringLiteral("11:05:44"),
                                                   171);
        QCOMPARE(ev.tMs, expect);
        QCOMPARE(ev.seq, -1);
    }

    // 3. 单行解析:SessionStart / SessionEnd
    void parseLine_sessionMarkers_ok()
    {
        {
            const QString line =
                QStringLiteral("2026-04-30 11:05:43 [INFO] [camerathread] Start taking pictures");
            LogEvent ev = LogParser::parseLine(line);
            QCOMPARE(int(ev.type), int(LogEventType::SessionStart));
            QVERIFY(ev.tMs > 0);
        }
        {
            const QString line =
                QStringLiteral("2026-04-30 11:11:24 [INFO] [camerathread] Turn off camera");
            LogEvent ev = LogParser::parseLine(line);
            QCOMPARE(int(ev.type), int(LogEventType::SessionEnd));
            QVERIFY(ev.tMs > 0);
        }
    }

    // 4. 单行解析:Other (兜底)
    void parseLine_other_fallback()
    {
        const QString line =
            QStringLiteral("2026-04-30 11:05:42 [INFO] [MainWindow] AI Optical Sorter Software Starting");
        LogEvent ev = LogParser::parseLine(line);
        QCOMPARE(int(ev.type), int(LogEventType::Other));
        QVERIFY(ev.tMs > 0);  // 行首戳仍然落到 ms 级 epoch (ms 部分 = 0)
        QCOMPARE(ev.raw, line);
    }

    // 5. toEpochMs 单调性:同日越晚的时间戳越大
    void toEpochMs_monotonic()
    {
        const qint64 a = LogParser::toEpochMs(QStringLiteral("2026-04-30"),
                                              QStringLiteral("11:05:43"), 450);
        const qint64 b = LogParser::toEpochMs(QStringLiteral("2026-04-30"),
                                              QStringLiteral("11:05:44"), 171);
        QVERIFY2(b > a, "11:05:44.171 应该大于 11:05:43.450");
        // 间隔 = 1000ms - 450ms + 171ms = 721ms
        QCOMPARE(b - a, qint64(721));
    }

    // 6. toEpochMs 输入非法:返回 0
    void toEpochMs_invalid_returnsZero()
    {
        QCOMPARE(LogParser::toEpochMs(QStringLiteral("not-a-date"),
                                      QStringLiteral("11:05:43"), 0),
                 qint64(0));
    }

    // 7. parseFile 整文件:事件类型分布
    void parseFile_miniFixture_eventCounts()
    {
        // 把 fixture 写到临时文件,然后让 parseFile 去读
        QTemporaryFile tmp;
        QVERIFY(tmp.open());
        tmp.write(kMiniFixture);
        tmp.close();

        QString err;
        QVector<LogEvent> evs = LogParser::parseFile(tmp.fileName(), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));

        // 行数:fixture 10 行,全部都应解析出事件
        QCOMPARE(evs.size(), 10);

        int captures = 0, velocities = 0, starts = 0, ends = 0, others = 0;
        for (const auto& e : evs) {
            switch (e.type) {
                case LogEventType::Capture:      ++captures;   break;
                case LogEventType::Velocity:     ++velocities; break;
                case LogEventType::SessionStart: ++starts;     break;
                case LogEventType::SessionEnd:   ++ends;       break;
                case LogEventType::Other:        ++others;     break;
            }
        }
        QCOMPARE(captures,   2);  // 两段 session 各 1 个 --取图--
        QCOMPARE(velocities, 2);  // 两段 session 各 1 个 --速度--
        QCOMPARE(starts,     2);
        QCOMPARE(ends,       2);
        QCOMPARE(others,     2);  // 两段 session 开头各 1 条 "AI Optical Sorter ... Starting"
    }

    // 8. splitSessions 在 mini fixture 上切两段
    void splitSessions_twoSessions_ok()
    {
        QTemporaryFile tmp;
        QVERIFY(tmp.open());
        tmp.write(kMiniFixture);
        tmp.close();

        const auto evs = LogParser::parseFile(tmp.fileName(), nullptr);
        QCOMPARE(evs.size(), 10);

        const auto sessions = LogParser::splitSessions(evs);
        QCOMPARE(sessions.size(), 2);

        // 第一段:从第 1 行的 "Start taking pictures"(idx=1)开始,
        // 到第 4 行的 "Turn off camera"(idx=4)结束。
        QCOMPARE(sessions[0].beginIdx, 1);
        QCOMPARE(sessions[0].endIdx,   4);
        // 第二段:idx 6..9
        QCOMPARE(sessions[1].beginIdx, 6);
        QCOMPARE(sessions[1].endIdx,   9);

        // 段内事件类型一致性抽查
        QCOMPARE(int(evs[sessions[0].beginIdx].type), int(LogEventType::SessionStart));
        QCOMPARE(int(evs[sessions[0].endIdx].type),   int(LogEventType::SessionEnd));
        QCOMPARE(int(evs[sessions[1].beginIdx].type), int(LogEventType::SessionStart));
        QCOMPARE(int(evs[sessions[1].endIdx].type),   int(LogEventType::SessionEnd));
    }

    // 9. parseFile 失败:打不开的文件,err 非空,返回空 vector
    void parseFile_missingFile_returnsEmpty()
    {
        QString err;
        auto evs = LogParser::parseFile(QStringLiteral("/no/such/path.log"), &err);
        QVERIFY(evs.isEmpty());
        QVERIFY2(!err.isEmpty(), "missing file 应该填 errMsg");
    }
};

QTEST_MAIN(LogParserTest)
#include "test_log_parser.moc"
