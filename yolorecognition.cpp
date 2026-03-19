#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <algorithm>
#include <vector>
#include <opencv2/opencv.hpp>
#include "yolorecognition.h"

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


struct RknnModelSession {
    rknn_context ctx = 0;
    unsigned char* model_data = nullptr;
    int model_data_size = 0;
    rknn_input_output_num io_num {};
    rknn_tensor_attr input0_attr {};
    std::vector<rknn_tensor_attr> output_attrs;
};

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
    printf("Loading model %s...\n", model_path);
    session.model_data = load_model(model_path, &session.model_data_size);
    if (!session.model_data) {
        return false;
    }

    int ret = rknn_init(&session.ctx, session.model_data, session.model_data_size, 0, NULL);
    if (ret < 0) {
        printf("rknn_init fail! ret=%d\n", ret);
        return false;
    }

    rknn_sdk_version sdk_ver;
    memset(&sdk_ver, 0, sizeof(sdk_ver));
    ret = rknn_query(session.ctx, RKNN_QUERY_SDK_VERSION, &sdk_ver, sizeof(sdk_ver));
    if (ret == 0) {
        printf("RKNN API version: %s\n", sdk_ver.api_version);
        printf("RKNN Driver version: %s\n", sdk_ver.drv_version);
    } else {
        printf("rknn_query RKNN_QUERY_SDK_VERSION failed: ret=%d\n", ret);
    }

    ret = rknn_set_core_mask(session.ctx, RKNN_NPU_CORE_0);
    if (ret < 0) {
        printf("rknn_set_core_mask(RKNN_NPU_CORE_0) failed: ret=%d (continue)\n", ret);
    } else {
        DBG_PRINT("set core mask to RKNN_NPU_CORE_0\n");
    }

    ret = rknn_query(session.ctx, RKNN_QUERY_IN_OUT_NUM, &session.io_num, sizeof(session.io_num));
    if (ret < 0) {
        printf("rknn_query RKNN_QUERY_IN_OUT_NUM fail! ret=%d\n", ret);
        return false;
    }
    printf("Model Input Num: %d, Output Num: %d\n", session.io_num.n_input, session.io_num.n_output);
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
            printf("rknn_query RKNN_QUERY_INPUT_ATTR(%d) fail! ret=%d\n", i, ret);
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
            printf("rknn_query RKNN_QUERY_OUTPUT_ATTR(%d) fail! ret=%d\n", i, ret);
            return false;
        }
        dump_tensor_attr("OUTPUT", out_attr);
        session.output_attrs[i] = out_attr;
    }
    return true;
}

