#include "pipeline/camera_worker.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QRandomGenerator>
#include <cstring>

#include "infra/logger.h"
#include "pipeline/pipeline_clock.h"
#include <MvCameraControl.h>

namespace {
constexpr int kGrabTimeoutMs    = 100;
// 连续丢帧累计到该阈值,或距上次告警超过 kDropWarnWindowMs 都会再发一次告警。
constexpr int  kDropWarnThresh  = 5;
constexpr int  kDropWarnWindowMs = 2000;
}

CameraWorker::CameraWorker(QObject* parent)
    : QObject(parent)
{
    m_tickTimer = new QTimer(this);
    m_tickTimer->setTimerType(Qt::PreciseTimer);
    connect(m_tickTimer, &QTimer::timeout, this, &CameraWorker::onTick);
}

CameraWorker::~CameraWorker()
{
    sessionStop();
}

void CameraWorker::sessionStart(const RuntimeConfig& cfg)
{
    if (m_sessionActive) {
        LOG_INFO("CameraWorker::sessionStart called while already active; restarting");
        sessionStop();
    }

    // soft_fps<=0 视为非法,兜底为 2Hz。
    const int fps = cfg.softFps > 0 ? cfg.softFps : 2;
    m_intervalMs = std::max(1, 1000 / fps);

    m_saveRaw        = cfg.saveRaw;
    m_rawSampleRatio = std::max(0.0f, std::min(1.0f, cfg.rawSampleRatio));
    m_saveDir        = cfg.saveDir;
    if (m_saveRaw && !m_saveDir.isEmpty()) {
        QDir().mkpath(m_saveDir);
    }

    if (!openCameraLocked(cfg.cameraIp)) {
        emit cameraError(QStringLiteral("CameraWorker: open failed (ip=%1)").arg(cfg.cameraIp));
        return;
    }

    // hw_fps 仅影响相机内部采集速率,软件节流保证 tick 拉取节奏。
    if (cfg.cameraHwFps > 0) {
        MV_CC_SetBoolValue(m_hCam, "AcquisitionFrameRateEnable", true);
        MV_CC_SetFloatValue(m_hCam, "AcquisitionFrameRate", (float)cfg.cameraHwFps);
    }

    m_inFlight.store(0);
    m_lastCaptureMs    = -1;
    m_frameCounter     = 0;
    m_droppedSinceWarn = 0;
    m_lastWarnMs       = -1;
    m_sessionActive    = true;
    m_tickTimer->start(m_intervalMs);
    LOG_INFO(QString("CameraWorker started ip=%1 soft_fps=%2 (tick=%3ms) save_raw=%4 ratio=%5")
             .arg(cfg.cameraIp).arg(fps).arg(m_intervalMs)
             .arg(m_saveRaw ? "1" : "0").arg(m_rawSampleRatio));
}

void CameraWorker::sessionStop()
{
    if (m_tickTimer && m_tickTimer->isActive()) {
        m_tickTimer->stop();
    }
    closeCameraLocked();
    m_inFlight.store(0);
    m_sessionActive    = false;
    m_droppedSinceWarn = 0;
    m_lastWarnMs       = -1;
}

bool CameraWorker::probeConnection(const QString& ip)
{
    // MVS 允许多次 Initialize(幂等),这里仅做枚举检查,不打开设备。
    if (MV_CC_Initialize() != MV_OK) return false;
    MV_CC_DEVICE_INFO_LIST devList = {0};
    MV_CC_EnumDevices(MV_GIGE_DEVICE, &devList);
    for (unsigned i = 0; i < devList.nDeviceNum; ++i) {
        MV_CC_DEVICE_INFO* dev = devList.pDeviceInfo[i];
        if (!dev || dev->nTLayerType != MV_GIGE_DEVICE) continue;
        MV_GIGE_DEVICE_INFO* gig = &dev->SpecialInfo.stGigEInfo;
        const QString camIp = QString("%1.%2.%3.%4")
            .arg((gig->nCurrentIp >> 24) & 0xFF)
            .arg((gig->nCurrentIp >> 16) & 0xFF)
            .arg((gig->nCurrentIp >> 8) & 0xFF)
            .arg(gig->nCurrentIp & 0xFF);
        if (camIp == ip) return true;
    }
    return false;
}

