#include "pipeline_clock.h"

#include <QElapsedTimer>

namespace pipeline {
namespace {

// 进程级单例。QElapsedTimer 启动后,多线程只读 msecsSinceReference()/elapsed(),
// Qt 文档明确这是 safe 的(单调时钟,无可变状态写入)。
QElapsedTimer& sharedTimer()
{
    static QElapsedTimer t;
    return t;
}

}  // namespace

void initClock()
{
    QElapsedTimer& t = sharedTimer();
    if (!t.isValid()) {
        t.start();
    }
}

qint64 nowMs()
{
    QElapsedTimer& t = sharedTimer();
    if (!t.isValid()) {
        t.start();
    }
    return t.elapsed();
}

}  // namespace pipeline
