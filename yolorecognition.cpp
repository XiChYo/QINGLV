#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <algorithm>
#include <chrono>
#include <vector>
#include <opencv2/opencv.hpp>
#include "rknn_api.h"
#include "postprocess.h"
#include "yolorecognition.h"

#include <QDateTime>

// Set to 1 to enable verbose diagnostic logs.
#ifndef RKNN_DEBUG
#define RKNN_DEBUG 1
#endif

#if RKNN_DEBUG
//#define DBG_PRINT(...) printf("[DEBUG] " __VA_ARGS__)
#define DBG_PRINT(...)
#else
#define DBG_PRINT(...)
#endif

static const char* tensor_type_to_str(rknn_tensor_type t) {
    switch (t) {
        case RKNN_TENSOR_FLOAT32: return "FLOAT32";
        case RKNN_TENSOR_FLOAT16: return "FLOAT16";
        case RKNN_TENSOR_INT8: return "INT8";
        case RKNN_TENSOR_UINT8: return "UINT8";
        case RKNN_TENSOR_INT16: return "INT16";
        case RKNN_TENSOR_UINT16: return "UINT16";
        case RKNN_TENSOR_INT32: return "INT32";
        case RKNN_TENSOR_UINT32: return "UINT32";
        default: return "UNKNOWN_TYPE";
    }
}

static const char* tensor_fmt_to_str(rknn_tensor_format f) {
    switch (f) {
        case RKNN_TENSOR_NCHW: return "NCHW";
        case RKNN_TENSOR_NHWC: return "NHWC";
        default: return "UNKNOWN_FMT";
    }
}

// FP32 -> IEEE754 half (uint16) conversion for RKNN FLOAT16 inputs.
static uint16_t fp32_to_fp16_bits(float v) {
    union {
        float f;
        uint32_t u;
    } in{};
    in.f = v;
    uint32_t x = in.u;

    uint32_t sign = (x >> 16) & 0x8000u;
    uint32_t mantissa = x & 0x007fffffu;
    int32_t exp = ((x >> 23) & 0xff) - 127 + 15;

    if (exp <= 0) {
        if (exp < -10) return (uint16_t)sign;
        mantissa = (mantissa | 0x00800000u) >> (1 - exp);
        return (uint16_t)(sign | ((mantissa + 0x00001000u) >> 13));
    } else if (exp >= 31) {
        if (mantissa == 0) return (uint16_t)(sign | 0x7c00u);  // inf
        return (uint16_t)(sign | 0x7c00u | ((mantissa + 0x00001000u) >> 13));  // nan
    }
    return (uint16_t)(sign | ((uint32_t)exp << 10) | ((mantissa + 0x00001000u) >> 13));
}

static void dump_tensor_attr(const char* tag, const rknn_tensor_attr& attr) {
    DBG_PRINT("%s idx=%u name=%s n_dims=%u dims=[", tag, attr.index, attr.name, attr.n_dims);
    for (uint32_t i = 0; i < attr.n_dims; i++) {
        DBG_PRINT("%u%s", attr.dims[i], (i + 1 < attr.n_dims ? ", " : ""));
    }
    DBG_PRINT("] n_elems=%u size=%u fmt=%s type=%s qnt_type=%d zp=%d scale=%f\n",
              attr.n_elems, attr.size, tensor_fmt_to_str(attr.fmt), tensor_type_to_str(attr.type),
              attr.qnt_type, attr.zp, attr.scale);
}

// 辅助函数：读取文件
static unsigned char* load_data(FILE* fp, size_t ofst, size_t sz) {
    unsigned char* data;
    int ret;
    data = NULL;
    if (NULL == fp) return NULL;
    ret = fseek(fp, ofst, SEEK_SET);
    if (ret != 0) return NULL;
    data = (unsigned char*)malloc(sz);
    if (data == NULL) return NULL;
    ret = fread(data, 1, sz, fp);
    return data;
}

static unsigned char* load_model(const char* filename, int* model_size) {
    FILE* fp;
    unsigned char* data;
    fp = fopen(filename, "rb");
    if (NULL == fp) {
        printf("Open file %s failed.\n", filename);
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);
    data = load_data(fp, 0, size);
    fclose(fp);
    *model_size = size;
    return data;
}


