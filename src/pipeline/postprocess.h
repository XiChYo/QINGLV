#ifndef POSTPROCESS_H
#define POSTPROCESS_H

// ----------------------------------------------------------------------------
// 仅保留 YOLOv8-Seg 后处理共享的数据结构 SegObject。
// 旧的 post_process_seg / draw_results 已被 postprocess_ex.{h,cpp} 中的
// post_process_seg_ex / draw_results_ex 取代(可运行时注入阈值、按类 NMS、
// 延后计算 mask),此文件不再提供任何函数实现。
// ----------------------------------------------------------------------------

#include <opencv2/opencv.hpp>

struct SegObject {
    cv::Rect box;
    int label = -1;
    float prob = 0.0f;
    cv::Mat mask;
};

#endif  // POSTPROCESS_H
