#ifndef OFFLINE_SIM_SORT_TASK_SINK_H
#define OFFLINE_SIM_SORT_TASK_SINK_H

// ============================================================================
// SortTaskSink:把 TrackerWorker::sortTaskReady(SortTask) 信号原样落到 CSV(M3)。
// 与 ValveSink 一同提供"业务正确性"主入口:
//   - sort_tasks.csv 一行 = 一次 SortTask 触发
//   - 配合 valve_commands.csv 的 trackId join,可以验证 AC4/AC5/AC8 等
//
// CSV 列(每条 SortTask 一行):
//   nowMs, trackId, finalClassId, classNameCn, tCaptureMs,
//   bbox_x_belt, bbox_y_belt, bbox_w_belt, bbox_h_belt,
//   maskAreaPxBelt, currentSpeedMmPerMs
//   - bbox_*_belt:皮带固定坐标系栅格图(单位 px,1 px = cfg.maskRasterMmPerPx mm)
//   - maskAreaPxBelt:maskBeltRaster 非零像素数(用于和 latestAreaMm2 互验)
//   - classNameCn:从 cfg.classButtons 反查;查不到回退 "cls_<id>"
//
// 单线程 OK;close() 必须显式调用以 flush。
// ============================================================================

#include <QFile>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTextStream>

#include "pipeline/pipeline_types.h"
#include "config/runtime_config.h"

namespace offline_sim {

class SortTaskSink : public QObject
{
    Q_OBJECT
public:
    explicit SortTaskSink(const QString& csvPath,
                          const RuntimeConfig& cfg,
                          QObject* parent = nullptr);
    ~SortTaskSink() override;

    bool open(QString* errMsg = nullptr);
    void close();

    int rowCount() const { return m_rows; }

public slots:
    void onSortTask(const SortTask& task);

private:
    void writeHeader();
    QString classNameOf(int classId) const;

    QString               m_path;
    QHash<int, QString>   m_id2nameCn;
    QFile*                m_file   = nullptr;
    QTextStream*          m_stream = nullptr;
    int                   m_rows   = 0;
};

}  // namespace offline_sim

#endif  // OFFLINE_SIM_SORT_TASK_SINK_H
