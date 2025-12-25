#ifndef LOGGER_H
#define LOGGER_H

#include <QString>
#include <QFile>
#include <QTextStream>
#include <QMutex>
#include <QObject>

// ---- 自动为日志添加类名 ----
//
// 只能用于 QObject 派生类（窗口、控件、对象等）
// 会自动打印 [类名]
//
#define LOG_INFO(msg)  Logger::info(msg, this->metaObject()->className())
#define LOG_WARN(msg)  Logger::warn(msg, this->metaObject()->className())

// 使用LOG_ERROR的时候，必须将出错的函数名称打入logMsg中
#define LOG_ERROR(msg) Logger::error(msg, this->metaObject()->className())


class Logger
{
public:
    static Logger& instance();

    // 增加static静态类型，防止每个类用logger的时候都要new一个logger
    static void info(const QString& msg, const QString& cls);
    static void warn(const QString& msg, const QString& cls);
    static void error(const QString& msg, const QString& cls);

private:
    Logger();
    ~Logger();

    // 防止复制logger对象
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void write(const QString& level, const QString& msg);

private:
    QFile file_;
    QTextStream out_;

    // 资源所，防止同时多个类调用write
    QMutex mutex_;
};

#endif // LOGGER_H
