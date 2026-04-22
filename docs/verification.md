# Phase 5 重构验证清单

> 范围:`refactor/phase5-pipeline` 上的 5 个 commit(PR1–PR5)。
> 目标:没有本地完整编译环境的情况下,给开发板侧一份**逐 commit、逐模块的静态检查 + 动态验证**脚本。
> 全部手动勾选;每条给出:**改了什么 / 验证入口 / 怎么验 / 通过标准**。

---

## 0. 先决条件 & 一次性步骤

| 步骤 | 目的 | 命令 | 通过标准 |
|---|---|---|---|
| 0.1 切到分支 | 保持代码与本清单对齐 | `git fetch && git checkout refactor/phase5-pipeline && git log --oneline -n 6` | 最近 5 条为 PR1~PR5(`c5553b9 2313e5e d27fafb 80be272 111de5d`),顺序正确 |
| 0.2 目标机编译 | 作为后续上板验证的前置 | `qmake -qt=qt5 guangxuan.pro && make -j$(nproc)` | 无 error。仅 warning 可先记录 |
| 0.3 测试工程编译 | 不依赖 RKNN/MVS,可在工作站跑 | `cd tests && qmake -qt=qt5 tests.pro && make && ./pipeline_tests` | 全部用例 PASS(含 `pipeline_tests: 0 failed`) |
| 0.4 `config.ini` 准备 | 运行期依赖 | 对比 `docs/design.md §5` 补齐 `[camera] [model] [yolo] [pipeline] [belt] [sorter] [valve] [arm_stub] [persistence]` | 所有字段都有值;未覆盖的字段在日志里打了默认值 |

> `tests/pipeline_tests` **必须在 PR5 顶上跑通**,它覆盖了纯逻辑模块(时钟、配置、后处理、Tracker、Dispatcher 的核心算法)。PR2–PR4 的 checklist 里凡是"单测覆盖"那一列打勾的点,只靠 `pipeline_tests` 就能拦住回归。

---

## 1. PR1 — 基础设施 & 死代码清理(`c5553b9`)

### 1.1 改了什么

- 删除:`ConveyorTracker.{h,cpp}` / `caldistance.{h,cpp}` / `yolothread.{h,cpp}` / `yoloresulttypes.h`。
- 新增:
  - `pipeline_types.h`:`DetectedObject / DetectedFrame / TrackedObject / DispatchedGhost / SortTask / SpeedSample / ValvePulse`,全部 `Q_DECLARE_METATYPE`。
  - `pipeline_clock.{h,cpp}`:`pipeline::initClock()` + `pipeline::nowMs()`(基于 `QElapsedTimer` 的进程级单调时钟)。
  - `runtime_config.{h,cpp}`:从 `config.ini` 一次性加载的 `RuntimeConfig`。
- 修改:
  - `main.cpp`:`pipeline::initClock()`,所有 pipeline 类型 `qRegisterMetaType`。
  - `mainwindow.h/.cpp`:删掉 caldistance/ConveyorTracker 引用、旧的机械臂标定槽(一大块 `on_u*/d*_clicked`/`getAndsendA`/`doTask`)、重复的 tab QSS → `kTabBtnActiveQss`/`kTabBtnIdleQss`。`on_reset_clicked` / `on_run_clicked` / `on_powerbutton_clicked` 退化成占位。
  - `.pro`:删旧源码,加 `pipeline_clock` / `runtime_config`。

### 1.2 Checklist

