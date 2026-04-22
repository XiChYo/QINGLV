# 光选分拣系统技术方案 v1.0

> 本文档是 `requirements.md` v1.1 的实现级设计,给出落地所需的模块边界、线程模型、数据结构、接口契约、关键算法伪代码、配置 schema 与分步落地计划。
>
> 本文档采用 **D1 轻量重构路线**:保留现有类骨架(`cameraThread` / `yoloRecognition` / `boardControl` / `tcpForRobot`),新增 `TrackerWorker` / `Dispatcher` 两个 QObject;`MainWindow` 只做 UI 与配置持久化。彻底重构方案作为长期演进目标(附录 B)。

---

## 0. 记号约定

- 所有时间戳统一 **`qint64`,单位 ms**,来自 `QElapsedTimer::msecsSinceReference()`(进程内单调)。整个程序启动时创建一个全局 `QElapsedTimer g_timer; g_timer.start();`,所有模块通过 `g_timer.elapsed()` 获取 `now_ms`。
- **皮带固定坐标系**(下称 `belt 系`)坐标记作 `(xb_mm, yb_mm)`;`yb` 沿皮带运动方向递增,`xb` 与运动方向正交。
- **图像像素坐标系**记作 `(xp, yp)`,原点左上,`yp` 向下递增;在本设计中约定"皮带运动方向 = 图像 `yp` 递增方向"。若实际相机安装为反向,统一由一个布尔配置项 `camera.y_flip` 处理。

---

## 1. 系统架构

### 1.1 模块图

```
┌──────────────────────────────────────────────────────────────────────────┐
│                             主线程 (Qt GUI)                              │
│                                                                          │
│    MainWindow                                                            │
│    - UI 控件、配置持久化、状态栏、告警汇聚                               │
│    - Session 状态机 (Idle/Running/Stopping)                              │
└──────────────────────────────────────────────────────────────────────────┘
      │ signals (QueuedConnection)             ▲  signals (QueuedConnection)
      ▼                                        │
┌─────────────┐  QImage+t_capture  ┌─────────────────────┐   DetectedFrame
│CameraWorker │ ─────────────────▶ │   YoloWorker        │ ────────────────┐
│  (QThread1) │                    │   (QThread2)        │                 │
│ ─ 采图      │                    │  - RKNN 常驻 session│                 │
│ ─ 软件节流  │                    │  - 预处理/推理/后处理│                │
│ ─ 反压      │                    │  - 仅过滤,不追踪    │                │
└─────────────┘                    └─────────────────────┘                 │
                                                                           ▼
┌─────────────┐   SpeedSample     ┌──────────────────────────┐  TrackedFrame
│ BoardWorker │ ────────────────▶ │     TrackerWorker        │◀─────────────┘
│  (QThread3) │                   │       (QThread4)         │
│ ─ 串口总线  │                   │  - belt系 mask IoU 关联   │
│ ─ 编码器速度│                   │  - 已分拣池               │
│ ─ 阀命令发送│                   │  - 触发判定               │
└─────────────┘                   └──────────────────────────┘
      ▲                                        │ SortTask (QueuedConnection)
      │ ValveFrame                             ▼
      │                           ┌──────────────────────────┐
      │                           │      Dispatcher          │
      │                           │       (QThread5)         │
      │                           │  - 按 sorter mode 路由    │
      │                           │  - 喷阀预计算时间轴       │
      │                           │  - 5ms 轮询调度           │
      └──────────────────────────▶│  - 速度重算              │
                                  │  - 机械臂 stub(日志)     │
                                  └──────────────────────────┘
```

信号连接一律 **`Qt::QueuedConnection`**,跨线程传参全部值拷贝。

### 1.2 线程清单

| 线程 | 宿主对象 | 用途 |
|---|---|---|
| T0(主)| `MainWindow` | UI、配置、状态栏、告警 |
| T1 | `CameraWorker` | 相机采图 + 节流 |
| T2 | `YoloWorker` | YOLO 推理 |
| T3 | `BoardWorker`(即现 `boardControl`) | 串口总线:编码器 + 阀命令 |
| T4 | `TrackerWorker` **(新)** | 跨帧追踪 |
| T5 | `Dispatcher` **(新)** | 阀时间轴调度、机械臂 stub |

T2~T5 全部 **单线程 + 单事件循环**,不做并行推理。T3 内部再起一个 5 ms 轮询 timer 驱动阀命令。

### 1.3 Session 生命周期

`MainWindow` 持有一个 Session 状态机:

```
        ┌───────────┐  start()  ┌───────────┐  stop()  ┌───────────┐
        │   Idle    │──────────▶│  Running  │─────────▶│ Stopping  │
        └───────────┘           └───────────┘          └───────────┘
              ▲                                               │
              └───────────────────────────────────────────────┘
                         所有 worker 确认已清空后
```

- **Idle**:参数输入可编辑,所有 worker 存活但**不处理帧**(Camera 线程空转读取但丢弃,Yolo/Tracker/Dispatcher 入口 guard 直接 return)。
- **Running**:全链路工作。参数控件只读禁用。
- **Stopping**:硬停——广播停止信号给所有 worker,worker 清空内部队列/状态后回发 `stopped()`,全部到齐后切回 Idle。

worker 线程本身**不重启**,整个程序生命周期内只 `start()` 一次;Session 状态只控制"是否放行数据"。这样避免线程重复创建销毁带来的资源问题。

---

## 2. 数据结构