static int infer_model_class_count(const RknnModelSession& session, int topk_class_count) {
    int det_idx = -1;
    int proto_idx = -1;
    for (int i = 0; i < session.io_num.n_output; ++i) {
        if (session.output_attrs[i].n_dims == 3 && det_idx < 0) {
            det_idx = i;
        } else if (session.output_attrs[i].n_dims == 4 && proto_idx < 0) {
            proto_idx = i;
        }
    }
    if (det_idx < 0 || proto_idx < 0) {
        return std::max(1, topk_class_count);
    }

    const rknn_tensor_attr& det_attr = session.output_attrs[det_idx];
    const rknn_tensor_attr& proto_attr = session.output_attrs[proto_idx];
    const int d1 = (int)det_attr.dims[1];
    const int d2 = (int)det_attr.dims[2];
    const int det_len = std::min(d1, d2);
    const int proto_c = (proto_attr.fmt == RKNN_TENSOR_NHWC)
                            ? (int)proto_attr.dims[3]
                            : (int)proto_attr.dims[1];

    if (det_len == (4 + 1 + 1 + proto_c)) {
        return std::max(1, topk_class_count);
    }
    return std::max(1, det_len - 4 - proto_c);
}

static bool set_enabled_classes(const RknnModelSession& session,
                                int topk_class_count,
                                const std::vector<int>& class_ids,
                                std::vector<uint8_t>& enabled_mask) {
    const int class_count = infer_model_class_count(session, topk_class_count);
    enabled_mask.assign(class_count, 1);

    // 传空列表表示不过滤，启用全部类别。
    if (class_ids.empty()) {
//        printf("Enabled classes: ALL (count=%d)\n", class_count);
        return true;
    }

    std::fill(enabled_mask.begin(), enabled_mask.end(), 0);
    for (int class_id : class_ids) {
        if (class_id < 0 || class_id >= class_count) {
            printf("set_enabled_classes failed: invalid class id %d, valid range [0, %d)\n",
                   class_id, class_count);
            return false;
        }
        enabled_mask[class_id] = 1;
    }
//    printf("Enabled classes configured, count=%zu / %d\n", class_ids.size(), class_count);
    return true;
}

