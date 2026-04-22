#include "legacy/tcpforrobot.h"
#include <QDebug>

tcpforrobot::tcpforrobot(QObject *parent)
    : QObject(parent),
      m_server(new QTcpServer(this)),
      m_clientSocket(nullptr)
{
    connect(m_server, &QTcpServer::newConnection,
            this, &tcpforrobot::onNewConnection);
}

tcpforrobot::~tcpforrobot()
{
    stopServer();
}

// 启动服务
bool tcpforrobot::startServer()
{
    if (m_server->listen(QHostAddress::Any, 5000))
    {
        qDebug() << "Server started!";
        qDebug() << "Real listen address:" << m_server->serverAddress().toString();
        qDebug() << "Real listen port:" << m_server->serverPort();
        return true;
    }
    else
    {
        qDebug() << "Server start failed:" << m_server->errorString();
        return false;
    }
}

// 停止服务
void tcpforrobot::stopServer()
{
    if (m_clientSocket)
    {
        m_clientSocket->disconnectFromHost();
        m_clientSocket->deleteLater();
        m_clientSocket = nullptr;
    }

    if (m_server->isListening())
    {
        m_server->close();
    }
}

// 新连接
//void tcpforrobot::onNewConnection()
//{
//    m_clientSocket = m_server->nextPendingConnection();

//    isConnected = true;

//    qDebug() << "Client connected:"
//             << m_clientSocket->peerAddress().toString()
//             << m_clientSocket->peerPort();

//    connect(m_clientSocket, &QTcpSocket::readyRead,
//            this, &tcpforrobot::onReadyRead);

//    connect(m_clientSocket, &QTcpSocket::disconnected,
//            this, &tcpforrobot::onDisconnected);
//}
void tcpforrobot::test()
{
//    QThread::msleep(1000);
    qDebug()<<"777777777777777777;";
}

void tcpforrobot::onNewConnection()
{
    while (m_server->hasPendingConnections())
    {
        QTcpSocket* client = m_server->nextPendingConnection();

        QString ip = client->peerAddress().toString();

        // 处理 ::ffff:192.168.x.x
        if (ip.contains("::ffff:"))
            ip = ip.split("::ffff:")[1];

        qDebug() << "Client connected:" << ip;


        m_clients[ip] = client;

        emit clientConnected(ip);

        connect(client, &QTcpSocket::readyRead, this, [=]()
        {
            QByteArray data = client->readAll();
            qDebug() << ip << "Received:" << data;

            client->write("ACK\n");
        });

        connect(client, &QTcpSocket::disconnected, this, [=]()
        {
            qDebug() << "Client disconnected:" << ip;

            m_clients.remove(ip);
            client->deleteLater();

            emit clientDisconnected(ip);
        });
    }
}
// 接收数据
void tcpforrobot::onReadyRead()
{
    QByteArray data = m_clientSocket->readAll();

    qDebug() << "Received:" << data;

    // 示例：收到数据后回复
    m_clientSocket->write("ACK\n");
}

// 客户端断开
void tcpforrobot::onDisconnected()
{
    qDebug() << "Client disconnected";

    m_clientSocket->deleteLater();
    m_clientSocket = nullptr;
}

// 发送数据
void tcpforrobot::sendData(const QByteArray &data, float time)
{
    qDebug() << time <<"ms后执行*****************";
    QThread::msleep(time);
    qDebug() << "执行*********************";
    if (m_clientSocket && m_clientSocket->state() == QAbstractSocket::ConnectedState)
    {
        qDebug()<<"^^^^^^^^^^^^^^^^^^^send data:"<< data<<"%%%%%%%%%%%%%%%%%%%%%";
        m_clientSocket->write(data);
    }
    else
    {
        qDebug() << "No client connected";
    }
}

void tcpforrobot::sendToIP(const QString& ip, const QByteArray &data)
{
    if (!m_clients.contains(ip))
    {
        qDebug() << "IP not connected:" << ip;
        return;
    }

    QTcpSocket* socket = m_clients[ip];


    if (socket->state() == QAbstractSocket::ConnectedState)
    {
//        qDebug() << "Send to" << ip << ":" << data;
        socket->write(data);
    }

}
bool tcpforrobot::isConnected(const QString& ip)
{
    if (!m_clients.contains(ip))
        return false;

    QTcpSocket* socket = m_clients.value(ip);

    return socket && socket->state() == QAbstractSocket::ConnectedState;
}