| ID | 检查点 | 验证方法 | 通过标准 |
|---|---|---|---|
| PR1-01 | 死文件已从 git 消失 | `git show --stat c5553b9 \| grep delete` | 列表包含 `ConveyorTracker.h/.cpp`、`caldistance.h/.cpp`、`yolothread.h/.cpp`、`yoloresulttypes.h`,共 7 个 |
| PR1-02 | 生产代码不再 include 已删文件 | `rg -n "caldistance\|ConveyorTracker\|yolothread\|yoloresulttypes" -g'!docs/*' -g'!tests/*'` | 无匹配 |
| PR1-03 | `pipeline::nowMs()` 单调 | `cd tests && ./pipeline_tests -select PipelineClockTest` | `PipelineClockTest::nowMs_isMonotonic` / `nowMs_advancesWithSleep` / `initClock_isIdempotent` 均 PASS |
| PR1-04 | `Q_DECLARE_METATYPE` 全齐 | `rg -n "Q_DECLARE_METATYPE" pipeline_types.h` | 至少 8 项:`DetectedObject/DetectedFrame/TrackedObject/DispatchedGhost/SortTask/SpeedSample/ValvePulse/QVector<ValvePulse>` |
| PR1-05 | `main.cpp` 注册顺序 | 打开 `main.cpp`,确认 `initClock()` 在任何 `qRegisterMetaType` 之前,且所有 pipeline 类型已注册 | 代码审查通过 |
| PR1-06 | `RuntimeConfig` 默认值 | 在没有 ini 的临时目录跑 `pipeline_tests -select RuntimeConfigTest::loadRuntimeConfig_missingFileFallsBackToDefaults` | PASS;读到的 cameraIp/softFps/valveTotalChannels 等于 `runtime_config.h` 里的默认 |
| PR1-07 | `RuntimeConfig` 全字段往返 | `pipeline_tests -select RuntimeConfigTest::loadRuntimeConfig_populatesAllSections` | PASS |
| PR1-08 | `sorter.mode` 大小写/兜底 | `pipeline_tests -select RuntimeConfigTest::loadRuntimeConfig_armSorterModeParsed` | PASS(`arm / ARM / other` 行为都对) |
| PR1-09 | `class_btn` 顺序与映射 | `pipeline_tests -select RuntimeConfigTest::loadRuntimeConfig_classButtonsParsedInOrder` | PASS |
| PR1-10 | Tab QSS 去重 | `rg -n "kTabBtnActiveQss\\|kTabBtnIdleQss" mainwindow.cpp` | 每个常量至少被 2 处引用,不再有 3 份 inline QSS 字符串 |
| PR1-11 | `mainwindow` 残留 std::thread 无漏删(下一 PR 最终清) | `rg -n "std::thread\\|m_running" mainwindow.h mainwindow.cpp` | PR5 commit 后无匹配。PR1 当时可允许存在,PR5 时必须清掉 |

### 1.3 上板联调点

| 动作 | 通过标准 |
|---|---|
| 启动软件,不点 Run | 无崩溃;`log.txt` 出现 `AI Optical Sorter Software Starting`;不会自动打开相机 |
| 点 Run(PR5 引入)后再回溯查 | 查 `log.txt` 能看到 `Session started. enabledClasses=N`(具体 N 由 UI 勾选数目决定) |

---

## 2. PR2 — CameraWorker + YoloWorker(`2313e5e`)

### 2.1 改了什么

- 新增:
  - `camera_worker.{h,cpp}`:`QObject + QTimer` 驱动,软件节流按 `soft_fps`,采图瞬间 `pipeline::nowMs()` 打 `tCaptureMs`,`in-flight` 计数做反压(上限 1)。
  - `yolo_session.{h,cpp}`:RKNN 会话的纯函数接口 `yolo_session_init / release / infer`,与 QObject 解耦。
  - `yolo_worker.{h,cpp}`:持有 `YoloSession`,从 `RuntimeConfig` 注入阈值,输出 `DetectedFrame` + 可选 overlay 图。
  - `postprocess_ex.{h,cpp}`:把 `conf_threshold/nms_threshold` 从 `#define` 提到 `PostProcessParams`;`draw_results_ex` 带 `label_from_id` 回调。
- 删除:`camerathread.{h,cpp}` / `yolorecognition.{h,cpp}`。
- 改:`mainwindow.h/.cpp` 改挂 `CameraWorker`/`YoloWorker` 到独立 QThread;`pipeline_types.h` 给 `DetectedObject` 补 `centerPx`;`RuntimeConfig` 补 `modelTopkClassCount` / `drawOverlay`。

### 2.2 Checklist

