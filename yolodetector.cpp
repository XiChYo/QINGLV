#include "yolodetector.h"
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QSettings>
#include <algorithm>
#include <cmath>
#include <vector>

// ONNX Runtime C API headers
extern "C" {
    #include <onnxruntime_c_api.h>
}

// Forward declare ONNX types to avoid including full headers everywhere
typedef struct OrtSession OrtSession;
typedef struct OrtEnv OrtEnv;
typedef struct OrtAllocatorInfo OrtAllocatorInfo;

static OrtApi const* g_ort = nullptr;
static OrtEnv* g_ortEnv = nullptr;

YoloDetector::YoloDetector() {
    // Initialize ONNX Runtime
    g_ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if (g_ort == nullptr) {
        m_lastError = "Failed to get ONNX Runtime API";
        qCritical() << m_lastError;
        return;
    }
    
    // Create environment
    if (g_ort->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "YoloDetector", &g_ortEnv) != ORT_OK) {
        m_lastError = "Failed to create ONNX Runtime environment";
        qCritical() << m_lastError;
        return;
    }
    
    // Load default COCO classes
    loadDefaultClasses();
}

bool YoloDetector::loadDefaultClasses() {
    // Initialize COCO class names (80 classes)
    m_classNames = {
        "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat",
        "traffic light", "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat",
        "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra", "giraffe", "backpack",
        "umbrella", "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard", "sports ball",
        "kite", "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket",
        "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
        "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake",
        "chair", "couch", "potted plant", "bed", "dining table", "toilet", "tv", "laptop",
        "mouse", "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink",
        "refrigerator", "book", "clock", "vase", "scissors", "teddy bear", "hair drier", "toothbrush"
    };
    
    // Build name-to-id map
    m_classNameToId.clear();
    for (int i = 0; i < m_classNames.size(); ++i) {
        m_classNameToId[m_classNames[i]] = i;
    }
    
    return true;
}

YoloDetector::~YoloDetector() {
    release();
    if (g_ortEnv) {
        g_ort->ReleaseEnv(g_ortEnv);
        g_ortEnv = nullptr;
    }
}

bool YoloDetector::initialize(const QString& modelPath, 
                               float confThreshold, 
                               float nmsThreshold,
                               int inputWidth,
                               int inputHeight,
                               const QStringList& customClasses) {
    if (!QFile::exists(modelPath)) {
        m_lastError = QString("Model file not found: %1").arg(modelPath);
        qCritical() << m_lastError;
        return false;
    }
    
    m_modelPath = modelPath;
    m_confThreshold = confThreshold;
    m_nmsThreshold = nmsThreshold;
    m_inputWidth = inputWidth;
    m_inputHeight = inputHeight;
    
    // Load custom classes if provided
    if (!customClasses.isEmpty()) {
        m_classNames = customClasses;
        m_classNameToId.clear();
        for (int i = 0; i < m_classNames.size(); ++i) {
            m_classNameToId[m_classNames[i]] = i;
        }
        qDebug() << "Loaded" << m_classNames.size() << "custom classes";
    }
    
    return loadModel();
}

bool YoloDetector::loadClassesFromConfig(const QString& configPath) {
    if (!QFile::exists(configPath)) {
        m_lastError = QString("Config file not found: %1").arg(configPath);
        qCritical() << m_lastError;
        return false;
    }
    
    QSettings settings(configPath, QSettings::IniFormat);
    settings.beginGroup("YOLO_Classes");
    
    // Read number of classes
    int numClasses = settings.value("count", 0).toInt();
    if (numClasses <= 0) {
        // Try to read classes as a comma-separated list
        QString classList = settings.value("classes", "").toString();
        if (!classList.isEmpty()) {
            m_classNames = classList.split(',', Qt::SkipEmptyParts);
            for (int i = 0; i < m_classNames.size(); ++i) {
                m_classNames[i] = m_classNames[i].trimmed();
            }
        }
    } else {
        // Read classes by index
        m_classNames.clear();
        for (int i = 0; i < numClasses; ++i) {
            QString className = settings.value(QString("class%1").arg(i), "").toString();
            if (!className.isEmpty()) {
                m_classNames.append(className);
            }
        }
    }
    
    settings.endGroup();
    
    if (m_classNames.isEmpty()) {
        m_lastError = "No classes found in config, using default COCO classes";
        qWarning() << m_lastError;
        return loadDefaultClasses();
    }
    
    // Build name-to-id map
    m_classNameToId.clear();
    for (int i = 0; i < m_classNames.size(); ++i) {
        m_classNameToId[m_classNames[i]] = i;
    }
    
    qDebug() << "Loaded" << m_classNames.size() << "classes from config:" << m_classNames;
    return true;
}

void YoloDetector::setEnabledClasses(const QStringList& classes) {
    m_enabledClasses = classes;
    m_enabledClassIds.clear();
    for (const auto& className : classes) {
        int id = getClassId(className);
        if (id >= 0) {
            m_enabledClassIds.append(id);
        }
    }
    m_useClassFilter = !m_enabledClasses.isEmpty();
    qDebug() << "Enabled classes filter:" << m_enabledClasses << "(IDs:" << m_enabledClassIds << ")";
}

