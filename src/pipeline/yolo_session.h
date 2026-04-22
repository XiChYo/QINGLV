#ifndef YOLO_SESSION_H
#define YOLO_SESSION_H

// ============================================================================
// YoloSession:RKNN YOLOv8-Seg 常驻会话 + 一次推理的纯函数接口。
// 与 QObject / signal 解耦,便于在 YoloWorker(或未来的测试代码)里复用。
// 替代旧的 yolorecognition.{h,cpp} 里嵌在成员函数里的实现。
// ============================================================================

#include "pipeline/postprocess.h"
#include <rknn_api.h>
#include <opencv2/core.hpp>
#include <vector>

struct YoloSession
{
    rknn_context ctx = 0;
    unsigned char* model_data = nullptr;
    int model_data_size = 0;
    rknn_input_output_num io_num {};
    rknn_tensor_attr input0_attr {};
    std::vector<rknn_tensor_attr> output_attrs;
};

// 初始化:载入模型文件,创建 context,查询 IO 属性。失败返回 false,内部已清理。
bool yolo_session_init(const char* model_path, YoloSession& s);

// 释放。幂等。
void yolo_session_release(YoloSession& s);

// 一次推理。输入 BGR 图(orig_img)与阈值参数;输出 SegObject 列表(像素坐标)。
// topk_class_count 仅在模型 top-k 布局时用作类别总数兜底。
// 推理失败返回 false。results 先清空再追加。
bool yolo_session_infer(const YoloSession& s,
                        const cv::Mat& orig_bgr,
                        int topk_class_count,
                        float conf_threshold,
                        float nms_threshold,
                        std::vector<SegObject>& results);

#endif  // YOLO_SESSION_H