void CameraWorker::onFrameConsumed()
{
    int prev = m_inFlight.fetch_sub(1);
    if (prev <= 0) {
        m_inFlight.store(0);
    }
}

void CameraWorker::onTick()
{
    if (!m_sessionActive || !m_hCam) return;

    // F10 反压:下游忙则跳过本轮,累计并按窗口告警;不做静默丢帧。
    if (m_inFlight.load() >= kInFlightMax) {
        m_droppedSinceWarn++;
        const qint64 nowMs = pipeline::nowMs();
        const bool timeExceeded = (m_lastWarnMs < 0) ||
                                  (nowMs - m_lastWarnMs >= kDropWarnWindowMs);
        if (m_droppedSinceWarn >= kDropWarnThresh && timeExceeded) {
            emit warning(QStringLiteral("Camera backpressure: dropped %1 ticks")
                         .arg(m_droppedSinceWarn));
            LOG_INFO(QString("CameraWorker backpressure dropped=%1").arg(m_droppedSinceWarn));
            m_droppedSinceWarn = 0;
            m_lastWarnMs       = nowMs;
        }
        return;
    }

    QImage img;
    qint64 tCapture = 0;
    if (!grabOneFrame(img, tCapture)) {
        return;
    }

    m_inFlight.fetch_add(1);
    // F16:根据 save_raw + 采样比决定是否落盘。命中则返回绝对路径,YoloWorker 据此同名落盘 result。
    const QString rawPath = trySaveRawFrame(img, tCapture);
    m_frameCounter++;
    emit frameReadySig(img, tCapture, rawPath);
}

QString CameraWorker::trySaveRawFrame(const QImage& img, qint64 tCaptureMs)
{
    if (!m_saveRaw || m_rawSampleRatio <= 0.0f || m_saveDir.isEmpty() || img.isNull()) {
        return QString();
    }
    const double roll = QRandomGenerator::global()->generateDouble();
    if (roll >= static_cast<double>(m_rawSampleRatio)) {
        return QString();
    }
    const QString tsDir = QDateTime::fromMSecsSinceEpoch(tCaptureMs)
                              .toString("yyyyMMdd");
    const QString dir = QStringLiteral("%1/%2").arg(m_saveDir, tsDir);
    QDir().mkpath(dir);
    const QString path = QStringLiteral("%1/raw_%2_%3.jpg")
        .arg(dir)
        .arg(QDateTime::fromMSecsSinceEpoch(tCaptureMs).toString("hhmmss_zzz"))
        .arg(m_frameCounter, 6, 10, QChar('0'));
    if (!img.save(path, "JPG", 90)) {
        LOG_ERROR(QString("CameraWorker: save raw failed %1").arg(path));
        return QString();
    }
    return path;
}