QPoint run_seg_predict(const RknnModelSession& session,
                            const cv::Mat& orig_img,
                            int topk_class_count,
                            cv::Mat& result_img) {
    // 预测函数：负责预处理、推理、后处理（det/proto 解码 + 分割绘制）。
    result_img = orig_img.clone();
    if (result_img.empty()) {
        printf("Input image is empty.\n");
        return QPoint(-1,-1);
    }

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

    cv::Mat resized_img;
    cv::resize(result_img, resized_img, cv::Size(new_w, new_h));
    cv::Mat input_img(target_height, target_width, CV_8UC3, cv::Scalar(114, 114, 114));
    resized_img.copyTo(input_img(cv::Rect(pad_w, pad_h, new_w, new_h)));
    // 常见 YOLOv8 导出输入为 RGB。
    cv::cvtColor(input_img, input_img, cv::COLOR_BGR2RGB);

    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].fmt = session.input0_attr.fmt;
    inputs[0].pass_through = 1;

    std::vector<float> input_fp32;
    std::vector<uint16_t> input_fp16;
    std::vector<int8_t> input_int8;
    if (session.input0_attr.type == RKNN_TENSOR_FLOAT16 || session.input0_attr.type == RKNN_TENSOR_FLOAT32) {
        // 浮点输入：归一化到 [0,1] 后喂给模型。
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
        if (session.input0_attr.type == RKNN_TENSOR_FLOAT16) {
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
    } else if (session.input0_attr.type == RKNN_TENSOR_INT8) {
        // INT8 输入：按输入 tensor 的 scale/zp 做量化映射。
        const float scale_in = (session.input0_attr.scale == 0.0f) ? 1.0f : session.input0_attr.scale;
        const int zp_in = session.input0_attr.zp;
        const size_t elem_count = input_img.total() * input_img.channels();
        input_int8.resize(elem_count);
        for (size_t i = 0; i < elem_count; ++i) {
            int q = (int)std::round((float)input_img.data[i] / scale_in) + zp_in;
            q = std::max(-128, std::min(127, q));
            input_int8[i] = (int8_t)q;
        }
        inputs[0].type = RKNN_TENSOR_INT8;
        inputs[0].size = input_int8.size() * sizeof(int8_t);
        inputs[0].buf = input_int8.data();
    } else {
        // UINT8 输入模型：直接传原始像素缓冲。
        inputs[0].type = RKNN_TENSOR_UINT8;
        inputs[0].size = input_img.total() * input_img.channels();
        inputs[0].buf = input_img.data;
    }

    DBG_PRINT("set input: type=%s fmt=%s size=%u expected_size=%u\n",
              tensor_type_to_str(inputs[0].type), tensor_fmt_to_str(inputs[0].fmt),
              inputs[0].size, session.input0_attr.size);

    int ret = rknn_inputs_set(session.ctx, session.io_num.n_input, inputs);
    if (ret < 0) {
        printf("rknn_inputs_set fail! ret=%d\n", ret);
        return QPoint(-1,-1);
    }

    printf("Running inference...\n");
    ret = rknn_run(session.ctx, NULL);
    if (ret < 0) {
        printf("rknn_run fail! ret=%d\n", ret);
        return QPoint(-1,-1);
    }

    std::vector<rknn_output> outputs(session.io_num.n_output);
    memset(outputs.data(), 0, sizeof(rknn_output) * outputs.size());
    for (int i = 0; i < session.io_num.n_output; i++) {
        outputs[i].want_float = 1;
    }
    ret = rknn_outputs_get(session.ctx, session.io_num.n_output, outputs.data(), NULL);
    if (ret < 0) {
        printf("rknn_outputs_get fail! ret=%d\n", ret);
        return QPoint(-1,-1);
    }

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
        printf("Cannot identify valid det/proto output.\n");
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

    int num_classes = det_len - 4 - proto_c;
    if (det_len == (4 + 1 + 1 + proto_c)) {
        // top-k 布局不携带完整类别维，这里走外部参数兜底。
        num_classes = topk_class_count;
    }
    if (num_classes <= 0) num_classes = 1;

    std::vector<SegObject> results;
    post_process_seg(det_packed.data(), det_count, det_len,
                     proto_raw, proto_c, proto_h, proto_w, proto_nchw,
                     num_classes,
                     target_width, target_height, result_img.cols, result_img.rows,
                     pad_w, pad_h, scale, results);
    draw_results(result_img, results);
    for (const auto& obj : results)
    {
        qDebug() << "label:" << obj.label
                 << "prob:" << obj.prob
                 << "box:"
                 << obj.box.x
                 << obj.box.y
                 << obj.box.width
                 << obj.box.height
                 << "mask size:"
                 << obj.mask.cols << "x" << obj.mask.rows
                 << "empty:" << obj.mask.empty();
    }

    if (!results.empty())
    {
        const auto& obj = results.front();

        int x = obj.box.x + obj.box.width / 2;
        int y = obj.box.y + obj.box.height / 2;

        qDebug() << "center:" << x << y;
        rknn_outputs_release(session.ctx, session.io_num.n_output, outputs.data());
        return QPoint(x,y);
    }
    rknn_outputs_release(session.ctx, session.io_num.n_output, outputs.data());


    return QPoint(-1,-1);
}

yolorecognition::yolorecognition(QObject* parent):QObject(parent)
{

}
cv::Mat yolorecognition::QImage2Mat(const QImage& image)
{
    if (image.format() == QImage::Format_RGB888)
    {
        cv::Mat mat(image.height(),
                    image.width(),
                    CV_8UC3,
                    const_cast<uchar*>(image.bits()),
                    image.bytesPerLine());

        return mat.clone();  // 深拷贝，线程安全
    }

    QImage converted = image.convertToFormat(QImage::Format_RGB888);

    cv::Mat mat(converted.height(),
                converted.width(),
                CV_8UC3,
                const_cast<uchar*>(converted.bits()),
                converted.bytesPerLine());

    return mat.clone();
}
int yolorecognition::recognition(const QImage& image) {

    const char* model_path = model;

    int topk_class_count = 80;


    RknnModelSession session;
    if (!init_model_session(model_path, session) && !initTF) {
        release_session(session);
        return -1;
    }
    initTF = true;

    cv::Mat orig_img = QImage2Mat(image);
    if (orig_img.empty()) {
        printf("Read image failed!\n");
        release_session(session);
        return -1;
    }

    cv::Mat result_img;
    const QPoint corPoint = run_seg_predict(session, orig_img, topk_class_count, result_img);
    release_session(session);


//    if (ok.x() == -1 && ok.y() == -1) {
//        return -1;
//    }

    emit objPointSig(corPoint);
    QImage img = matToQImage(result_img);
    emit resultImgSig(img);

//    cv::imwrite("result_seg.jpg", result_img);
//    printf("Saved result to result_seg.jpg\n");
    return 1;
}
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
