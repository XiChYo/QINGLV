#ifndef YOLO_RESULT_TYPES_H
#define YOLO_RESULT_TYPES_H

#include <QList>
#include <QPointF>
#include <QString>
#include <QMetaType>
#include <QVector>

struct YoloSegmentationObject
{
    int classId = -1;
    float score = 0.0f;
    QVector<QPointF> segmentation; // Relative coordinates in [0, 1]
    QPointF center;                // Relative coordinates in [0, 1]
};

struct YoloTaskResult
{
    quint64 taskId = 0;
    bool success = false;
    QString errorMessage;
    QList<YoloSegmentationObject> objects;
};

Q_DECLARE_METATYPE(YoloSegmentationObject)
Q_DECLARE_METATYPE(YoloTaskResult)

#endif // YOLO_RESULT_TYPES_H