全部放在 `include/pipeline_types.h`,统一 `Q_DECLARE_METATYPE` 以便跨线程信号传递。

```cpp
#pragma once
#include <QMetaType>
#include <QVector>
#include <QHash>
#include <opencv2/core.hpp>
#include <cstdint>

// 帧内一个识别目标(YOLO 阶段输出)
struct DetectedObject {
    int      classId;
    float    confidence;
    cv::Rect bboxPx;        // 像素坐标
    cv::Mat  maskPx;        // CV_8UC1, 值 0/255, 尺寸与 bbox 同
    cv::Point bboxOriginPx; // bbox 左上角在原图坐标(mask 的原点)
};

// 一帧完整 YOLO 输出(给 TrackerWorker)
struct DetectedFrame {
    qint64  tCaptureMs;
    qint64  tInferDoneMs;
    int     imgWidthPx;
    int     imgHeightPx;
    QVector<DetectedObject> objs;
    // 可视化用原图(可选,节流保存开关命中时)
    cv::Mat debugBgr;
};

// 皮带系下的追踪物体
struct TrackedObject {
    int     trackId;
    // belt系 mask:存 bbox 相对 belt 原点 + mask 图像。
    // mask 按固定 mm/px 分辨率栅格化(见 config pipeline.mask_raster_mm_per_px)
    cv::Mat maskBeltRaster;         // CV_8UC1
    cv::Rect bboxBeltRasterPx;      // 在 belt 系栅格图中的 bbox(栅格 px)
    qint64  tCaptureMs;             // 该 mask 对应的采图时间戳(用于位置外推)
    float   latestAreaMm2;
    int     missCount = 0;
    int     updateCount = 0;
    QHash<int, float> classAreaAcc; // classId -> 累计加权面积
};

// 已分拣池项(结构同 TrackedObject 的关键子集)
struct DispatchedGhost {
    cv::Mat maskBeltRaster;
    cv::Rect bboxBeltRasterPx;
    qint64  tCaptureMs;  // 继续沿用该戳做位置外推
    int     finalClassId;
};

// 分拣任务(TrackerWorker → Dispatcher)
struct SortTask {
    int      trackId;
    int      finalClassId;
    qint64   tCaptureMs;             // 作为时间基准
    cv::Mat  maskBeltRaster;
    cv::Rect bboxBeltRasterPx;       // 决定物体在 belt 系的空间范围
    float    currentSpeedMmPerMs;    // 触发瞬间的皮带速度(用于首次预计算)
};

// 编码器速度样本(BoardWorker → 所有需要方)
struct SpeedSample {
    qint64 tMs;
    float  speedMmPerMs;  // 皮带速度,mm/ms = m/s (乘 1000 后)
    bool   valid;         // false 表示读取失败,用 fallback
};

// 阀时间轴上的一条指令(Dispatcher 内部)
struct ValvePulse {
    qint64   tOpenMs;    // 绝对时刻(g_timer.elapsed() 语义)
    qint64   tCloseMs;
    quint8   boardId;    // 1..8
    quint16  channelMask; // bit0 对应通道 1, ... (该板内)
};

Q_DECLARE_METATYPE(DetectedObject)
Q_DECLARE_METATYPE(DetectedFrame)
Q_DECLARE_METATYPE(TrackedObject)
Q_DECLARE_METATYPE(SortTask)
Q_DECLARE_METATYPE(SpeedSample)
Q_DECLARE_METATYPE(ValvePulse)
```

**cv::Mat 跨线程拷贝注意**:`cv::Mat` 自带引用计数的浅拷贝,通过 Qt signal 值传递时只有 Mat 头被拷贝,数据缓冲区共享。为避免竞争,TrackerWorker 收到后立即 `mask.clone()` 一次进入 belt 系栅格化,之后原图可释放。

---

## 3. 模块接口契约

以下是每个类的**公开**信号/槽/关键成员。构造参数与私有实现细节在落地时补齐。

### 3.1 `MainWindow`

```cpp
class MainWindow : public QMainWindow {
    Q_OBJECT
signals:
    // 配置快照,任务启动时一次性下发给所有 worker
    void sessionStart(const RuntimeConfig& cfg);
    // 硬停信号
    void sessionStop();
    // 告警汇聚槽会接到所有 worker 的 warning 信号
public slots:
    void onWarning(const QString& source, const QString& msg);
    void onDroppedFrame(qint64 tCaptureMs, int reason);
    void onArmStubDispatched(int trackId, int classId,
                             cv::Point2f posArmA, cv::Point2f posArmB);
};
```

UI 层面的启动流程:

1. 点"启动" → 读取 UI 各控件值,合成 `RuntimeConfig` 快照。
2. 执行 **F4 强制检查**(相机已连接);不通过直接提示返回。
3. 执行 **F4 软检查**;不通过告警但继续。
4. `emit sessionStart(cfg)`,各 worker 自己在 slot 里切换到 Running。
5. UI 参数控件 `setEnabled(false)`,启停按钮改为"停止"。

### 3.2 `CameraWorker`(基于现 `cameraThread`)

```cpp
class CameraWorker : public QObject {
    Q_OBJECT
public slots:
    void onSessionStart(const RuntimeConfig& cfg);
    void onSessionStop();
signals:
    void frameReady(qint64 tCaptureMs, QImage img);
    void warning(const QString& msg);
};
```

