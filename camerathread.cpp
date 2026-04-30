#include "camerathread.h"
#include <QDebug>
#include <cstring>
#include "logger.h"
#include <QDir>
#include <QCoreApplication>
#include <QDateTime>

camerathread::camerathread(QObject* parent): QThread(parent)
{

}
camerathread::~camerathread()
{
    stop();
}
bool camerathread::openCamera(const QString& ip)
{
    m_ip = ip;
    LOG_INFO("Initialize camera");
    try{
        m_ip = ip;

        LOG_INFO("Initialize camera");

        int nRet = MV_CC_Initialize();
        if (MV_OK != nRet)
        {
            emit errorMegSig(QString("MV_CC_Initialize fail! 0x%1").arg(nRet));

            return false;
        }

        MV_CC_DEVICE_INFO_LIST devList = {0};
        MV_CC_EnumDevices(MV_GIGE_DEVICE, &devList);

        if (devList.nDeviceNum == 0)
        {
            emit errorMegSig("No camera found");
            return false;
        }

        MV_CC_DEVICE_INFO* targetDevice = nullptr;

        // 🔥 遍历设备，匹配 IP
        for (unsigned int i = 0; i < devList.nDeviceNum; i++)
        {
            MV_CC_DEVICE_INFO* pDeviceInfo = devList.pDeviceInfo[i];

            if (pDeviceInfo->nTLayerType != MV_GIGE_DEVICE)
                continue;

            MV_GIGE_DEVICE_INFO* pGigEInfo =
                &pDeviceInfo->SpecialInfo.stGigEInfo;

            // 转 IP
            QString camIP = QString("%1.%2.%3.%4")
                    .arg((pGigEInfo->nCurrentIp >> 24) & 0xFF)
                    .arg((pGigEInfo->nCurrentIp >> 16) & 0xFF)
                    .arg((pGigEInfo->nCurrentIp >> 8) & 0xFF)
                    .arg(pGigEInfo->nCurrentIp & 0xFF);

            qDebug() << "Find camera IP:" << camIP;

            if (camIP == ip)
            {
                targetDevice = pDeviceInfo;
                break;
            }
        }

        if (!targetDevice)
        {
            emit errorMegSig("Target IP camera not found: " + ip);
            return false;
        }

        // 🔥 用目标设备创建句柄
        if (MV_CC_CreateHandle(&m_hCam, targetDevice) != MV_OK)
        {
            emit errorMegSig("Create handle failed");
            return false;
        }

        if (MV_CC_OpenDevice(m_hCam) != MV_OK)
        {
            emit errorMegSig("Open camera failed");
            return false;
        }

        // ========= 原来的配置保持 =========
        MV_CC_SetEnumValue(m_hCam, "PixelFormat", PixelType_Gvsp_BGR8_Packed);
        MV_CC_SetEnumValue(m_hCam, "ExposureAuto", 0);
        MV_CC_SetFloatValue(m_hCam, "ExposureTime", 1000.0f);
        MV_CC_SetEnumValue(m_hCam, "BalanceWhiteAuto", MV_BALANCEWHITE_AUTO_ONCE);

        MV_CC_SetBoolValue(m_hCam, "AcquisitionFrameRateEnable", true);
        MV_CC_SetFloatValue(m_hCam, "AcquisitionFrameRate", 60.0f);

        MV_CC_SetEnumValue(m_hCam, "AcquisitionMode", 2);
        MV_CC_SetEnumValue(m_hCam, "TriggerMode", 0);

        MV_CC_StartGrabbing(m_hCam);

        m_running = true;
    }catch(std::exception& e)
    {
        emit errorMegSig(e.what());
        return false;
    }
    return true;
}
void camerathread::stop()
{
    LOG_INFO("Turn off camera");
    m_running = false;
    wait();

    if (m_hCam) {
        MV_CC_StopGrabbing(m_hCam);
        MV_CC_CloseDevice(m_hCam);
        MV_CC_DestroyHandle(m_hCam);
        m_hCam = nullptr;
    }

    delete[] rgbBuffer;
    rgbBuffer = nullptr;
}

void camerathread::run()
{
    LOG_INFO("Start taking pictures");
    try
        {
            m_running = true;

            MV_FRAME_OUT frame = {0};

            int timefortest = 0;
            while (m_running && !isInterruptionRequested())
            {
                QString now = QDateTime::currentDateTime().toString("--取图--yyyy-MM-dd HH:mm:ss.zzz || 第");
                QString timestrap = now + QString::number(timefortest) + "次";
                LOG_INFO(timestrap);
                timefortest++;

                // 时间没到，不取图
                QThread::msleep(0);

                // 取图
                if (MV_CC_GetImageBuffer(m_hCam, &frame, 100) != MV_OK)
                {
                    continue;
                }

                uint32_t frameNum = frame.stFrameInfo.nFrameNum; // 取帧号

                int width  = frame.stFrameInfo.nWidth;
                int height = frame.stFrameInfo.nHeight;

                int rgbSize = width * height * 3;

                if (!rgbBuffer)
                {
                    rgbBuffer = new unsigned char[rgbSize];
                }

                MV_CC_PIXEL_CONVERT_PARAM convertParam;
                memset(&convertParam, 0, sizeof(convertParam));

                convertParam.nWidth           = width;
                convertParam.nHeight          = height;
                convertParam.pSrcData         = frame.pBufAddr;
                convertParam.nSrcDataLen      = frame.stFrameInfo.nFrameLen;
                convertParam.enSrcPixelType   = frame.stFrameInfo.enPixelType;
                convertParam.enDstPixelType   = PixelType_Gvsp_RGB8_Packed;
                convertParam.pDstBuffer       = rgbBuffer;
                convertParam.nDstBufferSize   = rgbSize;

                if (MV_CC_ConvertPixelType(m_hCam, &convertParam) != MV_OK)
                {
                    MV_CC_FreeImageBuffer(m_hCam, &frame);
                    continue;
                }

                QImage img(rgbBuffer, width, height, QImage::Format_RGB888);

                QString fileName = QString("img_%1.jpg")
                    .arg(QDateTime::currentDateTime()
                         .toString("yyyyMMdd_hhmmss_zzz"));

//                // 保存照片
//                QImage image;
//                QString baseDir = QCoreApplication::applicationDirPath();
//                QString saveDirPath = baseDir + "/saveRawPic/img_20260327_172735_817.jpg";
//                bool ok = image.load(saveDirPath);

                emit frameReadySig(img, fileName, timefortest);
//                emit frameReadySig(img, timefortest);

                MV_CC_FreeImageBuffer(m_hCam, &frame);
            }
        }
        catch (const std::exception& e)
        {
            emit errorMegSig(e.what());
        }
}
