# YOLO 模型接入说明

本文档说明如何在当前项目中使用 `models` 目录下的 YOLO 线程能力，包括：

- 编译依赖
- 线程接口说明
- 调用方法（同步/异步）
- 测试程序使用方法

> 说明：当前实现是独立模型线程，暂未自动接入主业务流程。

## 1. 目录结构

`models` 目录中与接入相关的核心文件：

- `models/yolothread.h`
- `models/yolothread.cpp`
- `models/yoloresulttypes.h`
- `models/examples/postprocess.h`
- `models/examples/postprocess.cc`
- `models/examples/yolo_thread_test.cpp`

## 2. 编译依赖

模型线程基于以下依赖：

- RKNN Runtime（`rknn_api.h`，`librknnrt.so`）
- OpenCV（图像预处理与 mask 轮廓提取）
- Qt Core + Qt Gui（`QThread`、`QImage`、信号槽）
- C++14

### 2.1 Linux 常见依赖示例

根据开发机环境安装基础依赖（示例）：

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config qtbase5-dev libopencv-dev
```

RKNN Runtime 通常由板卡 SDK 提供，请确保：

- 头文件可见：`rknn_api.h`
- 动态库可链接：`librknnrt.so`
- 运行时可加载到该动态库（可通过 `LD_LIBRARY_PATH` 或系统库路径配置）

## 3. 线程能力说明

`yolothread` 在启动后会加载模型，并在同一线程内执行推理，内部使用互斥锁保证并发安全。

### 3.1 线程启动时加载模型

- 调用 `start()` 后，`run()` 会加载模型
- 结果通过信号返回：`modelReadySig(bool success, const QString& errorMessage)`

### 3.2 函数一：修改输出类别

接口：

```cpp
bool setEnabledClasses(const QList<int>& classIds);
```

约束：

- 模型默认支持 9 类（类别 ID：`0~8`）
- `classIds` 需为 `0~8` 的子集
- 空列表或越界 ID 会返回 `false`

### 3.3 函数二：执行预测

接口：

```cpp
YoloTaskResult predict(quint64 taskId, const QImage& image);
```

返回 `YoloTaskResult`，包含：

- `taskId`：原样返回任务 ID
- `success`：预测是否成功
- `errorMessage`：失败原因
- `objects`：图中所有目标列表，每个目标含：
  - `classId`
  - `score`
  - `segmentation`（相对坐标，范围 `[0,1]`）
  - `center`（相对坐标，范围 `[0,1]`）

## 4. 图像输入与坐标约定

- 输入图片尺寸不固定，内部会自动进行 letterbox 适配到模型输入尺寸（通常 `640x640`）
- 输出 segmentation 与中心点均为相对坐标（相对原图宽高归一化）

## 5. 调用方式

支持两种方式：

- 同步调用（直接调用成员函数）
- 异步调用（通过信号槽跨线程）

### 5.1 同步调用示例

```cpp
#include "models/yolothread.h"

yolothread* yolo = new yolothread("/path/to/model.rknn");
yolo->start();

// 建议先监听 modelReadySig 确认模型已加载成功
bool ok = yolo->setEnabledClasses({0, 2, 5});
QImage img("/path/to/test.jpg");
YoloTaskResult result = yolo->predict(1001, img);

yolo->stop();
delete yolo;
```

### 5.2 异步调用示例

线程类提供异步槽函数：

- `setEnabledClassesAsync(const QList<int>& classIds)`
- `predictAsync(quint64 taskId, const QImage& image)`

对应结果信号：

- `setEnabledClassesDoneSig(bool success)`
- `predictDoneSig(const YoloTaskResult& result)`

调用方可通过 `Qt::QueuedConnection` 与模型线程通信。

## 6. 测试程序

示例测试文件：`models/examples/yolo_thread_test.cpp`

最小业务接入参考：`models/examples/minimal_integration_example.cpp`

覆盖内容：

- 模型线程启动与加载
- 修改输出类别
- 模型预测
- 线程间通信调用函数一和函数二

### 6.1 构建测试（CMake）

在 `models/examples` 下：

```bash
cmake -S . -B build
cmake --build build -j4
```

### 6.2 运行测试

```bash
./build/yolo_thread_test <model_path.rknn> <image_path>
```

程序返回：

- `0`：测试通过
- 非 `0`：存在失败项，请根据日志排查

## 7. 常见问题排查

- `rknn_init failed`：
  - 模型文件路径错误
  - RKNN Runtime 版本与模型不匹配
- `rknn_outputs_get failed`：
  - 输入预处理与模型预期不一致
  - 运行时环境或驱动异常
- 无检测结果：
  - 图片内容不含可识别目标
  - 类别过滤列表未包含目标类别

## 8. 注意事项

- 当前实现未自动接入 `mainwindow` 等主流程，需要业务层自行决定接入点
- 线程析构前请调用 `stop()`，确保资源安全释放
- 如需高吞吐并发推理，建议按业务场景评估：
  - 单线程串行任务队列
  - 多模型实例并行（每实例独立上下文）
