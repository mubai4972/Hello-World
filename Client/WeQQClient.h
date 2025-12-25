#pragma once

#include <QWidget>
#include "ui_WeQQClient.h"
#include "ClientSocket.h"
#include "LocalDataManager.h"
#include <QKeyEvent>
#include <QListWidgetItem>
#include <QDir>
#include <QMenu>
#include <QPushButton>
#include <QMap> 

class WeQQClient : public QWidget
{
    Q_OBJECT

public:
    WeQQClient(QWidget* parent = nullptr);
    ~WeQQClient();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    // --- 界面交互 ---
    void on_btnConnect_clicked();
    void on_btnSend_clicked();
    void on_btnAddFriend_clicked();
    void on_btnJoinGroup_clicked();
    void on_btnCreateGroup_clicked();

    // 【核心】动态按钮点击槽
    void on_btnCmd_clicked();

    void on_btnRequestList_clicked();
    void onTabChanged(int index);

    // --- 列表点击事件 ---
    void on_listFriends_itemClicked(QListWidgetItem* item);
    void on_listGroups_itemClicked(QListWidgetItem* item);

    // 右键菜单
    void on_listGroupMembers_customContextMenuRequested(const QPoint& pos);
    void on_listRequests_customContextMenuRequested(const QPoint& pos);
    // 【新增】聊天记录右键菜单
    void onChatContextMenu(const QPoint& pos);

    // --- 网络回调 ---
    void onMsgReceived(QString msg);
    void onConnectedSuccess();

private:
    Ui::WeQQClientClass ui;
    ClientSocket* m_client;

    int m_currentSessionId = -1;
    QString m_currentSessionName = "";
    int m_currentType = 0;

    int m_myId = 0;

    // 【核心】动态按钮指针与状态位
    QPushButton* m_btnCmd;
    bool m_isCommandMode;

    // 【新增】BlockNumber -> UUID 映射
    QMap<int, QString> m_blockToUuid;

    QString m_privateRecordRoot;
    QString m_groupRecordRoot;

    QListWidget* m_listGroupMembers;
    QWidget* m_tabGroupMembers;
    QPushButton* m_btnRequestList;
    QListWidget* m_listRequests;
    QWidget* m_tabRequests;

    void appendLog(QString msg);

    void parseUserList(QString data);
    // 【核心新增】只更新在线状态，不覆盖列表
    void updateOnlineStatus(QString data);

    void parseGroupList(QString data);
    void parseFriendAdd(QString data);
    void parseLoginSuccess(QString data);

    void checkAndInitFolders();
    QString getPrivateChatFilePath(QString friendName, int friendId);
    QString getGroupChatFilePath(QString groupName);
};