void YoloDetector::setEnabledClassIds(const QVector<int>& classIds) {
    m_enabledClassIds = classIds;
    m_enabledClasses.clear();
    for (int id : classIds) {
        if (id >= 0 && id < m_classNames.size()) {
            m_enabledClasses.append(m_classNames[id]);
        }
    }
    m_useClassFilter = !m_enabledClassIds.isEmpty();
}

bool YoloDetector::isClassEnabled(int classId) const {
    if (!m_useClassFilter) {
        return true; // No filter, all classes enabled
    }
    return m_enabledClassIds.contains(classId);
}

int YoloDetector::getClassId(const QString& className) const {
    return m_classNameToId.value(className, -1);
}

bool YoloDetector::initialize(const QString& modelPath, float confThreshold, float nmsThreshold) {
    if (!QFile::exists(modelPath)) {
        m_lastError = QString("Model file not found: %1").arg(modelPath);
        qCritical() << m_lastError;
        return false;
    }
    
    m_modelPath = modelPath;
    m_confThreshold = confThreshold;
    m_nmsThreshold = nmsThreshold;
    
    return loadModel();
}

bool YoloDetector::loadModel() {
    try {
        // Create session options
        OrtSessionOptions* sessionOptions = nullptr;
        if (g_ort->CreateSessionOptions(&sessionOptions) != ORT_OK) {
            m_lastError = "Failed to create session options";
            return false;
        }
        
        // Set intra-op thread count
        g_ort->SetIntraOpNumThreads(sessionOptions, 4);
        
        // Create session
        std::wstring modelPathW = m_modelPath.toStdWString();
        if (g_ort->CreateSession(g_ortEnv, modelPathW.c_str(), sessionOptions, &m_session) != ORT_OK) {
            m_lastError = "Failed to create ONNX session";
            g_ort->ReleaseSessionOptions(sessionOptions);
            return false;
        }
        
        g_ort->ReleaseSessionOptions(sessionOptions);
        
        // Get allocator info
        if (g_ort->CreateCpuAllocatorInfo(MemTypeDefault, MemAllocatorDefault, &m_allocatorInfo) != ORT_OK) {
            m_lastError = "Failed to create allocator info";
            return false;
        }
        
        m_initialized = true;
        qDebug() << "YOLO model loaded successfully:" << m_modelPath;
        return true;
    }
    catch (const std::exception& e) {
        m_lastError = QString("Exception loading model: %1").arg(e.what());
        qCritical() << m_lastError;
        return false;
    }
}

void YoloDetector::release() {
    if (m_session) {
        g_ort->ReleaseSession(m_session);
        m_session = nullptr;
    }
    if (m_allocatorInfo) {
        g_ort->ReleaseAllocatorInfo(m_allocatorInfo);
        m_allocatorInfo = nullptr;
    }
    m_initialized = false;
}

QImage YoloDetector::preprocessImage(const QImage& image) {
    // Resize to model input size while maintaining aspect ratio
    QImage resized = image.scaled(m_inputWidth, m_inputHeight, 
                                   Qt::KeepAspectRatio, Qt::FastTransformation);
    
    // Create output image with padding if needed
    QImage processed(m_inputWidth, m_inputHeight, QImage::Format_RGB888);
    processed.fill(Qt::black);
    
    QPainter painter(&processed);
    painter.drawImage(0, 0, resized);
    
    return processed;
}

