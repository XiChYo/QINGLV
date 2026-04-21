#ifndef PIPELINE_CLOCK_H
#define PIPELINE_CLOCK_H

// ============================================================================
// 进程级单调时钟:所有模块通过 pipeline::nowMs() 获取毫秒级时间戳,
// 统一作为 tCapture / tOpen / tClose 等字段的参考系。详见 docs/design.md §0。
// ============================================================================

#include <QElapsedTimer>

namespace pipeline {

// 在 main() 入口尽早调用一次,确保后续 nowMs() 返回的是"启动后的单调 ms"。
void initClock();

// 返回自 initClock() 以来的毫秒数。线程安全(QElapsedTimer 方法本身无共享写)。
qint64 nowMs();

}  // namespace pipeline

#endif // PIPELINE_CLOCK_H
