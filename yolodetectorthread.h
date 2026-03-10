#ifndef YOLO_DETECTOR_THREAD_H
#define YOLO_DETECTOR_THREAD_H

#include <QThread>
#include <QImage>
#include <QVector>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <QAtomicInt>
#include "yolodetector.h"

struct DetectionTask {
    QImage image;
    QString taskId;
    qint64 timestamp;
};

class YoloDetectorThread : public QThread {
    Q_OBJECT

public:
    explicit YoloDetectorThread(QObject* parent = nullptr);
    ~YoloDetectorThread();

    // 初始化检测器
    bool initialize(const QString& modelPath,
                    float confThreshold = 0.5f,
                    float nmsThreshold = 0.4f,
                    int inputWidth = 640,
                    int inputHeight = 640,
                    const QStringList& customClasses = QStringList());
    
    // 从配置文件加载类别
    bool loadClassesFromConfig(const QString& configPath);
    
    // 设置类别过滤器
    void setEnabledClasses(const QStringList& classes);
    void setEnabledClassIds(const QVector<int>& classIds);
    
    // 提交检测任务 (线程安全)
    QString submitDetectionTask(const QImage& image);
    
    // 同步检测 (阻塞直到完成)
    QVector<DetectionResult> detectSync(const QImage& image, int timeoutMs = 5000);
    
    // 状态查询
    bool isInitialized() const;
    bool isRunning() const { return m_running; }
    int getPendingTaskCount() const;
    QString getLastError() const;
    QStringList getAvailableClasses() const;
    
    // 性能统计
    int getTotalProcessedCount() const { return m_totalProcessed.load(); }
    double getAverageInferenceTime() const { return m_avgInferenceTime; }

signals:
    // 检测结果信号
    void detectionResult(const QString& taskId, 
                         const QVector<DetectionResult>& results,
                         qint64 processingTimeMs);
    
    // 状态信号
    void initialized();
    void error(const QString& error);
    void taskQueueFull(int currentSize, int maxSize);

public slots:
    // 控制槽
    void startDetection();
    void stopDetection();
    void clearTaskQueue();
    
    // 动态配置
    void setConfidenceThreshold(float threshold);
    void setNmsThreshold(float threshold);

protected:
    void run() override;

private:
    YoloDetector m_detector;
    
    // 任务队列
    QQueue<DetectionTask> m_taskQueue;
    mutable QMutex m_queueMutex;
    QWaitCondition m_queueCondition;
    
    // 运行状态
    QAtomicInt m_running;
    QAtomicInt m_totalProcessed;
    
    // 配置
    int m_maxQueueSize = 10;
    float m_avgInferenceTime = 0.0;
    
    // 任务 ID 计数器
    QAtomicInt m_taskCounter;
    
    // 内部方法
    QString generateTaskId();
    void processTask(const DetectionTask& task);
    double calculateInferenceTime(qint64 start, qint64 end);
};

#endif // YOLO_DETECTOR_THREAD_H
