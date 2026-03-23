#include "postprocess.h"
#include <algorithm>
#include <math.h>
#include <cmath>
#include <stdio.h>
#include <vector>

#define NMS_THRESH 0.45
#define CONF_THRESH 0.25
#define MASK_PROTO_DIM 32

#ifndef RKNN_DEBUG
#define RKNN_DEBUG 1
#endif

#if RKNN_DEBUG
//#define DBG_PRINT(...) printf("[DEBUG][post] " __VA_ARGS__)
#define DBG_PRINT(...)
#else
#define DBG_PRINT(...)
#endif

// Internal helper for NMS
static float iou(const cv::Rect& a, const cv::Rect& b) {
    float inter_area = (a & b).area();
    float union_area = a.area() + b.area() - inter_area;
    if (union_area <= 0.0f) return 0.0f;
    return inter_area / union_area;
}

void post_process_seg(const float* det_data, int det_count, int det_len,
                      const float* proto_data, int proto_c, int proto_h, int proto_w, bool proto_nchw,
                      int num_classes,
                      int model_w, int model_h, int orig_w, int orig_h,
                      int pad_w, int pad_h, float scale,
                      std::vector<SegObject>& results)
{
    if (det_data == nullptr || proto_data == nullptr) {
        printf("post_process_seg got null pointer: det_data=%p proto_data=%p\n", det_data, proto_data);
        return;
    }
    if (det_count <= 0 || det_len <= 0 || proto_c <= 0 || proto_h <= 0 || proto_w <= 0) {
        printf("post_process_seg invalid shapes det_count=%d det_len=%d proto=(%d,%d,%d)\n",
               det_count, det_len, proto_c, proto_h, proto_w);
        return;
    }
    if (proto_c != MASK_PROTO_DIM) {
        printf("post_process_seg proto_c=%d unsupported, expected %d\n", proto_c, MASK_PROTO_DIM);
        return;
    }

    DBG_PRINT("start model_w=%d model_h=%d pad_w=%d pad_h=%d scale=%f\n",
              model_w, model_h, pad_w, pad_h, scale);
    DBG_PRINT("det_count=%d det_len=%d classes=%d proto=(c=%d,h=%d,w=%d,%s)\n",
              det_count, det_len, num_classes, proto_c, proto_h, proto_w, proto_nchw ? "NCHW" : "NHWC");

    // Layout A (common RKNN NMS output): [cx, cy, w, h, score, class_id, 32 coeff] => 38
    // Layout B (raw cls scores): [cx, cy, w, h, cls..., 32 coeff]
    const bool topk_layout = (det_len == (4 + 1 + 1 + MASK_PROTO_DIM));
    const int coeff_offset = topk_layout ? 6 : (4 + num_classes);
    if (coeff_offset + MASK_PROTO_DIM > det_len) {
        printf("post_process_seg unsupported det_len=%d (coeff overflow)\n", det_len);
        return;
    }

    cv::Mat proto_mat(MASK_PROTO_DIM, proto_h * proto_w, CV_32F);
    for (int c = 0; c < MASK_PROTO_DIM; c++) {
        float* dst = proto_mat.ptr<float>(c);
        for (int y = 0; y < proto_h; y++) {
            for (int x = 0; x < proto_w; x++) {
                if (proto_nchw) {
                    const int idx = (c * proto_h + y) * proto_w + x;
                    dst[y * proto_w + x] = proto_data[idx];
                } else {
                    const int idx = (y * proto_w + x) * MASK_PROTO_DIM + c;
                    dst[y * proto_w + x] = proto_data[idx];
                }
            }
        }
    }

    std::vector<SegObject> proposals;

    for (int i = 0; i < det_count; i++) {
        const float* row = det_data + i * det_len;

        float max_score = 0.0f;
        int class_id = -1;
        if (topk_layout) {
            max_score = row[4];
            class_id = (int)std::round(row[5]);
            class_id = std::max(0, std::min(class_id, std::max(0, num_classes - 1)));
        } else {
            for (int c = 0; c < num_classes; c++) {
                float score = row[4 + c];
                if (score > max_score) {
                    max_score = score;
                    class_id = c;
                }
            }
        }

        if (max_score > CONF_THRESH) {
            float cx = row[0];
            float cy = row[1];
            float w = row[2];
            float h = row[3];

            if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(w) || !std::isfinite(h)) {
                if (i < 20) {
                    DBG_PRINT("skip non-finite box at anchor=%d cx=%f cy=%f w=%f h=%f\n", i, cx, cy, w, h);
                }
                continue;
            }

            // Handle normalized output boxes.
            if (cx <= 1.5f && cy <= 1.5f && w <= 1.5f && h <= 1.5f) {
                cx *= model_w;
                cy *= model_h;
                w *= model_w;
                h *= model_h;
            }

            // Convert to original image coords.
            float x1 = (cx - w/2 - pad_w) / scale;
            float y1 = (cy - h/2 - pad_h) / scale;
            float w_orig = w / scale;
            float h_orig = h / scale;
            if (w_orig <= 0.f || h_orig <= 0.f) {
                continue;
            }

            SegObject obj;
            obj.box = cv::Rect(x1, y1, w_orig, h_orig);
            obj.box.x = std::max(0, std::min(obj.box.x, orig_w - 1));
            obj.box.y = std::max(0, std::min(obj.box.y, orig_h - 1));
            obj.box.width = std::max(1, std::min(obj.box.width, orig_w - obj.box.x));
            obj.box.height = std::max(1, std::min(obj.box.height, orig_h - obj.box.y));
            obj.label = class_id;
            obj.prob = max_score;

            cv::Mat coeffs(1, MASK_PROTO_DIM, CV_32F);
            for (int m = 0; m < MASK_PROTO_DIM; m++) {
                coeffs.at<float>(0, m) = row[coeff_offset + m];
            }

            cv::Mat mask_flat = coeffs * proto_mat;
            cv::exp(-mask_flat, mask_flat);
            mask_flat = 1.0 / (1.0 + mask_flat);

            cv::Mat mask_raw = mask_flat.reshape(1, proto_h);
            cv::Mat mask_upscaled;
            cv::resize(mask_raw, mask_upscaled, cv::Size(model_w, model_h));

            int bx1 = std::max(0, (int)(cx - w/2));
            int by1 = std::max(0, (int)(cy - h/2));
            int bx2 = std::min(model_w, (int)(cx + w/2));
            int by2 = std::min(model_h, (int)(cy + h/2));

            if (bx2 > bx1 && by2 > by1) {
                cv::Mat mask_cropped = mask_upscaled(cv::Rect(bx1, by1, bx2-bx1, by2-by1));

                cv::Mat mask_bin;
                cv::threshold(mask_cropped, mask_bin, 0.5, 1, cv::THRESH_BINARY);
                mask_bin.convertTo(mask_bin, CV_8U);

                cv::resize(mask_bin, obj.mask, obj.box.size(), 0, 0, cv::INTER_NEAREST);

                proposals.push_back(obj);
            }
        }
    }

    DBG_PRINT("proposals=%zu before nms\n", proposals.size());

    // NMS
    std::sort(proposals.begin(), proposals.end(), [](const SegObject& a, const SegObject& b) {
        return a.prob > b.prob;
    });

    std::vector<bool> suppressed(proposals.size(), false);
    for (size_t i = 0; i < proposals.size(); i++) {
        if (suppressed[i]) continue;
        results.push_back(proposals[i]);

        for (size_t j = i + 1; j < proposals.size(); j++) {
            if (suppressed[j]) continue;
            if (iou(proposals[i].box, proposals[j].box) > NMS_THRESH) {
                suppressed[j] = true;
            }
        }
    }
    DBG_PRINT("results=%zu after nms\n", results.size());
}

