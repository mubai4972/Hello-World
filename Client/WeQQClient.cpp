#include "WeQQClient.h"
#include <QMessageBox>
#include <QDateTime>
#include <QDebug>
#include <QTextStream>
#include <QTextBlock>
#include <QSet>

// ====================================================================
// 构造与析构
// ====================================================================

WeQQClient::WeQQClient(QWidget* parent) : QWidget(parent) {
    ui.setupUi(this);
    m_client = new ClientSocket(this);

    connect(m_client, &ClientSocket::msgReceived, this, &WeQQClient::onMsgReceived);
    connect(m_client, &ClientSocket::connectedSuccess, this, &WeQQClient::onConnectedSuccess);
    connect(m_client, &ClientSocket::connectionLost, this, [=]() {
        QMessageBox::critical(this, "错误", "与服务器断开连接！");
        ui.stackedWidget->setCurrentIndex(0);
        ui.btnConnect->setEnabled(true);
        ui.btnConnect->setText("确定");
        });

    ui.editMsg->installEventFilter(this);
    ui.stackedWidget->setCurrentIndex(0);

    ui.leServerIp->setPlaceholderText("默认: 127.0.0.1");
    ui.leServerPort->setPlaceholderText("默认: 9870");

    if (ui.lePassword) {
        ui.lePassword->setEchoMode(QLineEdit::Password);
    }

    // 动态 UI 初始化
    m_listGroupMembers = new QListWidget(this);
    m_listGroupMembers->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_listGroupMembers, &QListWidget::customContextMenuRequested,
        this, &WeQQClient::on_listGroupMembers_customContextMenuRequested);

    ui.tabWidget->addTab(m_listGroupMembers, "群成员");
    m_tabGroupMembers = ui.tabWidget->widget(ui.tabWidget->count() - 1);
    ui.tabWidget->removeTab(ui.tabWidget->indexOf(m_tabGroupMembers));

    m_btnRequestList = new QPushButton("申请列表", ui.widget_3);
    m_btnRequestList->setGeometry(10, 10, 61, 24);
    m_btnRequestList->show();
    connect(m_btnRequestList, &QPushButton::clicked, this, &WeQQClient::on_btnRequestList_clicked);

    m_listRequests = new QListWidget(this);
    m_listRequests->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_listRequests, &QListWidget::customContextMenuRequested,
        this, &WeQQClient::on_listRequests_customContextMenuRequested);

    ui.tabWidget->addTab(m_listRequests, "申请列表");
    m_tabRequests = ui.tabWidget->widget(ui.tabWidget->count() - 1);
    ui.tabWidget->removeTab(ui.tabWidget->indexOf(m_tabRequests));

    connect(ui.tabWidget, &QTabWidget::currentChanged, this, &WeQQClient::onTabChanged);

    // 【新增】聊天窗口右键菜单
    ui.browserChat->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui.browserChat, &QTextBrowser::customContextMenuRequested, this, &WeQQClient::onChatContextMenu);

    // 【核心】动态创建指令按钮
    m_btnCmd = new QPushButton("输入指令", ui.widget_2);
    m_btnCmd->setGeometry(280, 470, 80, 24);
    m_btnCmd->hide();
    connect(m_btnCmd, &QPushButton::clicked, this, &WeQQClient::on_btnCmd_clicked);
    m_isCommandMode = false;
}

WeQQClient::~WeQQClient() {
}

// ====================================================================
// Tab逻辑 & 事件过滤器
// ====================================================================

void WeQQClient::onTabChanged(int index) {
    QWidget* currentWidget = ui.tabWidget->widget(index);
    if (currentWidget != m_tabRequests) {
        int reqIndex = ui.tabWidget->indexOf(m_tabRequests);
        if (reqIndex != -1) {
            ui.tabWidget->removeTab(reqIndex);
            m_client->sendMsg("CMD:LEAVE_REQUEST_LIST");
        }
    }
}

