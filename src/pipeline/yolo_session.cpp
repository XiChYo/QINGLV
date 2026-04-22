#include "pipeline/yolo_session.h"
#include "pipeline/postprocess_ex.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <opencv2/imgproc.hpp>

namespace {

// ---------------------------------------------------------------------------
// FP32 → IEEE754 half
// ---------------------------------------------------------------------------
uint16_t fp32_to_fp16_bits(float v)
{
    union { float f; uint32_t u; } in{};
    in.f = v;
    const uint32_t x = in.u;
    const uint32_t sign = (x >> 16) & 0x8000u;
    uint32_t mantissa   = x & 0x007fffffu;
    int32_t  exp        = ((x >> 23) & 0xff) - 127 + 15;

    if (exp <= 0) {
        if (exp < -10) return (uint16_t)sign;
        mantissa = (mantissa | 0x00800000u) >> (1 - exp);
        return (uint16_t)(sign | ((mantissa + 0x00001000u) >> 13));
    } else if (exp >= 31) {
        if (mantissa == 0) return (uint16_t)(sign | 0x7c00u);
        return (uint16_t)(sign | 0x7c00u | ((mantissa + 0x00001000u) >> 13));
    }
    return (uint16_t)(sign | ((uint32_t)exp << 10) | ((mantissa + 0x00001000u) >> 13));
}

unsigned char* load_file(const char* path, int* out_size)
{
    FILE* fp = std::fopen(path, "rb");
    if (!fp) return nullptr;
    std::fseek(fp, 0, SEEK_END);
    const long sz = std::ftell(fp);
    if (sz <= 0) { std::fclose(fp); return nullptr; }
    std::fseek(fp, 0, SEEK_SET);
    auto* buf = (unsigned char*)std::malloc(sz);
    if (!buf) { std::fclose(fp); return nullptr; }
    std::fread(buf, 1, sz, fp);
    std::fclose(fp);
    *out_size = (int)sz;
    return buf;
}

}  // namespace

bool yolo_session_init(const char* model_path, YoloSession& s)
{
    s.model_data = load_file(model_path, &s.model_data_size);
    if (!s.model_data) {
        std::fprintf(stderr, "[YoloSession] load model failed: %s\n", model_path ? model_path : "(null)");
        return false;
    }

    int ret = rknn_init(&s.ctx, s.model_data, s.model_data_size, 0, nullptr);
    if (ret < 0) {
        std::fprintf(stderr, "[YoloSession] rknn_init fail ret=%d\n", ret);
        yolo_session_release(s);
        return false;
    }

    // NPU 核心选择:优先 AUTO 让 runtime 做负载均衡,AUTO 在某些 RK3588 固件版本
    // 上可能返回非 0(旧 librknnrt 不识别该枚举),此时退回 CORE_0 以保证推理能起来,
    // 避免跟 master 版本行为上出现"悄无声息降级成单核"的差异。
    int core_ret = rknn_set_core_mask(s.ctx, RKNN_NPU_CORE_AUTO);
    if (core_ret != 0) {
        std::fprintf(stderr,
                     "[YoloSession] rknn_set_core_mask(AUTO) ret=%d, fallback to CORE_0\n",
                     core_ret);
        core_ret = rknn_set_core_mask(s.ctx, RKNN_NPU_CORE_0);
        if (core_ret != 0) {
            std::fprintf(stderr,
                         "[YoloSession] rknn_set_core_mask(CORE_0) ret=%d, continue with default\n",
                         core_ret);
        }
    }

    ret = rknn_query(s.ctx, RKNN_QUERY_IN_OUT_NUM, &s.io_num, sizeof(s.io_num));
    if (ret < 0 || s.io_num.n_input < 1 || s.io_num.n_output < 2) {
        std::fprintf(stderr, "[YoloSession] RKNN_QUERY_IN_OUT_NUM fail ret=%d (n_input=%u n_output=%u)\n",
                     ret, s.io_num.n_input, s.io_num.n_output);
        yolo_session_release(s);
        return false;
    }

    s.output_attrs.assign(s.io_num.n_output, {});
    std::memset(&s.input0_attr, 0, sizeof(s.input0_attr));

    for (uint32_t i = 0; i < s.io_num.n_input; ++i) {
        rknn_tensor_attr attr{};
        attr.index = i;
        ret = rknn_query(s.ctx, RKNN_QUERY_INPUT_ATTR, &attr, sizeof(attr));
        if (ret < 0) { yolo_session_release(s); return false; }
        if (i == 0) s.input0_attr = attr;
    }
    for (uint32_t i = 0; i < s.io_num.n_output; ++i) {
        rknn_tensor_attr attr{};
        attr.index = i;
        ret = rknn_query(s.ctx, RKNN_QUERY_OUTPUT_ATTR, &attr, sizeof(attr));
        if (ret < 0) { yolo_session_release(s); return false; }
        s.output_attrs[i] = attr;
    }
    return true;
}