| ID | 检查点 | 验证方法 | 通过标准 |
|---|---|---|---|
| PR2-01 | 旧 camera/yolo 文件确已移除 | `git show --stat 2313e5e \| rg 'camerathread\|yolorecognition'` | 4 行均为 `delete mode` |
| PR2-02 | `CameraWorker` 软节流间隔正确 | 代码审查 `camera_worker.cpp::sessionStart`:`m_intervalMs = 1000 / soft_fps`,`cfg.softFps <= 0` 兜底为 2 | `soft_fps=2 → 500ms`;`soft_fps=0 → 500ms` |
| PR2-03 | `CameraWorker` 时间戳来源 | 审 `grabOneFrame`,`outCaptureMs = pipeline::nowMs()` 在 `MV_CC_GetImageBuffer` 返回后立即赋值 | 代码审查通过 |
| PR2-04 | `CameraWorker` 反压上限 | 审 `onTick`:`if (m_inFlight.load() >= kInFlightMax) return;`,`kInFlightMax == 1`,`onFrameConsumed` 做 `fetch_sub` | 代码审查通过 |
| PR2-05 | `YoloSession` 一次初始化 | 审 `yolo_session.cpp::yolo_session_init`:`rknn_init`/`rknn_query` 只在 init 中调 | 每个 session 只创建一次 ctx,不在 `infer` 里重建 |
| PR2-06 | `YoloWorker` 阈值从 cfg 注入 | 审 `yolo_worker.cpp::sessionStart`:`m_confThreshold = cfg.confThreshold` 等 | 审查通过 |
| PR2-07 | `YoloWorker::onFrame` 不丢 `frameConsumed` | 审所有早返回路径(session invalid、img null、orig empty) | 每条路径都 `emit frameConsumed()` |
| PR2-08 | `DetectedFrame` 尺寸字段填充 | 审 `onFrame`:`frame.imgWidthPx/imgHeightPx = orig.cols/rows` | 审查通过 |
| PR2-09 | 后处理阈值生效 | `pipeline_tests -select PostprocessExTest::postProcess_filtersByConfThreshold` | PASS,低于 `conf` 的 det 被丢 |
| PR2-10 | 后处理 NMS 生效 | `pipeline_tests -select PostprocessExTest::postProcess_filtersByNmsThreshold` | PASS,重合 bbox 只保留高分 |
| PR2-11 | 后处理 proto_c 兜底 | `pipeline_tests -select PostprocessExTest::postProcess_returnsEmptyForMismatchedProtoDim` | PASS;stderr 打印提示,out 为空 |
| PR2-12 | 线程绑定正确 | 审 `mainwindow.cpp` 构造函数:`moveToThread(m_cameraThread)` / `m_yoloThread`,信号全部 `Qt::QueuedConnection` | 审查通过 |
| PR2-13 | `pipeline_types.h::DetectedObject` 含 centerPx | `rg -n "centerPx" pipeline_types.h yolo_worker.cpp` | `pipeline_types.h` 有声明,`yolo_worker.cpp` 里有 `segCenterPx` 使用 |

### 2.3 上板联调点

| 动作 | 通过标准 |
|---|---|
| 点 Run;相机连好 | `log.txt` 出现 `CameraWorker started ip=... soft_fps=2 (tick=500ms)` 和 `YoloWorker started model=... conf=... nms=...` 各一行 |
| 取消勾选 `draw_overlay` 重启 | UI 不再显示 mask/bbox,仅显示 raw RGB,帧率无变化 |
| soft_fps 改到 5、再改到 1 | `log.txt` 起始行的 tick ms 分别为 200 / 1000;肉眼观察 UI 刷新频率变化 |
| 对着空场景跑 30 秒 | `log.txt` 每秒约 `Yolo: 0 objs @t_capture=...` 条数 ≈ soft_fps;不崩 |
| 对着 1 个真实分选物持续跑 | `Yolo: N objs`,N ≥ 1;`t_infer_done - t_capture` 一般在 50-150ms;如 > 500ms,记为性能告警 |

---

## 3. PR3 — BoardWorker 重构(`d27fafb`)

### 3.1 改了什么

- `boardcontrol.h/.cpp`(类名保留,功能等同 BoardWorker):
  - 新增 `onSessionStart / onSessionStop` 接入 `RuntimeConfig`。
  - 新增 `m_valveTick` (缺省 5ms) + `onValveTick`:扫 `m_pending`,按 `tOpenMs/tCloseMs` 发开/关阀帧。
  - 新增 `m_encoderTick` (缺省 500ms) + `onEncoderTick → requestEncoderSpeed`:解析后 `emit speedSample(SpeedSample)`。
  - 新增 `onEnqueuePulses / onCancelPulses`。cancel 保留已开未关的阀,避免漏关。
  - `writeFrame` 单一 `m_busMutex`;粒度=单帧;不再有 `busy / speedRead / m_valveBusySet / m_writeMutex / m_serialMutex`。
  - legacy `singleBoardControl / batchBoardControl` 把 `QThread::msleep(50)` 换成 `QTimer::singleShot`,共用 `m_busMutex`。
  - `onSessionStop` 对所有"已 open 未 close"阀强制发关阀帧。
- `mainwindow.h/.cpp`:
  - 独立 `m_boardThread`,构造时 `moveToThread` + `initSerial on started`。
  - 加 `onBoardSpeedSample(const SpeedSample&)`,PR3 只做 UI 占位,PR4 真正接 Tracker/Dispatcher。

### 3.2 Checklist