bool WeQQClient::eventFilter(QObject* watched, QEvent* event) {
    if (watched == ui.editMsg && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (keyEvent->modifiers() & Qt::ControlModifier) return QWidget::eventFilter(watched, event);
            else { on_btnSend_clicked(); return true; }
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ====================================================================
// 文件路径
// ====================================================================

void WeQQClient::checkAndInitFolders() {
    if (m_myId == 0) return;
    QString myName = ui.leName->text();
    m_privateRecordRoot = myName + QString::number(m_myId);
    QDir dirPrivate(m_privateRecordRoot);
    if (!dirPrivate.exists()) dirPrivate.mkpath(".");
    m_groupRecordRoot = QString::number(m_myId) + "ClientGroupRecord";
    QDir dirGroup(m_groupRecordRoot);
    if (!dirGroup.exists()) dirGroup.mkpath(".");
}

QString WeQQClient::getPrivateChatFilePath(QString friendName, int friendId) {
    if (m_privateRecordRoot.isEmpty()) return "";
    return m_privateRecordRoot + "/" + friendName + QString::number(friendId) + ".txt";
}

QString WeQQClient::getGroupChatFilePath(QString groupName) {
    if (m_groupRecordRoot.isEmpty()) return "";
    return m_groupRecordRoot + "/" + groupName + ".txt";
}

// ====================================================================
// 连接逻辑
// ====================================================================

void WeQQClient::on_btnConnect_clicked() {
    QString ip = ui.leServerIp->text().trimmed();
    if (ip.isEmpty()) ip = "127.0.0.1";
    QString portStr = ui.leServerPort->text().trimmed();
    int port = 9870;
    if (!portStr.isEmpty()) port = portStr.toInt();
    QString name = ui.leName->text().trimmed();
    if (name.isEmpty()) { QMessageBox::warning(this, "提示", "请输入昵称！"); return; }
    QString pwd = "";
    if (ui.lePassword) {
        pwd = ui.lePassword->text().trimmed();
        if (pwd.isEmpty()) { QMessageBox::warning(this, "提示", "请输入密码！"); return; }
    }
    ui.btnConnect->setText("连接中...");
    ui.btnConnect->setEnabled(false);
    m_client->connectToServer(ip, port, name, pwd);
}

void WeQQClient::onConnectedSuccess() {
    appendLog(">>> 系统: 已连接服务器，正在验证身份...");
}

// ====================================================================
// 指令模式逻辑
// ====================================================================

void WeQQClient::on_btnCmd_clicked() {
    m_isCommandMode = true;

    if (m_currentType == 1) m_client->sendMsg("CMD:LEAVE_GROUP");
    else if (m_currentType == 0 && m_currentSessionId != -1) m_client->sendMsg("CMD:LEAVE_FRIEND");

    m_currentSessionId = -1;
    m_currentSessionName = "";
    m_currentType = -1;

    ui.browserChat->clear();
    m_blockToUuid.clear();

    ui.labelChatTitle->setText(">>> 指令模式 (直接发送Server) <<<");
    ui.browserChat->append(">>> 已进入指令模式。");
    ui.browserChat->append(">>> 输入内容将直接发送给服务器执行。");
    ui.editMsg->clear();
    ui.editMsg->setFocus();
}

// ====================================================================
// 发送消息
// ====================================================================

void WeQQClient::on_btnSend_clicked() {
    QString content = ui.editMsg->toPlainText().trimmed();
    if (content.isEmpty()) return;

    if (m_isCommandMode) {
        QString cmdToSend = content;
        if (!cmdToSend.startsWith("/")) {
            cmdToSend.prepend("/");
        }
        m_client->sendMsg(cmdToSend);
        ui.browserChat->append("[Command] " + cmdToSend);
        ui.editMsg->clear();
        ui.editMsg->setFocus();
        return;
    }

    if (m_currentSessionId == -1) {
        QMessageBox::information(this, "提示", "请先在左侧选择一个好友或群组！");
        return;
    }

    QString safeContent = content;
    safeContent.replace("\n", "<br>");

    QString packet = QString("SEND:%1|%2|%3").arg(m_currentType).arg(m_currentSessionId).arg(safeContent);
    m_client->sendMsg(packet);

    ui.editMsg->clear();
    ui.editMsg->setFocus();
}

// ====================================================================
// 消息接收与解析
// ====================================================================

void WeQQClient::onMsgReceived(QString rawMsg) {
    QStringList lines = rawMsg.split('\n', Qt::SkipEmptyParts);

    for (const QString& line : lines) {
        QString cleanLine = line.trimmed();
        if (cleanLine.isEmpty()) continue;

        if (cleanLine.startsWith("CMD:LOGIN_SUCCESS|")) {
            parseLoginSuccess(cleanLine.mid(18));
            continue;
        }

        if (cleanLine.startsWith("CMD:LOGIN_FAIL|")) {
            QMessageBox::critical(this, "登录失败", "密码错误或登录异常！");
            ui.stackedWidget->setCurrentIndex(0);
            ui.btnConnect->setEnabled(true);
            ui.btnConnect->setText("确定");
            return;
        }

        if (cleanLine == "CMD:GRANT_ADMIN") {
            if (m_btnCmd) m_btnCmd->show();
            appendLog("[System] You have been granted Admin permissions.");
            continue;
        }

        if (cleanLine == "CMD:REVOKE_ADMIN") {
            if (m_btnCmd) m_btnCmd->hide();
            if (m_isCommandMode) {
                m_isCommandMode = false;
                ui.labelChatTitle->setText("模式已重置");
                ui.browserChat->append(">>> 管理员权限已失效，退出指令模式。");
                ui.editMsg->clear();
            }
            appendLog("[System] Your Admin permissions have been revoked.");
            continue;
        }

        if (cleanLine == "CMD:CLEAR_CHAT") {
            ui.browserChat->clear();
            m_blockToUuid.clear();
            continue;
        }

        if (cleanLine.startsWith("CMD:USER_LIST|")) {
            updateOnlineStatus(cleanLine.mid(14));
            continue;
        }

        if (cleanLine.startsWith("CMD:FRIEND_LIST|")) {
            parseUserList(cleanLine.mid(16));
            continue;
        }
        if (cleanLine.startsWith("CMD:FRIEND_ADD|")) {
            parseFriendAdd(cleanLine.mid(15));
            continue;
        }
        if (cleanLine.startsWith("CMD:GROUP_LIST|")) {
            parseGroupList(cleanLine.mid(15));
            continue;
        }

        if (cleanLine.startsWith("CMD:GROUP_MEMBERS|")) {
            QString body = cleanLine.mid(18);
            m_listGroupMembers->clear();
            QStringList list = body.split(';', Qt::SkipEmptyParts);
            for (const QString& itemStr : list) {
                QStringList parts = itemStr.split(',');
                if (parts.size() >= 3) {
                    int uid = parts[0].toInt();
                    QString name = parts[1];
                    int status = parts[2].toInt();
                    int role = 1;
                    if (parts.size() >= 4) role = parts[3].toInt();

                    QString displayName = name;
                    if (role == 3) displayName = "[群主] " + name;
                    else if (role == 2) displayName = "[管理员] " + name;

                    QListWidgetItem* item = new QListWidgetItem(displayName);
                    item->setData(Qt::UserRole, uid);

                    if (status == 1) {
                        item->setForeground(Qt::darkGreen);
                        item->setText(displayName + " [在线]");
                    }
                    else {
                        item->setForeground(Qt::gray);
                        item->setText(displayName + " [离线]");
                    }
                    m_listGroupMembers->addItem(item);
                }
            }
            continue;
        }

        // 【修改点1】STATUS_UPDATE 更新单个好友状态时
        if (cleanLine.startsWith("CMD:STATUS_UPDATE|")) {
            QStringList parts = cleanLine.mid(18).split('|');
            if (parts.size() >= 2) {
                int uid = parts[0].toInt();
                int status = parts[1].toInt();

                // 更新好友列表
                for (int i = 0; i < ui.listFriends->count(); ++i) {
                    QListWidgetItem* it = ui.listFriends->item(i);
                    if (it->data(Qt::UserRole).toInt() == uid) {
                        QString baseName = it->text().split(" [").first();
                        if (status == 1) {
                            it->setForeground(Qt::darkGreen);
                            it->setText(baseName + " [在线]");
                        }
                        else {
                            // 离线：灰色 + [离线]
                            it->setForeground(Qt::gray);
                            it->setText(baseName + " [离线]");
                        }
                    }
                }

                // 更新群成员列表
                for (int i = 0; i < m_listGroupMembers->count(); ++i) {
                    QListWidgetItem* it = m_listGroupMembers->item(i);
                    if (it->data(Qt::UserRole).toInt() == uid) {
                        QString baseName = it->text().split(" [").first();
                        if (status == 1) {
                            it->setForeground(Qt::darkGreen);
                            it->setText(baseName + " [在线]");
                        }
                        else {
                            it->setForeground(Qt::gray);
                            it->setText(baseName + " [离线]");
                        }
                    }
                }
            }
            continue;
        }

        if (cleanLine.startsWith("CMD:REQUEST_LIST|")) {
            QString body = cleanLine.mid(17);
            m_listRequests->clear();
            QStringList reqs = body.split(';', Qt::SkipEmptyParts);
            for (const QString& r : reqs) {
                QStringList parts = r.split(',');
                if (parts.size() >= 4) {
                    QString type = parts[0];
                    QString fromId = parts[1];
                    QString fromName = parts[2];
                    QString targetId = parts[3];
                    QString displayText;
                    if (type == "FRIEND") {
                        displayText = QString("好友申请: [%1] (%2)").arg(fromName).arg(fromId);
                    }
                    else {
                        displayText = QString("入群申请: [%1] -> 群 %2").arg(fromName).arg(targetId);
                    }
                    QListWidgetItem* item = new QListWidgetItem(displayText);
                    QString meta = type + "|" + fromId + "|" + targetId;
                    item->setData(Qt::UserRole, meta);
                    m_listRequests->addItem(item);
                }
            }
            continue;
        }

        if (cleanLine.startsWith("MSG:")) {
            QString body = cleanLine.mid(4);
            QStringList parts = body.split('|');
            if (parts.size() >= 7) {
                int targetId = parts[0].toInt(); // SessionID
                QString sender = parts[1];
                QString content = parts[2];
                int type = parts[3].toInt();
                QString time = parts[4];
                QString senderId = parts[5];
                QString uuid = parts[6];

                QString displayContent = content;
                displayContent.replace("<br>", "\n");

                ChatMessage incoming(targetId, senderId.toStdString(), sender.toStdString(), content.toStdString(), time.toStdString(), type, uuid.toStdString(), "");

                if (type == 0) {
                    QString friendName = "Unknown";
                    for (int i = 0; i < ui.listFriends->count(); ++i) {
                        QListWidgetItem* it = ui.listFriends->item(i);
                        if (it->data(Qt::UserRole).toInt() == targetId) {
                            friendName = it->text().split(" [").first();
                            break;
                        }
                    }
                    if (friendName == "Unknown") friendName = QString::number(targetId);
                    QString path = getPrivateChatFilePath(friendName, targetId);
                    if (!path.isEmpty()) LocalDataManager::saveMessage(path, incoming);
                }
                else if (type == 1) {
                    QString groupName = "Unknown";
                    for (int i = 0; i < ui.listGroups->count(); ++i) {
                        QListWidgetItem* it = ui.listGroups->item(i);
                        if (it->data(Qt::UserRole).toInt() == targetId) {
                            groupName = it->text();
                            break;
                        }
                    }
                    QString path = getGroupChatFilePath(groupName);
                    if (!path.isEmpty()) LocalDataManager::saveMessage(path, incoming);
                }

                if (m_currentSessionId == targetId && m_currentType == type) {
                    QString lineMsg = QString("[%1] %2: %3").arg(time).arg(sender).arg(displayContent);
                    ui.browserChat->append(lineMsg);

                    int blockId = ui.browserChat->document()->blockCount() - 1;
                    m_blockToUuid[blockId] = uuid;
                }
            }
            continue;
        }
        appendLog(cleanLine);
    }
}

// ====================================================================
// 解析辅助
// ====================================================================

void WeQQClient::parseLoginSuccess(QString data) {
    m_myId = data.toInt();
    if (m_myId > 0) {
        checkAndInitFolders();
        appendLog(">>> 登录成功，ID: " + QString::number(m_myId));
        ui.stackedWidget->setCurrentIndex(1);
        ui.btnConnect->setEnabled(true);
        ui.btnConnect->setText("连接");
        m_client->sendMsg("CMD:REQ_FRIEND_LIST");
        m_client->sendMsg("CMD:REQ_GROUP_LIST");
    }
}

// 【修改点2】初始加载列表时
void WeQQClient::parseUserList(QString data) {
    ui.listFriends->clear();
    QStringList users = data.split(';', Qt::SkipEmptyParts);
    for (const QString& u : users) {
        QStringList parts = u.split(',');
        if (parts.size() >= 2) {
            QString name = parts[1];
            if (name == ui.leName->text()) continue;
            QListWidgetItem* item = new QListWidgetItem(name);
            item->setData(Qt::UserRole, parts[0].toInt());

            // 兼容状态字段 (getFriendsListCmd 里带的)
            int status = 0;
            if (parts.size() >= 3) status = parts[2].toInt();

            if (status == 1) {
                item->setForeground(Qt::darkGreen);
                item->setText(name + " [在线]");
            }
            else {
                item->setForeground(Qt::gray);
                item->setText(name + " [离线]");
            }

            ui.listFriends->addItem(item);
        }
    }
}

// 【修改点3】广播更新时
void WeQQClient::updateOnlineStatus(QString data) {
    QSet<int> onlineIds;
    QStringList users = data.split(';', Qt::SkipEmptyParts);
    for (const QString& u : users) {
        QStringList parts = u.split(',');
        if (parts.size() >= 1) {
            onlineIds.insert(parts[0].toInt());
        }
    }

    for (int i = 0; i < ui.listFriends->count(); ++i) {
        QListWidgetItem* item = ui.listFriends->item(i);
        int uid = item->data(Qt::UserRole).toInt();
        QString baseName = item->text().split(" [").first();

        if (onlineIds.contains(uid)) {
            item->setForeground(Qt::darkGreen);
            item->setText(baseName + " [在线]");
        }
        else {
            // 离线：灰色 + [离线]
            item->setForeground(Qt::gray);
            item->setText(baseName + " [离线]");
        }
    }
}

void WeQQClient::parseFriendAdd(QString data) {
    QStringList parts = data.split(',');
    if (parts.size() >= 2) {
        int newId = parts[0].toInt();
        QString newName = parts[1];
        if (newName == ui.leName->text()) return;

        bool exists = false;
        for (int i = 0; i < ui.listFriends->count(); ++i) {
            QListWidgetItem* it = ui.listFriends->item(i);
            if (it->text().split(" [").first() == newName) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            // 新加的好友默认先给离线状态，等待下一次广播刷新
            QListWidgetItem* item = new QListWidgetItem(newName);
            item->setData(Qt::UserRole, newId);
            item->setForeground(Qt::gray);
            item->setText(newName + " [离线]");
            ui.listFriends->addItem(item);
        }
    }
}

void WeQQClient::parseGroupList(QString data) {
    ui.listGroups->clear();
    QStringList groups = data.split(';', Qt::SkipEmptyParts);
    for (const QString& g : groups) {
        QStringList parts = g.split(',');
        if (parts.size() >= 2) {
            QListWidgetItem* item = new QListWidgetItem(parts[1]);
            item->setData(Qt::UserRole, parts[0].toInt());
            ui.listGroups->addItem(item);
        }
    }
}

// ====================================================================
// 列表点击切换会话
// ====================================================================

void WeQQClient::on_listFriends_itemClicked(QListWidgetItem* item) {
    int uid = item->data(Qt::UserRole).toInt();
    QString name = item->text().split(" [").first();

    if (ui.tabWidget->indexOf(m_tabGroupMembers) != -1) ui.tabWidget->removeTab(ui.tabWidget->indexOf(m_tabGroupMembers));
    if (ui.tabWidget->indexOf(m_tabRequests) != -1) ui.tabWidget->removeTab(ui.tabWidget->indexOf(m_tabRequests));

    if (m_currentType == 1) m_client->sendMsg("CMD:LEAVE_GROUP");
    else if (m_currentType == 0 && m_currentSessionId != -1 && m_currentSessionId != uid) m_client->sendMsg("CMD:LEAVE_FRIEND");

    if (m_isCommandMode) {
        m_isCommandMode = false;
        ui.labelChatTitle->setText("正在与好友 [" + name + "] 聊天");
        ui.browserChat->clear();
        m_blockToUuid.clear();
    }

    m_currentSessionId = uid;
    m_currentSessionName = name;
    m_currentType = 0;
    ui.labelChatTitle->setText("正在与好友 [" + name + "] 聊天");

    ui.browserChat->clear();
    m_blockToUuid.clear();

    m_client->sendMsg("CMD:ENTER_FRIEND|" + QString::number(uid));
}

void WeQQClient::on_listGroups_itemClicked(QListWidgetItem* item) {
    int gid = item->data(Qt::UserRole).toInt();
    QString name = item->text();

    if (ui.tabWidget->indexOf(m_tabRequests) != -1) ui.tabWidget->removeTab(ui.tabWidget->indexOf(m_tabRequests));
    if (ui.tabWidget->indexOf(m_tabGroupMembers) == -1) ui.tabWidget->addTab(m_tabGroupMembers, "群成员");

    if (m_currentType == 1 && m_currentSessionId != -1 && m_currentSessionId != gid) m_client->sendMsg("CMD:LEAVE_GROUP");
    else if (m_currentType == 0 && m_currentSessionId != -1) m_client->sendMsg("CMD:LEAVE_FRIEND");

    if (m_isCommandMode) {
        m_isCommandMode = false;
        ui.labelChatTitle->setText("正在群组 [" + name + "] 发言");
        ui.browserChat->clear();
        m_blockToUuid.clear();
    }

    m_currentSessionId = gid;
    m_currentSessionName = name;
    m_currentType = 1;
    ui.labelChatTitle->setText("正在群组 [" + name + "] 发言");

    ui.browserChat->clear();
    m_blockToUuid.clear();

    m_client->sendMsg("CMD:ENTER_GROUP|" + QString::number(gid));
    m_client->sendMsg("CMD:REQ_GROUP_MEMBERS|" + QString::number(gid));
}

void WeQQClient::on_btnRequestList_clicked() {
    if (ui.tabWidget->indexOf(m_tabGroupMembers) != -1) ui.tabWidget->removeTab(ui.tabWidget->indexOf(m_tabGroupMembers));
    if (ui.tabWidget->indexOf(m_tabRequests) == -1) ui.tabWidget->addTab(m_tabRequests, "申请列表");
    ui.tabWidget->setCurrentWidget(m_tabRequests);

    if (m_currentType == 1) m_client->sendMsg("CMD:LEAVE_GROUP");
    else if (m_currentType == 0 && m_currentSessionId != -1) m_client->sendMsg("CMD:LEAVE_FRIEND");

    m_isCommandMode = false;
    m_currentSessionId = -1;
    m_currentSessionName = "";
    ui.labelChatTitle->setText("处理请求中...");
    ui.browserChat->clear();
    m_blockToUuid.clear();

    m_client->sendMsg("CMD:ENTER_REQUEST_LIST");
}

// ====================================================================
// 右键菜单逻辑
// ====================================================================

void WeQQClient::onChatContextMenu(const QPoint& pos) {
    QTextCursor cursor = ui.browserChat->cursorForPosition(pos);
    int blockNumber = cursor.block().blockNumber();

    if (m_blockToUuid.contains(blockNumber)) {
        QString uuid = m_blockToUuid[blockNumber];
        if (uuid.isEmpty()) return;

        QMenu* menu = new QMenu(this);
        QAction* actDelete = menu->addAction("删除此消息");
        connect(actDelete, &QAction::triggered, [=]() {
            m_client->sendMsg("/delete " + uuid);
            });
        menu->exec(ui.browserChat->mapToGlobal(pos));
        delete menu;
    }
}

void WeQQClient::on_listGroupMembers_customContextMenuRequested(const QPoint& pos) {
    QListWidgetItem* item = m_listGroupMembers->itemAt(pos);
    if (!item) return;
    int targetId = item->data(Qt::UserRole).toInt();

    QString rawName = item->text();
    QString targetName = rawName.split(" [").first();
    if (targetName.startsWith("[群主] ")) targetName = targetName.mid(5);
    else if (targetName.startsWith("[管理员] ")) targetName = targetName.mid(6);

    if (targetName == ui.leName->text()) return;

    QMenu* menu = new QMenu(this);
    bool isFriend = false;
    for (int i = 0; i < ui.listFriends->count(); ++i) {
        if (ui.listFriends->item(i)->text().startsWith(targetName)) {
            isFriend = true;
            break;
        }
    }
    if (!isFriend) {
        QAction* actAdd = menu->addAction("添加好友");
        connect(actAdd, &QAction::triggered, [=]() {
            m_client->sendMsg("/friend_add " + QString::number(targetId));
            });
    }

    QAction* actKick = menu->addAction("踢出");
    connect(actKick, &QAction::triggered, [=]() {
        if (m_currentType == 1 && m_currentSessionId != -1) {
            m_client->sendMsg("CMD:KICK_MEMBER|" + QString::number(m_currentSessionId) + "|" + QString::number(targetId));
        }
        });

    QAction* actMute = menu->addAction("禁言 (未实装)");
    actMute->setEnabled(false);

    QAction* actSetAdmin = menu->addAction("设为管理员");
    connect(actSetAdmin, &QAction::triggered, [=]() {
        if (m_currentType == 1 && m_currentSessionId != -1) {
            m_client->sendMsg("CMD:SET_ROLE|" + QString::number(m_currentSessionId) + "|" + QString::number(targetId) + "|2");
        }
        });

    QAction* actRemAdmin = menu->addAction("取消管理员");
    connect(actRemAdmin, &QAction::triggered, [=]() {
        if (m_currentType == 1 && m_currentSessionId != -1) {
            m_client->sendMsg("CMD:SET_ROLE|" + QString::number(m_currentSessionId) + "|" + QString::number(targetId) + "|1");
        }
        });

    menu->exec(QCursor::pos());
    delete menu;
}

void WeQQClient::on_listRequests_customContextMenuRequested(const QPoint& pos) {
    QListWidgetItem* item = m_listRequests->itemAt(pos);
    if (!item) return;
    QString meta = item->data(Qt::UserRole).toString();
    QMenu* menu = new QMenu(this);
    QAction* actAccept = menu->addAction("同意");
    connect(actAccept, &QAction::triggered, [=]() {
        m_client->sendMsg("CMD:DECISION_REQUEST|" + meta + "|1");
        delete m_listRequests->takeItem(m_listRequests->row(item));
        });
    QAction* actReject = menu->addAction("拒绝");
    connect(actReject, &QAction::triggered, [=]() {
        m_client->sendMsg("CMD:DECISION_REQUEST|" + meta + "|0");
        delete m_listRequests->takeItem(m_listRequests->row(item));
        });
    menu->exec(QCursor::pos());
    delete menu;
}

void WeQQClient::appendLog(QString msg) { ui.browserChat->append(msg); }
void WeQQClient::on_btnAddFriend_clicked() {
    QString target = ui.leSearch->text().trimmed();
    if (!target.isEmpty()) m_client->sendMsg("/friend_add " + target);
    ui.leSearch->clear();
}
void WeQQClient::on_btnJoinGroup_clicked() {
    QString target = ui.leSearch->text().trimmed();
    if (!target.isEmpty()) m_client->sendMsg("/g_join " + target);
    ui.leSearch->clear();
}
void WeQQClient::on_btnCreateGroup_clicked() {
    QString name = ui.leSearch->text().trimmed();
    if (!name.isEmpty()) m_client->sendMsg("/g_create " + name);
    ui.leSearch->clear();
}