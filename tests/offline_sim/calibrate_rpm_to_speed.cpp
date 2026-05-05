// ============================================================================
// calibrate_rpm_to_speed
//
// M0.7:角速度→线速度系数 K 标定（《测试需求.md》§3.5）
//
// 用法:
//   ./calibrate_rpm_to_speed                                                          \
//       --data-dir   /home/pi/offline_sim_data/saveRawPic                              \
//       --model      /home/pi/code/QINGLV/models/model-n-1024-robotic-final.rknn       \
//       [--seq-from  20]                                                               \
//       [--seq-to    70]                                                               \
//       [--config    /home/pi/code/QINGLV/config.ini]                                  \
//       [--output    offline_sim_outputs/calibration]                                  \
//       [--conf      0.25]                                                             \
//       [--nms       0.45]                                                             \
//       [--real-length-mm 1520]                                                        \
//       [--real-width-mm  1270]
//
// 模型说明(《测试需求.md》§2.3):仓库内 models/model-n-1024-robotic-final.rknn
//   是业务方指定的离线仿真专用 YOLOv8-seg(.rknn) 模型,输入 1024,n 系列(轻量)。
//   不要换成 model.rknn / model-n-640.rknn 等其他文件。
//
// 行为:
//   1. 解析 log.txt,提取 [seq_from, seq_to] 范围内的偶数 seq Capture 事件;
//      使用 body 内嵌 ms 级时间戳。
//   2. 在 data-dir 内按文件名 *_<seq>.jpg 索引文件路径。
//   3. 同线程串行调用 YoloWorker::onFrame,每帧只允许 0 或 1 个目标。
//      多目标视为标定窗口选错,直接 abort。
//   4. 像素 cy → mm cy_mm: cy_mm = cy_px * realWidthMm / imgHeightPx
//      (realWidthMm = 物理"宽度",对应 belt 运动方向 / 图像 y 方向)
//   5. 最小二乘拟合 (t_relative_s, cy_mm),得 slope_mm_per_s。
//      v_real_mm_per_ms = slope_mm_per_s / 1000。
//   6. RPM mean = 该窗口内所有 Velocity 事件 rpm 字段平均。
//   7. K = v_real_mm_per_ms / rpm_mean,单位 mm·min/(ms·r)。
//   8. 落 calib_detections.json / calib_fit.csv / calib_result.txt 三份产物。
//
// 守门 (§3.5.3):
//   - R² < 0.95 -> 警告但仍输出 K(由人工判断是否可用)
//   - 任一帧 objs.size() > 1 -> 错误退出,提示换窗口
//   - 任一帧加载失败 / 推理失败 -> 错误退出
//   - 窗口内 RPM mean ≤ 0 -> 错误退出
//
// 编译:
//   cd tests/offline_sim
//   qmake calibrate_rpm_to_speed.pro && make -j4
// ============================================================================

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QTextStream>
#include <QVector>

#include <opencv2/imgcodecs.hpp>

#include <cmath>
#include <cstdio>

#include "config/runtime_config.h"
#include "pipeline/pipeline_clock.h"
#include "pipeline/yolo_worker.h"

#include "log_parser.h"

