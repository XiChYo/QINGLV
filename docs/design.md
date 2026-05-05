# 光选分拣系统技术方案 v2.1

> 本文档是 `docs/requirements.md` v2.1 的实现级技术方案,描述系统的**架构设计、模块契约、跨模块数据 schema 与关键算法的设计决策**。
>
> 范围约定:
> - 本文档**不贴完整源码、不写函数级伪代码**;实现细节请按"附录 A 文件 ↔ 类对照"在 `src/` 中查阅。
> - 所有结构、信号、配置 key 与当前 `src/` 中的实现保持一致,如有偏差以代码为准。
> - 业务/代码图例化展开见 `docs/architecture.md`(三方对齐文档)。

---

## 0. 记号约定

- **时间戳**:`qint64`,单位 ms。来自进程级单调时钟 `pipeline::nowMs()`(底层是 `QElapsedTimer::msecsSinceReference()`)。`main()` 入口最早调用一次 `pipeline::initClock()`。
- **belt 系**:皮带固定坐标系。坐标 `(xb_mm, yb_mm)`;`yb` 沿皮带运动方向递增,`xb` 与运动方向正交;原点在**任务启动瞬间**确定。
- **像素系**:图像坐标 `(xp, yp)`,原点左上,`yp` 向下递增;按约定"皮带运动方向 = 图像 `yp` 递增方向",反向时由 `camera.y_flip` 处理。
- **belt 栅格系**:在 belt 系上以固定 `mask_raster_mm_per_px`(默认 2 mm/px)栅格化得到的二维像素图。Tracker 之后所有 mask / bbox 都在此系下。

---

## 1. 系统架构总览

### 1.1 进程 / 线程拓扑

整个分拣链路在**单进程、多 QThread** 内运行。每个 worker 是一个 `QObject`,通过 `moveToThread` 挂到独立线程,跨线程交互一律 `Qt::QueuedConnection` + 值拷贝传参。

| 线程 | 宿主对象 | 文件 | 用途 |
|---|---|---|---|
| T0(主)| `MainWindow` | `src/app/mainwindow.{h,cpp,ui}` | UI、Session 状态机、配置加载、告警汇聚 |
| T1 | `CameraWorker` | `src/pipeline/camera_worker.{h,cpp}` | 相机采集 + 软件节流 + 反压 + 原图采样落盘 |
| T2 | `YoloWorker` | `src/pipeline/yolo_worker.{h,cpp}` | RKNN 常驻推理 + 后处理 + overlay 渲染 |
| T3 | `TrackerWorker` | `src/pipeline/tracker_worker.{h,cpp}` | belt 系跨帧 mask-IoU 关联 + 触发判定 + 已分拣池 |
| T4 | `Dispatcher` | `src/pipeline/dispatcher.{h,cpp}` | SortTask → ValvePulse 时间轴(valve)/ Arm 坐标日志(arm) |
| T5 | `boardControl`(BoardWorker)| `src/pipeline/boardcontrol.{h,cpp}` | RS485 单总线:阀脉冲调度 + 编码器轮询 |
| T6 | `tcpforrobot` / `robotControl` / OSS / saveLocalpic | `src/legacy/*` | 遗留模块,**不在本期分拣主链路上**,仅保留 UI 状态显示 |

线程生命周期与进程同寿:启动时一次性 `start()`,Session 状态切换不重启线程,只控制"是否放行数据"。

### 1.2 数据流(单向流水线)

```
[Camera HW] → CameraWorker ──QImage+tCapture+filename──▶ YoloWorker
                  ▲                                          │
                  │ frameConsumed                            │ DetectedFrame
                  │  (反压解锁)                               ▼
                                                       TrackerWorker
                                                            │ SortTask
                                                            ▼
                                                       Dispatcher
                                                            │
                                  ┌─────────────────────────┴────────────┐
                                  │                                      │
                       enqueuePulses/cancelPulses               armStubDispatched
                                  ▼                                      ▼
                            BoardWorker ──RS485──▶ 阀板/编码器     MainWindow(日志+CSV)
                                  ▲
                                  │ speedSample(500ms 周期)
                                  │
                          所有 worker ────warning/error────▶ MainWindow(告警汇聚)
```

**关键性质**:

- 单生产者单消费者链路,**没有共享可变状态需要锁**。唯一例外是 `boardControl::m_busMutex` 保护底层串口 IO(粒度 = 单帧)。
- 反压只在 `Camera ↔ Yolo` 这一段:`CameraWorker::m_inFlight`(单元素队列)+ `YoloWorker::frameConsumed` 解锁。Yolo 之后的环节(Tracker → Dispatcher → Board)处理耗时都在 ms 量级,不再设反压。
- 时间戳 `tCaptureMs` 由 `CameraWorker` 一次性打戳,贯穿后续所有结构体,**任何下游不允许重新赋值**。

### 1.3 全局不变量

任何模块都必须遵守:

1. **时间一条线**:`tCaptureMs` 由相机一次性打戳,跨模块只读不写。所有"物体当前位置"由 `speed × (t_now - tCaptureMs)` 推出。
2. **位置一条线**:几何计算统一在 belt 系(mm)。像素 → mm 只在 Tracker 入口做一次,出 Tracker 之后全程 belt 栅格系。
3. **单一位移口径**:同一个物体从 `tCapture` 到任意时刻 `t` 的位移 = `speed * (t - tCapture)`,**只在 `extrapolateBbox` 处加一次**;栅格化阶段不预先叠加位移(避免历史的"双位移"bug)。
4. **停机硬清零**:一次 `sessionStop` 必须把追踪列表 / 已分拣池 / 待喷队列 / 串口未发命令全部清掉,串口不再下发,所有运行时控件恢复可用。
5. **启动后第一帧特殊处理**:任务启动瞬间已经在视野的物体不能形成正常追踪,见 §5.3 "首帧策略"。

---

## 2. Session 生命周期

### 2.1 状态机

`MainWindow` 持有一个三态状态机:

```
        ┌───────────┐  startSession()   ┌───────────┐  stopSession()  ┌───────────┐
        │   Idle    │ ─────────────────▶│  Running  │ ───────────────▶│ Stopping  │
        └───────────┘  (硬检查通过)       └───────────┘                  └───────────┘
              ▲                                                              │
              └──────────────────────────────────────────────────────────────┘
                                所有 worker BlockingQueued 返回完毕
```

- **Idle**:品类勾选控件可编辑;线程已就绪,worker 入口 guard 直接 return。
- **Running**:全链路工作,UI 品类勾选控件 `setEnabled(false)`。
- **Stopping**:`MainWindow::stopSession` 按反向顺序对所有 worker 发 `sessionStop`(`Qt::BlockingQueuedConnection`),保证返回时各 worker 已完成清理。

### 2.2 启动流程(`MainWindow::startSession`)

1. **刷新 UI 状态**:把品类按钮的勾选态合成 `m_cfg.enabledClassIds`(`refreshEnabledClassIdsFromUi`)。
2. **F4 强制检查**:`probeConnection(cfg.cameraIp)` 走 `Qt::BlockingQueuedConnection`,直接拿 `bool`;不通过即拒绝启动并状态栏提示。
3. **F4 软检查**:品类为空仅打告警不阻断;编码器连续失败由 BoardWorker 运行期节流告警。
4. **板卡线程 alive 校验**:`m_boardThread->isRunning()`。
5. **锁定 UI**:`setRuntimeControlsEnabled(false)` 锁定品类按钮。
6. **顺序下发 sessionStart**(`Qt::QueuedConnection`,值拷贝 `RuntimeConfig`):

```
Board → Tracker → Dispatcher → Yolo → Camera
   消费者先就位                       生产者最后启动
```

每个 worker 在自己 slot 内根据 cfg 完成本地副本初始化(配置快照),后续运行中**不再回读 ini**。

### 2.3 停止流程(`MainWindow::stopSession`)

按生产→消费反向:

```
Camera → Yolo → Tracker → Dispatcher → Board
```

每一步用 `Qt::BlockingQueuedConnection`,等 worker 真的清完队列才推进下一个。语义保证:

- `CameraWorker::sessionStop`:停 tick timer,关相机,`m_inFlight` 归零。
- `YoloWorker::sessionStop`:仅关 `saveResult` 之类 session 级副作用,**模型常驻**,不释放 RKNN session。
- `TrackerWorker::onSessionStop`:清空 `m_active`、`m_ghosts`,`m_firstFrame = true`。
- `Dispatcher::onSessionStop`:对 `m_pending` 内每个 trackId `emit cancelPulses(...)`,清空 pending,关 Arm CSV。
- `boardControl::onSessionStop`:停阀 tick;遍历所有 `sentOpen && !sentClose` 的 PulseSlot,**强制下发关阀帧**(避免硬件残留长开),清空 pending。

