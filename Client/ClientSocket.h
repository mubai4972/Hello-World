#pragma once
#include <QObject>
#include <QTcpSocket>

class ClientSocket : public QObject
{
    Q_OBJECT
public:
    explicit ClientSocket(QObject* parent = nullptr);

    // 【修改】连接服务器 (增加 password 参数)
    void connectToServer(QString ip, int port, QString nickName, QString password);

    // 发送消息 (通用方法)
    void sendMsg(QString msg);

signals:
    // 【信号】收到服务器发来的文本
    void msgReceived(QString msg);
    // 【信号】连接成功 (Socket层连接建立)
    void connectedSuccess();
    // 【信号】连接断开/出错
    void connectionLost();

private slots:
    // 内部处理：有数据可读
    void onReadyRead();
    // 内部处理：连接建立
    void onConnected();
    // 内部处理：断开连接
    void onDisconnected();

private:
    QTcpSocket* m_socket;
    QString m_myNickName; // 暂存昵称
    QString m_myPassword; // 【新增】暂存密码
};