| ID | 检查点 | 验证方法 | 通过标准 |
|---|---|---|---|
| PR3-01 | `QMutex` 只剩一把 `m_busMutex` | `rg -n "QMutex" boardcontrol.h` | 只有 `m_busMutex`;旧的 `m_writeMutex/m_serialMutex/speedRead` 均不出现 |
| PR3-02 | 无 `QThread::msleep(50)` 阻塞调用 | `rg -n "QThread::msleep" boardcontrol.cpp` | 结果为空 |
| PR3-03 | QTimer 替代阻塞 | `rg -n "QTimer::singleShot\|QTimer\\s*\\*\\s*m_valveTick\|m_encoderTick" boardcontrol.cpp boardcontrol.h` | 至少 3 处命中(2 个 Timer 成员 + legacy `singleShot`) |
| PR3-04 | `writeFrame` 锁粒度 | 审 `writeFrame`:一次 `QMutexLocker(&m_busMutex)`,然后 write + tcdrain,解锁。`readFrame` 不持有 busMutex | 代码审查通过 |
| PR3-05 | 编码器轮询间隔生效 | `onSessionStart` 读 `cfg.encoderRequestIntervalMs`,`m_encoderTick->start(...)` | 代码审查通过 |
| PR3-06 | `SpeedSample.valid=true` 只在解析成功时 | 审 `requestEncoderSpeed`:读不到帧直接 return;只有 `rx.size() >= 8` 才 emit | 代码审查通过 |
| PR3-07 | `onSessionStop` 强制关阀 | 审 `onSessionStop`:遍历 `m_pending` 找到 `sentOpen && !sentClose` 发 `buildPulseCloseFrame` | 代码审查通过 |
| PR3-08 | `onCancelPulses` 保留已开未关 | 审函数:`keep` 里只保留 `sentOpen && !sentClose` | 代码审查通过 |
| PR3-09 | MainWindow 线程绑定 | 审 `m_board->moveToThread(m_boardThread)`,构造时 `m_boardThread->start()` | 代码审查通过 |
| PR3-10 | `speedSample` 信号 Qt::QueuedConnection | 审 `mainwindow.cpp`:`connect(m_board, &boardControl::speedSample, this, &MainWindow::onBoardSpeedSample)` 使用默认(自动)连接,从 `m_boardThread` 跨线程自动变 Queued | 代码审查通过 |

### 3.3 上板联调点(串口真机才能跑)

| 动作 | 通过标准 |
|---|---|
| 开机不接 USB,观察 | `log.txt` 有 `boardControl: open serial failed for ttyUSB0/ttyUSB1`,程序不崩 |
| 接 USB 后点 Run | `log.txt` 有 `boardControl sessionStart tick=5ms enc=500ms raw2mPerMin=...` |
| 观察编码器数据 | 每 ~500ms 一条 `encoderSpeedReceived` 原始帧;UI 的 `speed` 标签从 0 变为合理值 |
| 手动停止(on_reset_clicked,PR5 接通后) | `log.txt` 有 `Session stopped.` 且后续无新的开阀帧打到 log |
| 并发单元模拟:Dispatcher 同时下发 5 条 pulse,跨越 500ms | 所有 pulse 的开/关帧按顺序送出,无丢失;示波器/协议分析仪上帧间无 1 ms 以上空档异常 |

---

## 4. PR4 — TrackerWorker + Dispatcher + 连线(`80be272`)

### 4.1 改了什么

- 新增 `tracker_worker.{h,cpp}`:首帧丢弃、`rasterizeToBelt` 像素→belt mm 栅格、`maskIoU`、贪婪关联、`DispatchedGhost` 抑制、达到 `missFramesX` 或 `updateFramesY` 触发 `sortTaskReady`。
- 新增 `dispatcher.{h,cpp}`:
  - valve 模式 `computePulses` 含 head_skip、滚动投影、per-board 合并(`min_cmd_interval_ms`)。
  - arm 模式 `dispatchArmStub` 写 `arm_stub_csv` 并 `emit armStubDispatched`。
  - `onSpeedSample` 在偏差超 `speed_recalc_threshold_pct` 时 cancel + 重算全部 pending。
  - `onSessionStop` cancel 所有 pending 并关 csv。
- `mainwindow.h/.cpp`:
  - 加 `m_trackerThread`/`m_dispatcherThread` + worker 指针。
  - 连线:`Yolo::detectedFrameReady → Tracker::onFrameInferred` / `Tracker::sortTaskReady → Dispatcher::onSortTask` / `Board::speedSample → Tracker+Dispatcher::onSpeedSample` / `Dispatcher::enqueuePulses|cancelPulses → Board::onEnqueuePulses|onCancelPulses` / `Dispatcher::armStubDispatched → UI 日志`。
  - Start 顺序:Board → Tracker → Dispatcher → Yolo → Camera。
  - 析构按反向 BlockingQueuedConnection 依次 sessionStop(PR5 又进一步收拢到 `stopSession()`)。
- `.gitignore`:新增屏蔽 `.idea/`、`*.user`、`build-*`。

### 4.2 Checklist

