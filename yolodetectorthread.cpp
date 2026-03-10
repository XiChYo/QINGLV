#include "yolodetectorthread.h"
#include <QDebug>
#include <QDateTime>
#include <QUuid>
#include <QElapsedTimer>

YoloDetectorThread::YoloDetectorThread(QObject* parent)
    : QThread(parent)
    , m_running(false)
    , m_totalProcessed(0)
    , m_taskCounter(0)
{
}

YoloDetectorThread::~YoloDetectorThread() {
    stopDetection();
    wait();
}

bool YoloDetectorThread::initialize(const QString& modelPath,
                                     float confThreshold,
                                     float nmsThreshold,
                                     int inputWidth,
                                     int inputHeight,
                                     const QStringList& customClasses) {
    bool result = m_detector.initialize(modelPath, confThreshold, nmsThreshold, 
                                         inputWidth, inputHeight, customClasses);
    if (result) {
        emit initialized();
        qDebug() << "YOLO Detector Thread initialized successfully";
    } else {
        emit error(m_detector.getLastError());
        qCritical() << "YOLO Detector Thread initialization failed:" << m_detector.getLastError();
    }
    return result;
}

bool YoloDetectorThread::loadClassesFromConfig(const QString& configPath) {
    return m_detector.loadClassesFromConfig(configPath);
}

void YoloDetectorThread::setEnabledClasses(const QStringList& classes) {
    m_detector.setEnabledClasses(classes);
}

void YoloDetectorThread::setEnabledClassIds(const QVector<int>& classIds) {
    m_detector.setEnabledClassIds(classIds);
}

QString YoloDetectorThread::submitDetectionTask(const QImage& image) {
    if (!m_running) {
        qWarning() << "YOLO detector thread is not running";
        return QString();
    }
    
    if (image.isNull()) {
        qWarning() << "Received null image for detection";
        return QString();
    }
    
    DetectionTask task;
    task.image = image.copy(); // Deep copy for thread safety
    task.taskId = generateTaskId();
    task.timestamp = QDateTime::currentMSecsSinceEpoch();
    
    {
        QMutexLocker locker(&m_queueMutex);
        
        // Check queue size
        if (m_taskQueue.size() >= m_maxQueueSize) {
            qWarning() << "Task queue is full (" << m_taskQueue.size() 
                       << "/" << m_maxQueueSize << "), dropping oldest task";
            m_taskQueue.dequeue(); // Drop oldest task
            emit taskQueueFull(m_taskQueue.size(), m_maxQueueSize);
        }
        
        m_taskQueue.enqueue(task);
        qDebug() << "Task submitted:" << task.taskId 
                 << "| Queue size:" << m_taskQueue.size();
    }
    
    m_queueCondition.wakeOne();
    return task.taskId;
}

QVector<DetectionResult> YoloDetectorThread::detectSync(const QImage& image, int timeoutMs) {
    if (!m_detector.isInitialized()) {
        qWarning() << "Detector not initialized";
        return QVector<DetectionResult>();
    }
    
    if (image.isNull()) {
        qWarning() << "Null image provided";
        return QVector<DetectionResult>();
    }
    
    QElapsedTimer timer;
    timer.start();
    
    QVector<DetectionResult> results = m_detector.detect(image);
    
    qint64 elapsed = timer.elapsed();
    m_totalProcessed.ref();
    
    // Update average inference time
    m_avgInferenceTime = (m_avgInferenceTime * (m_totalProcessed - 1) + elapsed) / m_totalProcessed;
    
    qDebug() << "Sync detection completed in" << elapsed << "ms"
             << "| Detections:" << results.size();
    
    return results;
}

bool YoloDetectorThread::isInitialized() const {
    return m_detector.isInitialized();
}

int YoloDetectorThread::getPendingTaskCount() const {
    QMutexLocker locker(&m_queueMutex);
    return m_taskQueue.size();
}

QString YoloDetectorThread::getLastError() const {
    return m_detector.getLastError();
}

QStringList YoloDetectorThread::getAvailableClasses() const {
    return m_detector.getAvailableClasses();
}

void YoloDetectorThread::startDetection() {
    if (!m_detector.isInitialized()) {
        emit error("Cannot start: detector not initialized");
        return;
    }
    
    if (m_running) {
        qWarning() << "Detector thread already running";
        return;
    }
    
    m_running = true;
    start();
    qDebug() << "YOLO detection thread started";
}

void YoloDetectorThread::stopDetection() {
    m_running = false;
    m_queueCondition.wakeOne(); // Wake up if waiting
    qDebug() << "YOLO detection thread stopping...";
}

void YoloDetectorThread::clearTaskQueue() {
    QMutexLocker locker(&m_queueMutex);
    m_taskQueue.clear();
    qDebug() << "Task queue cleared";
}

void YoloDetectorThread::setConfidenceThreshold(float threshold) {
    qWarning() << "Dynamic threshold change not supported without model reload";
    // Note: ONNX Runtime doesn't support dynamic threshold changes
    // Would need to reload the model or pass as parameter to detect()
}

void YoloDetectorThread::setNmsThreshold(float threshold) {
    qWarning() << "Dynamic NMS threshold change not supported without model reload";
}

void YoloDetectorThread::run() {
    qDebug() << "YOLO detector thread running (QThread)";
    
    while (m_running) {
        DetectionTask task;
        
        {
            QMutexLocker locker(&m_queueMutex);
            
            // Wait for task or timeout
            if (m_taskQueue.isEmpty()) {
                m_queueCondition.wait(&m_queueMutex, 100); // 100ms timeout
                continue;
            }
            
            task = m_taskQueue.dequeue();
        }
        
        processTask(task);
    }
    
    qDebug() << "YOLO detector thread stopped";
}

QString YoloDetectorThread::generateTaskId() {
    return QString("yolo_%1_%2")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz"))
        .arg(m_taskCounter.fetchAndAddRelaxed(1));
}

void YoloDetectorThread::processTask(const DetectionTask& task) {
    QElapsedTimer timer;
    timer.start();
    
    QVector<DetectionResult> results = m_detector.detect(task.image);
    
    qint64 elapsed = timer.elapsed();
    m_totalProcessed.ref();
    
    // Update average inference time
    m_avgInferenceTime = (m_avgInferenceTime * (m_totalProcessed - 1) + elapsed) / m_totalProcessed;
    
    emit detectionResult(task.taskId, results, elapsed);
    
    qDebug() << "Task completed:" << task.taskId
             << "| Time:" << elapsed << "ms"
             << "| Detections:" << results.size()
             << "| Avg:" << QString::number(m_avgInferenceTime, 'f', 2) << "ms";
}

double YoloDetectorThread::calculateInferenceTime(qint64 start, qint64 end) {
    return static_cast<double>(end - start);
}
