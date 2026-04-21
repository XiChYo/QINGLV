#ifndef POSTPROCESS_EX_H
#define POSTPROCESS_EX_H

// ============================================================================
// 可参数化的 YOLOv8-Seg 后处理入口。
// 区别于 postprocess.h 中用 #define 写死 CONF_THRESH/NMS_THRESH 的老函数,
// 此处把阈值暴露为运行时参数,供 YoloWorker 从 RuntimeConfig 注入。
// 输出类型复用旧的 SegObject(mask 尺寸 == bbox 尺寸),上层再转 DetectedObject。
// ============================================================================

#include "postprocess.h"

struct PostProcessParams
{
    float conf_threshold = 0.25f;
    float nms_threshold  = 0.45f;
};

void post_process_seg_ex(const float* det_data, int det_count, int det_len,
                         const float* proto_data, int proto_c, int proto_h, int proto_w, bool proto_nchw,
                         int num_classes,
                         int model_w, int model_h, int orig_w, int orig_h,
                         int pad_w, int pad_h, float scale,
                         const PostProcessParams& params,
                         std::vector<SegObject>& results);

// 在 BGR 图上叠加 bbox + mask + 类别/置信度标签。
// label_from_id 为 nullptr 时只写 id,否则调用回调取中文/英文名。
void draw_results_ex(cv::Mat& bgr_img,
                    const std::vector<SegObject>& results,
                    const char* (*label_from_id)(int) = nullptr);

#endif  // POSTPROCESS_EX_H