最后 `setRuntimeControlsEnabled(true)`,回到 Idle。

---

## 3. 模块职责与契约

每个模块给出"定位 / 输入(slot) / 输出(signal) / 关键内部状态 / 关键不变量"。**字段名与代码一致**,具体函数体见对应 `.cpp`。

### 3.1 `MainWindow`(应用层)

**定位**:UI 唯一入口、Session 状态机、worker 生命周期 owner、告警汇聚。

**职责**:

- 启动时 `loadRuntimeConfig("config.ini")` → `m_cfg`(配置快照)。
- 构造 5 个 worker、5 个 QThread(+ 2 个 legacy 线程池),`moveToThread` + `connect` 一次性建好所有跨线程信号。
- 启动时 `QMetaObject::invokeMethod(yolo, "preloadModel", Queued, modelPath)` → F8 模型常驻。
- `on_run_clicked` / `on_reset_clicked` 进入 §2 启停流程。
- 告警汇聚槽 `onPipelineWarning(QString)`:`LOG_ERROR` + `statusBar()->showMessage(..., 5000)`,相同 msg 1 秒内不重复刷新状态栏。
- 数据可视化:接 `YoloWorker::resultImgReady(QImage)` → `updateFrame` → `pixmapItem->setPixmap`。
- 落日志型 slot:`onDetectedFrame`、`onSortTask`、`onArmStubDispatched`、`onBoardSpeedSample`(均仅 `LOG_INFO`,不阻塞链路)。

**关键不变量**:

- 析构 (`~MainWindow`) 必须先 `stopSession()`(若不在 Idle),再 `quit/wait` 各 QThread,避免相机/RKNN/串口资源泄露。
- `probeConnection` 是少数 `BlockingQueuedConnection`(必须同步拿 bool)。

### 3.2 `CameraWorker`

**定位**:海康 MVS SDK 封装 + 软件节流 + 反压 + F16 #1/#2 原图采样落盘。

**关键 slot**:

| slot | 调用方 / 触发 | 语义 |
|---|---|---|
| `sessionStart(cfg)` | MainWindow | 打开相机、设置 `interval_ms = 1000 / cfg.softFps`、起 `m_tickTimer` |
| `sessionStop()` | MainWindow | 停 tick、`MV_CC_StopGrabbing/CloseDevice/DestroyHandle`、`m_inFlight = 0` |
| `probeConnection(ip)` | MainWindow(BlockingQueued) | 仅枚举 GigE 设备,看 `ip` 是否在线;返回 bool |
| `onFrameConsumed()` | YoloWorker(Queued) | 反压解锁:`m_inFlight.fetch_sub(1)` |

**关键 signal**:

| signal | 接收方 | 语义 |
|---|---|---|
| `frameReadySig(QImage, qint64 tCaptureMs, QString fileName)` | YoloWorker | 一帧已采集(深拷贝 QImage);`fileName` 非空表示该帧已被原图采样命中并落盘,YoloWorker 据此同名落盘 result |
| `cameraError(QString)` | MainWindow | 致命错误(打开失败等),由 MainWindow `LOG_ERROR` |
| `warning(QString)` | MainWindow | F10 反压窗口告警 |

**关键内部状态**:

```
m_tickTimer        QTimer(PreciseTimer),周期 = m_intervalMs
m_inFlight         atomic<int>(单元素队列,kInFlightMax = 1)
m_lastCaptureMs    上次成功放行帧的 tCapture
m_intervalMs       1000 / soft_fps,运行期不变
m_saveRaw          F16 总开关
m_rawSampleRatio   F16 抽样比 [0.0, 1.0]
m_droppedSinceWarn 反压窗口告警计数
```

**关键不变量**:

- 软件节流的"放行帧"才记 `tCaptureMs`,丢弃的帧不进入任何后续流程。
- `m_inFlight >= kInFlightMax` 时:跳过本轮 tick + `m_droppedSinceWarn++`;累计 ≥ 5 且距上次告警 ≥ 2 s 时 `emit warning`(F10 不静默)。
- `frameReadySig` 携带的 `QImage` **必须深拷贝**(`copy()`)一次,彻底切断相机驱动 buffer 生命周期。
- F16 落盘使用 `QRandomGenerator::global()` 抽样;路径 `save_dir/yyyyMMdd/raw_<hhmmss_zzz>_<seq>.jpg`。

### 3.3 `YoloWorker`(+ `YoloSession` + `postprocess_ex`)

**定位**:RKNN YOLOv8-Seg 常驻推理 + 后处理 + UI 可视化 + 结果图落盘。

**子模块拆分**:

- `yolo_session.{h,cpp}`:RKNN C API 的纯 C++ 封装(无 Qt),`init / release / infer`,便于复用与单测。
- `postprocess_ex.{h,cpp}`:`post_process_seg_ex(...)` 接收运行时阈值参数,实现"分桶 top-K → per-class NMS → cross-class NMS → 仅对保留项算 mask"**三阶段优化**(见 §5.6);`draw_results_ex(...)` 把 SegObject 叠成 BGR overlay。
- `yolo_worker.{h,cpp}`:Qt Worker 壳,负责生命周期与跨线程通信。

**关键 slot**:

| slot | 调用方 | 语义 |
|---|---|---|
| `preloadModel(QString modelPath)` | MainWindow(进程启动后调用一次) | F8 模型常驻:`yolo_session_init` 一次,持续到进程退出 |
| `sessionStart(cfg)` | MainWindow | 仅刷新阈值(`confThreshold` / `nmsThreshold`)、`drawOverlay` / `saveResult` / `saveDir`,以及 `setClassLabels(cfg.classButtons)`(供 overlay 中文标签);如果 `cfg.modelPath` 与已加载的不同才 release+reload |
| `sessionStop()` | MainWindow | 关 `saveResult` 等 session 级副作用,**模型保留** |
| `onFrame(QImage img, qint64 tCaptureMs, QString fileName)` | CameraWorker | 一次推理 |

**关键 signal**:

| signal | 接收方 | 语义 |
|---|---|---|
| `detectedFrameReady(DetectedFrame)` | TrackerWorker、MainWindow | 结构化推理结果 |
| `resultImgReady(QImage)` | MainWindow | F5 可视化叠加图(空检测时直接发原图,自动清屏) |
| `frameConsumed()` | CameraWorker(QueuedConnection 直连)| 反压解锁信号,触发 `CameraWorker::onFrameConsumed` |

**关键不变量**:

- F8:模型只在进程启动 / 模型路径变更时初始化一次,**每帧不重复 init/release**。
- 任何路径(模型未加载、空 QImage、`m_sessionValid = false`)都必须 `emit frameConsumed()`,否则会卡死 `m_inFlight`。
- F5 空检测:`segs` 为空时不画任何 overlay,等价于发原图,UI 自动清掉上一帧覆盖层。
- F11 字段:`DetectedObject = {classId, confidence, bboxPx, maskPx, centerPx}`;`tCaptureMs` / `tInferDoneMs` 在 `DetectedFrame` 上一次性带,不在每个对象上冗余。
- F16 #3:仅当 `m_saveResult = true` 且 `fileName 非空`(说明本帧的原图已落盘)时才落 `result_<base>.<suffix>`;两者满足才落。

### 3.4 `TrackerWorker`

**定位**:把 YOLO 检测从像素系搬到 belt 栅格系,跨帧 mask-IoU 关联合并,触发分拣判定,维护已分拣抑制池。

**关键 slot**:

| slot | 调用方 | 语义 |
|---|---|---|
| `onSessionStart(cfg)` | MainWindow | 拷快照、清 `m_active` / `m_ghosts`、`m_firstFrame = true` |
| `onSessionStop()` | MainWindow | 同上,但置 `m_sessionActive = false` |
| `onFrameInferred(DetectedFrame)` | YoloWorker | 主流程入口,见 §5.2 / §5.3 |
| `onSpeedSample(SpeedSample)` | BoardWorker | 仅当 `s.valid` 时更新 `m_lastSpeed` |

**关键 signal**:

| signal | 接收方 | 语义 |
|---|---|---|
| `sortTaskReady(SortTask)` | Dispatcher、MainWindow | 一颗物体满足触发条件,要派发 |
| `warning(QString)` | MainWindow | 业务级告警 |
| `frameAnnotationReady(qint64, QVector<DetTrackBinding>)` | 仅离线仿真消费(R-OBS) | 每帧无条件 emit;`bindings` 顺序与 `frame.objs` 一一对应;**不参与生产决策**,生产路径下 MainWindow 不连接此信号 |

**关键内部状态**:

```
m_active       QList<TrackedObject>     活跃追踪列表(belt 栅格系)
m_ghosts       QList<DispatchedGhost>   已分拣抑制池(belt 栅格系)
m_lastSpeed    SpeedSample              最近一次有效速度;valid=false → 用 nominalSpeedMs 兜底
m_firstFrame   bool                     §5.3 "首帧策略"标志
m_nextTrackId  int                      自增 id
m_mmPerPx      float                    栅格化分辨率(默认 2 mm/px)
```

**关键不变量**(防回归):

- 见 §5.1 "单一位移口径":`rasterizeToBelt` **不**叠加 belt 位移,位移只在 `extrapolateBbox` 加。
- 第 5 步 "未匹配 trk → miss++" 的循环边界**必须用 `trkUsed.size()`** 而非 `m_active.size()`(因为第 4 步可能向 `m_active` push 了新 track)。
- 物体一旦触发 `sortTaskReady`,该物体**不能再触发第二次**:由"trk 移出 m_active 同时入 m_ghosts"+"后续帧的同位置 det 走抑制路径"双重保证。
- 启动首帧:见 §5.3,所有检测**入 ghost,不入 active**。
- **关联与抑制同口径**(R-MERGE):关联候选打分与 ghost 抑制命中判定都用 `score = max(IoU, coverage)`,阈值都用 `cfg.iouThreshold`;贪心排序按 score 降序。两端口径不一致会让"残→全"在关联端被放过、在抑制端漏掉,直接导致重复分拣。

### 3.5 `Dispatcher`

**定位**:把 `SortTask` 翻译成执行机构指令——valve 模式下生成 `ValvePulse` 时间轴交给 BoardWorker,arm 模式下计算 A/B 臂坐标并写 CSV + 上报 UI。

**关键 slot**:

| slot | 调用方 | 语义 |
|---|---|---|
| `onSessionStart(cfg)` | MainWindow | 拷快照、清 `m_pending`;`Arm` 模式下 `openArmCsv()`(写表头),`Valve` 模式不开 CSV |
| `onSessionStop()` | MainWindow | 对所有 `m_pending` 中的 trackId `emit cancelPulses`,清空 pending,关 CSV |
| `onSortTask(SortTask)` | TrackerWorker | 路由:Arm → `dispatchArmStub`;Valve → `computePulses` 后 enqueuePulses |
| `onSpeedSample(SpeedSample)` | BoardWorker | 速度变化超阈值时整体重算,见 §5.4 "速度重算" |

**关键 signal**:

| signal | 接收方 | 语义 |
|---|---|---|
| `enqueuePulses(QVector<ValvePulse>, int trackId)` | BoardWorker | 一条物体的完整阀脉冲时间轴 |
| `cancelPulses(int trackId)` | BoardWorker | 取消该 trackId 尚未下发的 pulse(已开未关的保留) |
| `armStubDispatched(int trackId, int classId, float aX, float aY, float bX, float bY)` | MainWindow | F14.1 派发上报 |
| `warning(QString)` | MainWindow | 业务告警(如 `object_too_small`) |

**关键内部状态**:

```
m_pending      QMap<int, SortTask>     已排但仍可能需要重算的 valve 任务,key = trackId
m_lastSpeed    SpeedSample             用于速度变化的 Δ 计算;首次有效速度作为初始基线
m_armCsvFile   QFile* / QTextStream*   Arm 模式下的 CSV 句柄(每条派发后 flush)
```

**关键不变量**:

- Dispatcher 自己**不写串口**,时间轴 100% 交给 BoardWorker 在 5 ms tick 上执行。
- 空 pulse 序列(物体已越过喷阀线 / mask 为空 / 留白后已过线)**不入 pending**,只发 `warning`。
- 速度重算策略:见 §5.4。**整体取消整体重算**,不做 per-pulse 差分(简化一个数量级)。
- Arm CSV 仅在 Arm 模式派发时懒打开,并按 `YYYYMMDD/arm_stub.csv` 日期子目录滚动,避免 valve 部署环境下 `armStubCsv` 路径无效造成误日志污染。

### 3.6 `boardControl`(BoardWorker)

**定位**:RS485 单总线唯一持有者。同根串口承担**互斥的两件事**:

1. **5 ms tick** 扫 `m_pending`,把 `tOpen ≤ now / tClose ≤ now` 的 PulseSlot 真正下发开/关阀帧。
2. **500 ms tick** 发编码器请求,读 11 字节响应,解析后 `emit speedSample`。

**关键 slot**:

| slot | 调用方 | 语义 |
|---|---|---|
| `initSerial()` | 线程 `started`(自动) | 打开 `/dev/ttyUSB0`(失败回退 `ttyUSB1`,115200 8N1);创建两个 QTimer;立即起 `m_encoderTick`(与 session 解耦,UI 侧速度始终更新) |
| `onSessionStart(cfg)` | MainWindow | 缓存 `tickIntervalMs / encoderRequestIntervalMs / encoderRawToMPerMin`;启动 `m_valveTick`;`m_pending.clear()` |
| `onSessionStop()` | MainWindow | 停 valveTick;**强制下发关阀帧** for 所有 `sentOpen && !sentClose`;`m_pending.clear()` |
| `onEnqueuePulses(QVector<ValvePulse>, int trackId)` | Dispatcher | `m_pending[trackId] = pulses` 转成 `QVector<PulseSlot>`(初始 sentOpen/sentClose=false)|
| `onCancelPulses(int trackId)` | Dispatcher | 仅保留"已开未关"的,丢弃"未开"的;若全部已开未关则保留待自然关阀,否则全空时移除 trackId |
| `singleBoardControl(QString)` / `batchBoardControl(QString)` | UI Action 菜单(legacy)| 手动开关测试通道,统一走 `m_busMutex` |
| `requestEncoderSpeed()` | `m_encoderTick` / 测试入口 | 发 9 字节请求 + 读 11 字节响应,见 §7 |

**关键 signal**:

| signal | 接收方 | 语义 |
|---|---|---|
| `speedSample(SpeedSample)` | TrackerWorker / Dispatcher / MainWindow | 解析后的速度样本 |
| `encoderSpeedReceived(QByteArray)` | MainWindow | 原始帧(供 UI 速度 label 旧路径) |
| `errorOccured(QString)` | MainWindow | 串口打开失败等错误冒泡(H1) |

**关键内部状态**:

```
m_busMutex                QMutex                    保护 writeFrame + tcdrain;锁粒度=单帧
m_fd                      int                       串口 fd
m_pending                 QMap<int, QVector<PulseSlot>>
m_valveTick / m_encoderTick QTimer*                  分别 5 ms / 500 ms
m_valveTickIntervalMs     int (默认 5)
m_encoderRequestIntervalMs int (默认 500)
m_encoderRawToMPerMin     float (默认 0.502)
```

`PulseSlot = { ValvePulse pulse; bool sentOpen; bool sentClose; }` —— 在内部把"已发未发"的状态贴到每条 pulse 上。

**关键不变量**:

- `m_busMutex` **不跨开/关持锁**,粒度 = 单帧(几百 μs)。`onValveTick` 与 `onEncoderTick` 在同一线程事件循环上,天然不会重入。
- `onCancelPulses` **必须保留"已开未关"**(否则会漏关阀,产生硬件残留)。
- `onSessionStop` 必须强制下发关阀帧(避免硬停后阀门长开的安全问题)。
- 编码器请求/响应与阀命令在 `m_busMutex` 上 FIFO 排队;阀写入耗时 ~几百 μs,编码器读 timeout 50 ms 作为最差 case 阻塞——在 `soft_fps=2`、`tick=5 ms` 下整体可接受;实测抖动超标再拆队列。
- 编码器 raw 语义(master 口径):`raw` 是板卡内部采样好的"瞬时转速代理",`speed_m_per_min = raw × encoderRawToMPerMin`,`speed_mm_per_ms = speed_m_per_min / 60`。**不需要差分、不需要除以采样窗口**(数值上 m/s ≡ mm/ms)。

### 3.7 基础设施

- `src/pipeline/pipeline_clock.{h,cpp}`:全局 `QElapsedTimer`,`pipeline::initClock()` 在 `main()` 入口最早调用一次;`pipeline::nowMs()` 是所有 worker 取时间的唯一入口。
- `infra/logger.{h,cpp}`:`LOG_INFO / LOG_WARN / LOG_ERROR` 宏,要求调用方是 `QObject` 派生类(自动注入类名)。底层同步写文件 + stderr,通过 `QMutex` 串行化。

---

## 4. 跨模块数据契约 (Schema)

所有跨线程信号的参数都是 POD,定义在 `src/pipeline/pipeline_types.h`,在 `main()` 中 `qRegisterMetaType` 注册。语义概要如下(字段名与代码完全一致):