bool CameraWorker::openCameraLocked(const QString& ip)
{
    try {
        int nRet = MV_CC_Initialize();
        if (nRet != MV_OK) {
            LOG_ERROR(QString("MV_CC_Initialize fail 0x%1").arg(nRet, 0, 16));
            return false;
        }

        MV_CC_DEVICE_INFO_LIST devList = {0};
        MV_CC_EnumDevices(MV_GIGE_DEVICE, &devList);
        if (devList.nDeviceNum == 0) {
            LOG_ERROR("No camera found");
            return false;
        }

        MV_CC_DEVICE_INFO* target = nullptr;
        for (unsigned i = 0; i < devList.nDeviceNum; ++i) {
            MV_CC_DEVICE_INFO* dev = devList.pDeviceInfo[i];
            if (dev->nTLayerType != MV_GIGE_DEVICE) continue;
            MV_GIGE_DEVICE_INFO* gig = &dev->SpecialInfo.stGigEInfo;
            const QString camIp = QString("%1.%2.%3.%4")
                .arg((gig->nCurrentIp >> 24) & 0xFF)
                .arg((gig->nCurrentIp >> 16) & 0xFF)
                .arg((gig->nCurrentIp >> 8) & 0xFF)
                .arg(gig->nCurrentIp & 0xFF);
            if (camIp == ip) { target = dev; break; }
        }
        if (!target) {
            LOG_ERROR(QString("Target IP camera not found: %1").arg(ip));
            return false;
        }

        if (MV_CC_CreateHandle(&m_hCam, target) != MV_OK) { LOG_ERROR("MV_CC_CreateHandle fail"); return false; }
        if (MV_CC_OpenDevice(m_hCam) != MV_OK)           { LOG_ERROR("MV_CC_OpenDevice fail");   return false; }

        MV_CC_SetEnumValue(m_hCam, "PixelFormat", PixelType_Gvsp_BGR8_Packed);
        MV_CC_SetEnumValue(m_hCam, "ExposureAuto", 0);
        MV_CC_SetFloatValue(m_hCam, "ExposureTime", 1000.0f);
        MV_CC_SetEnumValue(m_hCam, "BalanceWhiteAuto", MV_BALANCEWHITE_AUTO_ONCE);
        MV_CC_SetEnumValue(m_hCam, "AcquisitionMode", 2);
        MV_CC_SetEnumValue(m_hCam, "TriggerMode", 0);
        MV_CC_StartGrabbing(m_hCam);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(QString("CameraWorker open exception: %1").arg(e.what()));
        return false;
    }
}

void CameraWorker::closeCameraLocked()
{
    if (m_hCam) {
        MV_CC_StopGrabbing(m_hCam);
        MV_CC_CloseDevice(m_hCam);
        MV_CC_DestroyHandle(m_hCam);
        m_hCam = nullptr;
    }
    delete[] m_rgbBuffer;
    m_rgbBuffer     = nullptr;
    m_rgbBufferSize = 0;
}

bool CameraWorker::grabOneFrame(QImage& outImg, qint64& outCaptureMs)
{
    MV_FRAME_OUT frame = {0};
    if (MV_CC_GetImageBuffer(m_hCam, &frame, kGrabTimeoutMs) != MV_OK) {
        return false;
    }

    // 立即打时间戳:MVS 返回 buffer 即视为采图瞬间。
    outCaptureMs = pipeline::nowMs();

    const int width  = frame.stFrameInfo.nWidth;
    const int height = frame.stFrameInfo.nHeight;
    const int rgbNeed = width * height * 3;

    if (m_rgbBufferSize < rgbNeed) {
        delete[] m_rgbBuffer;
        m_rgbBuffer     = new unsigned char[rgbNeed];
        m_rgbBufferSize = rgbNeed;
    }

    MV_CC_PIXEL_CONVERT_PARAM cvt = {};
    cvt.nWidth           = width;
    cvt.nHeight          = height;
    cvt.pSrcData         = frame.pBufAddr;
    cvt.nSrcDataLen      = frame.stFrameInfo.nFrameLen;
    cvt.enSrcPixelType   = frame.stFrameInfo.enPixelType;
    cvt.enDstPixelType   = PixelType_Gvsp_RGB8_Packed;
    cvt.pDstBuffer       = m_rgbBuffer;
    cvt.nDstBufferSize   = rgbNeed;

    if (MV_CC_ConvertPixelType(m_hCam, &cvt) != MV_OK) {
        MV_CC_FreeImageBuffer(m_hCam, &frame);
        return false;
    }

    // QImage 构造时要 copy,因为 m_rgbBuffer 马上会被下一帧覆写。
    outImg = QImage(m_rgbBuffer, width, height, QImage::Format_RGB888).copy();
    MV_CC_FreeImageBuffer(m_hCam, &frame);
    return true;
}
