#include "postprocess_ex.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

constexpr int kMaskProtoDim         = 32;
// 每个类别进入 NMS 前的候选数上限,避免极端场景 CPU 后处理爆炸。
// 与 master 分支 postprocess.cpp 对齐(PRE_NMS_TOPK_PER_CLASS=300)。
constexpr int kPreNmsTopkPerClass   = 300;

float rectIoU(const cv::Rect& a, const cv::Rect& b)
{
    const float inter = (a & b).area();
    const float uni   = a.area() + b.area() - inter;
    return uni <= 0.0f ? 0.0f : inter / uni;
}

// 轻量 Candidate:只存 bbox/label/score + mask 系数,不提前算 mask。
// NMS 后只对保留项计算 mask(coeffs × proto_mat + sigmoid + threshold + resize),
// 这是 master 产线版本的主要优化点(见 postprocess.cpp §" 只对保留目标计算 mask (重)")。
struct Candidate {
    cv::Rect box;                 // 原图坐标系(letterbox 反向 + clip)
    int      label = -1;
    float    prob  = 0.0f;
    float    cx_m  = 0.0f;        // 模型输入坐标系,用于 mask 裁剪
    float    cy_m  = 0.0f;
    float    w_m   = 0.0f;
    float    h_m   = 0.0f;
    float    coeffs[kMaskProtoDim]{};
};

}  // namespace

void post_process_seg_ex(const float* det_data, int det_count, int det_len,
                         const float* proto_data, int proto_c, int proto_h, int proto_w, bool proto_nchw,
                         int num_classes,
                         int model_w, int model_h, int orig_w, int orig_h,
                         int pad_w, int pad_h, float scale,
                         const PostProcessParams& params,
                         std::vector<SegObject>& results)
{
    if (!det_data || !proto_data) return;
    if (det_count <= 0 || det_len <= 0) return;
    if (proto_c != kMaskProtoDim) {
        std::fprintf(stderr, "[post_ex] proto_c=%d unsupported (expect %d)\n",
                     proto_c, kMaskProtoDim);
        return;
    }

    const bool topk_layout   = (det_len == (4 + 1 + 1 + kMaskProtoDim));
    const int  coeff_offset  = topk_layout ? 6 : (4 + num_classes);
    if (coeff_offset + kMaskProtoDim > det_len) return;

    // ------------------------------------------------------------------
    // Phase 1:扫候选(不算 mask)
    // ------------------------------------------------------------------
    std::vector<Candidate> candidates;
    candidates.reserve(256);

    for (int i = 0; i < det_count; ++i) {
        const float* row = det_data + i * det_len;

        float max_score = 0.0f;
        int   class_id  = -1;
        if (topk_layout) {
            max_score = row[4];
            class_id  = static_cast<int>(std::round(row[5]));
            class_id  = std::max(0, std::min(class_id, std::max(0, num_classes - 1)));
        } else {
            for (int c = 0; c < num_classes; ++c) {
                const float s = row[4 + c];
                if (s > max_score) { max_score = s; class_id = c; }
            }
        }
        if (max_score <= params.conf_threshold) continue;
        if (class_id < 0) continue;

        float cx = row[0], cy = row[1], w = row[2], h = row[3];
        if (!std::isfinite(cx) || !std::isfinite(cy) ||
            !std::isfinite(w)  || !std::isfinite(h))  continue;

        if (cx <= 1.5f && cy <= 1.5f && w <= 1.5f && h <= 1.5f) {
            cx *= model_w; cy *= model_h;
            w  *= model_w; h  *= model_h;
        }

        const float x1 = (cx - w * 0.5f - pad_w) / scale;
        const float y1 = (cy - h * 0.5f - pad_h) / scale;
        const float wo = w / scale;
        const float ho = h / scale;
        if (wo <= 0.f || ho <= 0.f) continue;

        Candidate cand;
        cand.box = cv::Rect(static_cast<int>(x1), static_cast<int>(y1),
                            static_cast<int>(wo), static_cast<int>(ho));
        cand.box.x      = std::max(0, std::min(cand.box.x, orig_w - 1));
        cand.box.y      = std::max(0, std::min(cand.box.y, orig_h - 1));
        cand.box.width  = std::max(1, std::min(cand.box.width,  orig_w - cand.box.x));
        cand.box.height = std::max(1, std::min(cand.box.height, orig_h - cand.box.y));
        cand.label = class_id;
        cand.prob  = max_score;
        cand.cx_m = cx; cand.cy_m = cy;
        cand.w_m  = w;  cand.h_m  = h;
        for (int m = 0; m < kMaskProtoDim; ++m) {
            cand.coeffs[m] = row[coeff_offset + m];
        }
        candidates.push_back(std::move(cand));
    }
    if (candidates.empty()) return;

    // ------------------------------------------------------------------
    // Phase 2:按 class 分桶,每桶 top-K + NMS(对齐 master 逻辑)
    // ------------------------------------------------------------------
    const int classBuckets = std::max(1, num_classes);
    std::vector<std::vector<int>> class_to_indices(classBuckets);
    for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
        const int cid = candidates[i].label;
        if (cid >= 0 && cid < classBuckets) {
            class_to_indices[cid].push_back(i);
        }
    }

    std::vector<int> keep_indices;
    keep_indices.reserve(candidates.size());
    for (int c = 0; c < classBuckets; ++c) {
        auto& idxs = class_to_indices[c];
        if (idxs.empty()) continue;

        std::sort(idxs.begin(), idxs.end(), [&](int a, int b) {
            return candidates[a].prob > candidates[b].prob;
        });
        if (static_cast<int>(idxs.size()) > kPreNmsTopkPerClass) {
            idxs.resize(kPreNmsTopkPerClass);
        }

        std::vector<uint8_t> suppressed(idxs.size(), 0);
        for (size_t i = 0; i < idxs.size(); ++i) {
            if (suppressed[i]) continue;
            keep_indices.push_back(idxs[i]);
            for (size_t j = i + 1; j < idxs.size(); ++j) {
                if (suppressed[j]) continue;
                if (rectIoU(candidates[idxs[i]].box,
                            candidates[idxs[j]].box) > params.nms_threshold) {
                    suppressed[j] = 1;
                }
            }
        }
    }
    if (keep_indices.empty()) return;

    // ------------------------------------------------------------------
    // Phase 3:只对保留的候选计算 mask
    //   mask 计算涉及 (1×32) × (32×H*W) 矩阵乘 + sigmoid + resize 三次,是后处理
    //   主要开销;master 实测 dense 场景下与"候选期算 mask"相比可节省数倍耗时。
    // ------------------------------------------------------------------
    cv::Mat proto_mat(kMaskProtoDim, proto_h * proto_w, CV_32F);
    for (int c = 0; c < kMaskProtoDim; ++c) {
        float* dst = proto_mat.ptr<float>(c);
        for (int y = 0; y < proto_h; ++y) {
            for (int x = 0; x < proto_w; ++x) {
                const int idx = proto_nchw
                    ? (c * proto_h + y) * proto_w + x
                    : (y * proto_w + x) * kMaskProtoDim + c;
                dst[y * proto_w + x] = proto_data[idx];
            }
        }
    }

    results.reserve(results.size() + keep_indices.size());
    for (int idx : keep_indices) {
        const Candidate& cand = candidates[idx];

        const int bx1 = std::max(0, static_cast<int>(cand.cx_m - cand.w_m * 0.5f));
        const int by1 = std::max(0, static_cast<int>(cand.cy_m - cand.h_m * 0.5f));
        const int bx2 = std::min(model_w, static_cast<int>(cand.cx_m + cand.w_m * 0.5f));
        const int by2 = std::min(model_h, static_cast<int>(cand.cy_m + cand.h_m * 0.5f));
        if (bx2 <= bx1 || by2 <= by1) continue;

        cv::Mat coeffs(1, kMaskProtoDim, CV_32F);
        for (int m = 0; m < kMaskProtoDim; ++m) {
            coeffs.at<float>(0, m) = cand.coeffs[m];
        }
        cv::Mat mask_flat = coeffs * proto_mat;
        cv::exp(-mask_flat, mask_flat);
        mask_flat = 1.0 / (1.0 + mask_flat);

        cv::Mat mask_raw = mask_flat.reshape(1, proto_h);
        cv::Mat mask_up;
        cv::resize(mask_raw, mask_up, cv::Size(model_w, model_h));

        cv::Mat mask_crop = mask_up(cv::Rect(bx1, by1, bx2 - bx1, by2 - by1));
        cv::Mat mask_bin;
        cv::threshold(mask_crop, mask_bin, 0.5, 1, cv::THRESH_BINARY);
        mask_bin.convertTo(mask_bin, CV_8U);

        SegObject obj;
        obj.box   = cand.box;
        obj.label = cand.label;
        obj.prob  = cand.prob;
        cv::resize(mask_bin, obj.mask, obj.box.size(), 0, 0, cv::INTER_NEAREST);
        results.push_back(std::move(obj));
    }
}

