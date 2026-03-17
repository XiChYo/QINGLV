#include "yolothread.h"

#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <opencv2/opencv.hpp>
#include <rknn_api.h>

#include "logger.h"
#include "postprocess.h"

namespace {
constexpr int kDefaultModelInputSize = 640;
constexpr int kClassCount = 9;
constexpr int kMaskCoeffDim = 32;

double clampValue(double value, double low, double high)
{
    return std::max(low, std::min(value, high));
}

const char* tensorTypeToStr(rknn_tensor_type t)
{
    switch (t) {
    case RKNN_TENSOR_FLOAT32: return "FLOAT32";
    case RKNN_TENSOR_FLOAT16: return "FLOAT16";
    case RKNN_TENSOR_INT8: return "INT8";
    case RKNN_TENSOR_UINT8: return "UINT8";
    case RKNN_TENSOR_INT16: return "INT16";
    case RKNN_TENSOR_UINT16: return "UINT16";
    case RKNN_TENSOR_INT32: return "INT32";
    case RKNN_TENSOR_UINT32: return "UINT32";
    default: return "UNKNOWN";
    }
}

uint16_t fp32ToFp16Bits(float v)
{
    union {
        float f;
        uint32_t u;
    } in {};
    in.f = v;
    const uint32_t x = in.u;
    const uint32_t sign = (x >> 16) & 0x8000u;
    uint32_t mantissa = x & 0x007fffffu;
    int32_t exp = static_cast<int32_t>((x >> 23) & 0xff) - 127 + 15;

    if (exp <= 0) {
        if (exp < -10) {
            return static_cast<uint16_t>(sign);
        }
        mantissa = (mantissa | 0x00800000u) >> (1 - exp);
        return static_cast<uint16_t>(sign | ((mantissa + 0x00001000u) >> 13));
    }
    if (exp >= 31) {
        if (mantissa == 0) {
            return static_cast<uint16_t>(sign | 0x7c00u);
        }
        return static_cast<uint16_t>(sign | 0x7c00u | ((mantissa + 0x00001000u) >> 13));
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | ((mantissa + 0x00001000u) >> 13));
}

QPointF calcRelativeCenter(const cv::Rect& box, const std::vector<cv::Point>& contour, int imgW, int imgH)
{
    const double safeW = std::max(1, imgW);
    const double safeH = std::max(1, imgH);
    if (!contour.empty()) {
        const cv::Moments m = cv::moments(contour);
        if (std::abs(m.m00) > 1e-6) {
            const double cx = (m.m10 / m.m00) + box.x;
            const double cy = (m.m01 / m.m00) + box.y;
            return QPointF(clampValue(cx / safeW, 0.0, 1.0), clampValue(cy / safeH, 0.0, 1.0));
        }
    }
    const double cx = box.x + box.width * 0.5;
    const double cy = box.y + box.height * 0.5;
    return QPointF(clampValue(cx / safeW, 0.0, 1.0), clampValue(cy / safeH, 0.0, 1.0));
}

QVector<QPointF> contourToRelativeSegmentation(const cv::Rect& box, const std::vector<cv::Point>& contour, int imgW, int imgH)
{
    QVector<QPointF> segmentation;
    const double safeW = std::max(1, imgW);
    const double safeH = std::max(1, imgH);

    if (!contour.empty()) {
        segmentation.reserve(static_cast<int>(contour.size()));
        for (const cv::Point& p : contour) {
            const double gx = box.x + p.x;
            const double gy = box.y + p.y;
            segmentation.push_back(QPointF(clampValue(gx / safeW, 0.0, 1.0), clampValue(gy / safeH, 0.0, 1.0)));
        }
        return segmentation;
    }

    segmentation.reserve(4);
    const double x1 = clampValue(static_cast<double>(box.x) / safeW, 0.0, 1.0);
    const double y1 = clampValue(static_cast<double>(box.y) / safeH, 0.0, 1.0);
    const double x2 = clampValue(static_cast<double>(box.x + box.width) / safeW, 0.0, 1.0);
    const double y2 = clampValue(static_cast<double>(box.y + box.height) / safeH, 0.0, 1.0);
    segmentation.push_back(QPointF(x1, y1));
    segmentation.push_back(QPointF(x2, y1));
    segmentation.push_back(QPointF(x2, y2));
    segmentation.push_back(QPointF(x1, y2));
    return segmentation;
}
} // namespace

