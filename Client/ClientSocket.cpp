#include "ClientSocket.h"
#include <QDebug>

ClientSocket::ClientSocket(QObject* parent)
    : QObject(parent)
{
    m_socket = new QTcpSocket(this);

    // 绑定 Qt 的信号槽
    connect(m_socket, &QTcpSocket::readyRead, this, &ClientSocket::onReadyRead);
    connect(m_socket, &QTcpSocket::connected, this, &ClientSocket::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &ClientSocket::onDisconnected);
}

// 【修改】接收密码参数并保存
void ClientSocket::connectToServer(QString ip, int port, QString nickName, QString password)
{
    m_myNickName = nickName;
    m_myPassword = password; // 保存密码

    // 如果之前连着，先断开，防止状态混乱
    m_socket->abort();
    // 发起连接
    m_socket->connectToHost(ip, port);
}

void ClientSocket::sendMsg(QString msg)
{
    if (m_socket->isOpen() && !msg.isEmpty()) {
        m_socket->write(msg.toUtf8());
        m_socket->flush(); // 立即发送
    }
}

void ClientSocket::onConnected()
{
    // === 关键逻辑修改 ===
    // 以前：只发送名字 m_socket->write(m_myNickName.toUtf8());
    // 现在：发送 CMD:LOGIN|Name|Password
    if (m_socket->isOpen()) {
        QString loginPacket = "CMD:LOGIN|" + m_myNickName + "|" + m_myPassword;
        m_socket->write(loginPacket.toUtf8());
        m_socket->flush();

        // 通知 UI：Socket连接已建立 (注意：真正的登录成功要等收到 CMD:LOGIN_SUCCESS)
        emit connectedSuccess();
    }
}

void ClientSocket::onDisconnected()
{
    emit connectionLost();
}

void ClientSocket::onReadyRead()
{
    // 读取所有数据
    QByteArray data = m_socket->readAll();
    // 转为 QString 并通知 UI 显示
    emit msgReceived(QString::fromUtf8(data));
}