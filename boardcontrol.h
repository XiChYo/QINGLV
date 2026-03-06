#ifndef BOARDCONTROL_H
#define BOARDCONTROL_H

#include <QObject>
#include <QByteArray>
#include <QTimer>
#include <QMutex>
#include <QtConcurrent>
#include <QMetaObject>
#include <QSet>

class boardControl : public QObject
{
    Q_OBJECT
public:
    explicit boardControl(QObject *parent = nullptr);
    ~boardControl();

public slots:
    void initSerial();                 // 在线程 started 时调用
    void singleBoardControl(QString order);          // 单板控制
    void batchBoardControl(QString order);           // 子板批量控制
    void requestEncoderSpeed();         // 请求编码器
    void stopWork();                    // 停止并关闭串口

signals:
    void encoderSpeedReceived(QByteArray frame);
    void errorOccured(QString msg);

private:
    bool openSerial();
    void closeSerial();
    bool writeFrame(const QByteArray& frame);
    bool readFrame(QByteArray& frame, int timeoutMs);

    QByteArray buildSingleControlFrame(const QByteArray& field);
    QByteArray buildControlFrame(const QByteArray& field);
    QByteArray buildBatchOpenFrame(const QByteArray& field);
    QByteArray buildBatchCloseFrame(const QByteArray& field);
    QByteArray buildBatchFrameCore(quint8, quint8, quint8, quint8);
    quint8 countBits(quint8 v);

    int sin = 0;
    int bat = 0;

    QSet<int> m_valveBusySet;   // 正在工作的喷阀集合

private:
    QMutex m_serialMutex;

    QByteArray m_rxBuffer;

    QTimer* m_speedTimer{nullptr};

    int m_fd{-1};
    QString m_dev{"/dev/ttyUSB0"};
};

#endif // BOARDCONTROL_H