void draw_results(cv::Mat& img, const std::vector<SegObject>& results) {
    for (const auto& obj : results) {
        // Draw Box
        cv::rectangle(img, obj.box, cv::Scalar(0, 255, 0), 2);

        // Draw Label
        std::string label = std::to_string(obj.label) + ": " + std::to_string(obj.prob).substr(0, 4);
        cv::putText(img, label, cv::Point(obj.box.x, obj.box.y - 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);

        // Draw Mask Overlay
        if (!obj.mask.empty()) {
            // Ensure mask is within image bounds
            cv::Rect roi_rect = obj.box & cv::Rect(0, 0, img.cols, img.rows);
            if (roi_rect.area() > 0) {
                // Get mask part that corresponds to the ROI
                // obj.mask is same size as obj.box
                int mx = roi_rect.x - obj.box.x;
                int my = roi_rect.y - obj.box.y;
                cv::Mat mask_roi = obj.mask(cv::Rect(mx, my, roi_rect.width, roi_rect.height));

                cv::Mat roi = img(roi_rect);
                cv::Mat color_mask(roi.size(), CV_8UC3, cv::Scalar(0, 0, 255)); // Red

                // Apply mask only where value is 1
                cv::Mat masked_color;
                color_mask.copyTo(masked_color, mask_roi);

                cv::addWeighted(roi, 0.7, masked_color, 0.3, 0, roi);
            }
        }
    }
}
