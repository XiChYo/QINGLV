// valve_sink.cpp — 见头文件。
#include "valve_sink.h"

#include <QFileInfo>
#include <QDir>
#include <QtDebug>

#include "pipeline/pipeline_clock.h"

namespace offline_sim {

ValveSink::ValveSink(const QString& csvPath, QObject* parent)
    : QObject(parent), m_path(csvPath) {}

ValveSink::~ValveSink() { close(); }

bool ValveSink::open(QString* errMsg)
{
    close();
    QFileInfo info(m_path);
    QDir dir = info.dir();
    if (!dir.exists() && !dir.mkpath(".")) {
        if (errMsg) *errMsg = QStringLiteral("ValveSink: 无法创建目录 %1").arg(dir.absolutePath());
        return false;
    }

    m_file = new QFile(m_path);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errMsg) *errMsg = QStringLiteral("ValveSink: 打开 %1 失败: %2").arg(
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

void ValveSink::close()
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

void ValveSink::writeHeader()
{
    if (!m_stream) return;
    *m_stream << "nowMs,event,trackId,boardId,channelMaskHex,tOpenMs,tCloseMs\n";
    m_stream->flush();
}

void ValveSink::onEnqueue(const QVector<ValvePulse>& pulses, int trackId)
{
    if (!m_stream) return;
    const qint64 now = pipeline::nowMs();
    for (const auto& p : pulses) {
        *m_stream << now << ",enqueue," << trackId
                  << ',' << static_cast<int>(p.boardId)
                  << ",0x" << QString::number(p.channelMask, 16).rightJustified(4, '0')
                  << ',' << p.tOpenMs
                  << ',' << p.tCloseMs
                  << '\n';
        ++m_enqueueRows;
    }
    m_stream->flush();
}

void ValveSink::onCancel(int trackId)
{
    if (!m_stream) return;
    const qint64 now = pipeline::nowMs();
    *m_stream << now << ",cancel," << trackId << ",,,,\n";
    m_stream->flush();
    ++m_cancelRows;
}

}  // namespace offline_sim