| ID | 检查点 | 验证方法 | 通过标准 |
|---|---|---|---|
| PR4-01 | Tracker 首帧丢弃 | `pipeline_tests -select TrackerWorkerTest::firstFrame_isDiscarded` | PASS;`m_active.size()==0`,`m_firstFrame==false`,`m_tOriginMs>0` |
| PR4-02 | Tracker 新物体新建 track | `pipeline_tests -select TrackerWorkerTest::newDet_createsActiveTrack` | PASS;`m_active.size()==1`,`updateCount==1` |
| PR4-03 | Tracker 达到 Y 触发分拣 | `pipeline_tests -select TrackerWorkerTest::repeatedDet_increasesUpdateCountAndTriggersSortTask_whenClassEnabled` | PASS;`sortTaskReady` 发 1 次;`SortTask.finalClassId==1`;`m_active.size()==0`,`m_ghosts.size()==1` |
| PR4-04 | Tracker 未启用类别不触发 | `pipeline_tests -select TrackerWorkerTest::repeatedDet_doesNotTrigger_whenClassDisabled` | PASS;`sortTaskReady` 发 0 次 |
| PR4-05 | Tracker miss≥X 触发 | `pipeline_tests -select TrackerWorkerTest::missCount_reachesThresholdAndTriggers_ifUpdateAlreadyEnough` | PASS;`miss=2` 时触发 |
| PR4-06 | Tracker ghost 抑制重复分拣 | `pipeline_tests -select TrackerWorkerTest::ghostSuppression_preventsResortOfSameObject` | PASS;再次出现同位置物体不新增 active,不 emit 第二次 |
| PR4-07 | Tracker maskIoU 边界 | `pipeline_tests -select TrackerWorkerTest::maskIoU_zeroOnDisjointBboxes`,`maskIoU_oneOnIdentical` | PASS,分别返回 0 / 1 |
| PR4-08 | Tracker 栅格化公式 | `pipeline_tests -select TrackerWorkerTest::rasterizeToBelt_scalesPixelToMmCorrectly` | PASS;bbox 与 area 精确吻合 |
| PR4-09 | Dispatcher valve 可达物体出脉冲 | `pipeline_tests -select DispatcherTest::valveMode_emitsEnqueuePulses_forReachableObject` | PASS;`enqueuePulses` 发 1 次;`boardId==1`,`channelMask & 0x1`,`tOpenMs >= tCaptureMs`,`tCloseMs > tOpenMs` |
| PR4-10 | Dispatcher valve 越线物体空脉冲 | `pipeline_tests -select DispatcherTest::valveMode_returnsEmpty_whenObjectAlreadyPastValveLine` | PASS;`enqueuePulses` 0 次,`warning` 1 次 |
| PR4-11 | Dispatcher `onSessionStop` 级联 cancel | `pipeline_tests -select DispatcherTest::valveMode_cancelPulses_onSessionStop` | PASS;所有 pending 被 cancel |
| PR4-12 | Dispatcher arm 模式写 CSV | `pipeline_tests -select DispatcherTest::armMode_writesCsvAndEmitsArmStubDispatched` | PASS;`arm_stub.csv` 文件含表头 + 一行;`armA_x`/`y` 与手算一致 |
| PR4-13 | Dispatcher 速度阈值上触发重算 | `pipeline_tests -select DispatcherTest::speedSample_triggersRecomputation_whenChangeExceedsThreshold` | PASS;超过 20% cancel+enqueue 各一次 |
| PR4-14 | Dispatcher 速度小波动不重算 | `pipeline_tests -select DispatcherTest::speedSample_noRecomputation_whenChangeWithinThreshold` | PASS;10% 变化不触发 |
| PR4-15 | MainWindow 连线完整 | `rg -n "detectedFrameReady\|sortTaskReady\|speedSample\|enqueuePulses\|cancelPulses\|armStubDispatched" mainwindow.cpp` | 至少 8 条 `connect(...)`,源/目标与设计一致 |
| PR4-16 | MainWindow::onSortTask 字段访问 | 看 `mainwindow.cpp::onSortTask`:用 `finalClassId / bboxBeltRasterPx / currentSpeedMmPerMs` | 不访问 `classId/target/areaMm2` 等不存在字段。**本次总结后已修复,编译期强验证** |
| PR4-17 | `.gitignore` 生效 | `git status` | `.idea/` 不再出现在 untracked 列表 |

### 4.3 上板联调点