### 4.1 `DetectedObject`(YOLO 阶段单目标)

| 字段 | 类型 | 含义 |
|---|---|---|
| `classId` | int | 模型类别 id |
| `confidence` | float | 置信度 |
| `bboxPx` | cv::Rect | **像素坐标**外接矩形 |
| `maskPx` | cv::Mat (CV_8UC1) | 像素 mask,尺寸 = `bboxPx.size()`,非零表示命中 |
| `centerPx` | cv::Point2f | 像素重心(`maskPx` 主轮廓矩;无 mask 时退化成 bbox 中心)|

### 4.2 `DetectedFrame`(YOLO 阶段一帧输出)

| 字段 | 类型 | 含义 |
|---|---|---|
| `tCaptureMs` | qint64 | 采图瞬间时间戳(贯穿全链)|
| `tInferDoneMs` | qint64 | 推理完成时间戳(仅供延迟统计)|
| `imgWidthPx` / `imgHeightPx` | int | 原图尺寸,Tracker 用作"像素 → mm"换算的分母 |
| `objs` | QVector\<DetectedObject\> | 该帧所有保留目标 |

### 4.3 `TrackedObject`(Tracker 内部)

| 字段 | 类型 | 含义 |
|---|---|---|
| `trackId` | int | 自增 id |
| `maskBeltRaster` | cv::Mat (CV_8UC1) | belt 栅格系 mask |
| `bboxBeltRasterPx` | cv::Rect | belt 栅格全局坐标系下的 bbox |
| `tCaptureMs` | qint64 | 该 mask/bbox 对应的快照采图时刻(用于位移外推)|
| `latestAreaMm2` | float | 最新非零像素面积 × `mmPerPx²` |
| `missCount` | int | 连续未关联帧数 |
| `updateCount` | int | 累计被关联次数 |
| `classAreaAcc` | QHash\<int, float\> | classId → 累计加权面积(决定最终类别)|

### 4.4 `DispatchedGhost`(已分拣抑制池项)

字段是 `TrackedObject` 的关键子集:`maskBeltRaster / bboxBeltRasterPx / tCaptureMs / finalClassId`。继续随皮带位移(同样按 `extrapolateBbox`),用于抑制二次分拣。

### 4.5 `SortTask`(Tracker → Dispatcher)

| 字段 | 类型 | 含义 |
|---|---|---|
| `trackId` | int | 用于后续 `enqueuePulses` / `cancelPulses` 关联 |
| `finalClassId` | int | 累计类别投票胜出者 |
| `tCaptureMs` | qint64 | belt 系快照基准时刻(Dispatcher 据此外推 yb)|
| `maskBeltRaster` | cv::Mat | belt 栅格系 mask(快照)|
| `bboxBeltRasterPx` | cv::Rect | belt 栅格 bbox(快照)|
| `currentSpeedMmPerMs` | float | 触发瞬间的速度,Dispatcher 据此首次预计算 |

### 4.6 `SpeedSample`(BoardWorker 广播)

| 字段 | 类型 | 含义 |
|---|---|---|
| `tMs` | qint64 | 采样时刻 |
| `speedMmPerMs` | float | 皮带速度(mm/ms ≡ m/s)|
| `valid` | bool | false 表示读取失败,消费者应使用 `nominalSpeedMs` 兜底 |

### 4.7 `ValvePulse`(Dispatcher → BoardWorker)

| 字段 | 类型 | 含义 |
|---|---|---|
| `tOpenMs` | qint64 | 绝对开阀时刻(`pipeline::nowMs()` 语义)|
| `tCloseMs` | qint64 | 绝对关阀时刻 |
| `boardId` | quint8 | 板号 1..8 |
| `channelMask` | quint16 | 该板内通道位图(每板 9 通道,用 bit0..bit8)|

`QVector<ValvePulse>` 也注册为 metatype,以便 `Q_ARG` 传递。

### 4.8 `RuntimeConfig`(配置快照)

定义在 `src/config/runtime_config.h`,`MainWindow` 在 `loadRuntimeConfig` 中一次性从 `config.ini` 读入,通过 `sessionStart(RuntimeConfig)` 信号下发给所有 worker;worker 本地保存副本,运行中不再回读文件。字段分组与 §6 配置 schema 一一对应。

`enabledClassIds` 是唯一不来自 ini 的字段,由 `MainWindow::refreshEnabledClassIdsFromUi` 根据按钮勾选态合成。

### 4.9 `DetTrackBinding`(R-OBS 帧观测,Tracker → 离线仿真层)

| 字段 | 类型 | 含义 |
|---|---|---|
| `detIndex` | int | 在 `DetectedFrame.objs` 中的下标(顺序与该帧 YOLO 输出一致) |
| `trackId` | int | 关联到的 trackId;**ghost 抑制 / 首帧 ghostify 取 -1** |
| `bestClassId` | int | 该帧"最终被标记成的类别",**恒 ≥ 0**,可直接用于标注 |
| `suppressedByGhost` | bool | 命中已分拣池被抑制(非首帧)|
| `isNewTrack` | bool | 本帧首次进入 active(关联失败但通过新建产生)|
| `firstFrameGhost` | bool | 首帧被作为 ghost 直接登记 |

**`bestClassId` 取值规则**(四种状态全部覆盖、互斥):

| 状态位组合 | bestClassId 来源 |
|---|---|
| 关联到现有 track | `argmax(track.classAreaAcc)`(合并后)|
| `suppressedByGhost = true` | `ghost.finalClassId` |
| `isNewTrack = true` | `det.classId`(首次仅一票) |
| `firstFrameGhost = true` | `det.classId` |

**信号语义**:`frameAnnotationReady(tCaptureMs, QVector<DetTrackBinding>)` 在 `onFrameInferred` 末尾**无条件 emit**(`frame.objs` 为空也 emit 一个空 vector,标注层据此清屏)。

**与生产的关系**:生产 MainWindow 不连接此信号,Qt 信号空发开销 ≈ 一次原子操作 + 一次 `QVector` 构造,可忽略;仅离线仿真工具 `tests/offline_sim/annotated_frame_sink` 消费。

---

## 5. 关键算法设计

每个算法以"目标 / 输入输出 / 步骤(按编号)/ 不变量与边界"组织,**不写伪代码**,只描述设计选择与必须满足的语义。

### 5.1 像素 → belt 栅格 (`TrackerWorker::rasterizeToBelt`)

**目标**:把图像像素 mask 转换到 belt 栅格系,得到与 belt 全局坐标对齐、可与历史追踪做 mask-IoU 的 `(maskBeltRaster, bboxBeltRasterPx)`。

**关键设计 —— 单一位移口径**:

`bbox(t)` 相对其 `tCapture` 时刻的栅格化结果只差一个位移,且**位移只在 `extrapolateBbox` 处加一次**。栅格化阶段输出的 bbox 是"该帧采图瞬间相机视野内的 image-lab mm 直接栅格化",**不预先叠加 `tCapture - tOrigin` 的位移**。

```
bbox_at(t) = rasterize(det, tCapture)  +  speed × (t − tCapture)
              ←─── §5.1 输出 ───→        ←──── extrapolateBbox ────→
```

历史上曾经出现过"栅格化时已经按 `tCapture - tOrigin` 加了一次位移,后续 IoU 计算时 `extrapolateBbox` 又按 `t - tCapture` 再加一次"的双位移 bug,这是**必须避免**的关键不变量。

**步骤**:

1. `pxToMmX = realLengthMm / imgWidthPx`,`pxToMmY = realWidthMm / imgHeightPx`(像素 → mm 比例)。
2. 物理 bbox(mm)= 像素 bbox × 比例。
3. 栅格化:`gx = round(bbox_mm.x / mmPerPx)`,`gw = max(1, round(bbox_mm.w / mmPerPx))`,等等。
4. mask 栅格化:像素 mask 通过 `cv::resize(INTER_NEAREST)` 变成 `(gw, gh)` 大小,再 `threshold` 保证二值。
5. `outAreaMm2 = countNonZero(outMask) × mmPerPx²`。

**注**:belt 系原点定在"任务启动瞬间相机底边对应的 belt y" —— 简化口径下,`yb = 0` 对应启动瞬间相机视野最远端,`yb = realWidthMm` 对应启动瞬间相机视野最近端;喷阀线 `yb = realWidthMm + valveDistanceMm`(见 §5.4)。这个简化在本期足够用,后续接入编码器累计位移作为"启动时编码器 raw → belt 原点"时再细化。

### 5.2 跨帧关联(贪心 `max(IoU, coverage)`,`TrackerWorker::onFrameInferred`)

**目标**:把每帧 YOLO 输出的 N 个检测合并到 `m_active` 的 M 个追踪上,允许多对一冲突。