**职责**:
- `onSessionStart`:读取 `cfg.fps`,设置节流间隔 `interval_ms = 1000 / fps`;打开相机(如未打开)。
- 主循环:阻塞拿相机一帧 → 记录 `t_capture = g_timer.elapsed()` → 与上次放行帧的时间戳对比;若 `t_capture - t_last_pass >= interval_ms` 则放行,否则丢弃。
- 放行前,根据 `cfg.save_raw / raw_sample_ratio` 决定是否写盘(文件命名 `raw_<tCaptureMs>.png`)。
- `frameReady` 信号发出的 `QImage` **必须 `copy()` 深拷贝一次**,彻底切断相机驱动的 buffer 生命周期。

### 3.3 `YoloWorker`(基于现 `yolorecognition`)

```cpp
class YoloWorker : public QObject {
    Q_OBJECT
public slots:
    void onStartupInitialize(const QString& modelPath);  // 进程启动时调用一次
    void onSessionStart(const RuntimeConfig& cfg);
    void onSessionStop();
    void onFrame(qint64 tCaptureMs, QImage img);
signals:
    void frameInferred(DetectedFrame frame);
    void visualizeReady(qint64 tCaptureMs, QImage rendered); // F5 给 UI
    void warning(const QString& msg);
    void droppedBusy(qint64 tCaptureMs); // F10 反压
};
```

**职责**:
- `onStartupInitialize`:**进程启动时一次性调用**。加载模型、分配 tensor 内存;常驻到进程退出。不在 `onSessionStart` 里做这件事。
- `onFrame`:
  - 若 `m_busy` 为真 → `emit droppedBusy(...)` 直接 return(F10 单元素队列)。
  - 否则置 `m_busy = true`,进入推理。
  - 推理完后走 F11 过滤(`conf_threshold` / NMS),组装 `DetectedFrame`。
  - **同时渲染可视化图**(box + mask + 品类名 + 置信度),`emit visualizeReady`(F5)。
  - `emit frameInferred`。
  - `m_busy = false`。
- F5 空检测:若 `DetectedFrame.objs` 为空,依然 emit `visualizeReady` 发原图;UI 收到后直接清空覆盖层显示原图(也可以直接发原图,因为 rendered 没叠加任何覆盖层)。

**废弃**:`recognition` 旧槽、每帧 `init_model_session`/`release_session`、每帧 `cv::imwrite`、所有 `CLOCK_PROFILE` 注释。

### 3.4 `BoardWorker`(基于现 `boardControl`)

```cpp
class BoardWorker : public QObject {
    Q_OBJECT
public slots:
    void onStartupInitialize();            // 打开串口
    void onSessionStart(const RuntimeConfig& cfg);
    void onSessionStop();                  // 清空阀队列
    // 新接口:一次提交一条物体的完整时间轴
    void onEnqueuePulses(const QVector<ValvePulse>& pulses, int trackId);
    // 新接口:取消某个 trackId 已排但未发的所有 pulse(速度重算用)
    void onCancelPulses(int trackId);
signals:
    void speedSample(SpeedSample s);      // 周期性上报
    void warning(const QString& msg);
};
```

**内部实现**:
- 一个 `QMap<int /*trackId*/, QVector<ValvePulse>> m_pending`。
- 一个 **5 ms 轮询 `QTimer` `m_tick`**(配置项 `pipeline.tick_interval_ms`),timeout 时:
  1. 遍历 `m_pending`,对所有 `pulse.tOpenMs <= now && !pulse.sent_open` 的,构造开帧、写串口,标记。
  2. 对所有 `pulse.tCloseMs <= now && sent_open && !sent_close` 的,构造关帧、写串口,标记。
  3. 全部 sent_close 的 trackId 整条移除。
- 另一个 **500 ms 周期 timer** `m_encoderTick` 请求编码器。
- 串口写操作用 `m_busMutex` 保护(替代原 `m_writeMutex + m_serialMutex`),**任一时刻只有一条帧正在写**,但不再跨帧持锁。
- 编码器响应帧与阀命令**不冲突**(阀命令硬件不回 ACK,按 E1-(a) 假设),编码器请求/读取串行化但不阻塞阀命令排队。

### 3.5 `TrackerWorker`(新增)

```cpp
class TrackerWorker : public QObject {
    Q_OBJECT
public slots:
    void onSessionStart(const RuntimeConfig& cfg);
    void onSessionStop();
    void onFrameInferred(DetectedFrame frame);
    void onSpeedSample(SpeedSample s);
signals:
    void sortTaskReady(SortTask task);
    void warning(const QString& msg);
};
```

**内部状态**:
- `QList<TrackedObject> m_active`:活跃追踪列表。
- `QList<DispatchedGhost> m_ghosts`:已分拣池。
- `SpeedSample m_lastSpeed`:最近一次速度样本。
- `qint64 m_originPulse`:任务启动瞬间的编码器脉冲读数(用于 belt 原点);若启动时编码器无效,用 0 作为相对零点并告警。
- `bool m_firstFrame`:F4 "启动后第一帧全量丢弃"标志。
- `int m_nextTrackId = 1`。
- `int m_maskRasterMmPerPx`:栅格化分辨率,2 mm/px。

**关键子程序**:详见第 4 节算法伪代码。

### 3.6 `Dispatcher`(新增)

```cpp
class Dispatcher : public QObject {
    Q_OBJECT
public slots:
    void onSessionStart(const RuntimeConfig& cfg);
    void onSessionStop();
    void onSortTask(SortTask task);
    void onSpeedSample(SpeedSample s);
signals:
    void enqueuePulses(QVector<ValvePulse> pulses, int trackId); // → BoardWorker
    void cancelPulses(int trackId);
    void armStubDispatched(int trackId, int classId,
                           cv::Point2f posArmA, cv::Point2f posArmB);
    void warning(const QString& msg);
};
```

