# 技术方案草稿(已归档,仅作历史留存)

> **状态**:Phase 4 已完成,正式技术方案见 `design.md`。本文件内容已全部被吸收,不再维护。保留原文仅用于追溯讨论演进过程。

---

---

## 1. 模块与线程规划(草稿)

建议保留/调整的运行期对象:

| 对象 | 所在线程 | 说明 |
|---|---|---|
| `MainWindow` | 主线程 | UI + 状态栏 + 配置读写 |
| `cameraThread` | 独立 QThread | 相机采图 + 软件节流 |
| `yoloRecognition` | 独立 QThread | 常驻 RKNN session,接收帧、输出识别结果 |
| `tracker`(新) | 独立 QThread 或与 yolo 合并 | 跨帧追踪与合并 |
| `dispatcher`(新) | 独立 QThread | 根据配置路由到机械臂 stub / 喷阀控制 |
| `boardControl` | 独立 QThread | 串口阀控 + 编码器 |
| `tcpForRobot` | 独立 QThread | 机械臂 TCP(本期仅 stub 打点) |

线程间用 signal/slot + `QueuedConnection` + 值拷贝传参,**禁止跨线程直接函数调用**。

---

## 2. 数据结构(草稿)

### 帧内识别结果

```cpp
struct DetectedObject {
    int classId;
    float confidence;
    cv::Rect bboxPx;      // 图像像素坐标
    cv::Mat maskPx;       // 图像像素坐标下的二值 mask
    qint64 captureTsMs;   // 采图时间戳
    qint64 inferDoneTsMs; // 推理完成时间戳
};
```

### 追踪物体

```cpp
struct TrackedObject {
    int trackId;
    cv::Mat latestMaskBelt;         // 皮带固定坐标系下的 mask(最近一次)
    cv::Point2f latestCenterBeltMm; // 皮带坐标系下最新几何中心(mm)
    float latestAreaMm2;
    int missCount;
    int updateCount;
    QHash<int, float> classAreaAcc; // classId -> 累计加权面积
    qint64 lastUpdateTsMs;
};
```

### 分拣任务

```cpp
struct SortTask {
    int trackId;
    int finalClassId;
    cv::Mat maskBelt;
    qint64 createTsMs;
    // 喷阀:预计算的开阀脉冲序列(阀索引、开启时刻、持续时长)
    // 机械臂:在 A/B 坐标系下的目标点
};
```

---

## 3. 坐标变换公式(草稿)

### 3.1 像素 → 皮带固定坐标系

```
x_belt_mm = (x_px / W) * realWidth_mm                  // 横向,与皮带运动方向正交
y_belt_mm(t_now) = (y_px / H) * realHeight_mm + offset // 采图瞬间在皮带坐标系下的 y
                 + v_belt * (t_now - t_capture)        // 延迟内位移,t_now 用调用瞬间系统时间
```

- `offset`:任务启动瞬间的编码器累计位移零点。
- `v_belt`:实时编码器速度;读不到则用配置的标称速度,并告警。
- `t_capture` / `t_now` 均由 `QElapsedTimer`/`std::chrono::steady_clock` 提供,消除经验补偿项。

### 3.2 皮带坐标 → 机械臂 A/B 坐标

- 仅做**平移 + 可选的轴向反转**,具体标定参数待 Phase 4。

---

## 4. 跨帧追踪算法(草稿)

- 帧到来时,对每个 `DetectedObject`,将其 mask 从像素坐标变换到皮带固定坐标系。
- 与所有活跃 `TrackedObject` 计算 mask IoU(在皮带坐标系下,按固定 mm/像素分辨率栅格化)。
- **贪心最大 IoU**:每轮选出全局 IoU 最大的一对关联,移除后重复直到无对 IoU ≥ 阈值。
- IoU ≥ 阈值 → 合并;否则 → 新建追踪。
- 本帧未被任何新检测关联到的追踪物体:`missCount++`。
- `missCount ≥ x` 或 `updateCount ≥ y` → 出队,进入分拣派发。
- 派发后该 TrackedObject 的最近 mask 进入"已分拣池",继续随皮带平移坐标,供后续帧做抑制匹配。
- 已分拣池 mask 越过喷阀线 + 配置距离后移除。
- 任务启动后第一帧:所有检测直接丢弃(实现上维持一个 `is_first_frame` 标志位)。