**关联打分(R-MERGE)**:`score = max(maskIoU, maskCoverage)`,其中 `maskCoverage(A, B) = |A∩B| / min(|A|, |B|)`。
- **物理含义**:`IoU` 关注两 mask 的"整体重合度",分母是并集面积;`coverage` 关注"小那一侧落在另一侧上的比例",分母是较小者面积。
- **必要性**(残→全场景):前一帧物体被画面边缘截断只露半截、后一帧整个进入视野时,两 mask 面积差异显著(典型实测残的面积 ≈ 全的 1/4~1/3),IoU = 残的面积/全的面积 ≈ 0.25~0.33,跨阈值 0.30 极其敏感;`coverage` = 残的面积/残的面积 = 1.0,稳定救回。
- **阈值复用** `cfg.iouThreshold`(默认 0.30):语义"几何相似度"已不再是纯 IoU,但阈值含义对齐到"高度相似"——`max(IoU, coverage) ≥ 0.30` 视为高度相似。
- **不退化**:当两 mask 面积接近(普通跨帧)时,`coverage ≈ IoU`,`max(IoU, coverage) ≈ IoU`,与原口径等价。

**步骤**(与代码顺序一致,共 7 步):

1. **栅格化**:对该帧所有 det 调 §5.1,得到 `dets[i] = {srcIdx, mask, bbox, areaMm2}`。
2. **构造候选集**:对每个 `(det, trk)` 对,把 `trk.bboxBeltRasterPx` 通过 `extrapolateBbox(.., trk.tCaptureMs, tNow)` 外推到本帧 `tCapture`,计算 `iou = maskIoU` + `cov = maskCoverage`,取 `score = max(iou, cov)`;`score ≥ cfg.iouThreshold` 的进 `candidates`。
3. **贪心匹配**:`candidates` 按 **score 降序**排序;扫一遍,跳过 `det / trk` 已 used 的;命中后做 mask union 融合(把 trk 的旧 mask 与 det 的当前 mask 在 belt 全局栅格系下取并集 → 同时更新 bbox = 两 bbox 并),写回 `trk.maskBeltRaster / bboxBeltRasterPx / tCaptureMs = tNow`,`updateCount++`,`missCount = 0`,`classAreaAcc[det.classId] += det.areaMm2`,`detUsed[i] = trkUsed[j] = true`。
4. **未匹配 det → 抑制 / 新建**:逐个未匹配 det,先扫 `m_ghosts`,任一 ghost 外推到 `tNow` 后 `max(IoU, coverage) ≥ 阈值`(**与第 2 步同口径**)则**与 ghost 做 union 融合并刷新 ghost 时间戳**(让抑制池跟随观测持续更新),并 `continue`;否则新建 `TrackedObject` push 到 `m_active`,`trackId = m_nextTrackId++`。
5. **未匹配 trk → miss++**:循环边界**必须用 `trkUsed.size()`** 而非 `m_active.size()`(因为第 4 步可能 push 了新 track,新 track 的 missCount 已初始化为 0)。
6. **触发判定**:遍历 `m_active`(反向以便 `removeAt`),`miss ≥ X` 或 `update ≥ Y` 即触发;选 `classAreaAcc` 最大者为 `bestClass`;`bestClass` 不在 `cfg.enabledClassIds` 集合内 → 仅 `removeAt`,**不 emit、不入 ghost**;否则 `emit sortTaskReady(makeSortTask(trk, bestClass))` + push DispatchedGhost + `removeAt`。
7. **purgeGhosts(tNow)**:每个 ghost 用 `extrapolateBbox` 外推到 `tNow`,若 `bbox.y * mmPerPx > dispatchLineYb + dispatchedPoolClearMm` 则丢弃。`dispatchLineYb` 按 `cfg.sorterMode` 取 `realWidthMm + valveDistanceMm`(valve)或 `realWidthMm + armDistanceMm`(arm)。

**最后一步:R-OBS 帧观测信号**。每个分支(关联 / ghost 抑制 / 新建 / 首帧 ghostify)在自己处填一份 `DetTrackBinding`,主流程末尾**无条件** `emit frameAnnotationReady(tCaptureMs, bindings)`。

**贪心 vs 匈牙利**:本期不引入匈牙利。理由:soft_fps=2 + 单帧检测数 N 通常 < 20 + 单视野 active track 数 M 通常 < 10,贪心给出的最大 score 优先关联在物理意义上一致(score 越大越像同一个物);代码实现简单且无依赖。

### 5.3 首帧策略(F4 / AC15)

**问题**:任务启动瞬间视野中可能已有部分穿越镜头的物体(belt 原点对它们不齐),贸然形成追踪会导致后续触发分拣时位置不准。

**本期方案 —— 首帧检测全部入 ghost,不入 active**:

- `m_firstFrame` 初始为 `true`。
- 收到的第一个 `DetectedFrame`:把每个 det 栅格化后**直接构造 `DispatchedGhost` push 到 `m_ghosts`**(`finalClassId` 仅作日志可读,不会真正派发),`m_firstFrame = false; return`。
- 后续帧的检测如果与这些 ghost IoU 命中,会按 §5.2 第 4 步的"已分拣抑制"路径处理;ghost 自然会被外推过分拣线 + `dispatchedPoolClearMm` 后由 `purgeGhosts` 回收。

注意:这与 v1.0 design.md 中"首帧全部丢弃"的描述等价,但实现上**借用了已分拣池**,因此能稳定抑制"在途物体在后续帧仍被检测到"的情况,鲁棒性更好。

### 5.4 喷阀预计算(`Dispatcher::computePulses`)

**目标**:输入一颗物体的 belt 系快照(SortTask)+ 当前速度,输出 `QVector<ValvePulse>` 时间轴。

**关键参数**(`cfg.valve.*`):
- `total_channels = 72`,`boards = 8`,`channels_per_board = 9`。
- `x_min_mm` / `x_max_mm`:喷阀覆盖横向范围;`channel_width_mm = (x_max - x_min) / total_channels`。
- `head_skip_ratio`(默认 0.125 = 1/8):头部留白比例。
- `open_duration_ms`(默认 50):单条 pulse 时长 + 滚动投影步长。
- `min_cmd_interval_ms`(默认 50):同 board 上相邻 pulse 的最小合并间隔。
- `valve_line_yb_mm = realWidthMm + valveDistanceMm`(belt 系喷阀线 y)。

**坐标约定**(与 belt 系一致):`bb.y` = trailing edge(后沿,远离喷阀线),`bb.y + bb.height` = leading edge(前沿,先到喷阀线)。

**步骤**:

1. **快速失败保护**:speed < 1e-3 mm/ms / mask 为空 / 通道配置非法 → 返回空。
2. **算到达时刻**:`dy_head = valveY - bbox_yb_max` / `dy_tail = valveY - bbox_yb_min`;`dy_tail < 0` 表示后沿已过喷阀线,物体全部越过 → 返回空。`tHead = tCap + dy_head / speed`,`tTail = tCap + dy_tail / speed`。
3. **头部留白起始行**:从 mask 最后一行(前沿侧)向后(后沿方向)累积非零像素数,达到 `totalArea × head_skip_ratio` 的那一行作为 `row_open_start`;对应 `yb_open = (bb.y + row_open_start) × mmPerPx`;若 `dy_open = valveY - yb_open < 0`,**不入 pending,不发 pulse,emit `warning` `object_too_small`**(F14.2 空脉冲处理)。`tOpenStart = tCap + dy_open / speed`。
4. **滚动投影**:以 `open_duration_ms` 为步长,从 `tOpenStart` 推进到 `tTail`。每步:
   - 算"该时刻喷阀线对应物体 mask 哪一行" `local_row = round((valveY - obj_tail_yb_at_t) / mmPerPx)`,其中 `obj_tail_yb_at_t = bbox_yb_min + speed × (t - tCap)`。
   - 取 `mask.row(local_row)` 的非零列 → 每列对应 belt xb → 用 `(xb - x_min) / channel_width_mm` 算通道 idx → `boardId = idx / channels_per_board + 1`,`bitPos = idx % channels_per_board` → 构造 `boardMask` 累计该步的 `{boardId: bitmap}`。
5. **合并相邻 pulse**:实际合并阈值 `merge_interval = max(open_duration_ms, min_cmd_interval_ms)`(避免 `min_cmd_interval < open_duration` 时出现"上一条还在开就发新一条"的逻辑空洞)。同 board 上前一条 pulse 的 `tOpenMs` 距 `t` 小于 `merge_interval` 时,把当前 bitmap OR 进上一条,`tCloseMs` 取大;否则新增一条 `ValvePulse{tOpenMs=t, tCloseMs=t+open_duration_ms, boardId, channelMask}`。
6. **结束**:返回 `result`。