class yolothread::YoloWorker : public QObject
{
    Q_OBJECT
public:
    explicit YoloWorker(const QString& modelPath)
        : m_modelPath(modelPath)
    {
        for (int i = 0; i < kClassCount; ++i) {
            m_enabledClasses.insert(i);
        }
    }

public slots:
    void initialize()
    {
        QMutexLocker locker(&m_mutex);
        QString errorMessage;
        const bool loaded = loadModelLocked(errorMessage);
        m_modelReady = loaded;
        emit modelReadySig(loaded, errorMessage);
        if (!loaded) {
            emit errorMegSig(errorMessage);
            return;
        }
        LOG_INFO("YOLO worker started and model loaded");
    }

    void shutdown()
    {
        QMutexLocker locker(&m_mutex);
        releaseModelLocked();
        m_modelReady = false;
        LOG_INFO("YOLO worker stopped");
    }

    bool setEnabledClassesInWorker(const QList<int>& classIds)
    {
        QMutexLocker locker(&m_mutex);
        if (classIds.isEmpty()) {
            return false;
        }
        QSet<int> nextClasses;
        for (int classId : classIds) {
            if (classId < 0 || classId >= kClassCount) {
                return false;
            }
            nextClasses.insert(classId);
        }
        if (nextClasses.isEmpty()) {
            return false;
        }
        m_enabledClasses = nextClasses;
        return true;
    }