| 动作 | 通过标准 |
|---|---|
| 启动、点 Run、空场景 30s | 每秒约 soft_fps 条 `Yolo: 0 objs` 日志;无 `SortTask` 日志;`SpeedSample` 每 500ms 一条;不崩 |
| 一个启用类别的物体单次过带 | 连续 `Yolo: 1 objs` 若干条 → 最后一条 `SortTask: trackId=1 cls=X bbox=(...) speed=0.5mm/ms` → 再出现阀门动作(示波器)或 `ArmStub dispatched` 一条 |
| 同一物体过带,检测不连续(模拟遮挡) | 最终 `miss >= X` 触发 SortTask;只触发一次(ghost 抑制) |
| 两个物体并排同时过阀线 | 日志里出现 2 条不同 trackId 的 SortTask,顺序可以不同,但总数匹配 |
| 未勾选任何类别,点 Run,放物体 | 有 `Yolo: N objs`,**无 SortTask**;无阀动作(或 armStubDispatched) |
| 中途加减皮带速度超 ±20% | 看到 `Dispatcher` 发 `cancelPulses` + `enqueuePulses` 重算日志,阀门按新速度落点 |
| 切到 arm 模式重启 | `/data/logs/arm_stub.csv` 自增;停机后文件含表头 + 若干行 |

---

## 5. PR5 — Session 状态机 + 残留清理(`111de5d`)

### 5.1 改了什么

- `MainWindow` 新增 `SessionState::{Idle, Running, Stopping}` + `startSession()` / `stopSession()` / `refreshEnabledClassIdsFromUi()`。
- `on_run_clicked` / `on_reset_clicked` 不再占位,直接走状态机;Run/Running、Stopping 幂等。
- 构造函数**不再**一次性 sessionStart;必须点 Run。
- 析构统一走 `stopSession()`,逻辑不再两份。
- 删 `std::thread m_thread` / `bool m_running` + `<thread>` 头。
- 删 `valvecmd.h`:`Task / ValveCmd` 无活跃消费者;`main.cpp` 不再注册,`mainwindow.h` 不再 include,`.pro` 中移除。

### 5.2 Checklist

| ID | 检查点 | 验证方法 | 通过标准 |
|---|---|---|---|
| PR5-01 | 状态机枚举存在 | `rg -n "SessionState::Idle\|SessionState::Running\|SessionState::Stopping" mainwindow.h mainwindow.cpp` | 3 个状态都被使用 |
| PR5-02 | `startSession` 启动顺序 | 审 `mainwindow.cpp::startSession`:`Board → Tracker → Dispatcher → Yolo → Camera`,均 `QueuedConnection`,用 `QMetaObject::invokeMethod` | 代码审查通过 |
| PR5-03 | `stopSession` 反向顺序 + Blocking | 审:`Camera → Yolo → Tracker → Dispatcher → Board`,`BlockingQueuedConnection` | 代码审查通过 |
| PR5-04 | 析构收拢到 `stopSession` | 审 `~MainWindow`:`if (m_sessionState != SessionState::Idle) stopSession();` | 代码审查通过 |
| PR5-05 | Run 幂等 | `on_run_clicked` 在 Running/Stopping 状态下只打日志不再启 | 代码审查通过 |
| PR5-06 | `refreshEnabledClassIdsFromUi` 按 objectName / 文本双路匹配 | 审:`if (btn->objectName() == cb.name \|\| btn->text() == cb.name)` | 代码审查通过 |
| PR5-07 | `std::thread` 清理干净 | `rg -n "std::thread\|m_running\|#include <thread>" mainwindow.h mainwindow.cpp` | 结果为空 |
| PR5-08 | `valvecmd.h` 彻底删除 | `ls valvecmd.h 2>/dev/null; rg -n "valvecmd\\.h\|\\bTask\\b\|\\bValveCmd\\b" main.cpp mainwindow.cpp mainwindow.h boardcontrol.h` | 文件不存在;代码里无匹配(robot SDK 内部 `Task` 不算) |
| PR5-09 | `.pro` 清理 valvecmd | `rg -n "valvecmd" guangxuan.pro` | 无匹配 |

### 5.3 上板联调点

| 动作 | 通过标准 |
|---|---|
| 启动软件(未 Run) | 相机 / YoloWorker / TrackerWorker / Dispatcher / BoardWorker 均 moveToThread 完毕,线程已 start;无 `CameraWorker started` 等 sessionStart 日志 |
| 不勾品类,直接 Run | `log.txt` 有 `Session started. enabledClasses=0`;此后无 SortTask,无阀动作 |
| 勾 1 个品类,Run | `Session started. enabledClasses=1`;放物体 → SortTask/阀动作 |
| Running 状态再点 Run | `log.txt` 有 `Run clicked while already Running; ignore.`,不重复启动 |
| Run 状态下点 Reset | `log.txt` 有 `Session stopping...` → 各 worker `sessionStop` 返回 → `Session stopped.` |
| Stop 之后再 Run | 行为与首次 Run 一致,不需重启进程 |
| 在 Stop 过程中再点 Run | 日志 `Run clicked while Stopping; please wait.`,忽略 |
| 关窗退出(Running) | 无崩溃;log 末尾有 `Session stopped.` |
| 手动 kill 进程 | 不要求 graceful;但相机/串口能被 OS 回收(再次启动不报"设备占用") |