**速度重算**(`Dispatcher::onSpeedSample`):

- 首次有效速度作为 `m_lastSpeed` 基线,**不触发重算**。
- 后续每次 `onSpeedSample(s)` 计算 `delta = |s.speed - oldSpeed| / oldSpeed`;`delta > cfg.valveSpeedRecalcPct/100` 即触发。
- 触发逻辑:遍历 `m_pending`,对每个 trackId:
  1. `emit cancelPulses(trackId)`。
  2. 用 `m_pending[trackId]`(原始 SortTask 快照,含 `tCapture / mask / bbox`)+ 新速度重新跑 §5.4 步骤 1-5。
  3. 重算结果为空 → 从 `m_pending` 移除该 trackId(物体已彻底越过喷阀线);否则 `emit enqueuePulses(new, trackId)`。
- 这是"整体取消整体重算",**不做 per-pulse 差分**。理由:差分要在两条时间轴间做精细对齐(头部留白 / 滚动相位 / 合并都受速度影响),实现复杂度爆炸但收益有限——重算耗时本身在 ms 量级。

### 5.5 机械臂 stub(`Dispatcher::dispatchArmStub`)

**目标**:给定 SortTask,计算物体重心在 A、B 臂本体坐标系下的位置,落 CSV + emit。

**步骤**:

1. 取 `SortTask.bboxBeltRasterPx` 的中心(belt 栅格 px),× `mmPerPx` 得 `(cx_mm, cy_mm)`(belt 系 mm)。
2. 套两套四参数标定:`outX = sx × (cx_mm - origin_x)`,`outY = sy × (cy_mm - origin_y)`,分别得到 `(aX, aY)` 与 `(bX, bY)`。
3. CSV 流式写一行:`ts_iso, trackId, classId, cx_mm, cy_mm, aX, aY, bX, bY`,`flush()`。
4. `emit armStubDispatched(...)` 给 MainWindow 落日志。

**不下发 TCP**:`tcpforrobot` 模块不在本期分拣链路上(F14.1)。

### 5.6 YOLO 后处理三阶段(`post_process_seg_ex`)

**目标**:把模型一次推理的 `det_data + proto_data` 转成可供 Tracker 消费的 `SegObject` 列表(每条带 box / mask / label / prob)。

**Phase 1 — 候选扫描**:逐 anchor 取出 `bbox + label + prob` + 32 维 mask coeffs;`prob > conf_threshold` 才入 `candidates`;**此阶段不算 mask**(把"系数 → proto 卷积 → sigmoid → resize → threshold"留到最后只对保留项算)。

**Phase 2 — per-class NMS**:按 class 分桶,每桶按 prob 降序取 top-K(默认 K=300),桶内做矩形 IoU NMS,IoU > `nms_threshold` 的低 prob 项被抑制。这一步**仅在同 class 内做**,目的是去掉同类重叠的多检。

**Phase 2.5 — class-agnostic NMS(R-NMS)**:把 Phase 2 全部保留的索引按 prob 降序统一再扫一遍,IoU > `nms_threshold` 的低 prob 项**无论 class 是否一致都抑制**。

- **物理依据**:垃圾分拣业务上类别**互斥**,同一物理位置不可能同时既是 beveragebottle 又是 tetrapak;即便模型偶发同位置打出多类高 conf,业务侧只接受 conf 最高那一条进入 Tracker。
- **必要性**:per-class NMS 不会跨桶抑制,实测出现过 `cls=1 conf=0.77` 与 `cls=5 conf=0.67` 在同一 bbox(IoU≈0.99)被双双保留 → Tracker 端立刻拆出两条 track → 重复分拣。
- **复杂度**:Phase 2 后保留项数 N 通常 < 20,Phase 2.5 是 O(N²) 的矩形 IoU 扫描,实测 < 1ms,可忽略。

**Phase 3 — 仅对保留项算 mask**:对最终 `keep_indices` 中的每条候选,做 `coeffs · proto + sigmoid + resize + threshold` 得到 `cv::Mat` mask,组装成 `SegObject` 输出。

**与 master 分支的差异**:master 没有 Phase 2.5;本期在仿真核验中发现并补齐(详见 `测试需求.md §7.9`)。如未来出现"两类物体合理重叠"(比如盒子套盒子)的真实场景,可按 P3 演进为 class pair 白名单/黑名单驱动的混合 NMS。

---

## 6. 配置 schema (`config.ini`)

位置:工程工作目录下 `config.ini`(`QSettings::IniFormat` + UTF-8)。所有运行期可调参数集中此处。修改后**必须重启进程生效**(F15)。前端 UI 仅在勾选"品类按钮"时影响 `enabledClassIds`,其它参数全部从 ini 读。

完整字段表(与 `RuntimeConfig` 一一对应):

### `[camera]`

| key | 默认值 | 含义 |
|---|---|---|
| `ip` | `192.168.1.30` | GigE 相机 IP |
| `hw_fps` | 60 | 相机硬件采集帧率 |
| `y_flip` | false | 皮带运动方向是否与图像 y 递增方向相反 |
| `real_length_mm` | 560 | 图像 x 方向对应物理宽度 |
| `real_width_mm` | 470 | 图像 y 方向对应物理长度 |

### `[model]`

| key | 默认值 | 含义 |
|---|---|---|
| `path` | `/opt/models/yolov8seg.rknn` | RKNN 模型路径(F8 不支持热切换)|
| `input_size` | 1024 | 模型输入边长(letterbox 目标尺寸)|
| `topk_class_count` | 80 | top-K 布局兜底类数 |
| `class_btn/size` | 0 | 品类按钮总数 |
| `class_btn/N/name` | — | 第 N 个品类按钮 key(N 从 1 起,与 `[categoryButtons]` key 对齐)|
| `class_btn/N/id` | — | 对应模型 class_id |

### `[yolo]`

| key | 默认值 | 含义 |
|---|---|---|
| `conf_threshold` | 0.25 | 置信度阈值 |
| `nms_threshold` | 0.45 | 按类 NMS 阈值 |
| `draw_overlay` | true | 是否给 UI 发带 overlay 的图(false 则发原图)|

### `[pipeline]`

| key | 默认值 | 含义 |
|---|---|---|
| `soft_fps` | 2 | 软件节流帧率(F9)|
| `iou_threshold` | 0.30 | belt 系 mask-IoU 关联阈值 |
| `miss_frames_x` | 2 | 触发条件 A:连续未关联帧数 |
| `update_frames_y` | 10 | 触发条件 B:累计关联次数 |
| `dispatched_pool_clear_distance_mm` | 200 | ghost 回收距离 |
| `mask_raster_mm_per_px` | 2 | belt 栅格分辨率 |
| `tick_interval_ms` | 5 | BoardWorker 阀轮询间隔 |

### `[belt]`

| key | 默认值 | 含义 |
|---|---|---|
| `nominal_speed_m_s` | 0.5 | 编码器读不到时的兜底速度 |
| `encoder_raw_to_m_per_min` | 0.502 | master 口径转换系数 |
| `encoder_request_interval_ms` | 500 | 编码器轮询周期 |

### `[sorter]`

| key | 默认值 | 含义 |
|---|---|---|
| `mode` | `valve` | `valve` \| `arm` |
| `arm_distance_mm` | 500 | 机械臂距图像最近边缘垂直距离 |
| `valve_distance_mm` | 800 | 喷阀距图像最近边缘垂直距离 |

### `[valve]`

| key | 默认值 | 含义 |
|---|---|---|
| `total_channels` | 72 | 总通道数 |
| `boards` | 8 | 板卡数 |
| `channels_per_board` | 9 | 每板通道数 |
| `x_min_mm` / `x_max_mm` | 0 / 560 | 喷阀覆盖的横向范围 |
| `head_skip_ratio` | 0.125 | 头部留白比例(1/a)|
| `open_duration_ms` | 50 | 单 pulse 时长 + 滚动步长 |
| `min_cmd_interval_ms` | 50 | 同 board 相邻 pulse 合并间隔 |
| `speed_recalc_threshold_pct` | 20 | 速度变化触发整体重算的百分比阈值 |

### `[arm_stub]`

| key | 默认值 | 含义 |
|---|---|---|
| `a_origin_x_mm` / `a_origin_y_mm` | 0 / 0 | A 臂原点偏移 |
| `a_x_sign` / `a_y_sign` | 1 / 1 | A 臂轴方向(±1)|
| `b_origin_x_mm` / `b_origin_y_mm` | 0 / 0 | B 臂原点偏移 |
| `b_x_sign` / `b_y_sign` | 1 / 1 | B 臂轴方向 |

### `[persistence]`