    YoloTaskResult predictInWorker(quint64 taskId, const QImage& image)
    {
        QMutexLocker locker(&m_mutex);
        YoloTaskResult taskResult;
        taskResult.taskId = taskId;

        if (!m_modelReady || m_ctx == 0) {
            taskResult.errorMessage = "Model is not ready";
            return taskResult;
        }
        if (image.isNull()) {
            taskResult.errorMessage = "Input image is empty";
            return taskResult;
        }

        QImage rgbImage = image.convertToFormat(QImage::Format_RGB888);
        cv::Mat rgbMat(rgbImage.height(), rgbImage.width(), CV_8UC3, const_cast<uchar*>(rgbImage.bits()), rgbImage.bytesPerLine());
        cv::Mat bgrMat;
        cv::cvtColor(rgbMat, bgrMat, cv::COLOR_RGB2BGR);

        int targetWidth = kDefaultModelInputSize;
        int targetHeight = kDefaultModelInputSize;
        if (m_inputAttr.n_dims == 4) {
            if (m_inputAttr.fmt == RKNN_TENSOR_NHWC) {
                targetHeight = static_cast<int>(m_inputAttr.dims[1]);
                targetWidth = static_cast<int>(m_inputAttr.dims[2]);
            } else {
                targetHeight = static_cast<int>(m_inputAttr.dims[2]);
                targetWidth = static_cast<int>(m_inputAttr.dims[3]);
            }
        }
        if (targetWidth <= 0 || targetHeight <= 0) {
            targetWidth = kDefaultModelInputSize;
            targetHeight = kDefaultModelInputSize;
        }

        const float scale = std::min(static_cast<float>(targetWidth) / bgrMat.cols, static_cast<float>(targetHeight) / bgrMat.rows);
        const int newW = std::max(1, static_cast<int>(std::round(bgrMat.cols * scale)));
        const int newH = std::max(1, static_cast<int>(std::round(bgrMat.rows * scale)));
        const int padW = (targetWidth - newW) / 2;
        const int padH = (targetHeight - newH) / 2;

        cv::Mat resized;
        cv::resize(bgrMat, resized, cv::Size(newW, newH));
        cv::Mat modelInput(targetHeight, targetWidth, CV_8UC3, cv::Scalar(114, 114, 114));
        resized.copyTo(modelInput(cv::Rect(padW, padH, newW, newH)));
        cv::cvtColor(modelInput, modelInput, cv::COLOR_BGR2RGB);

        rknn_input input {};
        input.index = 0;
        input.fmt = m_inputAttr.fmt;
        input.pass_through = 1;

        std::vector<float> inputFp32;
        std::vector<uint16_t> inputFp16;
        if (m_inputAttr.type == RKNN_TENSOR_FLOAT16 || m_inputAttr.type == RKNN_TENSOR_FLOAT32) {
            inputFp32.resize(modelInput.total() * modelInput.channels());
            size_t idx = 0;
            for (int y = 0; y < modelInput.rows; ++y) {
                const cv::Vec3b* row = modelInput.ptr<cv::Vec3b>(y);
                for (int x = 0; x < modelInput.cols; ++x) {
                    inputFp32[idx++] = row[x][0] / 255.0f;
                    inputFp32[idx++] = row[x][1] / 255.0f;
                    inputFp32[idx++] = row[x][2] / 255.0f;
                }
            }
            if (m_inputAttr.type == RKNN_TENSOR_FLOAT16) {
                inputFp16.resize(inputFp32.size());
                for (size_t i = 0; i < inputFp32.size(); ++i) {
                    inputFp16[i] = fp32ToFp16Bits(inputFp32[i]);
                }
                input.type = RKNN_TENSOR_FLOAT16;
                input.size = static_cast<uint32_t>(inputFp16.size() * sizeof(uint16_t));
                input.buf = inputFp16.data();
            } else {
                input.type = RKNN_TENSOR_FLOAT32;
                input.size = static_cast<uint32_t>(inputFp32.size() * sizeof(float));
                input.buf = inputFp32.data();
            }
        } else {
            input.type = RKNN_TENSOR_UINT8;
            input.size = static_cast<uint32_t>(modelInput.total() * modelInput.channels());
            input.buf = modelInput.data;
        }

        int ret = rknn_inputs_set(m_ctx, 1, &input);
        if (ret < 0) {
            taskResult.errorMessage = QString("rknn_inputs_set failed: ret=%1 input_type=%2").arg(ret).arg(tensorTypeToStr(input.type));
            return taskResult;
        }
        ret = rknn_run(m_ctx, nullptr);
        if (ret < 0) {
            taskResult.errorMessage = QString("rknn_run failed: ret=%1").arg(ret);
            return taskResult;
        }

        std::vector<rknn_output> outputs(m_ioNum.n_output);
        std::memset(outputs.data(), 0, sizeof(rknn_output) * outputs.size());
        for (uint32_t i = 0; i < m_ioNum.n_output; ++i) {
            outputs[i].want_float = 1;
        }
        ret = rknn_outputs_get(m_ctx, m_ioNum.n_output, outputs.data(), nullptr);
        if (ret < 0) {
            taskResult.errorMessage = QString("rknn_outputs_get failed: ret=%1").arg(ret);
            return taskResult;
        }

        int detIdx = -1;
        int protoIdx = -1;
        for (uint32_t i = 0; i < m_ioNum.n_output; ++i) {
            if (m_outputAttrs[i].n_dims == 3 && detIdx < 0) {
                detIdx = static_cast<int>(i);
            } else if (m_outputAttrs[i].n_dims == 4 && protoIdx < 0) {
                protoIdx = static_cast<int>(i);
            }
        }
        if (detIdx < 0 || protoIdx < 0 || outputs[detIdx].buf == nullptr || outputs[protoIdx].buf == nullptr) {
            rknn_outputs_release(m_ctx, m_ioNum.n_output, outputs.data());
            taskResult.errorMessage = "Cannot identify detection/proto outputs";
            return taskResult;
        }

        const rknn_tensor_attr& detAttr = m_outputAttrs[detIdx];
        const rknn_tensor_attr& protoAttr = m_outputAttrs[protoIdx];
        float* detRaw = static_cast<float*>(outputs[detIdx].buf);
        float* protoRaw = static_cast<float*>(outputs[protoIdx].buf);

        const int d1 = static_cast<int>(detAttr.dims[1]);
        const int d2 = static_cast<int>(detAttr.dims[2]);
        const int detCount = std::max(d1, d2);
        const int detLen = std::min(d1, d2);
        const bool detTransposed = (d1 < d2);

        std::vector<float> detPacked(static_cast<size_t>(detCount) * detLen);
        for (int r = 0; r < detCount; ++r) {
            for (int c = 0; c < detLen; ++c) {
                if (!detTransposed) {
                    detPacked[static_cast<size_t>(r) * detLen + c] = detRaw[static_cast<size_t>(r) * d2 + c];
                } else {
                    detPacked[static_cast<size_t>(r) * detLen + c] = detRaw[static_cast<size_t>(c) * d2 + r];
                }
            }
        }

        int protoC = 0;
        int protoH = 0;
        int protoW = 0;
        bool protoNchw = true;
        if (protoAttr.fmt == RKNN_TENSOR_NHWC) {
            protoNchw = false;
            protoH = static_cast<int>(protoAttr.dims[1]);
            protoW = static_cast<int>(protoAttr.dims[2]);
            protoC = static_cast<int>(protoAttr.dims[3]);
        } else {
            protoNchw = true;
            protoC = static_cast<int>(protoAttr.dims[1]);
            protoH = static_cast<int>(protoAttr.dims[2]);
            protoW = static_cast<int>(protoAttr.dims[3]);
        }

        int numClasses = detLen - 4 - kMaskCoeffDim;
        if (detLen == (4 + 1 + 1 + kMaskCoeffDim)) {
            numClasses = kClassCount;
        }
        if (numClasses <= 0) {
            numClasses = kClassCount;
        }

        std::vector<SegObject> postResults;
        post_process_seg(detPacked.data(), detCount, detLen,
                         protoRaw, protoC, protoH, protoW, protoNchw,
                         numClasses,
                         targetWidth, targetHeight, bgrMat.cols, bgrMat.rows,
                         padW, padH, scale, postResults);
        rknn_outputs_release(m_ctx, m_ioNum.n_output, outputs.data());

        for (const SegObject& obj : postResults) {
            if (!m_enabledClasses.contains(obj.label)) {
                continue;
            }
            std::vector<std::vector<cv::Point>> contours;
            if (!obj.mask.empty()) {
                cv::findContours(obj.mask.clone(), contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
            }
            std::vector<cv::Point> largestContour;
            double maxArea = 0.0;
            for (const auto& c : contours) {
                const double area = cv::contourArea(c);
                if (area > maxArea) {
                    maxArea = area;
                    largestContour = c;
                }
            }

            YoloSegmentationObject outObj;
            outObj.classId = obj.label;
            outObj.score = obj.prob;
            outObj.segmentation = contourToRelativeSegmentation(obj.box, largestContour, bgrMat.cols, bgrMat.rows);
            outObj.center = calcRelativeCenter(obj.box, largestContour, bgrMat.cols, bgrMat.rows);
            taskResult.objects.push_back(outObj);
        }

        taskResult.success = true;
        return taskResult;
    }

    void setEnabledClassesAsyncInWorker(const QList<int>& classIds)
    {
        emit setEnabledClassesDoneSig(setEnabledClassesInWorker(classIds));
    }

    void predictAsyncInWorker(quint64 taskId, const QImage& image)
    {
        emit predictDoneSig(predictInWorker(taskId, image));
    }

signals:
    void modelReadySig(bool success, const QString& errorMessage);
    void setEnabledClassesDoneSig(bool success);
    void predictDoneSig(const YoloTaskResult& result);
    void errorMegSig(const QString& errMeg);

private:
    bool loadModelLocked(QString& errorMessage)
    {
        if (m_ctx != 0) {
            return true;
        }

        int modelSize = 0;
        unsigned char* modelData = loadModelData(m_modelPath.toStdString().c_str(), &modelSize);
        if (modelData == nullptr || modelSize <= 0) {
            errorMessage = QString("Load model file failed: %1").arg(m_modelPath);
            return false;
        }
        m_modelBytes.assign(modelData, modelData + modelSize);
        free(modelData);

        int ret = rknn_init(&m_ctx, m_modelBytes.data(), modelSize, 0, nullptr);
        if (ret < 0) {
            m_ctx = 0;
            errorMessage = QString("rknn_init failed: ret=%1").arg(ret);
            return false;
        }
        ret = rknn_query(m_ctx, RKNN_QUERY_IN_OUT_NUM, &m_ioNum, sizeof(m_ioNum));
        if (ret < 0 || m_ioNum.n_input < 1 || m_ioNum.n_output < 2) {
            errorMessage = QString("rknn_query in/out failed: ret=%1").arg(ret);
            releaseModelLocked();
            return false;
        }

        std::memset(&m_inputAttr, 0, sizeof(m_inputAttr));
        m_inputAttr.index = 0;
        ret = rknn_query(m_ctx, RKNN_QUERY_INPUT_ATTR, &m_inputAttr, sizeof(m_inputAttr));
        if (ret < 0) {
            errorMessage = QString("rknn_query input attr failed: ret=%1").arg(ret);
            releaseModelLocked();
            return false;
        }

        m_outputAttrs.resize(m_ioNum.n_output);
        for (uint32_t i = 0; i < m_ioNum.n_output; ++i) {
            std::memset(&m_outputAttrs[i], 0, sizeof(rknn_tensor_attr));
            m_outputAttrs[i].index = i;
            ret = rknn_query(m_ctx, RKNN_QUERY_OUTPUT_ATTR, &m_outputAttrs[i], sizeof(rknn_tensor_attr));
            if (ret < 0) {
                errorMessage = QString("rknn_query output attr(%1) failed: ret=%2").arg(i).arg(ret);
                releaseModelLocked();
                return false;
            }
        }

        errorMessage.clear();
        return true;
    }

    void releaseModelLocked()
    {
        if (m_ctx != 0) {
            rknn_destroy(m_ctx);
            m_ctx = 0;
        }
        m_modelBytes.clear();
        m_outputAttrs.clear();
    }

    static unsigned char* loadModelData(const char* filename, int* modelSize)
    {
        FILE* fp = fopen(filename, "rb");
        if (fp == nullptr) {
            return nullptr;
        }
        if (fseek(fp, 0, SEEK_END) != 0) {
            fclose(fp);
            return nullptr;
        }
        const int size = static_cast<int>(ftell(fp));
        if (size <= 0) {
            fclose(fp);
            return nullptr;
        }
        if (fseek(fp, 0, SEEK_SET) != 0) {
            fclose(fp);
            return nullptr;
        }
        unsigned char* data = static_cast<unsigned char*>(malloc(size));
        if (data == nullptr) {
            fclose(fp);
            return nullptr;
        }
        const size_t readSize = fread(data, 1, static_cast<size_t>(size), fp);
        fclose(fp);
        if (readSize != static_cast<size_t>(size)) {
            free(data);
            return nullptr;
        }
        *modelSize = size;
        return data;
    }

private:
    QString m_modelPath;
    QMutex m_mutex;
    bool m_modelReady = false;
    QSet<int> m_enabledClasses;
    rknn_context m_ctx = 0;
    std::vector<unsigned char> m_modelBytes;
    rknn_input_output_num m_ioNum {};
    rknn_tensor_attr m_inputAttr {};
    std::vector<rknn_tensor_attr> m_outputAttrs;
};

yolothread::yolothread(const QString& modelPath, QObject* parent)
    : QThread(parent)
{
    qRegisterMetaType<QList<int>>("QList<int>");
    qRegisterMetaType<YoloTaskResult>("YoloTaskResult");

    m_worker = new YoloWorker(modelPath);
    m_worker->moveToThread(this);

    connect(this, &QThread::started, m_worker, &YoloWorker::initialize);
    connect(m_worker, &YoloWorker::modelReadySig, this, &yolothread::modelReadySig);
    connect(m_worker, &YoloWorker::setEnabledClassesDoneSig, this, &yolothread::setEnabledClassesDoneSig);
    connect(m_worker, &YoloWorker::predictDoneSig, this, &yolothread::predictDoneSig);
    connect(m_worker, &YoloWorker::errorMegSig, this, &yolothread::errorMegSig);
}

yolothread::~yolothread()
{
    stop();
    if (m_worker != nullptr) {
        delete m_worker;
        m_worker = nullptr;
    }
}

void yolothread::run()
{
    exec();
}

void yolothread::stop()
{
    if (!isRunning()) {
        return;
    }

    if (QThread::currentThread() == this) {
        QMetaObject::invokeMethod(m_worker, "shutdown", Qt::DirectConnection);
    } else {
        QMetaObject::invokeMethod(m_worker, "shutdown", Qt::BlockingQueuedConnection);
    }
    quit();
    wait();
}

bool yolothread::setEnabledClasses(const QList<int>& classIds)
{
    if (!isRunning() || m_worker == nullptr) {
        return false;
    }

    bool ok = false;
    const Qt::ConnectionType ct = (QThread::currentThread() == this) ? Qt::DirectConnection : Qt::BlockingQueuedConnection;
    QMetaObject::invokeMethod(m_worker,
                              "setEnabledClassesInWorker",
                              ct,
                              Q_RETURN_ARG(bool, ok),
                              Q_ARG(QList<int>, classIds));
    return ok;
}

YoloTaskResult yolothread::predict(quint64 taskId, const QImage& image)
{
    YoloTaskResult result;
    result.taskId = taskId;

    if (!isRunning() || m_worker == nullptr) {
        result.errorMessage = "YOLO thread is not running";
        return result;
    }

    const Qt::ConnectionType ct = (QThread::currentThread() == this) ? Qt::DirectConnection : Qt::BlockingQueuedConnection;
    QMetaObject::invokeMethod(m_worker,
                              "predictInWorker",
                              ct,
                              Q_RETURN_ARG(YoloTaskResult, result),
                              Q_ARG(quint64, taskId),
                              Q_ARG(QImage, image));
    return result;
}

bool yolothread::setEnabledClassesInThread(const QList<int>& classIds)
{
    return setEnabledClasses(classIds);
}

YoloTaskResult yolothread::predictInThread(quint64 taskId, const QImage& image)
{
    return predict(taskId, image);
}

void yolothread::setEnabledClassesAsync(const QList<int>& classIds)
{
    if (!isRunning() || m_worker == nullptr) {
        emit setEnabledClassesDoneSig(false);
        return;
    }
    QMetaObject::invokeMethod(m_worker,
                              "setEnabledClassesAsyncInWorker",
                              Qt::QueuedConnection,
                              Q_ARG(QList<int>, classIds));
}

void yolothread::predictAsync(quint64 taskId, const QImage& image)
{
    if (!isRunning() || m_worker == nullptr) {
        YoloTaskResult result;
        result.taskId = taskId;
        result.errorMessage = "YOLO thread is not running";
        emit predictDoneSig(result);
        return;
    }
    QMetaObject::invokeMethod(m_worker,
                              "predictAsyncInWorker",
                              Qt::QueuedConnection,
                              Q_ARG(quint64, taskId),
                              Q_ARG(QImage, image));
}

#include "yolothread.moc"