---

## 6. 回归观察项(所有 PR 合起来)

| ID | 观察项 | 方法 | 通过标准 |
|---|---|---|---|
| REG-01 | 启动-运行-停机内存不泄漏 | `valgrind --leak-check=full ./guangxuan`(开发板或 x86 样机) | No "definitely lost" 来自新模块;老代码泄漏可记录但不阻断 |
| REG-02 | 长时间连续运行 | 点 Run,空跑 2h 或连续过 1k 个物体 | 进程 RSS 稳定在 ±100MB 波动;未见 `QObject::connect: ...` 警告 |
| REG-03 | 阀门不漏关 | 停机前示波器/LED 指示 | `on_reset_clicked` 返回后所有阀都关(对比 PR3 的强制关阀实现) |
| REG-04 | 帧率-延迟配置生效 | soft_fps=1/2/5 三组,测 `Yolo: ... @t_capture/t_infer_done` | `t_infer_done - t_capture` 中位数不受 fps 影响;平均吞吐 = soft_fps |
| REG-05 | 速度动态响应 | 皮带 0.3→0.7 m/s 阶跃 | 阀落点跟随速度变化,不出现"旧速度的滞后打偏" |
| REG-06 | ghost 池清理 | 长时间单物连续过带 | `m_ghosts` 不会无上限增长(可通过调试加日志或断点确认;回归场景靠 `purgeGhosts` + `dispatchedPoolClearMm`) |

---

## 7. 已知限制与本次修复

- **CI 未启用**:沙箱缺 `cc1plus`,本 PR 链未做整盘编译,仅 ReadLints + 单元测试做拦截。**全量 `qmake && make` 必须在开发板或带 rknn/MVS/opencv4 的工作站手动跑通**。
- **RKNN/MVS 路径没有单测**:`YoloSession::yolo_session_init/infer`、`CameraWorker::openCameraLocked/grabOneFrame`、`boardControl::openSerial/readFrame/writeFrame` 依赖硬件,只能上板验证(见各 PR 的"上板联调点")。
- **boardControl::requestEncoderSpeed 读写竞态**:当前实现下 write + read 之间若其它函数路径再写串口,read 有可能读到无关响应。本轮未重构协议层,纳入下一轮改动。
- **已知 bug 修复**(本次总结新加到 commit 之外,尚未合入已有 PR):
  - `mainwindow.cpp::onSortTask` 访问了不存在的 `task.classId / target / areaMm2 / speedMmPerMsAtTrigger`,会让编译直接失败;现改为 `finalClassId / bboxBeltRasterPx / currentSpeedMmPerMs`。
  - `dispatcher.cpp::computePulses` 的"head/tail" 语义与 belt y 方向相反,原实现 `if (tTail <= tHead) return;` 恒成立 → **valve 路径永远无脉冲**。现已按 "yb_max 为前沿, yb_min 为后沿" 修正 `dy_head/dy_tail` 及 mask 扫描方向,单测 `DispatcherTest::valveMode_emitsEnqueuePulses_forReachableObject` 能复现该问题并锁定修复。

### 7.1 第二轮修复(本次批量 commit,上板前必合)

