#include <QtTest/QtTest>
#include <vector>
#include <cmath>
#include <algorithm>

#include "postprocess.h"
#include "postprocess_ex.h"

namespace {

constexpr int kMaskProtoDim = 32;

// 构造一条 det 记录(非 topk 布局),全部写入 buffer 的第 row 行。
// det_len = 4 + num_classes + 32
void writeDet(float* det, int det_len, int num_classes,
              float cx, float cy, float w, float h,
              int classId, float score,
              float maskCoeff = 1.0f)
{
    det[0] = cx; det[1] = cy; det[2] = w; det[3] = h;
    for (int c = 0; c < num_classes; ++c) det[4 + c] = 0.0f;
    if (classId >= 0 && classId < num_classes) det[4 + classId] = score;
    for (int m = 0; m < kMaskProtoDim; ++m) {
        det[4 + num_classes + m] = (m == 0) ? maskCoeff : 0.0f;
    }
}

}  // namespace

class PostprocessExTest : public QObject
{
    Q_OBJECT
private slots:
    void postProcess_filtersByConfThreshold();
    void postProcess_filtersByNmsThreshold();
    void postProcess_returnsEmptyForMismatchedProtoDim();
};

void PostprocessExTest::postProcess_filtersByConfThreshold()
{
    const int num_classes = 2;
    const int det_len     = 4 + num_classes + kMaskProtoDim;
    const int det_count   = 2;
    std::vector<float> det(det_count * det_len, 0.0f);

    // 两条 det:一条 score=0.9(class 0),一条 score=0.1(class 1);相隔足够远,不互相 NMS。
    writeDet(&det[0 * det_len], det_len, num_classes,
             100, 100, 50, 50, 0, 0.9f);
    writeDet(&det[1 * det_len], det_len, num_classes,
             400, 400, 50, 50, 1, 0.1f);

    // Proto:80x80x32,全 +10 保证 sigmoid 后接近 1,mask 全 on。
    const int proto_h = 80, proto_w = 80;
    std::vector<float> proto(kMaskProtoDim * proto_h * proto_w, 10.0f);

    PostProcessParams p;
    p.conf_threshold = 0.5f;
    p.nms_threshold  = 0.9f;

    std::vector<SegObject> out;
    post_process_seg_ex(det.data(), det_count, det_len,
                        proto.data(), kMaskProtoDim, proto_h, proto_w, /*nchw=*/true,
                        num_classes, 640, 640, 640, 640, 0, 0, 1.0f,
                        p, out);
    QCOMPARE(static_cast<int>(out.size()), 1);
    QCOMPARE(out[0].label, 0);
    QVERIFY(out[0].prob >= 0.5f);
}

void PostprocessExTest::postProcess_filtersByNmsThreshold()
{
    const int num_classes = 1;
    const int det_len     = 4 + num_classes + kMaskProtoDim;
    const int det_count   = 2;
    std::vector<float> det(det_count * det_len, 0.0f);

    // 两条 det 完全重合,保留分数高的那个。
    writeDet(&det[0 * det_len], det_len, num_classes,
             200, 200, 60, 60, 0, 0.9f);
    writeDet(&det[1 * det_len], det_len, num_classes,
             200, 200, 60, 60, 0, 0.8f);

    const int proto_h = 80, proto_w = 80;
    std::vector<float> proto(kMaskProtoDim * proto_h * proto_w, 10.0f);

    PostProcessParams p;
    p.conf_threshold = 0.1f;
    p.nms_threshold  = 0.5f;

    std::vector<SegObject> out;
    post_process_seg_ex(det.data(), det_count, det_len,
                        proto.data(), kMaskProtoDim, proto_h, proto_w, true,
                        num_classes, 640, 640, 640, 640, 0, 0, 1.0f,
                        p, out);
    QCOMPARE(static_cast<int>(out.size()), 1);
    QCOMPARE(out[0].prob, 0.9f);
}

void PostprocessExTest::postProcess_returnsEmptyForMismatchedProtoDim()
{
    // proto_c != 32 的兜底:函数直接返回,out 为空。
    const int num_classes = 1;
    const int det_len     = 4 + num_classes + kMaskProtoDim;
    std::vector<float> det(det_len, 0.0f);
    writeDet(det.data(), det_len, num_classes, 10, 10, 5, 5, 0, 0.9f);
    std::vector<float> proto(16 * 80 * 80, 1.0f);

    PostProcessParams p;
    std::vector<SegObject> out;
    post_process_seg_ex(det.data(), 1, det_len,
                        proto.data(), /*proto_c=*/16, 80, 80, true,
                        num_classes, 640, 640, 640, 640, 0, 0, 1.0f,
                        p, out);
    QVERIFY(out.empty());
}

QObject* makePostprocessExTest() { return new PostprocessExTest; }

#include "test_postprocess_ex.moc"
