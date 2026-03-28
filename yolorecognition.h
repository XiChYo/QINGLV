#ifndef YOLORECOGNITION_H
#define YOLORECOGNITION_H
#include <QImage>
#include <opencv2/opencv.hpp>
#include <QObject>
#include <QDebug>
#include <QPoint>
#include "rknn_api.h"
#include "postprocess.h"
class yolorecognition : public QObject
{
    Q_OBJECT
public:
    explicit yolorecognition(QObject* parent = nullptr);
    cv::Mat QImage2Mat(const QImage& image);
    char* model_path = "model-n-800-hsv-scaled.rknn";
    QPoint m_point;
    QImage matToQImage(const cv::Mat& mat);
public slots:
    int recognition(const QImage& image);
signals:
    void objPointSig(QPoint objPoint);
    void resultImgSig(const QImage& img);
private:
    bool initTF = false;
};

#endif // YOLORECOGNITION_H
