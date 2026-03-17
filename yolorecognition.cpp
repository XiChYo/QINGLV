#include "yolorecognition.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <vector>
#include <opencv2/opencv.hpp>
#include <rknn_api.h>
#include "postprocess.h"

// Set to 1 to enable verbose diagnostic logs.
#ifndef RKNN_DEBUG
#define RKNN_DEBUG 1
#endif

#if RKNN_DEBUG
#define DBG_PRINT(...) printf("[DEBUG] " __VA_ARGS__)
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

yolorecognition::yolorecognition()
{

}

int yolorecognition::recognition(int argc, char** argv)
{
    if (argc < 3) {
        printf("Usage: %s <rknn_model> <image_path>\n", argv[0]);
        return -1;
    }

    char* model_path = argv[1];
    char* image_path = argv[2];

    // 1. 加载模型
    printf("Loading model %s...\n", model_path);
    int model_data_size = 0;
    unsigned char* model_data = load_model(model_path, &model_data_size);
    if (!model_data) return -1;

    // 2. 初始化 RKNN
    rknn_context ctx;
    int ret = rknn_init(&ctx, model_data, model_data_size, 0, NULL);
    if (ret < 0) {
        printf("rknn_init fail! ret=%d\n", ret);
        free(model_data);
        return -1;
    }

    // Print SDK/driver versions for compatibility diagnosis.
    rknn_sdk_version sdk_ver;
    memset(&sdk_ver, 0, sizeof(sdk_ver));
    ret = rknn_query(ctx, RKNN_QUERY_SDK_VERSION, &sdk_ver, sizeof(sdk_ver));
    if (ret == 0) {
        printf("RKNN API version: %s\n", sdk_ver.api_version);
        printf("RKNN Driver version: %s\n", sdk_ver.drv_version);
    } else {
        printf("rknn_query RKNN_QUERY_SDK_VERSION failed: ret=%d\n", ret);
    }

    // Workaround: force single NPU core to avoid some runtime crashes on certain boards.
    ret = rknn_set_core_mask(ctx, RKNN_NPU_CORE_0);
    if (ret < 0) {
        printf("rknn_set_core_mask(RKNN_NPU_CORE_0) failed: ret=%d (continue)\n", ret);
    } else {
        DBG_PRINT("set core mask to RKNN_NPU_CORE_0\n");
    }

    // 3. 获取模型输入输出信息
    rknn_input_output_num io_num;
    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret < 0) {
        printf("rknn_query RKNN_QUERY_IN_OUT_NUM fail! ret=%d\n", ret);
        rknn_destroy(ctx);
        free(model_data);
        return -1;
    }
    printf("Model Input Num: %d, Output Num: %d\n", io_num.n_input, io_num.n_output);
    if (io_num.n_input < 1 || io_num.n_output < 2) {
        printf("Unexpected model io count: n_input=%d n_output=%d. YOLO-seg expects >=1 input and >=2 outputs.\n",
               io_num.n_input, io_num.n_output);
        rknn_destroy(ctx);
        free(model_data);
        return -1;
    }

    // Dump input/output attributes for debugging.
    rknn_tensor_attr input0_attr;
    memset(&input0_attr, 0, sizeof(input0_attr));
    std::vector<rknn_tensor_attr> output_attrs(io_num.n_output);

    for (int i = 0; i < io_num.n_input; i++) {
        rknn_tensor_attr in_attr;
        memset(&in_attr, 0, sizeof(in_attr));
        in_attr.index = i;
        ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &in_attr, sizeof(in_attr));
        if (ret < 0) {
            printf("rknn_query RKNN_QUERY_INPUT_ATTR(%d) fail! ret=%d\n", i, ret);
            rknn_destroy(ctx);
            free(model_data);
            return -1;
        }
        dump_tensor_attr("INPUT", in_attr);
        if (i == 0) {
            input0_attr = in_attr;
        }
    }
    for (int i = 0; i < io_num.n_output; i++) {
        rknn_tensor_attr out_attr;
        memset(&out_attr, 0, sizeof(out_attr));
        out_attr.index = i;
        ret = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &out_attr, sizeof(out_attr));
        if (ret < 0) {
            printf("rknn_query RKNN_QUERY_OUTPUT_ATTR(%d) fail! ret=%d\n", i, ret);
            rknn_destroy(ctx);
            free(model_data);
            return -1;
        }
        dump_tensor_attr("OUTPUT", out_attr);
        output_attrs[i] = out_attr;
    }

    // 4. 读取图像
    cv::Mat orig_img = cv::imread(image_path);
    if (orig_img.empty()) {
        printf("Read image failed!\n");
        return -1;
    }

    // 5. 预处理 (Letterbox) - derive size from model input attr.
    int target_width = 640;
    int target_height = 640;
    if (input0_attr.n_dims == 4) {
        if (input0_attr.fmt == RKNN_TENSOR_NHWC) {
            target_height = (int)input0_attr.dims[1];
            target_width = (int)input0_attr.dims[2];
        } else {
            target_height = (int)input0_attr.dims[2];
            target_width = (int)input0_attr.dims[3];
        }
    }

    float scale = std::min((float)target_width / orig_img.cols, (float)target_height / orig_img.rows);
    int new_w = (int)(orig_img.cols * scale);
    int new_h = (int)(orig_img.rows * scale);
    int pad_w = (target_width - new_w) / 2;
    int pad_h = (target_height - new_h) / 2;

    cv::Mat resized_img;
    cv::resize(orig_img, resized_img, cv::Size(new_w, new_h));

    cv::Mat input_img(target_height, target_width, CV_8UC3, cv::Scalar(114, 114, 114));
    resized_img.copyTo(input_img(cv::Rect(pad_w, pad_h, new_w, new_h)));

    // OpenCV 默认 BGR，RKNN 模型通常训练时也是 BGR (Ultralytics 默认)
    // 或者 RGB。这里取决于模型导出时的设置。
    // YOLOv8 默认是 RGB 训练的，所以需要转换。
    cv::cvtColor(input_img, input_img, cv::COLOR_BGR2RGB);

    // 6. 设置输入
    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    std::vector<float> input_fp32;
    std::vector<uint16_t> input_fp16;
    if (input0_attr.type == RKNN_TENSOR_FLOAT16 || input0_attr.type == RKNN_TENSOR_FLOAT32) {
        input_fp32.resize(input_img.total() * input_img.channels());
        size_t idx = 0;
        for (int y = 0; y < input_img.rows; y++) {
            const cv::Vec3b* row = input_img.ptr<cv::Vec3b>(y);
            for (int x = 0; x < input_img.cols; x++) {
                input_fp32[idx++] = row[x][0] / 255.0f;
                input_fp32[idx++] = row[x][1] / 255.0f;
                input_fp32[idx++] = row[x][2] / 255.0f;
            }
        }

        if (input0_attr.type == RKNN_TENSOR_FLOAT16) {
            input_fp16.resize(input_fp32.size());
            for (size_t i = 0; i < input_fp32.size(); ++i) {
                input_fp16[i] = fp32_to_fp16_bits(input_fp32[i]);
            }
            inputs[0].type = RKNN_TENSOR_FLOAT16;
            inputs[0].size = input_fp16.size() * sizeof(uint16_t);
            inputs[0].buf = input_fp16.data();
        } else {
            inputs[0].type = RKNN_TENSOR_FLOAT32;
            inputs[0].size = input_fp32.size() * sizeof(float);
            inputs[0].buf = input_fp32.data();
        }
    } else {
        inputs[0].type = RKNN_TENSOR_UINT8;
        inputs[0].size = input_img.total() * input_img.channels();
        inputs[0].buf = input_img.data;
    }
    inputs[0].fmt = input0_attr.fmt;
    // For exact-typed input buffers (e.g. FLOAT16), pass-through reduces runtime conversion risk.
    inputs[0].pass_through = 1;
    DBG_PRINT("set input: type=%s fmt=%s size=%u expected_size=%u\n",
              tensor_type_to_str(inputs[0].type), tensor_fmt_to_str(inputs[0].fmt),
              inputs[0].size, input0_attr.size);

    ret = rknn_inputs_set(ctx, io_num.n_input, inputs);
    if (ret < 0) {
        printf("rknn_inputs_set fail! ret=%d\n", ret);
        rknn_destroy(ctx);
        free(model_data);
        return -1;
    }

    // 7. 运行推理
    printf("Running inference...\n");
    ret = rknn_run(ctx, NULL);
    if (ret < 0) {
        printf("rknn_run fail! ret=%d\n", ret);
        rknn_destroy(ctx);
        free(model_data);
        return -1;
    }

    // 8. 获取输出
    rknn_output outputs[io_num.n_output];
    memset(outputs, 0, sizeof(outputs));
    for (int i = 0; i < io_num.n_output; i++) {
        outputs[i].want_float = 1; // 强制转换为 float 以便后处理
    }
    ret = rknn_outputs_get(ctx, io_num.n_output, outputs, NULL);
    if (ret < 0) {
        printf("rknn_outputs_get fail! ret=%d\n", ret);
        rknn_destroy(ctx);
        free(model_data);
        return -1;
    }

    for (int i = 0; i < io_num.n_output; i++) {
        DBG_PRINT("outputs[%d]: buf=%p is_prealloc=%d want_float=%d\n",
                  i, outputs[i].buf, outputs[i].is_prealloc, outputs[i].want_float);
        if (outputs[i].buf == NULL) {
            printf("outputs[%d].buf is NULL, abort postprocess to avoid crash.\n", i);
            rknn_outputs_release(ctx, io_num.n_output, outputs);
            rknn_destroy(ctx);
            free(model_data);
            return -1;
        }
    }

    // 9. 后处理：自动识别 det/proto 输出并兼容 [1,300,38] 与 [1,116,8400]
    int det_idx = -1;
    int proto_idx = -1;
    for (int i = 0; i < io_num.n_output; i++) {
        if (output_attrs[i].n_dims == 3 && det_idx < 0) {
            det_idx = i;
        } else if (output_attrs[i].n_dims == 4 && proto_idx < 0) {
            proto_idx = i;
        }
    }
    if (det_idx < 0 || proto_idx < 0) {
        printf("Cannot identify det/proto output. Need one 3D det output and one 4D proto output.\n");
        rknn_outputs_release(ctx, io_num.n_output, outputs);
        rknn_destroy(ctx);
        free(model_data);
        return -1;
    }

    const rknn_tensor_attr& det_attr = output_attrs[det_idx];
    const rknn_tensor_attr& proto_attr = output_attrs[proto_idx];
    float* det_raw = (float*)outputs[det_idx].buf;
    float* proto_raw = (float*)outputs[proto_idx].buf;
    DBG_PRINT("det_idx=%d proto_idx=%d det_ptr=%p proto_ptr=%p\n", det_idx, proto_idx, det_raw, proto_raw);

    int d1 = (int)det_attr.dims[1];
    int d2 = (int)det_attr.dims[2];
    int det_count = std::max(d1, d2);
    int det_len = std::min(d1, d2);
    bool det_transposed = (d1 < d2); // e.g. 116x8400
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

    int num_classes = det_len - 4 - 32;
    if (det_len == (4 + 1 + 1 + 32)) {
        // top-k output includes score + class_id, class count is not encoded in row length.
        num_classes = 9;
    }
    if (num_classes <= 0) num_classes = 1;

    DBG_PRINT("det_count=%d det_len=%d det_transposed=%d num_classes=%d proto=(%d,%d,%d,%s)\n",
              det_count, det_len, (int)det_transposed, num_classes,
              proto_c, proto_h, proto_w, proto_nchw ? "NCHW" : "NHWC");

    std::vector<SegObject> results;
    post_process_seg(det_packed.data(), det_count, det_len,
                     proto_raw, proto_c, proto_h, proto_w, proto_nchw,
                     num_classes,
                     target_width, target_height, orig_img.cols, orig_img.rows,
                     pad_w, pad_h, scale, results);

    // 10. 绘制结果
    draw_results(orig_img, results);

    cv::imwrite("result_seg.jpg", orig_img);
    printf("Saved result to result_seg.jpg\n");

    // 释放资源
    rknn_outputs_release(ctx, io_num.n_output, outputs);
    rknn_destroy(ctx);
    free(model_data);

    return 0;
}
