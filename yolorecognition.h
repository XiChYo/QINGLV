#ifndef YOLORECOGNITION_H
#define YOLORECOGNITION_H
#include <QImage>
#include <opencv2/opencv.hpp>
#include <QObject>
#include <QDebug>
#include <QPoint>
#include "rknn_api.h"
#include "postprocess.h"

struct RknnModelSession {
    rknn_context ctx = 0;
    unsigned char* model_data = nullptr;
    int model_data_size = 0;
    rknn_input_output_num io_num {};
    rknn_tensor_attr input0_attr {};
    std::vector<rknn_tensor_attr> output_attrs;
};

class yolorecognition : public QObject
{
    Q_OBJECT
public:
    explicit yolorecognition(QObject* parent = nullptr);
    cv::Mat QImage2Mat(const QImage& image);
    char* model_path = "model-n-1024-robotic.rknn";
    QPoint m_point;
    QImage matToQImage(const cv::Mat& mat);
public slots:
    int recognition(const QImage& image,const int timefortest);
signals:
    void objPointSig(QPoint objPoint);
    void resultImgSig(const QImage& img);

    void pointSig(int x);
    void frameReadySig(const QImage& img);
    
    
private:
    bool initTF = false;
    QPoint run_seg_predict(const RknnModelSession& session,
                           const cv::Mat& orig_img,
                           int topk_class_count,
                           const std::vector<uint8_t>& enabled_mask,
                           bool draw_overlay,
                           cv::Mat& result_img,const int timefortest);

};

#endif // YOLORECOGNITION_H