namespace {

// ---------- 数据结构 ----------
struct CalibPoint {
    int     seq        = -1;
    qint64  tMs        = 0;
    double  tRelativeS = 0.0;
    double  cxPx       = 0.0;   // 仅记录,不入拟合
    double  cyPx       = 0.0;
    double  cyMm       = 0.0;
    int     classId    = -1;
    float   conf       = 0.0f;
    int     bboxX      = 0;
    int     bboxY      = 0;
    int     bboxW      = 0;
    int     bboxH      = 0;
    QString fileName;
};

struct LinReg {
    double slope        = 0.0;
    double intercept    = 0.0;
    double r2           = 0.0;
    double residualMax  = 0.0;
    int    n            = 0;
};

// ---------- 工具 ----------
// 在 dataDir 内按 "*_<seq>.jpg" 抓文件,seq 取最后一段 _N.jpg 中的 N。
QHash<int, QString> indexFilesBySeq(const QString& dataDir)
{
    QHash<int, QString> out;
    QDir d(dataDir);
    if (!d.exists()) return out;
    const QStringList files = d.entryList(QStringList() << "*.jpg", QDir::Files);
    static const QRegularExpression re(R"(_(\d+)\.jpg$)");
    for (const QString& f : files) {
        const QRegularExpressionMatch m = re.match(f);
        if (!m.hasMatch()) continue;
        bool ok = false;
        const int seq = m.captured(1).toInt(&ok);
        if (!ok) continue;
        out.insert(seq, d.filePath(f));
    }
    return out;
}

// 从 events 中取 [seq_from, seq_to] 范围内**偶数 seq** 的 Capture,按 seq 升序。
// 所有偶数 seq 都必须有 capture(否则报错让人工排查 log)。
QVector<offline_sim::LogEvent> selectCaptures(
    const QVector<offline_sim::LogEvent>& events,
    int seqFrom, int seqTo)
{
    QHash<int, offline_sim::LogEvent> bySeq;
    for (const auto& ev : events) {
        if (ev.type != offline_sim::LogEventType::Capture) continue;
        if (ev.seq < seqFrom || ev.seq > seqTo) continue;
        if (ev.seq % 2 != 0) continue;
        bySeq.insert(ev.seq, ev);   // 同 seq 重复时取最后一次(防御)
    }
    QVector<offline_sim::LogEvent> out;
    for (int s = seqFrom; s <= seqTo; s += 2) {
        if (bySeq.contains(s)) out.push_back(bySeq.value(s));
    }
    return out;
}

double meanRpm(const QVector<offline_sim::LogEvent>& events,
               qint64 tFromMs, qint64 tToMs)
{
    double sum = 0.0;
    int    n   = 0;
    for (const auto& ev : events) {
        if (ev.type != offline_sim::LogEventType::Velocity) continue;
        if (ev.tMs < tFromMs || ev.tMs > tToMs) continue;
        sum += ev.rpm;
        ++n;
    }
    return (n > 0) ? (sum / n) : 0.0;
}

LinReg fitLine(const QVector<double>& x, const QVector<double>& y)
{
    LinReg r;
    r.n = x.size();
    if (r.n < 2 || y.size() != x.size()) return r;
    double sx = 0, sy = 0;
    for (int i = 0; i < r.n; ++i) { sx += x[i]; sy += y[i]; }
    const double mx = sx / r.n;
    const double my = sy / r.n;
    double num = 0, den = 0;
    for (int i = 0; i < r.n; ++i) {
        const double dx = x[i] - mx;
        const double dy = y[i] - my;
        num += dx * dy;
        den += dx * dx;
    }
    if (den <= 0.0) return r;
    r.slope     = num / den;
    r.intercept = my - r.slope * mx;
    double ssRes = 0, ssTot = 0, maxRes = 0;
    for (int i = 0; i < r.n; ++i) {
        const double yhat = r.slope * x[i] + r.intercept;
        const double res  = y[i] - yhat;
        ssRes += res * res;
        ssTot += (y[i] - my) * (y[i] - my);
        maxRes = std::max(maxRes, std::abs(res));
    }
    r.r2          = (ssTot > 0.0) ? (1.0 - ssRes / ssTot) : 0.0;
    r.residualMax = maxRes;
    return r;
}

// ---------- 输出落盘 ----------
bool dumpDetectionsJson(const QString& path,
                        const QVector<CalibPoint>& pts,
                        int imgW, int imgH)
{
    QJsonArray arr;
    for (const auto& p : pts) {
        QJsonObject o;
        o["seq"]        = p.seq;
        o["tCaptureMs"] = static_cast<qint64>(p.tMs);
        o["fileName"]   = p.fileName;
        o["classId"]    = p.classId;
        o["conf"]       = p.conf;
        QJsonObject c;
        c["x"] = p.cxPx;          // 仅 y 重心入拟合,x 仅参考
        c["y"] = p.cyPx;
        o["centerPx"]   = c;
        QJsonObject b;
        b["x"] = p.bboxX; b["y"] = p.bboxY;
        b["w"] = p.bboxW; b["h"] = p.bboxH;
        o["bboxPx"]     = b;
        arr.push_back(o);
    }
    QJsonObject root;
    root["imgW"] = imgW;
    root["imgH"] = imgH;
    root["dets"] = arr;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool dumpFitCsv(const QString& path, const QVector<CalibPoint>& pts, const LinReg& fit)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream ts(&f);
    ts.setCodec("UTF-8");
    ts << "seq,t_relative_s,cy_px,cy_mm,fit_mm,residual_mm\n";
    for (const auto& p : pts) {
        const double yhat = fit.slope * p.tRelativeS + fit.intercept;
        const double res  = p.cyMm - yhat;
        ts << p.seq << ','
           << QString::number(p.tRelativeS, 'f', 6) << ','
           << QString::number(p.cyPx,        'f', 3) << ','
           << QString::number(p.cyMm,        'f', 3) << ','
           << QString::number(yhat,          'f', 3) << ','
           << QString::number(res,           'f', 3) << '\n';
    }
    return true;
}

bool dumpResultTxt(const QString& path,
                   double K, double rpmMean, const LinReg& fit,
                   double vRealMmPerMs, int nUsed,
                   int seqFrom, int seqTo,
                   qint64 tFromMs, qint64 tToMs,
                   const QString& dataDir, const QString& modelPath)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream ts(&f);
    ts.setCodec("UTF-8");
    ts << "# calibrate_rpm_to_speed result\n";
    ts << "# (《测试需求.md》§3.5)\n";
    ts << "data_dir              = " << dataDir << "\n";
    ts << "model_path            = " << modelPath << "\n";
    ts << "seq_from              = " << seqFrom << "\n";
    ts << "seq_to                = " << seqTo   << "\n";
    ts << "frames_used           = " << nUsed << "\n";
    ts << "t_window_ms           = [" << tFromMs << ", " << tToMs
       << "]   (span=" << (tToMs - tFromMs) << " ms)\n";
    ts << "rpm_mean              = " << QString::number(rpmMean,     'f', 4) << "  r/min\n";
    ts << "slope_mm_per_s        = " << QString::number(fit.slope,   'f', 6) << "\n";
    ts << "intercept_mm          = " << QString::number(fit.intercept,'f', 4) << "\n";
    ts << "fit_R2                = " << QString::number(fit.r2,      'f', 6) << "\n";
    ts << "fit_residual_max_mm   = " << QString::number(fit.residualMax,'f', 4) << "\n";
    ts << "v_real_mm_per_ms      = " << QString::number(vRealMmPerMs,'f', 6) << "\n";
    ts << "K_mm_min_per_ms_r     = " << QString::number(K,           'f', 8) << "\n";
    ts << "speed_eq              = speedMmPerMs = K * rpm\n";
    ts << "verdict               = " << (fit.r2 >= 0.95 ? "OK" : "WARN_R2_BELOW_0.95") << "\n";
    return true;
}

}  // namespace