**内部状态**:
- `QMap<int /*trackId*/, SortTask> m_pending`:已排但未全部下发的喷阀任务。
- `QMap<int, QVector<ValvePulse>> m_lastPulses`:最近一次下发给 BoardWorker 的时间轴副本(用于速度重算时计算差分)。
- 模式切换 `SorterMode m_mode = Valve | Arm`。
- **不需要 m_tick**:时间轴交给 BoardWorker 执行,Dispatcher 只负责"预计算 + 重算"。

### 3.7 `tcpForRobot`(保留,本期仅记录)

本期不参与 ArmStub 路径;保留连接管理/UI 状态显示能力。Dispatcher 的 ArmStub 路径**不经过**此类,直接 `emit armStubDispatched` 给 MainWindow 落日志 + CSV(见第 7 节)。

---

## 4. 关键算法

### 4.1 像素 → belt 系栅格坐标

输入:
- `DetectedObject obj`(像素坐标)。
- `tCaptureMs`。
- 上下文:`m_originPulse`、`m_lastSpeed`、相机物理尺寸(`realLength_mm` 横向、`realWidth_mm` 纵向)、图像分辨率 `(W_px, H_px)`。

步骤:

```text
# 1. 像素 → 相机视野内物理坐标(mm)
xb0_mm = (obj.center.x / W_px) * realLength_mm
yb0_mm = (obj.center.y / H_px) * realWidth_mm     # 采图瞬间的 belt 系 y(相对原点)

# 2. 皮带位移外推
t_now = g_timer.elapsed()
Δy_mm = m_lastSpeed.speedMmPerMs * (t_now - tCaptureMs)
        若速度采样时间戳 < tCaptureMs,用 fallback 速度并告警

# 3. belt 系当前位置
xb_mm = xb0_mm
yb_mm = yb0_mm + (encoder_offset_at_capture - encoder_origin_mm) + Δy_mm

# 4. 栅格化
mm_per_px = cfg.pipeline.mask_raster_mm_per_px  # 默认 2
xb_rast_px = xb_mm / mm_per_px
yb_rast_px = yb_mm / mm_per_px
```

**mask 栅格化**:把像素 mask 按物理比例 `(realLength_mm/W_px, realWidth_mm/H_px)` → mm,再除以 `mm_per_px` 得到栅格图尺寸,用 `cv::resize(INTER_NEAREST)`。

### 4.2 belt 系 mask IoU 计算

两个 mask 都已存为"栅格图 + bbox(栅格 px)",其中 bbox 的 `(x, y)` 是**全局 belt 栅格坐标**。计算时:

1. 求 `bboxIntersect = bboxA & bboxB`(矩形交)。
2. 若 `bboxIntersect.empty()` → IoU = 0。
3. 从 A、B 的栅格图中各自裁出对应 bboxIntersect 区域。
4. `inter = countNonZero(A & B)`,`union_ = countNonZero(A) + countNonZero(B) - inter`。
5. `IoU = inter / union_`。

### 4.3 跨帧关联(贪心 IoU)

```text
candidates = []
for det in frame.objs:
    det_raster = rasterize_to_belt(det, frame.tCaptureMs)
    for trk in m_active:
        trk_extrapolated = extrapolate(trk, t_now = frame.tCaptureMs)  # 把 trk 的 bbox 按速度外推到"当前帧采图时刻"
        iou = mask_iou(det_raster, trk_extrapolated)
        if iou >= cfg.pipeline.iou_threshold:
            candidates.append((iou, det, trk))

# 贪心
sort candidates by iou desc
used_dets, used_trks = set(), set()
assigned = []
for (iou, det, trk) in candidates:
    if det in used_dets or trk in used_trks: continue
    assigned.append((det, trk))
    used_dets.add(det); used_trks.add(trk)

# 合并
for (det, trk) in assigned:
    trk.maskBeltRaster = det_raster.mask
    trk.bboxBeltRasterPx = det_raster.bbox
    trk.tCaptureMs = frame.tCaptureMs
    trk.updateCount += 1
    trk.missCount = 0
    trk.classAreaAcc[det.classId] += det.areaMm2

# 未匹配检测 → 查已分拣池抑制
for det in frame.objs:
    if det in used_dets: continue
    if any(mask_iou(det_raster, ghost) >= iou_threshold for ghost in m_ghosts):
        # 直接丢弃
        continue
    # 新建追踪
    t = TrackedObject(...)
    m_active.append(t)

# 未匹配追踪 → miss++
for trk in m_active:
    if trk not in used_trks: trk.missCount += 1

# 触发判定
for trk in m_active (safe copy):
    if trk.missCount >= cfg.pipeline.miss_frames_x or trk.updateCount >= cfg.pipeline.update_frames_y:
        final_class = argmax(trk.classAreaAcc)
        if final_class not in cfg.enabled_classes:
            m_active.remove(trk); continue
        emit sortTaskReady(build_sort_task(trk, final_class))
        m_ghosts.append(to_ghost(trk, final_class))
        m_active.remove(trk)

# 清理已分拣池
for ghost in m_ghosts (safe copy):
    yb_now = extrapolate_y(ghost, t_now = frame.tCaptureMs)
    if yb_now > sort_line_yb + cfg.pipeline.dispatched_pool_clear_distance_mm:
        m_ghosts.remove(ghost)
```

