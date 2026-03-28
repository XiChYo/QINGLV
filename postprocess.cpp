#include "postprocess.h"
#include <algorithm>
#include <math.h>
#include <cmath>
#include <stdio.h>
#include <vector>
#include <chrono>

#define NMS_THRESH 0.25
#define CONF_THRESH 0.60
// 限制每个类别进入 NMS 的候选数量，避免极端场景 CPU 后处理爆炸。
// 典型目标量 50-100/图时，这个值可适当放宽。
static constexpr int PRE_NMS_TOPK_PER_CLASS = 300;

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
    const auto t_all_start = std::chrono::steady_clock::now();
    double t_proto_mat_ms = 0.0;
    double t_candidate_scan_ms = 0.0;
    double t_nms_prepare_ms = 0.0;
    double t_nms_ms = 0.0;
    double t_mask_ms = 0.0;

    if (det_data == nullptr || proto_data == nullptr) {
        printf("post_process_seg got null pointer: det_data=%p proto_data=%p\n", det_data, proto_data);
        return;
    }
    if (det_count <= 0 || det_len <= 0 || proto_c <= 0 || proto_h <= 0 || proto_w <= 0) {
        printf("post_process_seg invalid shapes det_count=%d det_len=%d proto=(%d,%d,%d)\n",
               det_count, det_len, proto_c, proto_h, proto_w);
        return;
    }
    const int mask_proto_dim = proto_c;
    if (mask_proto_dim <= 0) {
        printf("post_process_seg invalid proto_c=%d\n", proto_c);
        return;
    }

    DBG_PRINT("start model_w=%d model_h=%d pad_w=%d pad_h=%d scale=%f\n",
              model_w, model_h, pad_w, pad_h, scale);
    DBG_PRINT("det_count=%d det_len=%d classes=%d proto=(c=%d,h=%d,w=%d,%s)\n",
              det_count, det_len, num_classes, proto_c, proto_h, proto_w, proto_nchw ? "NCHW" : "NHWC");

    // 布局A（常见RKNN top-k输出）：[cx, cy, w, h, score, class_id, mask_coeff...]
    // 布局B（常规分类分数输出）：[cx, cy, w, h, cls..., mask_coeff...]
    const bool topk_layout = (det_len == (4 + 1 + 1 + mask_proto_dim));
    const int coeff_offset = topk_layout ? 6 : (4 + num_classes);
    if (coeff_offset + mask_proto_dim > det_len) {
        printf("post_process_seg unsupported det_len=%d (coeff overflow)\n", det_len);
        return;
    }

    const auto t_proto_start = std::chrono::steady_clock::now();
    cv::Mat proto_mat(mask_proto_dim, proto_h * proto_w, CV_32F);
    for (int c = 0; c < mask_proto_dim; c++) {
        float* dst = proto_mat.ptr<float>(c);
        for (int y = 0; y < proto_h; y++) {
            for (int x = 0; x < proto_w; x++) {
                if (proto_nchw) {
                    const int idx = (c * proto_h + y) * proto_w + x;
                    dst[y * proto_w + x] = proto_data[idx];
                } else {
                    const int idx = (y * proto_w + x) * mask_proto_dim + c;
                    dst[y * proto_w + x] = proto_data[idx];
                }
            }
        }
    }
    const auto t_proto_end = std::chrono::steady_clock::now();
    t_proto_mat_ms = std::chrono::duration<double, std::milli>(t_proto_end - t_proto_start).count();

    // 方案1：先做候选框解析 + NMS（轻量），再只对保留目标计算 mask（重）
    struct Candidate {
        cv::Rect box;               // 原图坐标系 box
        int label = -1;
        float prob = 0.0f;
        // 模型输入坐标系 box，用于 mask 裁剪（在 model_w/model_h 上裁剪）
        float cx_m = 0.0f;
        float cy_m = 0.0f;
        float w_m = 0.0f;
        float h_m = 0.0f;
        std::vector<float> coeffs;  // mask 系数（长度=proto_c）
    };
    std::vector<Candidate> candidates;
    candidates.reserve(256);

    const auto t_scan_start = std::chrono::steady_clock::now();
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

            // 兼容归一化坐标输出（0~1）和像素坐标输出两种形式。
            if (cx <= 1.5f && cy <= 1.5f && w <= 1.5f && h <= 1.5f) {
                cx *= model_w;
                cy *= model_h;
                w *= model_w;
                h *= model_h;
            }

            // 将模型输入坐标映射回原图坐标（反向去除 letterbox）。
            float x1 = (cx - w/2 - pad_w) / scale;
            float y1 = (cy - h/2 - pad_h) / scale;
            float w_orig = w / scale;
            float h_orig = h / scale;
            if (w_orig <= 0.f || h_orig <= 0.f) {
                continue;
            }

            Candidate cand;
            cand.box = cv::Rect(x1, y1, w_orig, h_orig);
            cand.box.x = std::max(0, std::min(cand.box.x, orig_w - 1));
            cand.box.y = std::max(0, std::min(cand.box.y, orig_h - 1));
            cand.box.width = std::max(1, std::min(cand.box.width, orig_w - cand.box.x));
            cand.box.height = std::max(1, std::min(cand.box.height, orig_h - cand.box.y));
            cand.label = class_id;
            cand.prob = max_score;
            cand.cx_m = cx;
            cand.cy_m = cy;
            cand.w_m = w;
            cand.h_m = h;
            cand.coeffs.resize(mask_proto_dim);
            for (int m = 0; m < mask_proto_dim; ++m) {
                cand.coeffs[m] = row[coeff_offset + m];
            }
            candidates.push_back(std::move(cand));
        }
    }

    const auto t_scan_end = std::chrono::steady_clock::now();
    t_candidate_scan_ms = std::chrono::duration<double, std::milli>(t_scan_end - t_scan_start).count();

    DBG_PRINT("candidates=%zu before nms\n", candidates.size());

    // NMS（按类别分组，先 topK 再 NMS；只用 box/prob，不算 mask）
    const auto t_prepare_start = std::chrono::steady_clock::now();
    std::vector<std::vector<int>> class_to_indices(std::max(1, num_classes));
    for (int i = 0; i < (int)candidates.size(); ++i) {
        const int cid = candidates[i].label;
        if (cid >= 0 && cid < num_classes) {
            class_to_indices[cid].push_back(i);
        }
    }
    const auto t_prepare_end = std::chrono::steady_clock::now();
    t_nms_prepare_ms = std::chrono::duration<double, std::milli>(t_prepare_end - t_prepare_start).count();

    const auto t_nms_start = std::chrono::steady_clock::now();
    std::vector<int> keep_indices;
    keep_indices.reserve(256);
    for (int c = 0; c < num_classes; ++c) {
        auto& idxs = class_to_indices[c];
        if (idxs.empty()) continue;

        std::sort(idxs.begin(), idxs.end(), [&](int a, int b) {
            return candidates[a].prob > candidates[b].prob;
        });
        if ((int)idxs.size() > PRE_NMS_TOPK_PER_CLASS) {
            idxs.resize(PRE_NMS_TOPK_PER_CLASS);
        }

        std::vector<bool> suppressed(idxs.size(), false);
        for (size_t i = 0; i < idxs.size(); ++i) {
            if (suppressed[i]) continue;
            const int keep_i = idxs[i];
            keep_indices.push_back(keep_i);
            for (size_t j = i + 1; j < idxs.size(); ++j) {
                if (suppressed[j]) continue;
                if (iou(candidates[keep_i].box, candidates[idxs[j]].box) > NMS_THRESH) {
                    suppressed[j] = true;
                }
            }
        }
    }
    const auto t_nms_end = std::chrono::steady_clock::now();
    t_nms_ms = std::chrono::duration<double, std::milli>(t_nms_end - t_nms_start).count();
    DBG_PRINT("keep=%zu after nms\n", keep_indices.size());

    // 只对保留目标计算 mask（重）
    const auto t_mask_start = std::chrono::steady_clock::now();
    for (int idx : keep_indices) {
        const Candidate& cand = candidates[idx];
        const float cx = cand.cx_m;
        const float cy = cand.cy_m;
        const float w = cand.w_m;
        const float h = cand.h_m;

        int bx1 = std::max(0, (int)(cx - w / 2));
        int by1 = std::max(0, (int)(cy - h / 2));
        int bx2 = std::min(model_w, (int)(cx + w / 2));
        int by2 = std::min(model_h, (int)(cy + h / 2));
        if (bx2 <= bx1 || by2 <= by1) {
            continue;
        }

        cv::Mat coeffs(1, mask_proto_dim, CV_32F);
        for (int m = 0; m < mask_proto_dim; ++m) {
            coeffs.at<float>(0, m) = cand.coeffs[m];
        }
        cv::Mat mask_flat = coeffs * proto_mat;
        cv::exp(-mask_flat, mask_flat);
        mask_flat = 1.0 / (1.0 + mask_flat);

        cv::Mat mask_raw = mask_flat.reshape(1, proto_h);
        cv::Mat mask_upscaled;
        cv::resize(mask_raw, mask_upscaled, cv::Size(model_w, model_h));

        cv::Mat mask_cropped = mask_upscaled(cv::Rect(bx1, by1, bx2 - bx1, by2 - by1));
        cv::Mat mask_bin;
        cv::threshold(mask_cropped, mask_bin, 0.5, 1, cv::THRESH_BINARY);
        mask_bin.convertTo(mask_bin, CV_8U);

        SegObject obj;
        obj.box = cand.box;
        obj.label = cand.label;
        obj.prob = cand.prob;
        cv::resize(mask_bin, obj.mask, obj.box.size(), 0, 0, cv::INTER_NEAREST);
        results.push_back(std::move(obj));
    }
    const auto t_mask_end = std::chrono::steady_clock::now();
    t_mask_ms = std::chrono::duration<double, std::milli>(t_mask_end - t_mask_start).count();
    DBG_PRINT("results=%zu after mask\n", results.size());

    const auto t_all_end = std::chrono::steady_clock::now();
    const double t_all_ms = std::chrono::duration<double, std::milli>(t_all_end - t_all_start).count();
    DBG_PRINT("time breakdown (ms): total=%.3f proto_mat=%.3f candidate_scan=%.3f nms_prepare=%.3f nms=%.3f mask=%.3f\n",
              t_all_ms, t_proto_mat_ms, t_candidate_scan_ms, t_nms_prepare_ms, t_nms_ms, t_mask_ms);
}

void draw_results(cv::Mat& img, const std::vector<SegObject>& results) {
    for (const auto& obj : results) {
        // Draw Box
        cv::rectangle(img, obj.box, cv::Scalar(0, 255, 0), 2);

        // Draw Label
        std::string label = std::to_string(obj.label) + ": " + std::to_string(obj.prob).substr(0, 4);
        cv::putText(img, label, cv::Point(obj.box.x, obj.box.y - 5),
                    cv::FONT_HERSHEY_SIMPLEX, 4, cv::Scalar(0, 255, 0), 5);

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
