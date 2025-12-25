#pragma once
#include <QThread>
#include <QObject>
#include <map>
#include <set> 
#include <string>
#include <mutex>
#include <winsock2.h> 
#include <QDir> 
#include "DataManager.h"
#include "op.h"
#include "Group.h"
#pragma comment(lib,"ws2_32.lib")

class ServerThread : public QThread
{
    Q_OBJECT

public:
    explicit ServerThread(QObject* parent = nullptr);
    ~ServerThread();

    void setPort(int port);
    void executeConsoleCommand(QString cmd);

protected:
    void run() override;

signals:
    void logMessage(QString msg);

private:
    GroupManager m_groupMgr;
    int m_port = 9870;
    SOCKET m_serverSock;
    bool m_isRunning;

    DataManager m_userMgr;
    DataManager m_logMgr;
    std::map<std::string, SOCKET> m_onlineUsers;
    std::mutex m_userMutex;
    std::mutex m_logMutex;

    std::map<SOCKET, int> m_clientGroupMap;
    std::mutex m_groupStateMutex;
    std::map<SOCKET, int> m_clientFriendMap;
    std::mutex m_friendStateMutex;

    std::string m_requestFile = "Request.txt";
    std::mutex m_requestMutex;
    std::set<SOCKET> m_viewingRequestSockets;
    std::mutex m_viewingReqMutex;

    void saveRequest(std::string type, int fromId, std::string fromName, int targetId);
    void checkAndPushRequestUpdate(int targetId);
    std::string loadRequestsForUser(int userId, std::string userName);
    void handleRequestDecision(std::string decisionStr);
    void removeRequest(std::string type, int fromId, int targetId);

    void logToGui(std::string msg);
    std::string buildUserList();

    void initGroupRecordFolder();
    // 【修改】增加 senderId
    void saveGroupMessageToFile(const std::string& groupName, int senderId, const std::string& sender, const std::string& content, const std::string& time);
    // 【修改】增加 requesterId 用于过滤
    std::vector<std::string> loadGroupHistory(const std::string& groupName, int requesterId);

    void initFriendRecordFolder();
    void saveFriendMessageToFile(int id1, int id2, int senderId, const std::string& senderName, const std::string& content, const std::string& time);
    // 【修改】增加 requesterId 用于过滤
    std::vector<std::string> loadFriendHistory(int id1, int id2, int requesterId);

    // 【新增】服务器端 UUID 生成
    static std::string generateUUID();

    // 【新增】处理删除消息
    void handleDeleteMessage(SOCKET clientSock, int myId, const std::string& uuid);
    void updateFileForDelete(const std::string& filePath, const std::string& uuid, int myId);

    std::string getFriendsListCmd(int userId);

    void setClientGroupState(SOCKET s, int gid);
    int getClientGroupState(SOCKET s);
    void setClientFriendState(SOCKET s, int fid);
    int getClientFriendState(SOCKET s);

    void broadcastStatusChange(int userId, std::string userName, int status);

    static DWORD WINAPI ClientHandlerStatic(LPVOID lpParam);
};

struct ThreadInfo {
    SOCKET sock;
    ServerThread* pServer;
};