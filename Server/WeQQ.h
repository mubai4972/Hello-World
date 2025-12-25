#pragma once

#include <QtWidgets/QWidget>
#include "ui_WeQQ.h"
#include "ServerThread.h" // 【新增1】引入服务器线程的头文件

class WeQQ : public QWidget
{
    Q_OBJECT

public:
    WeQQ(QWidget* parent = nullptr);
    ~WeQQ();
private slots:
    // 原有的连接按钮槽函数
    void on_btnConnect_clicked();

    // 【新增2】发送指令按钮的槽函数 (对应你 cpp 里的定义)
    void on_btnSend_clicked();

private:
    Ui::WeQQClass ui;

    // 【新增3】声明服务器线程指针 (对应报错 E0020)
    ServerThread* m_serverThread;
};