**注意:启动后第一帧**:`m_firstFrame` 为真时,在进入上述主流程之前直接 `m_firstFrame = false; return;`,所有该帧 detection 全部丢弃。

### 4.4 喷阀预计算

给定 `SortTask task`,生成 `QVector<ValvePulse>`:

配置:
- `valve.total_channels = 72`,`valve.boards = 8`,`valve.channels_per_board = 9`。
- `valve.x_min_mm` / `valve.x_max_mm`:喷阀覆盖的横向范围。`channel_width_mm = (x_max - x_min) / total_channels`。
- `valve.line_yb_mm`:喷阀线在 belt 系的 y(任务启动时由 `cfg.valve_distance_mm + img_bottom_yb_mm` 换算得出)。
- `valve.head_skip_ratio = 1/a`(默认 1/8)。
- `valve.open_duration_ms`(默认 50)。

算法:

```text
# 1. 预估物体通过喷阀线的时间窗口
#    约定:belt 运动方向为 +yb;bbox 的 y 字段是 trailing edge(离 valve 远),y+h 是 leading edge(先过 valve)。
speed = current_speed_mm_per_ms                      # 从 SpeedSample 取
mask = task.maskBeltRaster (栅格, mm/px = m)
bbox_yb_lead  = (task.bboxBeltRasterPx.y + h) * m    # 物体前沿 yb(leading edge,先到 valve)
bbox_yb_trail = task.bboxBeltRasterPx.y * m          # 物体后沿 yb(trailing edge,最后到 valve)

Δy_head = valve.line_yb_mm - bbox_yb_lead   # 前沿到喷阀线的剩余距离(≥0 表示还没到)
Δy_tail = valve.line_yb_mm - bbox_yb_trail
# task.tCaptureMs 为 belt 系快照时刻
t_head_arrive = task.tCaptureMs + Δy_head / speed    # 物体前沿抵达喷阀线
t_tail_arrive = task.tCaptureMs + Δy_tail / speed    # 物体后沿抵达喷阀线

# 2. 总面积
totalArea = countNonZero(mask)

# 3. 累积经过面积达 1/a 时开始
skip_area = totalArea * valve.head_skip_ratio        # 0.125 * totalArea
t_open_start = solve time s.t. 累积经过的 mask 面积 = skip_area
             # 具体做法:从前沿到某个 yb_cut,mask 在 yb ≤ yb_cut 区域的非零像素数 = skip_area
             # 再换算成时间

# 4. 滚动投影
# 在 [t_open_start, t_tail_arrive] 区间内,以 open_duration_ms 为步长采样,每步:
#   current_yb_line = valve.line_yb_mm
#   拿物体当前 mask(按速度平移后)与一条水平线 yb == line_yb 的交集,得一个横向区间
#   具体:在 belt 栅格图中取"物体穿过喷阀线位置"所在一行的像素集合
#   → 这些像素的 xb 范围 → 映射到通道索引集合
#   → 按 board 分组 → 每块 board 得一个 channel_mask(9 位)

for t = t_open_start; t < t_tail_arrive; t += valve.open_duration_ms:
    line_xb_set = columns of mask where mask 在物体穿过 valve line 的那一行 非零
    # 由于物体随时间移动,不是 mask 的某一固定行,而是
    # "该时刻喷阀线对应物体坐标系里的哪一行"
    relative_y_in_obj = (valve.line_yb_mm - obj_front_yb_at_t) in obj local mask
    row = mask.row(relative_y_in_obj / m)    # 栅格行
    active_cols = np.where(row > 0)[0]
    # cols → belt 系 xb:xb = bbox_x_start + col*m
    # xb → channel_idx = floor((xb - valve.x_min_mm) / channel_width_mm)
    channel_set = {channel_idx(xb) for xb in cols}
    # 分组
    for board in 1..8:
        ch_in_board = {c for c in channel_set if (c-1)//9 + 1 == board}
        if not empty:
            mask_bits = sum of (1 << ((c-1) % 9)) for c in ch_in_board
            pulses.append(ValvePulse{
                tOpenMs = t,
                tCloseMs = t + valve.open_duration_ms,
                boardId = board,
                channelMask = mask_bits
            })

# 5. 速度重算时:
#   Dispatcher 对比"新速度"与"预计算时的速度",|Δ|/|old| > speed_recalc_threshold_pct 触发
#   → emit cancelPulses(trackId) 给 BoardWorker
#   → 按当前 yb_now(用新速度外推)重新算 Δy_head,重跑上述流程
#   → emit enqueuePulses
```

**边界约束**:
- `min_cmd_interval_ms`:相邻两条 pulse(同一 board)的 `tOpenMs` 差 < 该值时,合并(取时间靠前的 open,取时间靠后的 close)。
- 若 `t_open_start > t_tail_arrive`:物体太小,头部留白后已经过线,不产生任何 pulse,告警 `object_too_small`。

### 4.5 机械臂 stub

仅计算 `posArmA` / `posArmB`(本期公式占位,按 belt 系直接赋值即可),`emit armStubDispatched`。具体标定公式待后续接入物理机械臂时再定。

---

## 5. 配置文件 schema(完整)

位置:`/opt/qinglv/config.ini`(沿用现 `QSettings` 路径);重启进程生效(D5 方案 a)。

