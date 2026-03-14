#ifndef POSTPROCESS_H
#define POSTPROCESS_H

#include <opencv2/opencv.hpp>
#include <vector>

struct SegObject {
    cv::Rect box;
    int label;
    float prob;
    cv::Mat mask;
};

void post_process_seg(const float* det_data, int det_count, int det_len,
                      const float* proto_data, int proto_c, int proto_h, int proto_w, bool proto_nchw,
                      int num_classes,
                      int model_w, int model_h, int orig_w, int orig_h,
                      int pad_w, int pad_h, float scale,
                      std::vector<SegObject>& results);

void draw_results(cv::Mat& img, const std::vector<SegObject>& results);

#endif
