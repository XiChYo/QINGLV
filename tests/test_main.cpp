// 测试汇总入口。每个 Test 类提供自己的注册函数,避免一个 TU 一个 main。
// 使用 QTest::qExec 依次运行;任一失败则返回非零。

#include <QCoreApplication>
#include <QtTest/QtTest>

#include "pipeline_clock.h"
#include "pipeline_types.h"

// 前向声明各测试类。为避免互相包含,各 test_*.cpp 里声明 getter 函数。
QObject* makePipelineClockTest();
QObject* makeRuntimeConfigTest();
QObject* makePostprocessExTest();
QObject* makeTrackerWorkerTest();
QObject* makeDispatcherTest();

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // 保证测试用例也能取到 pipeline::nowMs()。
    pipeline::initClock();

    // 需要的 QMetaType 也注册一下(Tracker/Dispatcher 若内部 emit 信号并 QueuedConnection 时会用到)。
    qRegisterMetaType<DetectedObject>("DetectedObject");
    qRegisterMetaType<DetectedFrame>("DetectedFrame");
    qRegisterMetaType<TrackedObject>("TrackedObject");
    qRegisterMetaType<DispatchedGhost>("DispatchedGhost");
    qRegisterMetaType<SortTask>("SortTask");
    qRegisterMetaType<SpeedSample>("SpeedSample");
    qRegisterMetaType<ValvePulse>("ValvePulse");
    qRegisterMetaType<QVector<ValvePulse>>("QVector<ValvePulse>");
    qRegisterMetaType<RuntimeConfig>("RuntimeConfig");

    int status = 0;
    auto runOne = [&](QObject* t) {
        status |= QTest::qExec(t, argc, argv);
        delete t;
    };
    runOne(makePipelineClockTest());
    runOne(makeRuntimeConfigTest());
    runOne(makePostprocessExTest());
    runOne(makeTrackerWorkerTest());
    runOne(makeDispatcherTest());
    return status;
}