// ============================================================================
// main
// ============================================================================
int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("calibrate_rpm_to_speed");
    pipeline::initClock();

    QCommandLineParser p;
    p.setApplicationDescription("M0.7 RPM->mm/ms 标定工具(单线程同步推理 + 最小二乘)");
    p.addHelpOption();

    QCommandLineOption optDataDir("data-dir",   "saveRawPic 目录(含 log.txt 与 img_*.jpg)", "DIR");
    QCommandLineOption optModel  ("model",      "RKNN 模型路径", "PATH");
    QCommandLineOption optSeqFrom("seq-from",   "起始 seq(默认 20)",   "N", "20");
    QCommandLineOption optSeqTo  ("seq-to",     "结束 seq(默认 70)",   "N", "70");
    QCommandLineOption optConfig ("config",     "config.ini(可选,提供 conf/nms 等)", "PATH");
    QCommandLineOption optOutput ("output",     "输出根目录(默认 offline_sim_outputs/calibration)",
                                   "DIR", "offline_sim_outputs/calibration");
    QCommandLineOption optConf   ("conf",       "覆盖 confThreshold", "F");
    QCommandLineOption optNms    ("nms",        "覆盖 nmsThreshold",  "F");
    QCommandLineOption optRealL  ("real-length-mm", "图像 x 方向物理长度(默认 1520)", "MM", "1520");
    QCommandLineOption optRealW  ("real-width-mm",  "图像 y 方向物理宽度(默认 1270)", "MM", "1270");
    QCommandLineOption optProbe  ("probe-only",
        "诊断模式:跳过 >1 物体 abort、跳过拟合,只逐帧打印 det 数 + 每个 det 的 cls/conf/bbox。"
        "用于排查 0 det / 多目标分布问题,不产出 calib_result.txt。");
    p.addOption(optDataDir); p.addOption(optModel);
    p.addOption(optSeqFrom); p.addOption(optSeqTo);
    p.addOption(optConfig);  p.addOption(optOutput);
    p.addOption(optConf);    p.addOption(optNms);
    p.addOption(optRealL);   p.addOption(optRealW);
    p.addOption(optProbe);
    p.process(app);

    const bool probeOnly = p.isSet(optProbe);

    auto fail = [](const QString& msg) -> int {
        QTextStream(stderr) << "[calibrate] FAIL: " << msg << "\n";
        return 2;
    };

    if (!p.isSet(optDataDir)) return fail("缺少 --data-dir");
    if (!p.isSet(optModel))   return fail("缺少 --model");

    const QString dataDir   = p.value(optDataDir);
    const QString modelPath = p.value(optModel);
    const int     seqFrom   = p.value(optSeqFrom).toInt();
    const int     seqTo     = p.value(optSeqTo).toInt();
    const QString outRoot   = p.value(optOutput);
    const float   realL     = p.value(optRealL).toFloat();
    const float   realW     = p.value(optRealW).toFloat();

    if (seqFrom < 0 || seqTo < seqFrom) return fail("--seq-from / --seq-to 非法");
    if (seqFrom % 2 != 0 || seqTo % 2 != 0) return fail("seq 必须是偶数(saveRawPic 仅保留偶数帧)");

    // 1. 解析 log
    const QString logPath = QDir(dataDir).filePath("log.txt");
    QString logErr;
    const QVector<offline_sim::LogEvent> events =
        offline_sim::LogParser::parseFile(logPath, &logErr);
    if (events.isEmpty()) return fail(QString("LogParser failed: %1 (%2)").arg(logErr, logPath));

    const QVector<offline_sim::LogEvent> caps = selectCaptures(events, seqFrom, seqTo);
    const int expectedFrames = (seqTo - seqFrom) / 2 + 1;
    if (caps.size() != expectedFrames) {
        return fail(QString("窗口内 Capture 事件数 %1 != 预期 %2 (seq=[%3,%4] 偶数步长 2)")
                    .arg(caps.size()).arg(expectedFrames).arg(seqFrom).arg(seqTo));
    }

    // 2. 索引文件
    const QHash<int, QString> seqToFile = indexFilesBySeq(dataDir);
    for (const auto& cap : caps) {
        if (!seqToFile.contains(cap.seq))
            return fail(QString("dataDir 内找不到 seq=%1 的 .jpg").arg(cap.seq));
    }

    // 3. 准备 RuntimeConfig
    RuntimeConfig cfg;
    if (p.isSet(optConfig)) {
        cfg = loadRuntimeConfig(p.value(optConfig));
    }
    cfg.modelPath     = modelPath;
    cfg.realLengthMm  = realL;
    cfg.realWidthMm   = realW;
    cfg.drawOverlay   = false;   // 标定不需要 overlay
    cfg.saveResult    = false;
    if (p.isSet(optConf)) cfg.confThreshold = p.value(optConf).toFloat();
    if (p.isSet(optNms))  cfg.nmsThreshold  = p.value(optNms).toFloat();

    // 4. YoloWorker(同线程,直连 signal -> lambda,同步)
    YoloWorker yolo;
    yolo.preloadModel(modelPath);
    yolo.sessionStart(cfg);

    DetectedFrame lastFrame;
    bool          haveResult = false;
    QObject::connect(&yolo, &YoloWorker::detectedFrameReady, &yolo,
        [&](const DetectedFrame& f) { lastFrame = f; haveResult = true; });

    QVector<CalibPoint> points;
    points.reserve(caps.size());
    int imgW = 0, imgH = 0;

    QTextStream out(stdout);
    out.setCodec("UTF-8");
    out << "[calibrate] 解析 log: events=" << events.size()
        << "  caps in window=" << caps.size() << "\n";

    for (const auto& cap : caps) {
        const QString filePath = seqToFile.value(cap.seq);
        QImage qimg(filePath);
        if (qimg.isNull()) return fail(QString("加载失败: %1").arg(filePath));

        haveResult = false;
        yolo.onFrame(qimg, cap.tMs, QFileInfo(filePath).fileName());
        if (!haveResult) return fail(QString("YoloWorker 未 emit detectedFrameReady seq=%1").arg(cap.seq));

        if (imgW == 0) { imgW = lastFrame.imgWidthPx; imgH = lastFrame.imgHeightPx; }

        if (probeOnly) {
            out << "  seq=" << cap.seq << "  t=" << cap.tMs
                << "  detCount=" << lastFrame.objs.size() << "\n";
            for (int oi = 0; oi < lastFrame.objs.size(); ++oi) {
                const auto& d = lastFrame.objs[oi];
                out << "    [" << oi << "] cls=" << d.classId
                    << " conf=" << QString::number(d.confidence, 'f', 4)
                    << " bbox=(" << d.bboxPx.x << "," << d.bboxPx.y
                    << "," << d.bboxPx.width << "," << d.bboxPx.height << ")"
                    << " cy_px=" << QString::number(d.centerPx.y, 'f', 1)
                    << "\n";
            }
            continue;  // probe 模式下不入 points / 不拟合
        }

        if (lastFrame.objs.size() > 1) {
            return fail(QString("seq=%1 检出 %2 个目标(>1)。请换一段更纯粹的窗口")
                        .arg(cap.seq).arg(lastFrame.objs.size()));
        }
        if (lastFrame.objs.isEmpty()) {
            out << "  seq=" << cap.seq << "  -> 0 det,跳过\n";
            continue;
        }

        const auto& d = lastFrame.objs[0];
        CalibPoint cp;
        cp.seq      = cap.seq;
        cp.tMs      = cap.tMs;
        cp.cxPx     = d.centerPx.x;
        cp.cyPx     = d.centerPx.y;
        cp.cyMm     = static_cast<double>(d.centerPx.y) * realW / std::max(1, lastFrame.imgHeightPx);
        cp.classId  = d.classId;
        cp.conf     = d.confidence;
        cp.bboxX    = d.bboxPx.x;
        cp.bboxY    = d.bboxPx.y;
        cp.bboxW    = d.bboxPx.width;
        cp.bboxH    = d.bboxPx.height;
        cp.fileName = QFileInfo(filePath).fileName();
        points.push_back(cp);

        out << "  seq=" << cap.seq
            << "  t=" << cap.tMs
            << "  cls=" << d.classId
            << "  conf=" << QString::number(d.confidence, 'f', 3)
            << "  cy_px=" << QString::number(cp.cyPx, 'f', 1)
            << "  cy_mm=" << QString::number(cp.cyMm, 'f', 1)
            << "\n";
    }

    if (probeOnly) {
        out << "[calibrate] probe-only 模式,共扫描 " << caps.size() << " 帧,不拟合,退出。\n";
        return 0;
    }

    if (points.size() < 2) return fail("有效拟合点不足 2,无法做线性回归");

    // 5. 拟合
    const qint64 t0 = points.first().tMs;
    QVector<double> xs, ys;
    xs.reserve(points.size()); ys.reserve(points.size());
    for (auto& cp : points) {
        cp.tRelativeS = (cp.tMs - t0) / 1000.0;
        xs.push_back(cp.tRelativeS);
        ys.push_back(cp.cyMm);
    }
    const LinReg fit = fitLine(xs, ys);
    const double vRealMmPerMs = fit.slope / 1000.0;

    // 6. RPM mean
    const qint64 tFromMs  = points.first().tMs;
    const qint64 tToMs    = points.last().tMs;
    const double rpmMean  = meanRpm(events, tFromMs, tToMs);
    if (!(rpmMean > 0.0))
        return fail(QString("窗口内 Velocity 事件 rpm 平均 = %1,无法标定").arg(rpmMean));

    const double K = vRealMmPerMs / rpmMean;

    // 7. 输出三份产物
    const QString runTs = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString outDir = QDir(outRoot).filePath(runTs);
    if (!QDir().mkpath(outDir)) return fail(QString("mkpath 失败: %1").arg(outDir));

    const QString detsPath   = QDir(outDir).filePath("calib_detections.json");
    const QString fitPath    = QDir(outDir).filePath("calib_fit.csv");
    const QString resultPath = QDir(outDir).filePath("calib_result.txt");

    if (!dumpDetectionsJson(detsPath, points, imgW, imgH)) return fail("写 calib_detections.json 失败");
    if (!dumpFitCsv       (fitPath,  points, fit))         return fail("写 calib_fit.csv 失败");
    if (!dumpResultTxt    (resultPath, K, rpmMean, fit, vRealMmPerMs,
                           points.size(), seqFrom, seqTo, tFromMs, tToMs,
                           dataDir, modelPath))
        return fail("写 calib_result.txt 失败");

    out << "\n[calibrate] DONE\n";
    out << "  K            = " << QString::number(K,            'f', 8) << "  mm·min/(ms·r)\n";
    out << "  v_real       = " << QString::number(vRealMmPerMs, 'f', 6) << "  mm/ms"
        << "  (≈ "             << QString::number(vRealMmPerMs * 1000, 'f', 3) << " mm/s)\n";
    out << "  rpm_mean     = " << QString::number(rpmMean,      'f', 4) << "  r/min\n";
    out << "  R²           = " << QString::number(fit.r2,       'f', 6)
        << "   residual_max = " << QString::number(fit.residualMax, 'f', 3) << " mm\n";
    out << "  frames_used  = " << points.size() << " / " << caps.size() << "\n";
    out << "  outputs in   = " << outDir << "\n";

    return (fit.r2 >= 0.95) ? 0 : 3;   // 3 = R² 不达标(产物已落,人工判断)
}