```ini
; ============================================================================
; Camera
; ============================================================================
[camera]
ip                       = 192.168.1.30
hw_fps                   = 60
y_flip                   = false       ; 皮带运动方向与图像 y 递增方向一致为 false
real_length_mm           = 560         ; 图像横向(x)对应的物理宽度
real_width_mm            = 470         ; 图像纵向(y)对应的物理长度(图像内)

; ============================================================================
; YOLO Model & Filters
; ============================================================================
[model]
path                     = /opt/models/yolov8seg.rknn
input_size               = 1024
; 品类按钮定义:序号从 1 开始递增,name 为中文可读名,id 为模型 class_id
class_btn/size           = 2
class_btn/1/name         = 苹果
class_btn/1/id           = 0
class_btn/2/name         = 橙子
class_btn/2/id           = 1

[yolo]
conf_threshold           = 0.25
nms_threshold            = 0.45

; ============================================================================
; Pipeline
; ============================================================================
[pipeline]
soft_fps                 = 2
iou_threshold            = 0.30
miss_frames_x            = 2
update_frames_y          = 10
dispatched_pool_clear_distance_mm = 200
mask_raster_mm_per_px    = 2
tick_interval_ms         = 5          ; BoardWorker 阀轮询间隔

; ============================================================================
; Belt & Encoder
; ============================================================================
[belt]
nominal_speed_m_s        = 0.5         ; fallback 速度
; master 口径: raw(u16 大端) 即板卡采样好的"瞬时转速代理",
; speed_m_per_min = raw * encoder_raw_to_m_per_min (master 实测 0.502),
; speed_mm_per_ms = m/s = m_per_min / 60。不需要差分 / 窗口除法。
encoder_raw_to_m_per_min = 0.502
encoder_request_interval_ms = 500

; ============================================================================
; Sorter
; ============================================================================
[sorter]
mode                     = valve       ; valve | arm
arm_distance_mm          = 500         ; 机械臂距图像最近边缘
valve_distance_mm        = 800         ; 喷阀距图像最近边缘

; ============================================================================
; Valve (喷阀)
; ============================================================================
[valve]
total_channels           = 72
boards                   = 8
channels_per_board       = 9
x_min_mm                 = 0           ; 喷阀覆盖的横向起点(belt 系 x)
x_max_mm                 = 560
head_skip_ratio          = 0.125       ; 1/a
open_duration_ms         = 50
min_cmd_interval_ms      = 50
speed_recalc_threshold_pct = 20

; ============================================================================
; Arm Stub (本期仅日志)
; ============================================================================
[arm_stub]
a_origin_x_mm            = 0
a_origin_y_mm            = 0
a_x_sign                 = 1
a_y_sign                 = 1
b_origin_x_mm            = 0
b_origin_y_mm            = 0
b_x_sign                 = 1
b_y_sign                 = 1

; ============================================================================
; Persistence
; ============================================================================
[persistence]
save_raw                 = false
raw_sample_ratio         = 0.1
save_result              = false
save_dir                 = /data/captures
arm_stub_csv             = /data/logs/arm_stub.csv
```

**运行时读取**:`MainWindow::buildRuntimeConfig()` 从 `QSettings` 读所有字段组装成 `RuntimeConfig` 结构体(POD),通过 `sessionStart` 信号下发给所有 worker。worker 本地持有快照,启动后不回读文件。

---

## 6. 串口子系统重构

### 6.1 协议规格确认(来自 E1 阅读)

**批量阀控帧(11 字节)**:

```
AA 55 | 11 06 02 | B6 B7 B8 B9 | CHK | 55 AA
```

| 字段 | 说明 |
|---|---|
| `B6` | 板号 1..8 |
| `B7` | 开帧 = `bitCount(B8) + bitCount(B9)`;关帧 = `0x09` |
| `B8` | 通道位图高字节(bit0 对应通道 9,即一块 9 通道板的第 9 位) |
| `B9` | 通道位图低字节(bit0..bit7 对应通道 1..8) |
| `CHK` | `(0x11 + 0x06 + 0x02 + B6 + B7 + B8 + B9) & 0xFF` |

**关帧**:同一板号,`B7=0x09, B8=0x00, B9=0x00`(关闭该板所有通道)。

**编码器请求/响应帧**:
- 请求:`AA 55 11 03 03 0A 21 55 AA`(9 字节)
- 响应头:`AA 55 11 05 03 0A`,11 字节,以 `55 AA` 结尾。

### 6.2 保留 / 废弃 / 新增

| 函数 / 成员 | 处置 |
|---|---|
| `openSerial` / `closeSerial` | **保留** |
| `writeFrame(frame, speedOrJet)` | **简化**:去掉 `speedOrJet` 参数,统一串口互斥;去掉 `tcdrain` 前的注释冗余 |
| `readFrame` | **保留** |
| `buildControlFrame` / `buildSingleControlFrame` | **删除**(单控协议本期不用) |
| `buildBatchOpenFrame` / `buildBatchCloseFrame` / `buildBatchFrameCore` | **保留** |
| `countBits` | **保留** |
| `singleBoardControl(QString)` | **删除**(同步阻塞 50 ms,不适用) |
| `batchBoardControl(QString)` | **删除**(同上) |
| `requestEncoderSpeed` | **保留**,但不再与阀命令抢同一把 `m_serialMutex` |
| `speedRead` 标志位 | **删除**(由统一的 `m_busMutex` 串行化读写) |
| `m_valveBusySet` | **删除**(忙碌判定交给 Dispatcher/Tracker 层) |
| `busy` 成员 | **删除** |
| `m_fd1` / fallback 双文件描述符 | **删除**(fallback 打开逻辑保留但只用一个 `m_fd`) |

