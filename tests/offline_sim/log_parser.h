#ifndef OFFLINE_SIM_LOG_PARSER_H
#define OFFLINE_SIM_LOG_PARSER_H

// ============================================================================
// log_parser:saveRawPic/log.txt 解析器(M0.5 + M1)。
//
// 当前职责(完整 4 类业务事件 + Other 兜底):
//   * Capture         [camerathread]    --取图--<HH:MM:SS.mmm> || 第N次
//   * Velocity        [MainWindow]      --速度--<HH:MM:SS.mmm>线速度：Xm/min   转速：Yr/min
//   * YoloInferStart  [yolorecognition] --开始识别--<HH:MM:SS.mmm> || 第N次
//   * YoloInferEnd    [yolorecognition] --识别完成--<HH:MM:SS.mmm> || 第N次
//   * SessionStart  "Start taking pictures"
//   * SessionEnd    "Turn off camera"
//   * Other         其余行(也保留,raw 字段非空,便于诊断)
//
// YoloInfer{Start,End} 仅用于"参考校验"(对比仿真 YoloWorker 的 onFrame ms vs.
// 真机推理耗时 = end.tMs - start.tMs),**不**喂入回放;§2.2 / §3.4。
//
// 时间戳口径:
//   - 行首"YYYY-MM-DD HH:MM:SS"是 logger 自身写入秒级戳;
//   - 但 Capture / Velocity 的 body 内嵌"YYYY-MM-DD HH:MM:SS.mmm"是 ms 级真值,
//     来自 camerathread / MainWindow 自己的打戳调用,**优先使用 body 内嵌戳**。
//   - 没有内嵌戳的行(SessionStart / SessionEnd / Other)用行首秒级戳,
//     ms 部分填 0。
//
// 接口纯 Qt(不依赖 OpenCV / RKNN / MVS),可在 mac 本地直接编译验证。
// ============================================================================

#include <QString>
#include <QVector>

namespace offline_sim {

enum class LogEventType {
    Capture,
    Velocity,
    YoloInferStart,
    YoloInferEnd,
    SessionStart,
    SessionEnd,
    Other,
};

struct LogEvent {
    LogEventType type    = LogEventType::Other;
    qint64       tMs     = 0;     // 解析后的绝对时间戳(ms since epoch),见 toEpochMs
    int          seq     = -1;    // Capture / YoloInferStart / YoloInferEnd: 第N次;其它类型 = -1
    int          mPerMin = 0;     // Velocity: 线速度,业务方告知不准,仅记录不使用
    int          rpm     = 0;     // Velocity: 转速,标定 K 用
    QString      raw;             // 原始整行(去掉行尾换行),便于诊断
};

class LogParser {
public:
    // 解析整个 log 文件,返回事件按出现顺序排列(即按 tMs 严格升序的近似)。
    // 失败(打不开文件等)返回空 vector,errMsg 非 nullptr 时填错误描述。
    static QVector<LogEvent> parseFile(const QString& filePath, QString* errMsg = nullptr);

    // 解析单行字符串。无法识别为已知类型的返回 type=Other + raw=line。
    // 不会抛异常;格式错误的"伪 Capture/Velocity 行"也会落到 Other。
    static LogEvent parseLine(const QString& line);

    // "YYYY-MM-DD" + "HH:MM:SS" + ms(可选,默认 0) -> ms-since-epoch(本地时区)。
    // 失败返回 0。
    // 仅供内部和测试使用;public 方便单测覆盖。
    static qint64 toEpochMs(const QString& dateLocal,
                            const QString& timeLocal,
                            int msPart = 0);

    // 切分 session:返回 [beginIdx, endIdx] 闭区间索引列表,索引指向 events vector。
    //   - 一段 session 由 SessionStart 起,到下一个 SessionEnd 止(都包含)。
    //   - 没有匹配 SessionEnd 的孤儿 SessionStart 也作为一段(endIdx = events.size()-1)。
    //   - 没有 SessionStart 的前缀 / 无 session 的尾部直接丢弃。
    struct SessionRange {
        int beginIdx;  // 第一个事件下标(>=0)
        int endIdx;    // 最后一个事件下标(>=beginIdx),闭区间
    };
    static QVector<SessionRange> splitSessions(const QVector<LogEvent>& events);
};

}  // namespace offline_sim

#endif  // OFFLINE_SIM_LOG_PARSER_H
