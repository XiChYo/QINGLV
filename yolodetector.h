#ifndef YOLO_DETECTOR_H
#define YOLO_DETECTOR_H

#include <QString>
#include <QImage>
#include <QVector>
#include <QRect>

struct DetectionResult {
    QString className;
    float confidence;
    QRect boundingBox;
};

class YoloDetector {
public:
    YoloDetector();
    ~YoloDetector();

    bool initialize(const QString& modelPath, float confThreshold = 0.5f, float nmsThreshold = 0.4f);
    void release();
    
    QVector<DetectionResult> detect(const QImage& image);
    
    bool isInitialized() const { return m_initialized; }
    QString getLastError() const { return m_lastError; }

private:
    bool m_initialized = false;
    float m_confThreshold;
    float m_nmsThreshold;
    QString m_modelPath;
    QString m_lastError;
    
    // ONNX Runtime session (void* to avoid header dependency)
    void* m_session = nullptr;
    void* m_allocatorInfo = nullptr;
    
    // Model input info
    int m_inputWidth = 640;
    int m_inputHeight = 640;
    int m_inputChannels = 3;
    
    // Class names (COCO dataset - can be customized)
    QStringList m_classNames;
    
    bool loadModel();
    QImage preprocessImage(const QImage& image);
    QVector<DetectionResult> postprocessResults(float* output, int imgWidth, int imgHeight);
};

#endif // YOLO_DETECTOR_H