**新增接口**:

```cpp
// BoardWorker 新增
public slots:
    void onEnqueuePulses(const QVector<ValvePulse>& pulses, int trackId);
    void onCancelPulses(int trackId);

private:
    void tick();                          // 5ms QTimer
    bool sendOpenFrame(quint8 board, quint16 channelMask);
    bool sendCloseFrame(quint8 board);
    QMap<int /*trackId*/, QVector<ValvePulse>> m_pendingByTrack;
    QMutex m_busMutex;                    // 保护所有串口读写
    QTimer* m_tick = nullptr;             // pulse 调度
    QTimer* m_encoderTick = nullptr;      // 编码器请求
```

`tick` 的职责在 3.4 已述,此处补充两点:

1. 同一个 tick 内可能同时触发 A 板的开帧和 B 板的开帧,**串行**写串口,间隔就是 `write` 自身耗时(~几百 μs);不再 `msleep(50)` 堵塞。
2. `sendOpenFrame`/`sendCloseFrame` 调用 `m_busMutex.lock() → write → m_busMutex.unlock()`,锁的粒度是**单帧**,不跨开/关。

### 6.3 编码器与阀命令的总线仲裁

两者都经过 `m_busMutex`,但顺序是"FIFO 到锁上":
- `tick()` 每 5 ms 触发一次阀命令序列,每帧各自抢锁。
- `m_encoderTick` 每 500 ms 触发,抢同一把锁,调用 `writeFrame + readFrame`。

阀命令的 `write` 耗时远小于 1 ms,不会阻塞编码器 > 5 ms;编码器 `readFrame(50)` 最多阻塞 50 ms 等响应,这段时间内的阀命令会**排队在锁上等待**,本期可接受(1 s 窗口,fps=2 下业务最小间隔 500 ms,影响可忽略)。后续若实测抖动超标,再拆两条 `QTimer` 用 `QQueue<QByteArray>` 合并调度。

---

## 7. 错误处理与日志

### 7.1 告警分级

| 级别 | 用例 | 行为 |
|---|---|---|
| ERROR | 相机打开失败(启动时)、模型加载失败 | 阻断任务启动;日志 `[ERROR] <source> <msg>`;状态栏红色 |
| WARN | 编码器读不到、速度用 fallback、帧丢弃、阀预计算物体过小 | 任务继续;日志 `[WARN] ...`;状态栏黄色;节流后只显示最近一条 |
| INFO | 任务启动/停止、每次 sortTask 触发、ArmStub 派发 | 只进日志 |

所有 worker 的 `warning` 信号统一连到 `MainWindow::onWarning(source, msg)`;状态栏 1 秒内合并展示,避免刷屏。

### 7.2 日志文件

沿用现 `logger.h/cpp`(LOG_INFO / LOG_WARN / LOG_ERROR)。

**新增**:ArmStub CSV(F14.1 + D7):

```csv
# /data/logs/arm_stub.csv
# 字段:ts_ms, track_id, class_id, arm_a_x_mm, arm_a_y_mm, arm_b_x_mm, arm_b_y_mm
1234567, 17, 0, 125.3, 480.0, -98.1, 480.0
```

CSV 路径由 `persistence.arm_stub_csv` 指定;每天一个文件(按 `YYYYMMDD` 滚动)。

### 7.3 丢帧统计

`CameraWorker` 在节流时丢弃的帧**不计告警**(本来就要丢)。
`YoloWorker::droppedBusy` 视为反压丢帧,按 F10 要求:每次 `emit warning` + 维护计数器,每 10 秒汇总一次 `LOG_WARN` 打印累计丢帧数。

---

## 8. 废弃代码清单(按文件粒度)

| 文件 | 处理 |
|---|---|
| `yolothread.h/cpp` | **删除**(git 已 D) |
| `yoloresulttypes.h` | **删除**(git 已 D) |
| `ConveyorTracker.h/cpp` | **删除**(基于时间的任务调度由 Dispatcher 取代) |
| `caldistance.h/cpp` | **删除**(坐标变换移到 TrackerWorker,喷阀预计算移到 Dispatcher) |
| `mainwindow.cpp` | **大幅删改**,详见下方 "mainwindow 内联清单" |
| `mainwindow.ui` | 机械臂分段延时标定面板整块删除;新增运行参数区域(皮带速度/fps/两个距离/分拣器 radio) |
| `valvecmd.h` | **保留**,但 `Task` 结构被 `ValvePulse` + `SortTask` 取代;`Task` 可删 |
| `yolorecognition.cpp/h` | **保留骨架**,按 3.3 重构 |
| `boardcontrol.cpp/h` | **保留骨架**,按 6.2 清单逐项改 |
| `tcpforrobot.cpp/h` | **保留**,本期 stub 路径不经过它 |
| `camerathread.cpp/h` | **保留骨架**,按 3.2 重构;`captureIntervalMs = 0` 改为节流间隔 |
| `logger.h/cpp` | **保留** |
| `UpdateManager.*` | **保留**(与本改造无关) |
| `postprocess.cpp` | **保留**(NMS/阈值由参数传入,不再 `#define`) |