static cv::Point2f calc_seg_center_px(const SegObject& obj) {
    if (!obj.mask.empty()) {
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(obj.mask.clone(), contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        double max_area = 0.0;
        int best_idx = -1;
        for (size_t i = 0; i < contours.size(); ++i) {
            const double area = cv::contourArea(contours[i]);
            if (area > max_area) {
                max_area = area;
                best_idx = static_cast<int>(i);
            }
        }
        if (best_idx >= 0) {
            const cv::Moments m = cv::moments(contours[best_idx]);
            if (std::abs(m.m00) > 1e-6) {
                const float cx = static_cast<float>(m.m10 / m.m00) + obj.box.x;
                const float cy = static_cast<float>(m.m01 / m.m00) + obj.box.y;
                return cv::Point2f(cx, cy);
            }
        }
    }
    // 无有效分割轮廓时回退到检测框中心
    return cv::Point2f(obj.box.x + obj.box.width * 0.5f, obj.box.y + obj.box.height * 0.5f);
}

static void release_session(RknnModelSession& s) {
    if (s.ctx != 0) {
        rknn_destroy(s.ctx);
        s.ctx = 0;
    }
    if (s.model_data != nullptr) {
        free(s.model_data);
        s.model_data = nullptr;
    }
    s.output_attrs.clear();
}

static bool init_model_session(const char* model_path, RknnModelSession& session) {
    // 初始化函数：负责模型加载 + RKNN 上下文初始化 + IO 属性查询。
//    printf("Loading model %s...\n", model_path);
    session.model_data = load_model(model_path, &session.model_data_size);
    if (!session.model_data) {
        return false;
    }

    int ret = rknn_init(&session.ctx, session.model_data, session.model_data_size, 0, NULL);
    if (ret < 0) {
//        printf("rknn_init fail! ret=%d\n", ret);
        return false;
    }

    rknn_sdk_version sdk_ver;
    memset(&sdk_ver, 0, sizeof(sdk_ver));
    ret = rknn_query(session.ctx, RKNN_QUERY_SDK_VERSION, &sdk_ver, sizeof(sdk_ver));
    if (ret == 0) {
//        printf("RKNN API version: %s\n", sdk_ver.api_version);
//        printf("RKNN Driver version: %s\n", sdk_ver.drv_version);
    } else {
//        printf("rknn_query RKNN_QUERY_SDK_VERSION failed: ret=%d\n", ret);
    }

    // 默认使用自动核心调度，通常比固定单核更快。
    ret = rknn_set_core_mask(session.ctx, RKNN_NPU_CORE_AUTO);
    if (ret < 0) {
//        printf("rknn_set_core_mask(RKNN_NPU_CORE_AUTO) failed: ret=%d, fallback CORE_0\n", ret);
        ret = rknn_set_core_mask(session.ctx, RKNN_NPU_CORE_0);
        if (ret < 0) {
//            printf("rknn_set_core_mask(RKNN_NPU_CORE_0) failed: ret=%d (continue)\n", ret);
        } else {
            DBG_PRINT("fallback core mask to RKNN_NPU_CORE_0\n");
        }
    } else {
        DBG_PRINT("set core mask to RKNN_NPU_CORE_AUTO\n");
    }

    ret = rknn_query(session.ctx, RKNN_QUERY_IN_OUT_NUM, &session.io_num, sizeof(session.io_num));
    if (ret < 0) {
//        printf("rknn_query RKNN_QUERY_IN_OUT_NUM fail! ret=%d\n", ret);
        return false;
    }
//    printf("Model Input Num: %d, Output Num: %d\n", session.io_num.n_input, session.io_num.n_output);
    if (session.io_num.n_input < 1 || session.io_num.n_output < 2) {
        printf("Unexpected model io count: n_input=%d n_output=%d. YOLO-seg expects >=1 input and >=2 outputs.\n",
               session.io_num.n_input, session.io_num.n_output);
        return false;
    }

    memset(&session.input0_attr, 0, sizeof(session.input0_attr));
    session.output_attrs.resize(session.io_num.n_output);

    for (int i = 0; i < session.io_num.n_input; i++) {
        rknn_tensor_attr in_attr;
        memset(&in_attr, 0, sizeof(in_attr));
        in_attr.index = i;
        ret = rknn_query(session.ctx, RKNN_QUERY_INPUT_ATTR, &in_attr, sizeof(in_attr));
        if (ret < 0) {
//            printf("rknn_query RKNN_QUERY_INPUT_ATTR(%d) fail! ret=%d\n", i, ret);
            return false;
        }
        dump_tensor_attr("INPUT", in_attr);
        if (i == 0) {
            session.input0_attr = in_attr;
        }
    }

    for (int i = 0; i < session.io_num.n_output; i++) {
        rknn_tensor_attr out_attr;
        memset(&out_attr, 0, sizeof(out_attr));
        out_attr.index = i;
        ret = rknn_query(session.ctx, RKNN_QUERY_OUTPUT_ATTR, &out_attr, sizeof(out_attr));
        if (ret < 0) {
//            printf("rknn_query RKNN_QUERY_OUTPUT_ATTR(%d) fail! ret=%d\n", i, ret);
            return false;
        }
        dump_tensor_attr("OUTPUT", out_attr);
        session.output_attrs[i] = out_attr;
    }
    return true;
}

QPoint yolorecognition::run_seg_predict(const RknnModelSession& session,
                            const cv::Mat& orig_img,
                            int topk_class_count,
                            const std::vector<uint8_t>& enabled_mask,
                            bool draw_overlay,
                            cv::Mat& result_img,const int timefortest) {
    // 预测函数：预处理、推理、后处理；绘制与写盘在计时外执行。
    int x;
    int y;
    float area;
    result_img = orig_img.clone();
    if (result_img.empty()) {
//        printf("Input image is empty.\n");
        return QPoint(-1,-1);
    }

    // 从图像已在内存（clone 完成）起，到每个物体的分割结果（mask 等）就绪为止；
    // 不含：控制台打印、draw_results、main 中的 imwrite。
    const auto t_pipeline_start = std::chrono::steady_clock::now();

    // 输入侧调试耗时（在 postprocess pipeline 结束后打印，避免污染 pipeline 口径）。
    double t_fp32_fill_ms = 0.0;
    double t_fp16_conv_ms = 0.0;
    double t_int8_quant_ms = 0.0;
    // Pipeline 分段耗时（统一在 pipeline 结束后打印）
    double t_letterbox_ms = 0.0;
    double t_cvtcolor_ms = 0.0;
    double t_inputs_set_ms = 0.0;
    double t_outputs_get_ms = 0.0;
    double t_det_pack_ms = 0.0;
    double t_postprocess_ms = 0.0;
    double t_class_filter_ms = 0.0;

    int target_width = 640;
    int target_height = 640;
    // 输入尺寸优先从模型输入 tensor 读取，兼容不同训练分辨率导出的模型。
    if (session.input0_attr.n_dims == 4) {
        if (session.input0_attr.fmt == RKNN_TENSOR_NHWC) {
            target_height = (int)session.input0_attr.dims[1];
            target_width = (int)session.input0_attr.dims[2];
        } else {
            target_height = (int)session.input0_attr.dims[2];
            target_width = (int)session.input0_attr.dims[3];
        }
    }

    float scale = std::min((float)target_width / result_img.cols, (float)target_height / result_img.rows);
    int new_w = std::max(1, (int)std::round(result_img.cols * scale));
    int new_h = std::max(1, (int)std::round(result_img.rows * scale));
    int pad_w = (target_width - new_w) / 2;
    int pad_h = (target_height - new_h) / 2;

    const auto t_letterbox_start = std::chrono::steady_clock::now();
    cv::Mat resized_img;
    cv::resize(result_img, resized_img, cv::Size(new_w, new_h));
    cv::Mat input_img(target_height, target_width, CV_8UC3, cv::Scalar(114, 114, 114));
    resized_img.copyTo(input_img(cv::Rect(pad_w, pad_h, new_w, new_h)));
    const auto t_letterbox_end = std::chrono::steady_clock::now();
    t_letterbox_ms = std::chrono::duration<double, std::milli>(t_letterbox_end - t_letterbox_start).count();

    // 常见 YOLOv8 导出输入为 RGB。
    const auto t_cvtcolor_start = std::chrono::steady_clock::now();
    cv::cvtColor(input_img, input_img, cv::COLOR_BGR2RGB);
    const auto t_cvtcolor_end = std::chrono::steady_clock::now();
    t_cvtcolor_ms = std::chrono::duration<double, std::milli>(t_cvtcolor_end - t_cvtcolor_start).count();

    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].fmt = session.input0_attr.fmt;
    inputs[0].pass_through = 1;

    std::vector<float> input_fp32;
    std::vector<uint16_t> input_fp16;
    std::vector<int8_t> input_int8;
    if (session.input0_attr.type == RKNN_TENSOR_FLOAT16) {
        // FP16 输入：直接填充 fp16 buffer，避免先生成 fp32 再逐元素转换的额外开销。
        const auto t_fp16_fill_start = std::chrono::steady_clock::now();
        input_fp16.resize(input_img.total() * input_img.channels());
        size_t idx = 0;
        for (int y = 0; y < input_img.rows; ++y) {
            const cv::Vec3b* row = input_img.ptr<cv::Vec3b>(y);
            for (int x = 0; x < input_img.cols; ++x) {
                input_fp16[idx++] = fp32_to_fp16_bits(row[x][0] / 255.0f);
                input_fp16[idx++] = fp32_to_fp16_bits(row[x][1] / 255.0f);
                input_fp16[idx++] = fp32_to_fp16_bits(row[x][2] / 255.0f);
            }
        }
        const auto t_fp16_fill_end = std::chrono::steady_clock::now();
        t_fp32_fill_ms = std::chrono::duration<double, std::milli>(t_fp16_fill_end - t_fp16_fill_start).count();
        t_fp16_conv_ms = 0.0;
        inputs[0].type = RKNN_TENSOR_FLOAT16;
        inputs[0].size = input_fp16.size() * sizeof(uint16_t);
        inputs[0].buf = input_fp16.data();
    } else if (session.input0_attr.type == RKNN_TENSOR_FLOAT32) {
        // FP32 输入：归一化到 [0,1] 后喂给模型。
        const auto t_fp32_fill_start = std::chrono::steady_clock::now();
        input_fp32.resize(input_img.total() * input_img.channels());
        size_t idx = 0;
        for (int y = 0; y < input_img.rows; ++y) {
            const cv::Vec3b* row = input_img.ptr<cv::Vec3b>(y);
            for (int x = 0; x < input_img.cols; ++x) {
                input_fp32[idx++] = row[x][0] / 255.0f;
                input_fp32[idx++] = row[x][1] / 255.0f;
                input_fp32[idx++] = row[x][2] / 255.0f;
            }
        }
        const auto t_fp32_fill_end = std::chrono::steady_clock::now();
        t_fp32_fill_ms = std::chrono::duration<double, std::milli>(t_fp32_fill_end - t_fp32_fill_start).count();
        t_fp16_conv_ms = 0.0;
        inputs[0].type = RKNN_TENSOR_FLOAT32;
        inputs[0].size = input_fp32.size() * sizeof(float);
        inputs[0].buf = input_fp32.data();
    } else if (session.input0_attr.type == RKNN_TENSOR_INT8) {
        // INT8 量化模型：按 float 路径的预处理（像素 /255）先得到 float，再按 scale/zp 手动量化成 int8。
        // 这样保证与 FLOAT16/FLOAT32 输入的归一化行为一致。
        const float scale_in = (session.input0_attr.scale == 0.0f) ? 1.0f : session.input0_attr.scale;
        const int zp_in = session.input0_attr.zp;
        const size_t elem_count = input_img.total() * input_img.channels();
        input_int8.resize(elem_count);

        const auto t_int8_quant_start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < elem_count; ++i) {
            const float f = static_cast<float>(input_img.data[i]) / 255.0f;
            int q = static_cast<int>(std::round(f / scale_in)) + zp_in;
            q = std::max(-128, std::min(127, q));
            input_int8[i] = static_cast<int8_t>(q);
        }
        const auto t_int8_quant_end = std::chrono::steady_clock::now();
        t_int8_quant_ms = std::chrono::duration<double, std::milli>(t_int8_quant_end - t_int8_quant_start).count();

        inputs[0].type = RKNN_TENSOR_INT8;
        inputs[0].size = static_cast<uint32_t>(input_int8.size() * sizeof(int8_t));
        inputs[0].buf = input_int8.data();
        // 提供的是模型期望的精确类型（int8），pass_through=1 避免 runtime 再次变换。
        inputs[0].pass_through = 1;
    } else {
        // UINT8 输入模型：直接传原始像素缓冲。
        inputs[0].type = RKNN_TENSOR_UINT8;
        inputs[0].size = input_img.total() * input_img.channels();
        inputs[0].buf = input_img.data;
    }

    DBG_PRINT("set input: type=%s fmt=%s size=%u expected_size=%u\n",
              tensor_type_to_str(inputs[0].type), tensor_fmt_to_str(inputs[0].fmt),
              inputs[0].size, session.input0_attr.size);

    const auto t_inputs_set_start = std::chrono::steady_clock::now();
    int ret = rknn_inputs_set(session.ctx, session.io_num.n_input, inputs);
    const auto t_inputs_set_end = std::chrono::steady_clock::now();
    if (ret < 0) {
//        printf("rknn_inputs_set fail! ret=%d\n", ret);
        return QPoint(-1,-1);
    }
    t_inputs_set_ms = std::chrono::duration<double, std::milli>(t_inputs_set_end - t_inputs_set_start).count();