| ID | 问题 | 严重度 | 修复文件 | 验证点 |
|---|---|---|---|---|
| B1 | `TrackerWorker::rasterizeToBelt` 与 `extrapolateBbox` 同时加位移,跨帧 IoU 恒对不齐 | 高 | `tracker_worker.cpp` | 速度>0 时连续两帧同一物体 update_count 能涨;`TrackerWorkerTest` 全绿 |
| B2 | `onFrameInferred` 中 `trkUsed` 按 push_back 前长度构造,miss++ 循环按 push_back 后长度遍历,造成越界 | 高 | `tracker_worker.cpp` | ASAN/UB 扫描干净;`missCount_reachesThresholdAndTriggers_ifUpdateAlreadyEnough` 稳过 |
| B11 | 首帧仅丢弃,lingering 物体进入下一帧仍被识别并触发分拣 | 中 | `tracker_worker.cpp` | 新增 `firstFrame_lingeringObjectIsSuppressedNextFrame` 单测 |
| B4 | `Dispatcher::onSortTask` 无论 pulses 是否为空都写 `m_pending`,长时间运行只增不减 | 高 | `dispatcher.cpp` | `valveMode_returnsEmpty_whenObjectAlreadyPastValveLine` 追加 `m_pending.size()==0` 断言 |
| B12 | Valve 模式下也强制打开 arm_stub CSV,空路径疯狂刷 LOG_ERROR | 低 | `dispatcher.cpp` | Valve 模式运行 5min,`log.txt` 不再出现 `arm_stub_csv open failed` |
| B14 | `m_lastPulses / m_lastPulseSpeed` 只写不读的死字段 | 低 | `dispatcher.h/.cpp` | 代码审查;头文件已删,跑 `pipeline_tests` 绿 |
| B5 | `YoloWorker::sessionStart` 每次都 release+init 模型,每次开工多等 3s+ | 中 | `yolo_worker.{h,cpp}` + `mainwindow.cpp` | 进程启动日志出现 `model loaded (persistent)`;连续 Run/Stop/Run,没有第二次 `yolo_session_init` 日志 |
| B6 | `defaultLabelFromId` 恒 nullptr,叠加层只显示数字 class_id | 中 | `yolo_worker.cpp` | 用 `std::unordered_map<int,std::string>` 轻量注册表替代 1024-槽定长数组;overlay 图标签是 ini 里的中文名,未配置的 classId 回落到数字 id |
| B10 | `save_raw`/`save_result` 只读配置不落盘 | 中 | `camera_worker.{h,cpp}` + `yolo_worker.cpp` | `save_raw=true`+`raw_sample_ratio=1.0` 时,`save_dir/yyyyMMdd/` 下出现 `raw_*.jpg`;启用 `save_result=true` 同目录出现同名 `result_*.jpg` |
| B9 | `CameraWorker::onTick` 反压时静默丢帧 | 中 | `camera_worker.cpp` | 刻意堵塞下游(停 YoloWorker 或长曝光),`log.txt` 出现 `Camera backpressure: dropped N ticks`,状态栏同步告警 |
| B7 | `MainWindow::startSession` 无硬/软检查,也不锁运行中 UI | 中 | `mainwindow.{h,cpp}` + `camera_worker.{h,cpp}` | 拔网线状态点 Run,状态栏显示 `启动失败:相机未连接`,session 保持 Idle;Running 中品类按钮全灰 |
| B8 | `onPipelineWarning` 只写日志,UI 无感知 | 中 | `mainwindow.cpp` | 触发任一 warning(如空脉冲 SortTask),状态栏出现 `告警: ...` 5s |
| B3 | 编码器 raw 语义未定论(瞬时/累计) | 高 | `boardcontrol.{h,cpp}` + `runtime_config.{h,cpp}` + `docs/design.md` | 按 master 口径收敛: raw(u16 BE) 为板卡内部采样好的"转速代理",`speed_m_per_min = raw * encoder_raw_to_m_per_min`(默认 0.502),`speed_mm_per_ms = m/s = m_per_min / 60`。不再需要差分或除采样窗口,`m_lastEncoderPulse/TMs` 已删。ini 键名由 `encoder_pulse_to_mm` 改为 `encoder_raw_to_m_per_min`,升级 ini 必改 |

- 跑 `cd tests && ./pipeline_tests` 必须全部 PASS(含更新的 `firstFrame_registersAllAsGhosts` / `firstFrame_lingeringObjectIsSuppressedNextFrame` / `valveMode_returnsEmpty_whenObjectAlreadyPastValveLine` 新增断言)。

---

## 8. 失败排查对照表

| 症状 | 可能原因 | 先看 |
|---|---|---|
| 点 Run 无反应 | `startSession` 发现 `m_boardThread` 未 running | `log.txt::startSession: board thread not running.` |
| `SortTask` 不出 | 未勾类别 / classId 与 ini 中 `class_btn/N/id` 不匹配 / Tracker 的 Y / X 配置过大 | 先看 `Yolo: N objs` 是否有;再看 `enabledClassIds` 日志数量 |
| 阀永远不动 | `sorterMode` 被设成 `arm` / `valveXMaxMm - xMinMm` / `valveTotalChannels` 使 `chanW<=0` | `Dispatcher sessionStart mode=...`;`Dispatcher: empty pulse list for trackId=N` 告警 |
| 阀开启顺序乱 | `boardControl::onSessionStart` 没接到 / `m_valveTick` 未启动 | `boardControl sessionStart tick=...ms` 日志缺失 |
| `ArmStub CSV` 不生成 | `cfg.armStubCsv` 为空或目录无写权限 | `Dispatcher: arm_stub_csv open failed path=...` |
| 停机后还弹阀 | `onSessionStop` 没跑 / `BlockingQueuedConnection` 在目标线程阻塞 | `Session stopped.` 是否打出;目标线程事件循环是否还活着 |
