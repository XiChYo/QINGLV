#ifndef TCPFORROBOT_H
#define TCPFORROBOT_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

class tcpforrobot : public QObject
{
    Q_OBJECT

public:
    explicit tcpforrobot(QObject *parent = nullptr);
    ~tcpforrobot();

    // 启动服务
    bool startServer();

    // 停止服务
    void stopServer();

    // 发送数据
    void sendData(const QByteArray &data, float time);

    int port = 5000;

    void sendToIP(const QString& ip, const QByteArray &data);

    bool isConnected(const QString& ip);

private slots:
    // 新连接
    void onNewConnection();

    // 接收数据
    void onReadyRead();

    // 客户端断开
    void onDisconnected();

private:
    QTcpServer *m_server;
    QTcpSocket *m_clientSocket;

    QMap<QString, QTcpSocket*> m_clients;


signals:
    void clientConnected(QString ip);
    void clientDisconnected(QString ip);
};

#endif // TCPFORROBOT_H