//    printf("Running inference...\n");
    const auto t_infer_start = std::chrono::steady_clock::now();
    ret = rknn_run(session.ctx, NULL);
    const auto t_infer_end = std::chrono::steady_clock::now();
    if (ret < 0) {
//        printf("rknn_run fail! ret=%d\n", ret);
        return QPoint(-1,-1);
    }
    const double infer_ms = std::chrono::duration<double, std::milli>(t_infer_end - t_infer_start).count();
//    printf("Inference time (rknn_run): %.3f ms\n", infer_ms);

    std::vector<rknn_output> outputs(session.io_num.n_output);
    memset(outputs.data(), 0, sizeof(rknn_output) * outputs.size());
    for (int i = 0; i < session.io_num.n_output; i++) {
        outputs[i].want_float = 1;
    }
    const auto t_outputs_get_start = std::chrono::steady_clock::now();
    ret = rknn_outputs_get(session.ctx, session.io_num.n_output, outputs.data(), NULL);
    const auto t_outputs_get_end = std::chrono::steady_clock::now();
    if (ret < 0) {
//        printf("rknn_outputs_get fail! ret=%d\n", ret);
        return QPoint(-1,-1);
    }
    t_outputs_get_ms = std::chrono::duration<double, std::milli>(t_outputs_get_end - t_outputs_get_start).count();

    int det_idx = -1;
    int proto_idx = -1;
    // 通过维度自动识别 det/proto，减少对固定输出顺序的依赖。
    for (int i = 0; i < session.io_num.n_output; i++) {
        if (session.output_attrs[i].n_dims == 3 && det_idx < 0) {
            det_idx = i;
        } else if (session.output_attrs[i].n_dims == 4 && proto_idx < 0) {
            proto_idx = i;
        }
    }
    if (det_idx < 0 || proto_idx < 0 || outputs[det_idx].buf == NULL || outputs[proto_idx].buf == NULL) {
//        printf("Cannot identify valid det/proto output.\n");
        rknn_outputs_release(session.ctx, session.io_num.n_output, outputs.data());
        return QPoint(-1,-1);
    }

    const rknn_tensor_attr& det_attr = session.output_attrs[det_idx];
    const rknn_tensor_attr& proto_attr = session.output_attrs[proto_idx];
    float* det_raw = (float*)outputs[det_idx].buf;
    float* proto_raw = (float*)outputs[proto_idx].buf;

    int d1 = (int)det_attr.dims[1];
    int d2 = (int)det_attr.dims[2];
    int det_count = std::max(d1, d2);
    int det_len = std::min(d1, d2);
    bool det_transposed = (d1 < d2);
    const auto t_det_pack_start = std::chrono::steady_clock::now();
    std::vector<float> det_packed((size_t)det_count * det_len);
    for (int r = 0; r < det_count; r++) {
        for (int c = 0; c < det_len; c++) {
            if (!det_transposed) {
                det_packed[(size_t)r * det_len + c] = det_raw[(size_t)r * d2 + c];
            } else {
                det_packed[(size_t)r * det_len + c] = det_raw[(size_t)c * d2 + r];
            }
        }
    }
    const auto t_det_pack_end = std::chrono::steady_clock::now();
    t_det_pack_ms = std::chrono::duration<double, std::milli>(t_det_pack_end - t_det_pack_start).count();

    int proto_c = 0, proto_h = 0, proto_w = 0;
    bool proto_nchw = true;
    if (proto_attr.fmt == RKNN_TENSOR_NHWC) {
        proto_nchw = false;
        proto_h = (int)proto_attr.dims[1];
        proto_w = (int)proto_attr.dims[2];
        proto_c = (int)proto_attr.dims[3];
    } else {
        proto_nchw = true;
        proto_c = (int)proto_attr.dims[1];
        proto_h = (int)proto_attr.dims[2];
        proto_w = (int)proto_attr.dims[3];
    }

    int num_classes = det_len - 4 - proto_c;
    if (det_len == (4 + 1 + 1 + proto_c)) {
        // top-k 布局不携带完整类别维，这里走外部参数兜底。
        num_classes = topk_class_count;
    }
    if (num_classes <= 0) num_classes = 1;

    std::vector<SegObject> results;
    const auto t_postprocess_start = std::chrono::steady_clock::now();
    post_process_seg(det_packed.data(), det_count, det_len,
                     proto_raw, proto_c, proto_h, proto_w, proto_nchw,
                     num_classes,
                     target_width, target_height, result_img.cols, result_img.rows,
                     pad_w, pad_h, scale, results);
    const auto t_postprocess_end = std::chrono::steady_clock::now();
    t_postprocess_ms = std::chrono::duration<double, std::milli>(t_postprocess_end - t_postprocess_start).count();
    std::vector<SegObject> filtered_results;
    filtered_results.reserve(results.size());
    const auto t_class_filter_start = std::chrono::steady_clock::now();
    for (const auto& obj : results) {
        if (obj.label < 0 || obj.label >= (int)enabled_mask.size()) {
            continue;
        }
        if (enabled_mask[obj.label]) {
            filtered_results.push_back(obj);
        }
    }
    const auto t_class_filter_end = std::chrono::steady_clock::now();
    t_class_filter_ms = std::chrono::duration<double, std::milli>(t_class_filter_end - t_class_filter_start).count();

    const auto t_pipeline_end = std::chrono::steady_clock::now();
    const double pipeline_ms =
        std::chrono::duration<double, std::milli>(t_pipeline_end - t_pipeline_start).count();
