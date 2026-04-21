#include "postprocess_ex.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

constexpr int kMaskProtoDim = 32;

float rectIoU(const cv::Rect& a, const cv::Rect& b)
{
    const float inter = (a & b).area();
    const float uni   = a.area() + b.area() - inter;
    return uni <= 0.0f ? 0.0f : inter / uni;
}

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

    // 重排 proto 到 (32, H*W) 便于后面用矩阵乘求 mask。
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

    std::vector<SegObject> proposals;
    proposals.reserve(static_cast<size_t>(det_count) / 4);

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

        SegObject obj;
        obj.box = cv::Rect(static_cast<int>(x1), static_cast<int>(y1),
                           static_cast<int>(wo), static_cast<int>(ho));
        obj.box.x      = std::max(0, std::min(obj.box.x, orig_w - 1));
        obj.box.y      = std::max(0, std::min(obj.box.y, orig_h - 1));
        obj.box.width  = std::max(1, std::min(obj.box.width,  orig_w - obj.box.x));
        obj.box.height = std::max(1, std::min(obj.box.height, orig_h - obj.box.y));
        obj.label = class_id;
        obj.prob  = max_score;

        cv::Mat coeffs(1, kMaskProtoDim, CV_32F);
        for (int m = 0; m < kMaskProtoDim; ++m) {
            coeffs.at<float>(0, m) = row[coeff_offset + m];
        }

        cv::Mat mask_flat = coeffs * proto_mat;
        cv::exp(-mask_flat, mask_flat);
        mask_flat = 1.0 / (1.0 + mask_flat);

        cv::Mat mask_raw = mask_flat.reshape(1, proto_h);
        cv::Mat mask_up;
        cv::resize(mask_raw, mask_up, cv::Size(model_w, model_h));

        const int bx1 = std::max(0, static_cast<int>(cx - w * 0.5f));
        const int by1 = std::max(0, static_cast<int>(cy - h * 0.5f));
        const int bx2 = std::min(model_w, static_cast<int>(cx + w * 0.5f));
        const int by2 = std::min(model_h, static_cast<int>(cy + h * 0.5f));
        if (bx2 <= bx1 || by2 <= by1) continue;

        cv::Mat mask_crop = mask_up(cv::Rect(bx1, by1, bx2 - bx1, by2 - by1));
        cv::Mat mask_bin;
        cv::threshold(mask_crop, mask_bin, 0.5, 1, cv::THRESH_BINARY);
        mask_bin.convertTo(mask_bin, CV_8U);
        cv::resize(mask_bin, obj.mask, obj.box.size(), 0, 0, cv::INTER_NEAREST);

        proposals.push_back(std::move(obj));
    }

    // NMS
    std::sort(proposals.begin(), proposals.end(),
              [](const SegObject& a, const SegObject& b) { return a.prob > b.prob; });
    std::vector<uint8_t> suppressed(proposals.size(), 0);
    for (size_t i = 0; i < proposals.size(); ++i) {
        if (suppressed[i]) continue;
        results.push_back(proposals[i]);
        for (size_t j = i + 1; j < proposals.size(); ++j) {
            if (suppressed[j]) continue;
            if (rectIoU(proposals[i].box, proposals[j].box) > params.nms_threshold) {
                suppressed[j] = 1;
            }
        }
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