| key | 默认值 | 含义 |
|---|---|---|
| `save_raw` | false | 总开关 |
| `raw_sample_ratio` | 0.1 | 抽样比 [0.0, 1.0] |
| `save_result` | false | 是否保存结果图(仅当 raw 命中且非空 fileName 时)|
| `save_dir` | `/data/captures` | 落盘根目录 |
| `arm_stub_csv` | `/data/logs/arm_stub.csv` | Arm CSV 基准路径;实际落到其父目录的 `YYYYMMDD/arm_stub.csv` |

> 现 `config.ini` 同时保留遗留 UI 段(`colorButtons` / `categoryButtons` / ...)与上述新管道 section;worker 启动时读取新管道 section,UI 按遗留段生成按钮。

---

## 7. 串口协议规格(RS485)

`/dev/ttyUSB0`(回退 `ttyUSB1`),`115200 8N1`,无流控。所有帧由 `0xAA 0x55` 头 + `0x55 0xAA` 尾分界。

### 7.1 帧布局

| 帧类型 | 字节序列 | 长度 | 说明 |
|---|---|---|---|
| 批量开阀 | `AA 55  ·  11 06 02  ·  b6 b7 b8 b9  ·  CHK  ·  55 AA` | 11 | b6 = 板号 1..8;b7 = `bitCount(b8)+bitCount(b9)`;b8 = 通道高字节(bit0 → 通道 9);b9 = 通道低字节(bit0..bit7 → 通道 1..8)|
| 批量关阀 | 同上,`b7=0x09, b8=b9=0x00` | 11 | 关该板所有通道 |
| 编码器请求 | `AA 55  ·  11 03 03  ·  0A 21  ·  55 AA` | 9 | 固定 |
| 编码器响应 | `AA 55  ·  11 05 03  ·  0A rawH rawL ...  ·  55 AA` | 11 | `raw = (rawH << 8) | rawL` u16 BE |
| CHK 计算 | `(0x11 + 0x06 + 0x02 + b6 + b7 + b8 + b9) & 0xFF` | — | 仅阀控帧 |

### 7.2 编码器 raw 解析(master 口径)

```
raw            = (rx[6] << 8) | rx[7]    // u16 BE
speed_m_per_min = raw × encoder_raw_to_m_per_min      // master 实测 0.502
speed_mm_per_ms = speed_m_per_min / 60                // m/s 与 mm/ms 数值相同
```

raw 是板卡内部采样好的"瞬时转速代理",不是累计脉冲也不是窗口增量,**无需差分、无需除以采样窗口**。若后续硬件版本改变此口径,改 `encoderRawToMPerMin` 系数或重写 `requestEncoderSpeed` 中 raw 解析段即可,不影响其它模块。

### 7.3 阀板地址映射

```
boardId = (channel_idx − 1) / channels_per_board + 1   // 1..8
bitPos  = (channel_idx − 1) % channels_per_board       // 0..8
channelMask |= (1u << bitPos)
```

`channels_per_board = 9` 时 `bit8` 落到 `b8` 的 bit0(协议规定的"通道 9"位)。

### 7.4 总线仲裁

`m_busMutex` 串行化所有 `writeFrame + tcdrain`,粒度 = 单帧。`onValveTick`(5 ms)与 `onEncoderTick`(500 ms)在同一事件循环上,**自然不重入**;锁的实际竞争只在"请求/响应等待中阀命令排队"这一极小窗口,正常负载下不会成为瓶颈。

---

## 8. 横切关注点

### 8.1 时钟与时间戳传递

- `pipeline::initClock()` 在 `main()` 入口最早调用一次。
- `pipeline::nowMs()` 是所有 worker 取时间的唯一入口;`tCaptureMs` 由 `CameraWorker::grabOneFrame` 在 `MV_CC_GetImageBuffer` 返回后立即打戳,贯穿 `DetectedFrame → TrackedObject → SortTask → ValvePulse(间接)`。
- 任何模块**不允许**用 `QDateTime` / `time()` 等其它时钟做跨模块对齐(只有 `arm_stub.csv` 的可读时间戳列允许 ISO 时间,因为它面向人不面向算法)。

### 8.2 告警与日志

- `LOG_INFO` / `LOG_ERROR`(暂未启用 `LOG_WARN`,告警直接走信号 + `LOG_ERROR`)由 `infra/logger.h` 提供,底层加 `QMutex` 串行化。
- 告警**汇聚槽是 `MainWindow::onPipelineWarning(QString)`**,所有 worker 的 `warning` / `errorOccured` 都连到这里,内部:`LOG_ERROR` + `statusBar()->showMessage(..., 5000)`;相同 msg 1 秒内不重复刷新状态栏。
- 等级语义:
  - **ERROR**:相机/串口打开失败、模型加载失败 → 阻断启动 / 状态栏红条。
  - **WARN(走 warning / errorOccured 信号)**:编码器连续读取失败、反压丢帧、`object_too_small`、品类未勾选等 → 任务继续。
  - **INFO**:任务启停、SortTask 触发、ArmStub 派发、speedSample(可选)。
- 反压丢帧是窗口告警(累计 ≥ 5 且距上次告警 ≥ 2 s 才发一次),**不静默**也**不每帧刷屏**。

### 8.3 持久化(F16)

| 开关 | 路径生成 | 触发 |
|---|---|---|
| `save_raw + raw_sample_ratio` | `save_dir/yyyyMMdd/raw_<hhmmss_zzz>_<seq>.jpg` | `CameraWorker::trySaveRawFrame` 抽样命中时 |
| `save_result` | 与原图同目录 `result_<base>.<suffix>` | 仅当 `fileName` 非空(原图已落盘)+ `m_saveResult=true` |
| `arm_stub_csv` | `dirname(cfg.armStubCsv)/YYYYMMDD/arm_stub.csv`(按需 `mkpath`)| Arm 模式下首次派发或日期变化时打开,每次派发追加一行 |

### 8.4 错误处理与降级

| 失败 | 行为 |
|---|---|
| 相机打开失败(启动时)| `cameraError` → `MainWindow` `LOG_ERROR`;启动流程 §2.2 之前的 `probeConnection` 已先做硬检查 |
| 相机运行中掉线 | 仅 `cameraError` 告警,**不中断任务**(F4 / 6.2)|
| 编码器连续读取失败 / 帧无效 | `BoardWorker` 广播 `valid=false` 的 `SpeedSample`,连续失败达到阈值后告警;raw=0 视为有效停止;Tracker / Dispatcher 在无有效速度样本时用 `nominalSpeedMs` 兜底;**不阻断**链路 |
| 串口打开失败 | `errorOccured` → `MainWindow.onPipelineWarning`(H1:必须冒泡,否则阀沉默业务静默)|
| 模型加载失败 | `m_sessionValid = false`,`onFrame` 仅 emit `frameConsumed`;前端可见结果一直为空 |
| 阀脉冲列表为空 | `Dispatcher` emit `warning(object_too_small)`,**不入 pending,不下发** |
| 速度突变超阈值 | `Dispatcher` 整体 cancel + 重算(§5.4)|
| 停机时阀已开未关 | `BoardWorker::onSessionStop` 强制下发关阀帧 |

---

## 9. 性能预算

默认配置:`soft_fps=2`、`mask_raster_mm_per_px=2`、模型输入 `1024×1024`,RK3588 NPU。

| 指标 | 预算 | 关键贡献者 |
|---|---|---|
| 单帧"采图 → YOLO → Tracker"总耗时 | < 500 ms(`1000 / soft_fps`)| YOLO 推理(~200 ms)+ 后处理(~10 ms)+ Tracker(~1 ms)|
| 采图到喷阀首次下发的端到端延迟 | < 600 ms(不含物体飞行时间)| 上一项 + Dispatcher computePulses(~1 ms)+ Board tick(0~5 ms)|
| 阀控命令延迟(Dispatcher → 串口完成 write) | < 50 ms | `tick_interval_ms=5` + 串口 write ~几百 μs |
| 1 小时内存波动 | < 50 MB | 模型常驻 + 缓存 mask 复用 |
| 1 小时丢帧计数 | = 0(`fps=2` 下) | YOLO 单帧 ~200 ms < 500 ms 节流间隔 |

---

## 10. 设计决策与权衡(为什么这么做)