//    printf("Pipeline time (image in memory -> seg objects ready): %.3f ms "
//           "(preprocess + rknn_inputs_set + rknn_run + rknn_outputs_get + postprocess + class filter; "
//           "excludes console print, draw_results, disk write)\n",
//           pipeline_ms);

    // 细分耗时汇总（不计入 pipeline_ms）。
//    printf("Pipeline breakdown (ms): letterbox=%.3f cvtColor=%.3f inputs_set=%.3f rknn_run=%.3f outputs_get=%.3f det_pack=%.3f postprocess=%.3f class_filter=%.3f\n",
//           t_letterbox_ms, t_cvtcolor_ms, t_inputs_set_ms, infer_ms, t_outputs_get_ms, t_det_pack_ms, t_postprocess_ms, t_class_filter_ms);

    // 打印输入侧耗时（不计入 pipeline_ms）。
    tensor_type_to_str(session.input0_attr.type);
    tensor_fmt_to_str(session.input0_attr.fmt);
//    printf("Input preprocess detail: input_type=%s fmt=%s\n",
//           tensor_type_to_str(session.input0_attr.type),
//           tensor_fmt_to_str(session.input0_attr.fmt));
    if (session.input0_attr.type == RKNN_TENSOR_FLOAT16) {
//        printf("  fp16 fill: %.3f ms\n", t_fp32_fill_ms);
    } else if (session.input0_attr.type == RKNN_TENSOR_FLOAT32) {
//        printf("  fp32 fill: %.3f ms\n", t_fp32_fill_ms);
    } else if (session.input0_attr.type == RKNN_TENSOR_INT8) {
//        printf("  int8 quant: %.3f ms, scale=%.6f, zp=%d\n",
//               t_int8_quant_ms, session.input0_attr.scale, session.input0_attr.zp);
    }

    // 输出每个目标的 segmentation 中心点（像素坐标 + 相对坐标），不计入上方 pipeline 耗时
    for (size_t i = 0; i < filtered_results.size(); ++i) {
        const SegObject& obj = filtered_results[i];
        const cv::Point2f center_px = calc_seg_center_px(obj);
        const float center_x_rel = center_px.x / std::max(1, result_img.cols);
        const float center_y_rel = center_px.y / std::max(1, result_img.rows);
    }

    rknn_outputs_release(session.ctx, session.io_num.n_output, outputs.data());

    if (draw_overlay) {
        draw_results(result_img, filtered_results);
            for (const auto& obj : filtered_results)
            {
//                qDebug() << "label:" << obj.label
//                         << "prob:" << obj.prob
//                         << "box:"
//                         << obj.box.x
//                         << obj.box.y
//                         << obj.box.width
//                         << obj.box.height
//                         << "mask size:"
//                         << obj.mask.cols << "x" << obj.mask.rows
//                         << "empty:" << obj.mask.empty();
                int a = obj.box.width * obj.box.height;
                area = obj.box.width * obj.box.height;


//                qDebug()<<"Area of label:"<< obj.label <<" is:" << QString::number(a);
                if (obj.mask.cols > 2000 || obj.mask.rows > 1700)
                {
                    return QPoint(-1,-1);
                }
                x = obj.box.x + obj.box.width / 2;
                y = obj.box.y + obj.box.height / 2;                

                QString now = QDateTime::currentDateTime().toString("--识别完成--yyyy-MM-dd HH:mm:ss.zzz || 第");
                qDebug() << now << timefortest << "次";
                emit objPointSig(QPoint(x,y));
//                if (area>=600000 && (obj.label == 8 || obj.label == 6) && (y <= 1300 && y >= 800))
//                {

//                    qDebug() << "center:" << x << y;
//                    qDebug()<<"muban or zhipi";

//                    emit pointSig(x);
                    // QImage save50 = matToQImage(result_img);
                    // emit frameReadySig(save50);
//                }

            }

            if (!filtered_results.empty())
            {
                const auto& obj = filtered_results.front();

                x = obj.box.x + obj.box.width / 2;
                y = obj.box.y + obj.box.height / 2;

//                qDebug() << "center:" << x << y;

            }
    }
