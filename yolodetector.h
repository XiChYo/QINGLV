#ifndef YOLO_DETECTOR_H
#define YOLO_DETECTOR_H

#include <QString>
#include <QImage>
#include <QVector>
#include <QRect>
#include <QStringList>
#include <QMap>

struct DetectionResult {
    QString className;
    int classId;
    float confidence;
    QRect boundingBox;
};

class YoloDetector {
public:
    YoloDetector();
    ~YoloDetector();

    // 初始化 - 支持自定义类别
    bool initialize(const QString& modelPath, 
                    float confThreshold = 0.5f, 
                    float nmsThreshold = 0.4f,
                    int inputWidth = 640,
                    int inputHeight = 640,
                    const QStringList& customClasses = QStringList());
    
    // 从配置文件加载类别
    bool loadClassesFromConfig(const QString& configPath);
    
    void release();
    
    QVector<DetectionResult> detect(const QImage& image);
    
    // 设置启用的类别过滤器 (空列表表示检测所有类别)
    void setEnabledClasses(const QStringList& classes);
    void setEnabledClassIds(const QVector<int>& classIds);
    
    bool isInitialized() const { return m_initialized; }
    QString getLastError() const { return m_lastError; }
    QStringList getAvailableClasses() const { return m_classNames; }
    int getClassId(const QString& className) const;

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
    
    // Class names (COCO dataset or custom)
    QStringList m_classNames;
    QMap<QString, int> m_classNameToId;
    
    // 启用的类别过滤器 (空表示全部启用)
    QStringList m_enabledClasses;
    QVector<int> m_enabledClassIds;
    bool m_useClassFilter = false;
    
    bool loadModel();
    bool loadDefaultClasses();
    QImage preprocessImage(const QImage& image);
    QVector<DetectionResult> postprocessResults(float* output, int imgWidth, int imgHeight);
    bool isClassEnabled(int classId) const;
};

#endif // YOLO_DETECTOR_H