QVector<DetectionResult> YoloDetector::detect(const QImage& image) {
    QVector<DetectionResult> results;
    
    if (!m_initialized || image.isNull()) {
        return results;
    }
    
    try {
        // Preprocess image
        QImage processed = preprocessImage(image);
        
        // Prepare input tensor
        std::vector<float> inputTensorValues(m_inputChannels * m_inputWidth * m_inputHeight);
        
        // Convert QImage to NCHW format (normalized to 0-1)
        for (int y = 0; y < m_inputHeight; ++y) {
            for (int x = 0; x < m_inputWidth; ++x) {
                QRgb pixel = processed.pixel(x, y);
                int idx = y * m_inputWidth + x;
                inputTensorValues[idx] = qRed(pixel) / 255.0f;
                inputTensorValues[idx + m_inputWidth * m_inputHeight] = qGreen(pixel) / 255.0f;
                inputTensorValues[idx + 2 * m_inputWidth * m_inputHeight] = qBlue(pixel) / 255.0f;
            }
        }
        
        // Create input tensor
        std::vector<int64_t> inputShape = {1, 3, m_inputHeight, m_inputWidth};
        OrtValue* inputTensor = nullptr;
        if (g_ort->CreateTensorWithDataAsOrtValue(
                m_allocatorInfo, inputTensorValues.data(),
                inputTensorValues.size() * sizeof(float),
                inputShape.data(), inputShape.size(),
                ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                &inputTensor) != ORT_OK) {
            m_lastError = "Failed to create input tensor";
            return results;
        }
        
        // Get input and output names
        OrtAllocator* allocator = nullptr;
        g_ort->GetAllocatorWithInfo(m_allocatorInfo, &allocator);
        
        char* inputName = g_ort->SessionGetInputName(m_session, 0, allocator);
        char* outputName = g_ort->SessionGetOutputName(m_session, 0, allocator);
        
        const char* inputNames[] = {inputName};
        const char* outputNames[] = {outputName};
        
        // Run inference
        OrtValue* outputTensor = nullptr;
        if (g_ort->Run(m_session, nullptr, inputNames, 
                       (const OrtValue* const*)&inputTensor, 1,
                       outputNames, 1, &outputTensor) != ORT_OK) {
            m_lastError = "Failed to run inference";
            g_ort->Release(inputTensor);
            g_ort->Free(inputName, allocator);
            g_ort->Free(outputName, allocator);
            return results;
        }
        
        // Get output data
        float* outputData = nullptr;
        if (g_ort->GetTensorMutableData(outputTensor, (void**)&outputData) != ORT_OK) {
            m_lastError = "Failed to get output data";
            g_ort->Release(inputTensor);
            g_ort->Release(outputTensor);
            g_ort->Free(inputName, allocator);
            g_ort->Free(outputName, allocator);
            return results;
        }
        
        // Get output shape
        size_t numOutputDims = 0;
        g_ort->GetNumOfDimensions(outputTensor, &numOutputDims);
        
        std::vector<int64_t> outputShape(numOutputDims);
        g_ort->GetDimensions(outputTensor, outputShape.data(), numOutputDims);
        
        // Postprocess results
        results = postprocessResults(outputData, image.width(), image.height());
        
        // Cleanup
        g_ort->Release(inputTensor);
        g_ort->Release(outputTensor);
        g_ort->Free(inputName, allocator);
        g_ort->Free(outputName, allocator);
    }
    catch (const std::exception& e) {
        m_lastError = QString("Detection error: %1").arg(e.what());
        qCritical() << m_lastError;
    }
    
    return results;
}

QVector<DetectionResult> YoloDetector::postprocessResults(float* output, int imgWidth, int imgHeight) {
    QVector<DetectionResult> results;
    
    // YOLO output format: [batch, detections, 84] where 84 = 4 (bbox) + 80 (classes)
    // Adjust based on your model architecture
    int numDetections = 8400; // Typical for YOLOv8
    int numClasses = 80;
    
    struct Detection {
        QRect box;
        float confidence;
        int classId;
    };
    
    std::vector<Detection> detections;
    
    float scaleW = (float)imgWidth / m_inputWidth;
    float scaleH = (float)imgHeight / m_inputHeight;
    
    for (int i = 0; i < numDetections; ++i) {
        // Find class with max confidence
        float maxConf = 0;
        int maxClassId = -1;
        
        for (int c = 0; c < numClasses; ++c) {
            float conf = output[c * numDetections + i + 4]; // Assuming CHW format
            if (conf > maxConf) {
                maxConf = conf;
                maxClassId = c;
            }
        }
        
        if (maxConf >= m_confThreshold && maxClassId >= 0) {
            // Check if this class is enabled (if filter is active)
            if (!isClassEnabled(maxClassId)) {
                continue; // Skip disabled classes
            }
            
            // Get bounding box (center_x, center_y, width, height)
            float cx = output[0 * numDetections + i];
            float cy = output[1 * numDetections + i];
            float w = output[2 * numDetections + i];
            float h = output[3 * numDetections + i];
            
            // Convert to corner coordinates
            float x1 = (cx - w / 2) * scaleW;
            float y1 = (cy - h / 2) * scaleH;
            float x2 = (cx + w / 2) * scaleW;
            float y2 = (cy + h / 2) * scaleH;
            
            Detection det;
            det.box = QRect(QPoint(x1, y1), QPoint(x2, y2));
            det.confidence = maxConf;
            det.classId = maxClassId;
            detections.push_back(det);
        }
    }
    
    // Non-Maximum Suppression (NMS)
    std::sort(detections.begin(), detections.end(), 
              [](const Detection& a, const Detection& b) {
                  return a.confidence > b.confidence;
              });
    
    std::vector<bool> suppressed(detections.size(), false);
    
    for (size_t i = 0; i < detections.size(); ++i) {
        if (suppressed[i]) continue;
        
        DetectionResult result;
        result.className = m_classNames.value(detections[i].classId, "unknown");
        result.confidence = detections[i].confidence;
        result.boundingBox = detections[i].box;
        results.push_back(result);
        
        // Suppress overlapping detections
        for (size_t j = i + 1; j < detections.size(); ++j) {
            if (suppressed[j]) continue;
            
            QRect rect1 = detections[i].box;
            QRect rect2 = detections[j].box;
            
            // Calculate IoU
            int intersection = rect1.intersected(rect2).area();
            int unionArea = rect1.area() + rect2.area() - intersection;
            float iou = unionArea > 0 ? (float)intersection / unionArea : 0;
            
            if (iou > m_nmsThreshold) {
                suppressed[j] = true;
            }
        }
    }
    
    return results;
}