void yolo_session_release(YoloSession& s)
{
    if (s.ctx != 0) {
        rknn_destroy(s.ctx);
        s.ctx = 0;
    }
    if (s.model_data) {
        std::free(s.model_data);
        s.model_data = nullptr;
    }
    s.model_data_size = 0;
    s.output_attrs.clear();
    std::memset(&s.input0_attr, 0, sizeof(s.input0_attr));
    std::memset(&s.io_num, 0, sizeof(s.io_num));
}

bool yolo_session_infer(const YoloSession& s,
                        const cv::Mat& orig_bgr,
                        int topk_class_count,
                        float conf_threshold,
                        float nms_threshold,
                        std::vector<SegObject>& results)
{
    results.clear();
    if (s.ctx == 0 || orig_bgr.empty()) return false;

    // -------- 1. 从模型输入 tensor 读目标尺寸 --------
    int target_w = 640, target_h = 640;
    if (s.input0_attr.n_dims == 4) {
        if (s.input0_attr.fmt == RKNN_TENSOR_NHWC) {
            target_h = (int)s.input0_attr.dims[1];
            target_w = (int)s.input0_attr.dims[2];
        } else {
            target_h = (int)s.input0_attr.dims[2];
            target_w = (int)s.input0_attr.dims[3];
        }
    }

    // -------- 2. Letterbox + BGR→RGB --------
    const float scale = std::min((float)target_w / orig_bgr.cols,
                                 (float)target_h / orig_bgr.rows);
    const int new_w = std::max(1, (int)std::round(orig_bgr.cols * scale));
    const int new_h = std::max(1, (int)std::round(orig_bgr.rows * scale));
    const int pad_w = (target_w - new_w) / 2;
    const int pad_h = (target_h - new_h) / 2;

    cv::Mat resized;
    cv::resize(orig_bgr, resized, cv::Size(new_w, new_h));
    cv::Mat input_img(target_h, target_w, CV_8UC3, cv::Scalar(114, 114, 114));
    resized.copyTo(input_img(cv::Rect(pad_w, pad_h, new_w, new_h)));
    cv::cvtColor(input_img, input_img, cv::COLOR_BGR2RGB);

    // -------- 3. 喂输入(匹配模型数据类型) --------
    rknn_input inputs[1]{};
    inputs[0].index = 0;
    inputs[0].fmt   = s.input0_attr.fmt;
    inputs[0].pass_through = 1;

    std::vector<float>    buf_fp32;
    std::vector<uint16_t> buf_fp16;
    std::vector<int8_t>   buf_int8;

    const rknn_tensor_type itype = s.input0_attr.type;
    if (itype == RKNN_TENSOR_FLOAT16) {
        buf_fp16.resize(input_img.total() * input_img.channels());
        size_t idx = 0;
        for (int y = 0; y < input_img.rows; ++y) {
            const cv::Vec3b* row = input_img.ptr<cv::Vec3b>(y);
            for (int x = 0; x < input_img.cols; ++x) {
                buf_fp16[idx++] = fp32_to_fp16_bits(row[x][0] / 255.0f);
                buf_fp16[idx++] = fp32_to_fp16_bits(row[x][1] / 255.0f);
                buf_fp16[idx++] = fp32_to_fp16_bits(row[x][2] / 255.0f);
            }
        }
        inputs[0].type = RKNN_TENSOR_FLOAT16;
        inputs[0].size = (uint32_t)(buf_fp16.size() * sizeof(uint16_t));
        inputs[0].buf  = buf_fp16.data();
    } else if (itype == RKNN_TENSOR_FLOAT32) {
        buf_fp32.resize(input_img.total() * input_img.channels());
        size_t idx = 0;
        for (int y = 0; y < input_img.rows; ++y) {
            const cv::Vec3b* row = input_img.ptr<cv::Vec3b>(y);
            for (int x = 0; x < input_img.cols; ++x) {
                buf_fp32[idx++] = row[x][0] / 255.0f;
                buf_fp32[idx++] = row[x][1] / 255.0f;
                buf_fp32[idx++] = row[x][2] / 255.0f;
            }
        }
        inputs[0].type = RKNN_TENSOR_FLOAT32;
        inputs[0].size = (uint32_t)(buf_fp32.size() * sizeof(float));
        inputs[0].buf  = buf_fp32.data();
    } else if (itype == RKNN_TENSOR_INT8) {
        const float scale_in = (s.input0_attr.scale == 0.0f) ? 1.0f : s.input0_attr.scale;
        const int   zp_in    = s.input0_attr.zp;
        const size_t elem    = input_img.total() * input_img.channels();
        buf_int8.resize(elem);
        for (size_t i = 0; i < elem; ++i) {
            const float f = input_img.data[i] / 255.0f;
            int q = (int)std::round(f / scale_in) + zp_in;
            q = std::max(-128, std::min(127, q));
            buf_int8[i] = (int8_t)q;
        }
        inputs[0].type = RKNN_TENSOR_INT8;
        inputs[0].size = (uint32_t)(buf_int8.size() * sizeof(int8_t));
        inputs[0].buf  = buf_int8.data();
    } else {
        inputs[0].type = RKNN_TENSOR_UINT8;
        inputs[0].size = (uint32_t)(input_img.total() * input_img.channels());
        inputs[0].buf  = input_img.data;
    }

    if (rknn_inputs_set(s.ctx, s.io_num.n_input, inputs) < 0) return false;
    if (rknn_run(s.ctx, nullptr) < 0)                          return false;

    std::vector<rknn_output> outputs(s.io_num.n_output);
    std::memset(outputs.data(), 0, sizeof(rknn_output) * outputs.size());
    for (auto& o : outputs) o.want_float = 1;
    if (rknn_outputs_get(s.ctx, s.io_num.n_output, outputs.data(), nullptr) < 0) {
        return false;
    }

    // -------- 4. 识别 det / proto --------
    int det_idx = -1;
    int proto_idx = -1;
    for (int i = 0; i < (int)s.io_num.n_output; ++i) {
        if (s.output_attrs[i].n_dims == 3 && det_idx < 0)     det_idx = i;
        else if (s.output_attrs[i].n_dims == 4 && proto_idx < 0) proto_idx = i;
    }
    if (det_idx < 0 || proto_idx < 0 ||
        outputs[det_idx].buf == nullptr || outputs[proto_idx].buf == nullptr) {
        rknn_outputs_release(s.ctx, s.io_num.n_output, outputs.data());
        return false;
    }

    const auto& det_attr   = s.output_attrs[det_idx];
    const auto& proto_attr = s.output_attrs[proto_idx];
    auto* det_raw   = (float*)outputs[det_idx].buf;
    auto* proto_raw = (float*)outputs[proto_idx].buf;

    const int d1 = (int)det_attr.dims[1];
    const int d2 = (int)det_attr.dims[2];
    const int det_count = std::max(d1, d2);
    const int det_len   = std::min(d1, d2);
    const bool transposed = (d1 < d2);

    std::vector<float> det_packed((size_t)det_count * det_len);
    for (int r = 0; r < det_count; ++r) {
        for (int c = 0; c < det_len; ++c) {
            det_packed[(size_t)r * det_len + c] = transposed
                ? det_raw[(size_t)c * d2 + r]
                : det_raw[(size_t)r * d2 + c];
        }
    }

    int proto_c = 0, proto_h = 0, proto_w = 0;
    bool proto_nchw = true;
    if (proto_attr.fmt == RKNN_TENSOR_NHWC) {
        proto_nchw = false;
        proto_h = (int)proto_attr.dims[1];
        proto_w = (int)proto_attr.dims[2];
        proto_c = (int)proto_attr.dims[3];
    } else {
        proto_c = (int)proto_attr.dims[1];
        proto_h = (int)proto_attr.dims[2];
        proto_w = (int)proto_attr.dims[3];
    }

    int num_classes = det_len - 4 - proto_c;
    if (det_len == (4 + 1 + 1 + proto_c)) {
        num_classes = topk_class_count;
    }
    if (num_classes <= 0) num_classes = 1;

    PostProcessParams params;
    params.conf_threshold = conf_threshold;
    params.nms_threshold  = nms_threshold;
    post_process_seg_ex(det_packed.data(), det_count, det_len,
                        proto_raw, proto_c, proto_h, proto_w, proto_nchw,
                        num_classes,
                        target_w, target_h, orig_bgr.cols, orig_bgr.rows,
                        pad_w, pad_h, scale,
                        params, results);

    rknn_outputs_release(s.ctx, s.io_num.n_output, outputs.data());
    return true;
}