**mainwindow 内联删除清单**:
- `on_u550_clicked` / `on_d550_clicked` ... `on_u330_clicked` / `on_d330_clicked`(约 20 个槽,机械臂分段延时标定)
- `getAndsendA`(L1005–L1158,硬编码 l0~l330)
- `doTask`(旧的 Task 派发)
- 3 份 tab 按钮 QSS 重复块 → 合并为一份
- `on_run_clicked` / `on_reset_clicked` 中已注释的块 → 直接删
- `retryUploadFailedImages` / OSS 相关已注释块 → 删除
- `chan1_chan` / `on_chan1_clicked` 的"连接机械臂"按钮 → 保留 UI 连接状态显示,但不与 stub 路径关联

---

## 9. 分步落地计划

建议拆成 **5 个独立 commit / PR**,每步都可以单独验证,回滚成本低:

### PR1 — 基础设施 & 废弃代码清理
- 删除 `yolothread.*` / `yoloresulttypes.h`(已 D,补提交)、`ConveyorTracker.*` / `caldistance.*`。
- `mainwindow` 移除约 20 个机械臂标定槽、`getAndsendA`、旧 `doTask`、各注释块、重复 QSS。
- 引入 `include/pipeline_types.h`(仅类型定义,不使用)。
- 引入全局 `QElapsedTimer g_timer`。
- 引入 `RuntimeConfig` 结构体 + `MainWindow::buildRuntimeConfig()` 骨架。

**验证**:编译通过、现有相机+YOLO 功能不回归。

### PR2 — CameraWorker + YoloWorker 重构
- `CameraWorker`:实现软件节流、`tCaptureMs` 打戳、`frameReady` 新签名。
- `YoloWorker`:模型常驻;新 `onFrame` 槽;单元素反压队列;`frameInferred` + `visualizeReady` 信号;`conf_threshold` / `nms_threshold` 从配置读(作为 `post_process_seg` 参数传入)。
- `MainWindow` 连接新信号到一个临时"可视化+空实现"的日志 slot,验证 YOLO 仍工作。

**验证**:UI 显示 box+mask+类别标签;手动观察帧率 ≈ fps;反压下日志有 droppedBusy。

### PR3 — BoardWorker 重构
- 按 6.2 清单:删除 `single/batchBoardControl` / `speedRead` / `m_valveBusySet` / `busy` 等。
- 新增 `onEnqueuePulses` / `onCancelPulses` / 5ms `tick`。
- 编码器周期性 `speedSample` 信号。
- 用一个临时"固定序列"的测试 hook(比如按下 UI 上的"Test Pulses"按钮,发一条覆盖多板的 ValvePulse 队列)验证硬件反应正常。

**验证**:编码器速度 UI 正常显示;测试 hook 触发时,多板阀门按预期开关。

### PR4 — TrackerWorker + Dispatcher
- 实现 4.1~4.3 追踪算法。
- 实现 4.4 喷阀预计算。
- Dispatcher 的速度重算分支。
- ArmStub 分支:`emit armStubDispatched` + CSV 落盘。
- MainWindow Session 状态机:`sessionStart` / `sessionStop` 信号广播,worker 自己 guard。

**验证**:在现场放一系列物体,观察 valve 开阀时机;日志里 sort_task 触发时间对得上物体位置。

### PR5 — UI 整理
- 新增运行参数区域(皮带速度/fps/两个距离/分拣器 radio)。
- 品类按钮从配置动态加载。
- F4 启停按钮与 Session 状态机联动;参数控件 Running 时只读。
- 告警汇聚到状态栏(第 7 节)。
- 禁用 4 组"颜色/标签/形态/品相"按钮的业务联动(只保留视觉)。

**验证**:跑完整 AC1~AC19 的验收剧本。

---

## 10. 附录

### A. 模块类/文件对应表

| 新/改 | 类 | 文件 |
|---|---|---|
| 改 | `MainWindow` | `mainwindow.{h,cpp,ui}` |
| 改 | `CameraWorker`(现 `cameraThread`) | `camerathread.{h,cpp}` |
| 改 | `YoloWorker`(现 `yolorecognition`) | `yolorecognition.{h,cpp}` |
| 改 | `BoardWorker`(现 `boardControl`) | `boardcontrol.{h,cpp}` |
| 新 | `TrackerWorker` | `tracker_worker.{h,cpp}` |
| 新 | `Dispatcher` | `dispatcher.{h,cpp}` |
| 新 | pipeline types | `pipeline_types.h` |
| 新 | 运行时配置 | `runtime_config.{h,cpp}` |

### B. 长期演进(D1 方案 B,不在本期)

下一阶段可考虑:
- 把 `CameraWorker` / `YoloWorker` / `TrackerWorker` / `Dispatcher` / `BoardWorker` 挪到独立 `src/pipeline/` 子目录,`MainWindow` 只持有 `Pipeline` facade。
- 引入 gtest + 纯算法单测(mask IoU、贪心关联、喷阀预计算)。
- `BoardWorker` 拆出 `SerialBus`(底层)与 `ValveProtocol`(协议层),为机械臂 TCP 走相同的 Bus 抽象铺路。
- `config.ini` 切到 YAML/JSON,支持 schema 校验。

### C. 硬件响应(E1 追问)

本设计按"阀命令无 ACK"假设(方案 a)。如果后续实测发现硬件会回 ACK:
- `BoardWorker::sendOpenFrame` 内部改为异步发送 + ACK 超时等待(可配置 `valve.ack_timeout_ms`,默认 0 即不等)。
- 丢失 ACK 时 emit `warning`,但不重发(避免阀门动作被重复触发)。

---

## 文档状态

- **版本**:v1.0
- **依赖**:`requirements.md` v1.1
- **状态**:待用户确认
