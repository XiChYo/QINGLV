#include "logger.h"
#include <QDateTime>

Logger& Logger::instance()
{
    static Logger inst;
    return inst;
}

// 将所有信息输出输出到log.txt，不需要根据运行日期的不同而生成不同日期的txt文件
// 直接把所有文件输出到log.txt即可
// 输出的格式为：时间 [信息类型] [信息产生的类名] 信息
Logger::Logger()
{
    file_.setFileName("log.txt");
    file_.open(QIODevice::Append | QIODevice::Text);
    out_.setDevice(&file_);
}

Logger::~Logger()
{
    file_.close();
}

void Logger::info(const QString& msg, const QString& cls)
{
    instance().write("INFO", QString("[%1] %2").arg(cls, msg));
}

void Logger::warn(const QString& msg, const QString& cls)
{
    instance().write("WARN", QString("[%1] %2").arg(cls, msg));
}

void Logger::error(const QString& msg, const QString& cls)
{
    instance().write("ERROR", QString("[%1] %2").arg(cls, msg));
}

void Logger::write(const QString& level, const QString& msg)
{
    QMutexLocker locker(&mutex_);

    QString time = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    out_ << time << " [" << level << "] " << msg << "\n";
    out_.flush();
}
