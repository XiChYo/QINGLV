#include "tcpforrobot.h"
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
    qDebug() << "Server started listening";
    if (m_server->listen(QHostAddress::Any, port))
    {
        qDebug() << "Server started, port:" << port;
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
void tcpforrobot::onNewConnection()
{
    m_clientSocket = m_server->nextPendingConnection();

    qDebug() << "Client connected:"
             << m_clientSocket->peerAddress().toString()
             << m_clientSocket->peerPort();

    connect(m_clientSocket, &QTcpSocket::readyRead,
            this, &tcpforrobot::onReadyRead);

    connect(m_clientSocket, &QTcpSocket::disconnected,
            this, &tcpforrobot::onDisconnected);
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
void tcpforrobot::sendData(const QByteArray &data)
{
    if (m_clientSocket && m_clientSocket->state() == QAbstractSocket::ConnectedState)
    {
        m_clientSocket->write(data);
    }
    else
    {
        qDebug() << "No client connected";
    }
}