void draw_results_ex(cv::Mat& img,
                     const std::vector<SegObject>& results,
                     const char* (*label_from_id)(int))
{
    for (const auto& obj : results) {
        cv::rectangle(img, obj.box, cv::Scalar(0, 255, 0), 2);

        char buf[64];
        const char* cls = label_from_id ? label_from_id(obj.label) : nullptr;
        if (cls && cls[0] != '\0') {
            std::snprintf(buf, sizeof(buf), "%s %.2f", cls, obj.prob);
        } else {
            std::snprintf(buf, sizeof(buf), "%d %.2f", obj.label, obj.prob);
        }
        cv::putText(img, buf, cv::Point(obj.box.x, std::max(12, obj.box.y - 5)),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);

        if (!obj.mask.empty()) {
            const cv::Rect roi_rect = obj.box & cv::Rect(0, 0, img.cols, img.rows);
            if (roi_rect.area() > 0) {
                const int mx = roi_rect.x - obj.box.x;
                const int my = roi_rect.y - obj.box.y;
                cv::Mat mask_roi = obj.mask(cv::Rect(mx, my, roi_rect.width, roi_rect.height));
                cv::Mat roi      = img(roi_rect);
                cv::Mat color_mask(roi.size(), CV_8UC3, cv::Scalar(0, 0, 255));
                cv::Mat masked_color;
                color_mask.copyTo(masked_color, mask_roi);
                cv::addWeighted(roi, 0.7, masked_color, 0.3, 0.0, roi);
            }
        }
    }
}