---

## 5. 喷阀预计算(草稿)

触发分拣时,对该物体:

1. 计算物体 mask 在喷阀横线(Y = 喷阀距最近边缘 + 图像内偏移)上的**投影区间序列**——即"物体的前沿到后沿,每推进 Δy,对应的横向覆盖区间"。
2. 将每个时刻的投影区间映射成阀通道位图。
3. 由皮带当前速度,把"推进 Δy"换算成"时间 Δt",生成开阀指令时间轴。
4. "头部留白":累计经过面积达到总面积 `1/a` 才开始第一条开阀指令。
5. "尾部保留":直到物体后沿越过喷阀线才关最后一组阀。
6. 连续两条命令间隔强制 ≥ 当前喷阀开启时长(可配置)。
7. 运行中若速度变化超阈值:丢弃剩余未发出的指令序列,重新从当前物体位置预计算。

---

## 6. 明确要废弃/删除的现有代码

| 模块 / 代码 | 处理 |
|---|---|
| `yolothread.h/cpp` | 删除(已 git D) |
| `yoloresulttypes.h` | 删除(已 git D) |
| `mainwindow.cpp` 中机械臂分段延时标定面板(`on_uXXX_clicked` / `on_dXXX_clicked` 共 ~20 个槽函数) | 删除 |
| `mainwindow.cpp` 中 `getAndsendA`(硬编码 l0~l330 区间延时) | 删除,由 dispatcher 取代 |
| `calDistance::distance` 当前被注释的主路径 | 重写(移入 dispatcher/tracker) |
| `ConveyorTracker` 基于时间的任务调度 | 废弃,由喷阀预计算时间轴取代 |
| 多份 tab 按钮 QSS 重复块 | 合并成一份 |
| `yolorecognition.cpp` 中每帧 `init_model_session` / `release_session` | 移到启动一次 |
| `yolorecognition.cpp` 中每帧 `cv::imwrite` 调试图 | 受 F16 开关控制 |
| `camerathread` 中 `captureIntervalMs = 0` | 改为按 fps 节流 |

---

## 7. 配置项清单草稿

```ini
[camera]
ip=192.168.1.30
hw_fps=60

[yolo]
conf_threshold=0.25
nms_threshold=0.45

[pipeline]
soft_fps=2
iou_threshold=0.3
miss_frames_x=2
update_frames_y=10
dispatched_pool_clear_distance_mm=200

[belt]
nominal_speed_m_s=0.5

[sorter]
mode=valve  ; valve | arm
arm_distance_mm=500
valve_distance_mm=800

[valve]
total_channels=72
groups=6
head_skip_ratio=8           ; 对应 1/a 的分母
min_cmd_interval_ms=50
speed_recalc_threshold_pct=20

[arm_stub]
a_offset_x_mm=0
a_offset_y_mm=0
b_offset_x_mm=0
b_offset_y_mm=0

[model]
path=/opt/models/yolov8seg.rknn
; class buttons (前端 → model class_id 映射)
class_btn_1_name=苹果
class_btn_1_id=0
class_btn_2_name=橙子
class_btn_2_id=1

[persistence]
save_raw=false
raw_sample_ratio=0.1
save_result=false
save_dir=/data/captures
```

---

## 8. 非代码类补充记录

- 机械臂坐标系标定参数的字段结构待 Phase 4。
- 喷阀每通道对应的横向区间映射表的存储格式待 Phase 4(目前倾向于 `channel_i_x_start_mm` / `channel_i_x_end_mm`,或独立 CSV)。
- 编码器脉冲→mm 系数来源待 Phase 4。