| 决策 | 原因 | 可能的演进 |
|---|---|---|
| 5 个 worker 各自独立线程,而不是合并(如 Tracker + Dispatcher 合一)| Tracker/Dispatcher 都需要 `speedSample`,合一会让单线程兼具高频(5 ms 阀 tick)+ 中频(500 ms 编码器)+ 低频(单帧 ~500 ms 推理)负载,易抢占;拆开后每个 worker 都是低频或单一节奏 | 后续可拆 `BoardWorker` 为 `SerialBus`(底层)+ `ValveProtocol`(协议层)|
| 跨帧关联用贪心 IoU,而不是匈牙利 | N、M < 20,贪心 O(NM log NM) 足够;匈牙利对实现/调试代价大;最大 IoU 物理上即"最像同一个物" | 多目标密集场景再切匈牙利 |
| 速度重算"整体取消整体重算",而不是 per-pulse 差分 | 头部留白 / 滚动相位 / 合并都受速度影响,差分对齐复杂度高;整体重算耗时 ms 级,完全在阀 tick 间隙 | 现场实测速度抖动剧烈再优化 |
| 模型路径、运行参数、valve / 算法工艺参数均不暴露前端 | F2 + F15:配置集中在 ini,改了重启;前端只负责启停、品类勾选和可视化 | 后续切 yaml/json + schema 校验(附录 B)|
| 首帧检测入 ghost 而不是直接丢弃 | "在途物体在第二帧仍可能被检测到",直接丢弃在 F1 后会被 F2 当新物体登记,误分拣;入 ghost 可以利用现成的抑制路径自动处理 | — |
| `BoardWorker` 同时承担阀 + 编码器,单 mutex 保护 | RS485 物理上只有一根总线,任何并行写都会乱码;mutex 锁粒度=单帧,实测无瓶颈 | 实测瓶颈再拆队列调度 |
| Arm CSV 仅 Arm 模式派发时懒打开并按日期滚动 | Valve 部署环境下 `arm_stub_csv` 路径常无效,频繁失败日志会污染告警总线;日期目录与图像落盘口径一致 | — |
| `legacy/*` 模块继续存在但不接业务 | 仅保留 UI 状态显示与未来对接 stub,不影响 Running 路径 | tcpforrobot 接入后再纳入主链路 |
| 后处理在 per-class NMS 之上再做一道 cross-class NMS(R-NMS)| 垃圾分拣类别互斥,同位置异类双检会让 Tracker 拆出两条 track 触发重复分拣;Phase 2.5 复杂度 O(N²) N<20 可忽略 | 出现"两类物体合理重叠"实例时改 class pair 白名单/黑名单 |
| 跨帧关联与 ghost 抑制都用 `max(IoU, coverage)`(R-MERGE)| 残→全场景下 IoU 被分母拉低 ≈ 0.25-0.33 跨阈值漏关联;coverage 衡量"小那块在另一块上的覆盖率",此时接近 1 稳定救回;两端同口径杜绝"关联放过、抑制漏掉"的连锁失效 | 物体在前进方向尺寸 < 200 px 的极端小物体仍可能临界,可加入卡尔曼 / 中心距离打分 |
| Tracker 增加 `frameAnnotationReady` 帧观测信号(R-OBS)| 离线仿真 / 可视化标注需要"该帧每个 det → 最终 trackId / bestClassId"的逐帧绑定;无条件 emit 简化消费端逻辑;生产 MainWindow 不连接,Qt 信号空发开销可忽略 | 后续如果 UI 需要在主路径上画 trackId,直接连同一信号即可 |

---

## 附录 A. 文件 ↔ 类对照

| 路径 | 关键类 / 函数 | 职责 |
|---|---|---|
| `src/app/main.cpp` | `main()` | `pipeline::initClock`、`qRegisterMetaType`、构造 `MainWindow` |
| `src/app/mainwindow.{h,cpp,ui}` | `MainWindow` | UI、Session 状态机、worker owner、告警汇聚 |
| `src/config/runtime_config.{h,cpp}` | `RuntimeConfig`、`loadRuntimeConfig` | 配置 POD + ini 加载器 |
| `src/pipeline/pipeline_types.h` | `DetectedObject` / `DetectedFrame` / `TrackedObject` / `DispatchedGhost` / `SortTask` / `SpeedSample` / `ValvePulse` | 跨线程数据 schema |
| `src/pipeline/pipeline_clock.{h,cpp}` | `pipeline::initClock` / `pipeline::nowMs` | 全局单调时钟 |
| `src/pipeline/camera_worker.{h,cpp}` | `CameraWorker` | T1 |
| `src/pipeline/yolo_worker.{h,cpp}` | `YoloWorker` | T2 壳 |
| `src/pipeline/yolo_session.{h,cpp}` | `YoloSession` + `yolo_session_init/release/infer` | RKNN 封装(无 Qt)|
| `src/pipeline/postprocess.h` | `SegObject` | 后处理输出类型 |
| `src/pipeline/postprocess_ex.{h,cpp}` | `post_process_seg_ex` / `draw_results_ex` | 参数化后处理 + overlay 绘制 |
| `src/pipeline/tracker_worker.{h,cpp}` | `TrackerWorker` | T3 |
| `src/pipeline/dispatcher.{h,cpp}` | `Dispatcher` | T4 |
| `src/pipeline/boardcontrol.{h,cpp}` | `boardControl`(BoardWorker)| T5 |
| `src/infra/logger.{h,cpp}` | `Logger` + `LOG_INFO/WARN/ERROR` 宏 | 同步日志 |
| `src/legacy/*` | `tcpforrobot` / `robotcontrol` / `uploadpictoOSS` / `saveLocalpic` / `updatemanager` | 不在分拣主链路上 |
| `tests/` | QTest 单测(只依赖无 SDK 的模块)| — |

## 附录 B. 长期演进(不在本期)

- 把 `pipeline/*` 进一步拆出 `Pipeline` facade,`MainWindow` 不再直接持有 5 个 worker,改为依赖一个 facade。
- `BoardWorker` 拆 `SerialBus`(底层)+ `ValveProtocol`(协议层),为机械臂 TCP 走相同的 Bus 抽象铺路。
- `config.ini` 切到 YAML / JSON,引入 schema 校验。
- 引入 gtest/gmock + 纯算法单测(mask IoU、贪心关联、`computePulses`)。
- belt 系原点从"`yb=0` ↔ 启动瞬间相机视野最远端"切换为"基于编码器累计 raw 的真实位移",支持非简化的 `valveLineYbMm` 计算。

---

## 文档状态

- **版本**:v2.1(对齐 R-NMS / R-MERGE / R-OBS 三处主逻辑改动 + `requirements.md` v2.1)
- **依赖**:`docs/requirements.md` v2.1、`docs/architecture.md` v1.1、`docs/verification.md`(逐 PR 验证清单)
- **v2.0 → v2.1 变更**:
  - §3.3 子模块拆分:`postprocess_ex` 由"两阶段优化"修订为"三阶段优化",对应代码新增 Phase 2.5 cross-class NMS。
  - §3.4 关键 signal 表新增 `frameAnnotationReady`,关键不变量新增"关联与抑制同口径"。
  - §4 跨模块数据契约新增 §4.9 `DetTrackBinding` schema(R-OBS),含状态位互斥规则与 `bestClassId` 取值规则表。
  - §5.2 标题与算法描述全部从"贪心 IoU"改为"贪心 max(IoU, coverage)";新增"关联打分"节专门解释残→全场景的物理动机;步骤 2/3/4 显式带打分公式;末尾补一段 R-OBS 信号 emit 时机。
  - §5 新增 §5.6 "YOLO 后处理三阶段",把 Phase 1/2/2.5/3 设计动机与复杂度说清。
  - §10 设计决策与权衡新增三行:R-NMS、R-MERGE、R-OBS。
  - **不调整**模块拆分、Session 状态机、跨模块时序、性能预算、配置 schema、附录 A 文件对照——这些都没受 R1/R2/R4 改动影响。
- **v2.0 变更**:
  - 删除全部函数级伪代码(原 §4 跨帧关联 / §4.4 喷阀预计算)和完整源码片段;数据 schema 从代码贴片改为字段表 + 语义说明。
  - 删除"6. 串口子系统重构 / 8. 废弃代码清单 / 9. 分步落地计划"等迁移期内容(已落地)。
  - 新增"3. 模块职责与契约"统一 slot/signal/不变量表;"5. 关键算法设计"按"步骤 + 不变量"组织而非伪代码。
  - 修正与现实代码不一致处:`YoloWorker` 信号名 `resultImgReady`(非 `visualizeReady`);反压由 `CameraWorker::m_inFlight` 实现(非 `YoloWorker::droppedBusy`);`DetectedObject` 字段为 `centerPx`(非 `bboxOriginPx`);时间戳只在 `DetectedFrame` 上一次性带,不在每个目标上冗余;`tcpforrobot` 已下沉为 `src/legacy/`。
  - 新增"10. 设计决策与权衡"沉淀本期"为什么这么做"。
- **v1.0 变更(历史)**:首版,基于 `requirements.md` v1.1 给出 D1 轻量重构路线。
