#include <QtTest/QtTest>
#include <QThread>
#include "pipeline_clock.h"

class PipelineClockTest : public QObject
{
    Q_OBJECT
private slots:
    void initClock_isIdempotent();
    void nowMs_isMonotonic();
    void nowMs_advancesWithSleep();
};

void PipelineClockTest::initClock_isIdempotent()
{
    // 连续 init 多次不应使时钟复位(即第二次 init 不清零)。
    pipeline::initClock();
    const qint64 a = pipeline::nowMs();
    pipeline::initClock();
    pipeline::initClock();
    const qint64 b = pipeline::nowMs();
    QVERIFY2(b >= a, qPrintable(QString("b=%1 a=%2").arg(b).arg(a)));
}

void PipelineClockTest::nowMs_isMonotonic()
{
    pipeline::initClock();
    qint64 last = pipeline::nowMs();
    for (int i = 0; i < 1000; ++i) {
        const qint64 cur = pipeline::nowMs();
        QVERIFY2(cur >= last, "nowMs must be monotonic");
        last = cur;
    }
}

void PipelineClockTest::nowMs_advancesWithSleep()
{
    pipeline::initClock();
    const qint64 t0 = pipeline::nowMs();
    QThread::msleep(30);
    const qint64 t1 = pipeline::nowMs();
    // 实测 sleep 精度 +/- 5ms,这里放宽容忍到 20ms 下限以避免 jitter 假阳性。
    QVERIFY2(t1 - t0 >= 20, qPrintable(QString("delta=%1").arg(t1 - t0)));
}

QObject* makePipelineClockTest() { return new PipelineClockTest; }

#include "test_pipeline_clock.moc"
