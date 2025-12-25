#include "WeQQ.h"
#include <QMessageBox>

// =========================================
// 1. 构造函数
// =========================================
WeQQ::WeQQ(QWidget* parent)
    : QWidget(parent)
{
    ui.setupUi(this);

    // 初始化后台服务器线程
    m_serverThread = new ServerThread(this);

    // 连接日志信号：后台发日志 -> 显示在界面 txtLog
    connect(m_serverThread, &ServerThread::logMessage, this, [=](QString msg) {
        ui.txtLog->append(msg);
        });

    // 连接回车键：在输入框按 Enter -> 触发发送按钮
    connect(ui.editCmd, &QLineEdit::returnPressed, this, &WeQQ::on_btnSend_clicked);

    // 默认显示第 0 页 (配置页)
    ui.stackedWidget->setCurrentIndex(0);

    ui.editPort->setPlaceholderText("默认: 9870");
    ui.editIp->setPlaceholderText("本机 IP");
}

WeQQ::~WeQQ()
{
    // 窗口关闭时，确保线程安全退出
    if (m_serverThread->isRunning()) {
        m_serverThread->terminate();
        m_serverThread->wait();
    }
}

// =========================================
// 2. 点击“开始运行”按钮
// =========================================
void WeQQ::on_btnConnect_clicked()
{
    QString portStr = ui.editPort->text().trimmed();
    int port = 9870;
    if (!portStr.isEmpty()) {
        port = portStr.toInt();
    }

    if (m_serverThread) {
        m_serverThread->setPort(port);
        m_serverThread->start(); // 启动后台线程
    }

    // 切换到第 1 页 (控制台界面)
    ui.stackedWidget->setCurrentIndex(1);

    // 禁用配置按钮防止重复点击
    ui.btnConnect->setEnabled(false);
}

// =========================================
// 3. 点击“发送指令”按钮 (核心修复)
// =========================================
void WeQQ::on_btnSend_clicked()
{
    // 1. 获取输入框的内容
    QString cmd = ui.editCmd->text().trimmed();
    if (cmd.isEmpty()) return;

    // 2. 只有当线程存在且运行时，才发送指令
    if (m_serverThread && m_serverThread->isRunning()) {
        // 调用后台的执行函数
        m_serverThread->executeConsoleCommand(cmd);
    }
    else {
        ui.txtLog->append("Error: Server is not running!");
    }

    // 3. 清空输入框
    ui.editCmd->clear();
    ui.editCmd->setFocus();
}