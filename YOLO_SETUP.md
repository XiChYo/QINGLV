# YOLO 目标检测集成指南

本分支 (`feature/yolo`) 添加了 YOLO 目标检测功能，使用 ONNX Runtime 进行推理。

## 功能特性

- ✅ 支持 ONNX 格式的 YOLO 模型 (YOLOv5/v8/v10)
- ✅ 实时目标检测
- ✅ 可配置的置信度阈值和 NMS 阈值
- ✅ 与现有相机采集线程集成
- ✅ 支持 COCO 80 类物体检测

## 安装依赖

### Ubuntu/Debian

```bash
# 方法 1: 使用包管理器 (推荐)
sudo apt update
sudo apt install libonnxruntime-dev

# 方法 2: 手动安装 ONNX Runtime
# 下载：https://github.com/microsoft/onnxruntime/releases
wget https://github.com/microsoft/onnxruntime/releases/download/v1.18.0/onnxruntime-linux-x64-1.18.0.tgz
tar -xzf onnxruntime-linux-x64-1.18.0.tgz
sudo cp -r onnxruntime-linux-x64-1.18.0/lib/* /usr/lib/
sudo cp -r onnxruntime-linux-x64-1.18.0/include/* /usr/include/onnxruntime/
```

### 下载 YOLO 模型

```bash
# 创建模型目录
mkdir -p models

# 下载 YOLOv8n (最小版本，适合实时检测)
# 从 Ultralytics 导出或使用预转换的 ONNX 模型
wget https://github.com/ultralytics/assets/releases/download/v0.0.0/yolov8n.onnx -O models/yolo.onnx

# 或使用其他 YOLO 版本
# yolov8s.onnx - small
# yolov8m.onnx - medium
# yolov8l.onnx - large
```

## 编译项目

```bash
cd QINGLV

# 使用 qmake
qmake guangxuan.pro
make -j$(nproc)

# 或使用 Qt Creator 打开项目并构建
```

## 配置

编辑 `config.ini`:

```ini
[YOLO]
enabled=true              # 启用 YOLO 检测
modelPath=models/yolo.onnx  # 模型路径
confThreshold=0.5         # 置信度阈值 (0-1)
nmsThreshold=0.4          # NMS 阈值 (0-1)
inputWidth=640            # 模型输入宽度
inputHeight=640           # 模型输入高度
```

## 使用示例

### 在代码中使用

```cpp
#include "yolodetector.h"

// 创建检测器
YoloDetector detector;

// 初始化模型
if (!detector.initialize("models/yolo.onnx", 0.5f, 0.4f)) {
    qDebug() << "Failed to load YOLO model:" << detector.getLastError();
    return;
}

// 检测图像
QImage image = ...; // 从相机获取的图像
QVector<DetectionResult> results = detector.detect(image);

// 处理结果
for (const auto& det : results) {
    qDebug() << "Detected:" << det.className 
             << "Confidence:" << det.confidence
             << "Box:" << det.boundingBox;
    
    // 在图像上绘制检测结果
    // ...
}
```

### 集成到相机线程

修改 `camerathread.cpp` 在 `frameReadySig` 发出前进行 YOLO 检测：

```cpp
// 在 camerathread.h 中添加
#include "yolodetector.h"
YoloDetector* m_yoloDetector = nullptr;

// 在 run() 方法中
QVector<DetectionResult> detections = m_yoloDetector->detect(img);

// 发送带检测结果的通知
emit frameReadySig(img, fileName, detections);
```

## COCO 类别列表

支持检测 80 类常见物体：

- 人员、车辆 (自行车、汽车、摩托车、公交车等)
- 动物 (鸟、猫、狗、马等)
- 日常物品 (瓶子、杯子、椅子、桌子等)
- 食物 (香蕉、苹果、三明治等)
- 电子产品 (笔记本、手机、键盘等)

完整列表见 `yolodetector.cpp` 中的 `m_classNames`。

## 自定义模型

可以使用自己的数据集训练 YOLO 模型并导出为 ONNX 格式：

```python
from ultralytics import YOLO

# 加载预训练模型
model = YOLO('yolov8n.pt')

# 使用自定义数据集训练
model.train(data='your_dataset.yaml', epochs=100)

# 导出为 ONNX
model.export(format='onnx')
```

## 性能优化

- 使用较小的模型 (YOLOv8n) 获得更快的推理速度
- 调整 `confThreshold` 减少误检
- 在 GPU 上运行需要 CUDA 版本的 ONNX Runtime
- 降低输入分辨率可提升速度但可能影响精度

## 故障排除

### "Failed to get ONNX Runtime API"
- 确保已安装 `libonnxruntime-dev`
- 检查库路径是否在 `LD_LIBRARY_PATH` 中

### "Model file not found"
- 确认模型文件存在于指定路径
- 检查文件权限

### 检测速度慢
- 使用更小的模型 (YOLOv8n)
- 降低相机帧率或检测频率
- 考虑使用 GPU 加速

## 下一步计划

- [ ] 在 UI 中显示检测结果
- [ ] 支持自定义类别过滤
- [ ] 添加检测结果统计
- [ ] 支持多模型切换
- [ ] GPU 加速支持

## 分支信息

- **分支**: `feature/yolo`
- **创建时间**: 2026-03-11
- **依赖**: ONNX Runtime >= 1.16.0