//    float area = obj.box.width * obj.box.height;
    if (area >= 2250)
    {
         return QPoint(x,y);
    }else
    {
        return QPoint(-1,-1);
    }

}
yolorecognition::yolorecognition(QObject* parent):QObject(parent)
{
    
}
int yolorecognition::recognition(const QImage& image,const int timefortest) {

    QString now = QDateTime::currentDateTime().toString("--开始识别--yyyy-MM-dd HH:mm:ss.zzz || 第");
    qDebug() << now << timefortest << "次";
//    if (argc < 3) {
//        printf("Usage: %s <rknn_model> <image_path> [topk_class_count] [draw_overlay:1|0]\n", argv[0]);
//        return -1;
//    }

//    const char* model_path = argv[1];
//    const char* image_path = argv[2];
    int topk_class_count = 80;
//    if (argc >= 4) {
//        topk_class_count = atoi(argv[3]);
//        if (topk_class_count <= 0) {
//            topk_class_count = 80;
//        }
//    }
    bool draw_overlay = true;
//    if (argc >= 5) {
//        draw_overlay = (atoi(argv[4]) != 0);
//    }

    RknnModelSession session;
    if (!init_model_session(model_path, session)) {
        release_session(session);
        return -1;
    }

    // 在模型初始化后、读取图片前设置类别过滤。
    // 示例传空表示启用全部类别；如需过滤可改为 {0,2,5} 这种列表。
    std::vector<uint8_t> enabled_mask;
    const std::vector<int> enabled_class_ids = {};
    if (!set_enabled_classes(session, topk_class_count, enabled_class_ids, enabled_mask)) {
        release_session(session);
        return -1;
    }

//    cv::Mat orig_img = cv::imread(image_path);

    cv::Mat orig_img = QImage2Mat(image);
    if (orig_img.empty()) {
        printf("Read image failed!\n");
        release_session(session);
        return -1;
    }

    cv::Mat result_img;
    const QPoint corPoint = run_seg_predict(session, orig_img, topk_class_count, enabled_mask, draw_overlay, result_img, timefortest);

    QImage img = matToQImage(result_img);

    emit resultImgSig(img);

    release_session(session);
    if (corPoint.x() == -1 || corPoint.y() == -1) {
        return -1;
    }

    if (draw_overlay) {
        const auto t_write_start = std::chrono::steady_clock::now();
        const auto t_write_end = std::chrono::steady_clock::now();
        const double write_ms = std::chrono::duration<double, std::milli>(t_write_end - t_write_start).count();
//        printf("Saved result to result_seg.jpg\n");
//        printf("Disk write time (excluded from predict time): %.3f ms\n", write_ms);
    }
    cv::imwrite("orig_img.jpg", orig_img);
    cv::imwrite("result_seg.jpg", result_img);

//    emit objPointSig(corPoint);

    return 0;
}
cv::Mat yolorecognition::QImage2Mat(const QImage& image)
{
    QImage converted = image.convertToFormat(QImage::Format_RGB888);

    cv::Mat mat(converted.height(),
                converted.width(),
                CV_8UC3,
                const_cast<uchar*>(converted.bits()),
                converted.bytesPerLine());

    cv::Mat mat_clone = mat.clone();  // 深拷贝

    // ⭐ 关键：RGB → BGR
    cv::cvtColor(mat_clone, mat_clone, cv::COLOR_RGB2BGR);

    return mat_clone;
}
//cv::Mat yolorecognition::QImage2Mat(const QImage& image)
//{
//    if (image.format() == QImage::Format_RGB888)
//    {
//        cv::Mat mat(image.height(),
//                    image.width(),
//                    CV_8UC3,
//                    const_cast<uchar*>(image.bits()),
//                    image.bytesPerLine());

//        return mat.clone();  // 深拷贝，线程安全
//    }

//    QImage converted = image.convertToFormat(QImage::Format_RGB888);

//    cv::Mat mat(converted.height(),
//                converted.width(),
//                CV_8UC3,
//                const_cast<uchar*>(converted.bits()),
//                converted.bytesPerLine());

//    return mat.clone();
//}

QImage yolorecognition::matToQImage(const cv::Mat& mat)
{
    if (mat.type() == CV_8UC3)
    {
        return QImage(mat.data,
                      mat.cols,
                      mat.rows,
                      mat.step,
                      QImage::Format_RGB888).rgbSwapped();
    }
    else if (mat.type() == CV_8UC1)
    {
        return QImage(mat.data,
                      mat.cols,
                      mat.rows,
                      mat.step,
                      QImage::Format_Grayscale8);
    }
    else if (mat.type() == CV_8UC4)
    {
        return QImage(mat.data,
                      mat.cols,
                      mat.rows,
                      mat.step,
                      QImage::Format_ARGB32);
    }

    return QImage();
}
