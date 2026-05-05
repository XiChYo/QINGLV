// sort_task_sink.cpp — 见头文件。
#include "sort_task_sink.h"

#include <QDir>
#include <QFileInfo>
#include <QtDebug>

#include "pipeline/pipeline_clock.h"

namespace offline_sim {

SortTaskSink::SortTaskSink(const QString& csvPath,
                           const RuntimeConfig& cfg,
                           QObject* parent)
    : QObject(parent), m_path(csvPath)
{
    for (const auto& cb : cfg.classButtons) {
        if (cb.classId >= 0) m_id2nameCn.insert(cb.classId, cb.name);
    }
}

SortTaskSink::~SortTaskSink() { close(); }

bool SortTaskSink::open(QString* errMsg)
{
    close();
    QFileInfo info(m_path);
    QDir dir = info.dir();
    if (!dir.exists() && !dir.mkpath(".")) {
        if (errMsg) *errMsg = QStringLiteral("SortTaskSink: 无法创建目录 %1").arg(dir.absolutePath());
        return false;
    }
    m_file = new QFile(m_path);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errMsg) *errMsg = QStringLiteral("SortTaskSink: 打开 %1 失败: %2").arg(
            m_path, m_file->errorString());
        delete m_file;
        m_file = nullptr;
        return false;
    }
    m_stream = new QTextStream(m_file);
    m_stream->setCodec("UTF-8");
    writeHeader();
    return true;
}

void SortTaskSink::close()
{
    if (m_stream) {
        m_stream->flush();
        delete m_stream;
        m_stream = nullptr;
    }
    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
}

void SortTaskSink::writeHeader()
{
    if (!m_stream) return;
    *m_stream << "nowMs,trackId,finalClassId,classNameCn,tCaptureMs,"
              << "bbox_x_belt,bbox_y_belt,bbox_w_belt,bbox_h_belt,"
              << "maskAreaPxBelt,currentSpeedMmPerMs\n";
    m_stream->flush();
}

QString SortTaskSink::classNameOf(int classId) const
{
    const auto it = m_id2nameCn.find(classId);
    if (it == m_id2nameCn.end()) {
        return QStringLiteral("cls_%1").arg(classId);
    }
    return it.value();
}

void SortTaskSink::onSortTask(const SortTask& t)
{
    if (!m_stream) return;
    const qint64 now = pipeline::nowMs();
    int areaPx = 0;
    if (!t.maskBeltRaster.empty()) {
        areaPx = cv::countNonZero(t.maskBeltRaster);
    }
    *m_stream << now
              << ',' << t.trackId
              << ',' << t.finalClassId
              << ',' << classNameOf(t.finalClassId)
              << ',' << t.tCaptureMs
              << ',' << t.bboxBeltRasterPx.x
              << ',' << t.bboxBeltRasterPx.y
              << ',' << t.bboxBeltRasterPx.width
              << ',' << t.bboxBeltRasterPx.height
              << ',' << areaPx
              << ',' << QString::number(t.currentSpeedMmPerMs, 'f', 4)
              << '\n';
    m_stream->flush();
    ++m_rows;
}

}  // namespace offline_sim
