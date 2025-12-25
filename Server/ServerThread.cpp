#include "ServerThread.h"
#include <iostream>
#include <WS2tcpip.h>
#include <vector>
#include <sstream>
#include <ctime>   
#include <iomanip>
#include <fstream>

std::string getCurrentTimeStr() {
    auto now = std::time(nullptr);
    struct tm tstruct;
    if (localtime_s(&tstruct, &now) != 0) return "00:00:00";
    std::ostringstream oss;
    oss << std::put_time(&tstruct, "%H:%M:%S");
    return oss.str();
}

// ====================================================================
// ServerThread 构造与析构
// ====================================================================

ServerThread::ServerThread(QObject* parent)
    : QThread(parent)
    , m_userMgr("Users.txt", TYPE_USER)
    , m_logMgr("Log.txt", TYPE_LOG)
    , m_serverSock(INVALID_SOCKET)
    , m_isRunning(false)
{
    initGroupRecordFolder();
    initFriendRecordFolder();

    int pubGid = m_groupMgr.getGroupIdByName("公共聊天室");
    if (pubGid == -1) {
        pubGid = m_groupMgr.createGroup("公共聊天室", "");
        if (pubGid != -1) {
            m_groupMgr.save();
            std::cout << "[System] Auto-created default group '公共聊天室' (ID: " << pubGid << ")" << std::endl;
        }
    }
}

ServerThread::~ServerThread() {
    m_isRunning = false;
    if (m_serverSock != INVALID_SOCKET) {
        closesocket(m_serverSock);
    }
    m_userMgr.save();
    m_logMgr.save();
    m_groupMgr.save();
    WSACleanup();
}

void ServerThread::setPort(int port) {
    m_port = port;
}

void ServerThread::logToGui(std::string msg) {
    std::lock_guard<std::mutex> lock(m_logMutex);
    m_logMgr.addLog(msg);
    emit logMessage(QString::fromStdString(msg));
}

std::string ServerThread::buildUserList() {
    std::string listStr = "CMD:USER_LIST|";
    std::lock_guard<std::mutex> lock(m_userMutex);
    for (auto const& [name, sock] : m_onlineUsers) {
        int uid = 0;
        std::vector<User> all = m_userMgr.getAllUsers();
        for (auto& u : all) {
            if (u.getUsername() == name) {
                uid = u.getId();
                break;
            }
        }
        if (uid != 0) {
            listStr += std::to_string(uid) + "," + name + ";";
        }
    }
    listStr += "\n";
    return listStr;
}

void ServerThread::initGroupRecordFolder() {
    QDir dir("GroupRecord");
    if (!dir.exists()) dir.mkpath(".");
}

void ServerThread::initFriendRecordFolder() {
    QDir dir("FriendRecord");
    if (!dir.exists()) dir.mkpath(".");
}

// 服务器端生成 UUID
std::string ServerThread::generateUUID() {
    srand((unsigned)time(NULL) + rand());
    char buf[64];
    sprintf_s(buf, sizeof(buf), "%lld-%d", (long long)time(NULL), rand());
    return std::string(buf);
}

void ServerThread::saveGroupMessageToFile(const std::string& groupName, int senderId, const std::string& sender, const std::string& content, const std::string& time) {
    std::string path = "GroupRecord/" + groupName + ".txt";
    std::ofstream ofs(path, std::ios::app);
    if (ofs.is_open()) {
        std::string uuid = generateUUID();
        ofs << time << "|" << senderId << "|" << sender << "|" << content << "|" << uuid << "|" << "" << std::endl;
        ofs.close();
    }
}

void ServerThread::saveFriendMessageToFile(int id1, int id2, int senderId, const std::string& senderName, const std::string& content, const std::string& time) {
    int minId = (id1 < id2) ? id1 : id2;
    int maxId = (id1 > id2) ? id1 : id2;
    std::string path = "FriendRecord/" + std::to_string(minId) + "_" + std::to_string(maxId) + ".txt";

    std::ofstream ofs(path, std::ios::app);
    if (ofs.is_open()) {
        std::string uuid = generateUUID();
        ofs << time << "|" << senderId << "|" << senderName << "|" << content << "|" << uuid << "|" << "" << std::endl;
        ofs.close();
    }
}

std::vector<std::string> ServerThread::loadGroupHistory(const std::string& groupName, int requesterId) {
    std::vector<std::string> history;
    std::string path = "GroupRecord/" + groupName + ".txt";
    std::ifstream ifs(path);
    if (!ifs.is_open()) return history;
    std::string line;
    std::string delToken = "d" + std::to_string(requesterId) + ",";

    while (std::getline(ifs, line)) {
        if (!line.empty()) {
            std::stringstream ss(line);
            std::string segment;
            std::vector<std::string> parts;
            while (std::getline(ss, segment, '|')) parts.push_back(segment);

            if (parts.size() >= 6) {
                if (parts[5].find(delToken) != std::string::npos) {
                    continue;
                }
            }
            history.push_back(line);
        }
    }
    return history;
}

std::vector<std::string> ServerThread::loadFriendHistory(int id1, int id2, int requesterId) {
    std::vector<std::string> history;
    int minId = (id1 < id2) ? id1 : id2;
    int maxId = (id1 > id2) ? id1 : id2;
    std::string path = "FriendRecord/" + std::to_string(minId) + "_" + std::to_string(maxId) + ".txt";
    std::ifstream ifs(path);
    if (!ifs.is_open()) return history;
    std::string line;
    std::string delToken = "d" + std::to_string(requesterId) + ",";

    while (std::getline(ifs, line)) {
        if (!line.empty()) {
            std::stringstream ss(line);
            std::string segment;
            std::vector<std::string> parts;
            while (std::getline(ss, segment, '|')) parts.push_back(segment);

            if (parts.size() >= 6) {
                if (parts[5].find(delToken) != std::string::npos) {
                    continue;
                }
            }
            history.push_back(line);
        }
    }
    return history;
}

void ServerThread::handleDeleteMessage(SOCKET clientSock, int myId, const std::string& uuid) {
    int gid = getClientGroupState(clientSock);
    int fid = getClientFriendState(clientSock);

    std::string path = "";
    bool isGroup = false;

    if (gid != -1) {
        std::string gName = m_groupMgr.getGroupName(gid);
        if (gName != "Unknown") {
            path = "GroupRecord/" + gName + ".txt";
            isGroup = true;
        }
    }
    else if (fid != -1) {
        int minId = (myId < fid) ? myId : fid;
        int maxId = (myId > fid) ? myId : fid;
        path = "FriendRecord/" + std::to_string(minId) + "_" + std::to_string(maxId) + ".txt";
    }

    if (!path.empty()) {
        updateFileForDelete(path, uuid, myId);

        std::string clearCmd = "CMD:CLEAR_CHAT\n";
        send(clientSock, clearCmd.c_str(), (int)clearCmd.length(), 0);

        std::vector<std::string> history;
        if (isGroup) history = loadGroupHistory(m_groupMgr.getGroupName(gid), myId);
        else history = loadFriendHistory(myId, fid, myId);

        for (const auto& line : history) {
            std::stringstream ss(line);
            std::string item;
            std::vector<std::string> parts;
            while (std::getline(ss, item, '|')) parts.push_back(item);

            if (parts.size() >= 5) {
                std::string packet;
                if (isGroup) {
                    packet = "MSG:" + std::to_string(gid) + "|" + parts[2] + "|" + parts[3] + "|1|" + parts[0] + "|" + parts[1] + "|" + parts[4] + "\n";
                }
                else {
                    packet = "MSG:" + std::to_string(fid) + "|" + parts[2] + "|" + parts[3] + "|0|" + parts[0] + "|" + parts[1] + "|" + parts[4] + "\n";
                }
                send(clientSock, packet.c_str(), (int)packet.length(), 0);
            }
        }
        logToGui("User " + std::to_string(myId) + " deleted msg " + uuid);
    }
}

void ServerThread::updateFileForDelete(const std::string& filePath, const std::string& uuid, int myId) {
    std::vector<std::string> lines;
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) return;

    std::string line;
    std::string searchUUID = "|" + uuid + "|";
    while (std::getline(ifs, line)) {
        if (line.find(searchUUID) != std::string::npos) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            line += "d" + std::to_string(myId) + ",";
        }
        lines.push_back(line);
    }
    ifs.close();

    std::ofstream ofs(filePath, std::ios::trunc);
    for (const auto& l : lines) {
        ofs << l << std::endl;
    }
}

std::string ServerThread::getFriendsListCmd(int userId) {
    std::string listCmd = "CMD:FRIEND_LIST|";
    QDir dir("FriendRecord");
    QStringList filters;
    filters << "*.txt";
    QFileInfoList fileList = dir.entryInfoList(filters, QDir::Files);

    std::vector<User> allUsers = m_userMgr.getAllUsers();

    for (const QFileInfo& fileInfo : fileList) {
        QString fileName = fileInfo.baseName();
        QStringList ids = fileName.split('_');
        if (ids.size() == 2) {
            int id1 = ids[0].toInt();
            int id2 = ids[1].toInt();
            int friendId = -1;

            if (id1 == userId) friendId = id2;
            else if (id2 == userId) friendId = id1;

            if (friendId != -1) {
                std::string friendName = "Unknown";
                for (const auto& u : allUsers) {
                    if (u.getId() == friendId) {
                        friendName = u.getUsername();
                        break;
                    }
                }

                int status = 0;
                {
                    std::lock_guard<std::mutex> lock(m_userMutex);
                    if (m_onlineUsers.count(friendName)) status = 1;
                }

                listCmd += std::to_string(friendId) + "," + friendName + "," + std::to_string(status) + ";";
            }
        }
    }
    listCmd += "\n";
    return listCmd;
}

void ServerThread::setClientGroupState(SOCKET s, int gid) {
    std::lock_guard<std::mutex> lock(m_groupStateMutex);
    m_clientGroupMap[s] = gid;
}

int ServerThread::getClientGroupState(SOCKET s) {
    std::lock_guard<std::mutex> lock(m_groupStateMutex);
    if (m_clientGroupMap.count(s)) return m_clientGroupMap[s];
    return -1;
}

void ServerThread::setClientFriendState(SOCKET s, int fid) {
    std::lock_guard<std::mutex> lock(m_friendStateMutex);
    m_clientFriendMap[s] = fid;
}

int ServerThread::getClientFriendState(SOCKET s) {
    std::lock_guard<std::mutex> lock(m_friendStateMutex);
    if (m_clientFriendMap.count(s)) return m_clientFriendMap[s];
    return -1;
}

void ServerThread::broadcastStatusChange(int userId, std::string userName, int status) {
    std::string packet = "CMD:STATUS_UPDATE|" + std::to_string(userId) + "|" + std::to_string(status) + "\n";
    std::lock_guard<std::mutex> lock(m_userMutex);
    for (auto const& [name, sock] : m_onlineUsers) {
        if (name == userName) continue;
        send(sock, packet.c_str(), (int)packet.length(), 0);
    }
}

void ServerThread::saveRequest(std::string type, int fromId, std::string fromName, int targetId) {
    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        std::ofstream ofs(m_requestFile, std::ios::app);
        if (ofs.is_open()) {
            ofs << type << "|" << fromId << "|" << fromName << "|" << targetId << std::endl;
            ofs.close();
        }
    }
    checkAndPushRequestUpdate(targetId);
}

void ServerThread::checkAndPushRequestUpdate(int targetId) {
    std::vector<SOCKET> socketsToPush;

    {
        std::lock_guard<std::mutex> lock(m_userMutex);
        std::lock_guard<std::mutex> viewLock(m_viewingReqMutex);

        for (const auto& [name, sock] : m_onlineUsers) {
            if (m_viewingRequestSockets.count(sock)) {
                socketsToPush.push_back(sock);
            }
        }
    }

    for (SOCKET s : socketsToPush) {
        int uid = 0;
        std::string uname = "";
        {
            std::lock_guard<std::mutex> lock(m_userMutex);
            for (auto& p : m_onlineUsers) if (p.second == s) { uname = p.first; break; }
        }

        if (!uname.empty()) {
            std::vector<User> all = m_userMgr.getAllUsers();
            for (auto& u : all) if (u.getUsername() == uname) { uid = u.getId(); break; }
        }

        if (uid != 0) {
            std::string resp = loadRequestsForUser(uid, uname);
            send(s, resp.c_str(), (int)resp.length(), 0);
        }
    }
}

std::string ServerThread::loadRequestsForUser(int userId, std::string userName) {
    std::lock_guard<std::mutex> lock(m_requestMutex);
    std::ifstream ifs(m_requestFile);
    if (!ifs.is_open()) return "CMD:REQUEST_LIST|\n";

    std::string line;
    std::string listStr = "CMD:REQUEST_LIST|";

    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string type, fromIdStr, fromName, targetIdStr;
        std::getline(ss, type, '|');
        std::getline(ss, fromIdStr, '|');
        std::getline(ss, fromName, '|');
        std::getline(ss, targetIdStr, '|');

        try {
            if (targetIdStr.empty() || fromIdStr.empty()) continue;

            int targetId = std::stoi(targetIdStr);
            int fromId = std::stoi(fromIdStr);

            bool isForMe = false;
            if (type == "FRIEND" && targetId == userId) {
                isForMe = true;
            }
            else if (type == "GROUP") {
                if (m_groupMgr.checkPermission(targetId, userName, ROLE_ADMIN)) {
                    isForMe = true;
                }
            }

            if (isForMe) {
                listStr += type + "," + fromIdStr + "," + fromName + "," + targetIdStr + ";";
            }
        }
        catch (...) {
            continue;
        }
    }
    listStr += "\n";
    return listStr;
}

void ServerThread::removeRequest(std::string type, int fromId, int targetId) {
    std::lock_guard<std::mutex> lock(m_requestMutex);
    std::ifstream ifs(m_requestFile);
    if (!ifs.is_open()) return;

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string t, fStr, fName, tStr;
        std::getline(ss, t, '|');
        std::getline(ss, fStr, '|');
        std::getline(ss, fName, '|');
        std::getline(ss, tStr, '|');

        try {
            if (fStr.empty() || tStr.empty()) {
                continue;
            }

            if (t == type && std::stoi(fStr) == fromId && std::stoi(tStr) == targetId) {
                continue;
            }
        }
        catch (...) {
            continue;
        }
        lines.push_back(line);
    }
    ifs.close();

    std::ofstream ofs(m_requestFile, std::ios::trunc);
    for (const auto& l : lines) {
        ofs << l << std::endl;
    }
}

void ServerThread::handleRequestDecision(std::string decisionStr) {
    std::stringstream ss(decisionStr);
    std::string type, fromIdStr, targetIdStr, resultStr;
    std::getline(ss, type, '|');
    std::getline(ss, fromIdStr, '|');
    std::getline(ss, targetIdStr, '|');
    std::getline(ss, resultStr, '|');

    int fromId = std::stoi(fromIdStr);
    int targetId = std::stoi(targetIdStr);
    bool accepted = (resultStr == "1");

    removeRequest(type, fromId, targetId);

    if (!accepted) return;

    if (type == "FRIEND") {
        std::string fromName = "";
        auto users = m_userMgr.getAllUsers();
        for (auto& u : users) if (u.getId() == fromId) fromName = u.getUsername();

        std::string targetName = "";
        for (auto& u : users) if (u.getId() == targetId) targetName = u.getUsername();

        if (!fromName.empty() && !targetName.empty()) {
            saveFriendMessageToFile(fromId, targetId, 0, "System", "Friend Added", getCurrentTimeStr());
            logToGui("[Request] Friend request accepted: " + fromName + " <-> " + targetName);

            // =========================================================
            // 【核心修复】死锁解决 + 状态缓存
            // =========================================================
            SOCKET fromSock = INVALID_SOCKET;
            SOCKET targetSock = INVALID_SOCKET;
            bool isFromOnline = false;
            bool isTargetOnline = false;

            {
                std::lock_guard<std::mutex> lock(m_userMutex);
                if (m_onlineUsers.count(fromName)) {
                    fromSock = m_onlineUsers[fromName];
                    isFromOnline = true;
                }
                if (m_onlineUsers.count(targetName)) {
                    targetSock = m_onlineUsers[targetName];
                    isTargetOnline = true;
                }
            }

            if (isFromOnline && fromSock != INVALID_SOCKET) {
                std::string list1 = getFriendsListCmd(fromId);
                send(fromSock, list1.c_str(), (int)list1.length(), 0);

                int statusVal = isTargetOnline ? 1 : 0;
                std::string statusMsg = "CMD:STATUS_UPDATE|" + std::to_string(targetId) + "|" + std::to_string(statusVal) + "\n";
                send(fromSock, statusMsg.c_str(), (int)statusMsg.length(), 0);
            }

            if (isTargetOnline && targetSock != INVALID_SOCKET) {
                std::string list2 = getFriendsListCmd(targetId);
                send(targetSock, list2.c_str(), (int)list2.length(), 0);

                int statusVal = isFromOnline ? 1 : 0;
                std::string statusMsg = "CMD:STATUS_UPDATE|" + std::to_string(fromId) + "|" + std::to_string(statusVal) + "\n";
                send(targetSock, statusMsg.c_str(), (int)statusMsg.length(), 0);
            }
        }
    }
    else if (type == "GROUP") {
        std::string fromName = "";
        auto users = m_userMgr.getAllUsers();
        for (auto& u : users) if (u.getId() == fromId) fromName = u.getUsername();

        if (!fromName.empty()) {
            if (m_groupMgr.joinGroup(targetId, fromName)) {
                m_groupMgr.save();
                logToGui("[Request] Group join accepted: " + fromName + " -> Group " + std::to_string(targetId));

                SOCKET fromSock = INVALID_SOCKET;
                {
                    std::lock_guard<std::mutex> lock(m_userMutex);
                    if (m_onlineUsers.count(fromName)) {
                        fromSock = m_onlineUsers[fromName];
                    }
                }

                if (fromSock != INVALID_SOCKET) {
                    std::string list = m_groupMgr.getMyGroupListCmd(fromName) + "\n";
                    send(fromSock, list.c_str(), (int)list.length(), 0);
                }
            }
        }
    }
}

void ServerThread::run() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        emit logMessage("Error: WSAStartup failed.");
        return;
    }

    m_serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (m_serverSock == INVALID_SOCKET) {
        emit logMessage("Error: Creating socket failed.");
        return;
    }

    SOCKADDR_IN addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(m_serverSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        emit logMessage("Error: Bind failed. Port might be in use.");
        closesocket(m_serverSock);
        m_serverSock = INVALID_SOCKET;
        return;
    }

    if (listen(m_serverSock, 5) == SOCKET_ERROR) {
        emit logMessage("Error: Listen failed.");
        closesocket(m_serverSock);
        m_serverSock = INVALID_SOCKET;
        return;
    }

    m_isRunning = true;
    emit logMessage(">>> Server started on port " + QString::number(m_port));
    emit logMessage(">>> Waiting for connections...");

    while (m_isRunning) {
        SOCKADDR_IN clientAddr = {};
        int nLen = sizeof(SOCKADDR_IN);
        SOCKET sockClient = accept(m_serverSock, (sockaddr*)&clientAddr, &nLen);

        if (!m_isRunning) break;

        if (sockClient != INVALID_SOCKET) {
            ThreadInfo* info = new ThreadInfo;
            info->sock = sockClient;
            info->pServer = this;
            CreateThread(NULL, 0, ClientHandlerStatic, (LPVOID)info, 0, NULL);
        }
    }
}

// --- 客户端处理 ---

DWORD WINAPI ServerThread::ClientHandlerStatic(LPVOID lpParam) {
    ThreadInfo* info = (ThreadInfo*)lpParam;
    SOCKET sockClient = info->sock;
    ServerThread* server = info->pServer;

    std::string clientName = "";
    int clientId = 0;

    char szData[2048] = { 0 };
    int len = recv(sockClient, szData, 2047, 0);

    if (len > 0) {
        szData[len] = '\0';
        std::string rawMsg = szData;
        while (!rawMsg.empty() && (rawMsg.back() == '\r' || rawMsg.back() == '\n'))
            rawMsg.pop_back();

        std::string password = "";

        if (rawMsg.find("CMD:LOGIN|") == 0) {
            std::string body = rawMsg.substr(10);
            std::stringstream ss(body);
            std::string segment;
            std::vector<std::string> parts;
            while (std::getline(ss, segment, '|')) parts.push_back(segment);

            if (parts.size() >= 2) {
                clientName = parts[0];
                password = parts[1];
            }
            else {
                closesocket(sockClient); delete info; return 0;
            }
        }
        else {
            clientName = rawMsg;
            password = "123456";
        }

        if (!clientName.empty()) {
            std::vector<User> all = server->m_userMgr.getAllUsers();
            User* existingUser = nullptr;
            for (auto& u : all) {
                if (u.getUsername() == clientName) {
                    existingUser = &u;
                    break;
                }
            }

            if (existingUser != nullptr) {
                if (existingUser->getPassword() == password) {
                    clientId = existingUser->getId();
                    server->logToGui("User [" + clientName + "] login success.");
                }
                else {
                    std::string failMsg = "CMD:LOGIN_FAIL|Wrong Password\n";
                    send(sockClient, failMsg.c_str(), (int)failMsg.length(), 0);
                    server->logToGui("User [" + clientName + "] login failed (wrong password).");
                    closesocket(sockClient); delete info; return 0;
                }
            }
            else {
                clientId = server->m_userMgr.getNextId();
                server->m_userMgr.addUser(User(clientId, clientName, password, "127.0.0.1"));
                server->logToGui("[System] Registered new User " + clientName + " ID:" + std::to_string(clientId));

                int pubGid = server->m_groupMgr.getGroupIdByName("公共聊天室");
                if (pubGid != -1) {
                    if (server->m_groupMgr.joinGroup(pubGid, clientName)) {
                        server->m_groupMgr.save();
                        server->logToGui("[System] User " + clientName + " auto-joined '公共聊天室'");
                    }
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(server->m_userMutex);
            server->m_onlineUsers[clientName] = sockClient;
        }
        server->setClientGroupState(sockClient, -1);
        server->setClientFriendState(sockClient, -1);

        std::string loginMsg = "CMD:LOGIN_SUCCESS|" + std::to_string(clientId) + "\n";
        send(sockClient, loginMsg.c_str(), (int)loginMsg.length(), 0);

        if (AdminManager::getUserGroup(clientName) == GROUP_ADMIN) {
            std::string adminMsg = "CMD:GRANT_ADMIN\n";
            send(sockClient, adminMsg.c_str(), (int)adminMsg.length(), 0);
            std::string welcome = "[System] Welcome Administrator " + clientName + "\n";
            send(sockClient, welcome.c_str(), (int)welcome.length(), 0);
        }

        server->broadcastStatusChange(clientId, clientName, 1);

        std::string userList = server->buildUserList();
        {
            std::lock_guard<std::mutex> lock(server->m_userMutex);
            for (auto const& [name, sock] : server->m_onlineUsers) {
                send(sock, userList.c_str(), (int)userList.length(), 0);
            }
        }
    }
    else {
        closesocket(sockClient);
        delete info;
        return 0;
    }

    while (true)
    {
        char szRecv[4096] = { 0 };
        int ret = recv(sockClient, szRecv, 4095, 0);

        if (ret > 0)
        {
            szRecv[ret] = '\0';
            std::string rawMsg = szRecv;
            while (!rawMsg.empty() && (rawMsg.back() == '\r' || rawMsg.back() == '\n'))
                rawMsg.pop_back();

            if (rawMsg.find("/delete ") == 0) {
                std::string uuid = rawMsg.substr(8);
                server->handleDeleteMessage(sockClient, clientId, uuid);
                continue;
            }

            if (rawMsg.find("/friend_add ") == 0) {
                std::string arg = rawMsg.substr(12);
                int targetId = 0;
                bool isDigit = !arg.empty();
                for (char c : arg) if (!isdigit(c)) isDigit = false;

                if (isDigit) {
                    int id = std::stoi(arg);
                    auto users = server->m_userMgr.getAllUsers();
                    for (auto& u : users) if (u.getId() == id) { targetId = id; break; }
                }
                if (targetId == 0) {
                    auto users = server->m_userMgr.getAllUsers();
                    for (auto& u : users) if (u.getUsername() == arg) { targetId = u.getId(); break; }
                }

                if (targetId != 0 && targetId != clientId) {
                    server->saveRequest("FRIEND", clientId, clientName, targetId);
                    std::string msg = "[System] Friend request sent to " + arg + "\n";
                    send(sockClient, msg.c_str(), (int)msg.length(), 0);
                }
                else {
                    std::string msg = "[Error] User not found: " + arg + "\n";
                    send(sockClient, msg.c_str(), (int)msg.length(), 0);
                }
                continue;
            }

            if (rawMsg.find("/g_join ") == 0) {
                std::string arg = rawMsg.substr(8);
                int gid = 0;
                bool isDigit = !arg.empty();
                for (char c : arg) if (!isdigit(c)) isDigit = false;
                if (isDigit) {
                    int id = std::stoi(arg);
                    if (server->m_groupMgr.getGroupName(id) != "Unknown") gid = id;
                }
                if (gid == 0) gid = server->m_groupMgr.getGroupIdByName(arg);

                if (gid != 0) {
                    server->saveRequest("GROUP", clientId, clientName, gid);
                    std::string gName = server->m_groupMgr.getGroupName(gid);
                    std::string promptName = gName + "(ID:" + std::to_string(gid) + ")";
                    std::string msg = "[System] Join request sent to Group " + promptName + "\n";
                    send(sockClient, msg.c_str(), (int)msg.length(), 0);
                }
                else {
                    std::string msg = "[Error] Group not found: " + arg + "\n";
                    send(sockClient, msg.c_str(), (int)msg.length(), 0);
                }
                continue;
            }

            if (rawMsg.find("CMD:KICK_MEMBER|") == 0) {
                std::string body = rawMsg.substr(16);
                std::stringstream ss(body);
                std::string sGid, sTid;
                std::getline(ss, sGid, '|'); std::getline(ss, sTid, '|');
                int gid = std::stoi(sGid);
                int targetId = std::stoi(sTid);

                std::string targetName = "";
                auto users = server->m_userMgr.getAllUsers();
                for (auto& u : users) if (u.getId() == targetId) targetName = u.getUsername();

                if (!targetName.empty()) {
                    int myRole = server->m_groupMgr.getUserRole(gid, clientName);
                    int targetRole = server->m_groupMgr.getUserRole(gid, targetName);

                    if (myRole > targetRole) {
                        server->m_groupMgr.leaveGroup(gid, targetName);
                        server->m_groupMgr.save();
                        std::lock_guard<std::mutex> lock(server->m_userMutex);

                        if (server->m_onlineUsers.count(targetName)) {
                            SOCKET targetSock = server->m_onlineUsers[targetName];
                            std::string kickCmd = "CMD:KICKED_FROM_GROUP|" + sGid + "\n";
                            send(targetSock, kickCmd.c_str(), (int)kickCmd.length(), 0);
                            std::string list = server->m_groupMgr.getMyGroupListCmd(targetName) + "\n";
                            send(targetSock, list.c_str(), (int)list.length(), 0);
                            std::string notice = "[System] You have been kicked from Group " + std::to_string(gid) + "\n";
                            send(targetSock, notice.c_str(), (int)notice.length(), 0);
                        }

                        for (auto const& [uName, uSock] : server->m_onlineUsers) {
                            if (server->getClientGroupState(uSock) == gid) {
                                std::vector<std::string> members = server->m_groupMgr.getGroupMembers(gid);
                                std::string resp = "CMD:GROUP_MEMBERS|";
                                for (const auto& memName : members) {
                                    int memId = 0;
                                    for (auto& u : users) { if (u.getUsername() == memName) { memId = u.getId(); break; } }
                                    int status = (server->m_onlineUsers.count(memName) > 0) ? 1 : 0;
                                    int role = server->m_groupMgr.getUserRole(gid, memName);
                                    if (memId != 0) resp += std::to_string(memId) + "," + memName + "," + std::to_string(status) + "," + std::to_string(role) + ";";
                                }
                                resp += "\n";
                                send(uSock, resp.c_str(), (int)resp.length(), 0);
                            }
                        }
                        server->logToGui("[Group] " + clientName + " kicked " + targetName + " from Group " + sGid);
                    }
                }
                continue;
            }

            if (rawMsg.find("CMD:SET_ROLE|") == 0) {
                std::string body = rawMsg.substr(13);
                std::stringstream ss(body);
                std::string sGid, sTid, sRole;
                std::getline(ss, sGid, '|'); std::getline(ss, sTid, '|'); std::getline(ss, sRole, '|');
                int gid = std::stoi(sGid);
                int targetId = std::stoi(sTid);
                int newRole = std::stoi(sRole);

                std::string targetName = "";
                auto users = server->m_userMgr.getAllUsers();
                for (auto& u : users) if (u.getId() == targetId) targetName = u.getUsername();

                if (!targetName.empty()) {
                    if (server->m_groupMgr.checkPermission(gid, clientName, ROLE_OWNER)) {
                        server->m_groupMgr.setUserRole(gid, targetName, (GroupRole)newRole);
                        server->m_groupMgr.save();
                        std::lock_guard<std::mutex> lock(server->m_userMutex);
                        for (auto const& [uName, uSock] : server->m_onlineUsers) {
                            if (server->getClientGroupState(uSock) == gid) {
                                std::vector<std::string> members = server->m_groupMgr.getGroupMembers(gid);
                                std::string resp = "CMD:GROUP_MEMBERS|";
                                for (const auto& memName : members) {
                                    int memId = 0;
                                    for (auto& u : users) { if (u.getUsername() == memName) { memId = u.getId(); break; } }
                                    int status = (server->m_onlineUsers.count(memName) > 0) ? 1 : 0;
                                    int role = server->m_groupMgr.getUserRole(gid, memName);
                                    if (memId != 0) resp += std::to_string(memId) + "," + memName + "," + std::to_string(status) + "," + std::to_string(role) + ";";
                                }
                                resp += "\n";
                                send(uSock, resp.c_str(), (int)resp.length(), 0);
                            }
                        }
                        server->logToGui("[Group] " + clientName + " set role " + sRole + " for " + targetName);
                    }
                }
                continue;
            }

            if (rawMsg == "CMD:ENTER_REQUEST_LIST") {
                std::lock_guard<std::mutex> lock(server->m_viewingReqMutex);
                server->m_viewingRequestSockets.insert(sockClient);
                std::string resp = server->loadRequestsForUser(clientId, clientName);
                send(sockClient, resp.c_str(), (int)resp.length(), 0);
                continue;
            }

            if (rawMsg == "CMD:LEAVE_REQUEST_LIST") {
                std::lock_guard<std::mutex> lock(server->m_viewingReqMutex);
                server->m_viewingRequestSockets.erase(sockClient);
                continue;
            }

            if (rawMsg.find("CMD:DECISION_REQUEST|") == 0) {
                std::string body = rawMsg.substr(21);
                server->handleRequestDecision(body);
                std::string resp = server->loadRequestsForUser(clientId, clientName);
                send(sockClient, resp.c_str(), (int)resp.length(), 0);
                continue;
            }

            if (rawMsg[0] == '/') {
                if (AdminManager::processClientCommand(
                    clientName, rawMsg, sockClient,
                    server->m_onlineUsers, server->m_userMutex, server->m_groupMgr,
                    server->m_userMgr))
                {
                    server->logToGui("[" + clientName + "] Cmd: " + rawMsg);
                    if (rawMsg.find("/g_create") == 0) {
                        std::string list = server->m_groupMgr.getMyGroupListCmd(clientName) + "\n";
                        send(sockClient, list.c_str(), (int)list.length(), 0);
                    }
                    continue;
                }
            }

            if (rawMsg == "CMD:REQ_FRIEND_LIST") {
                std::string friendList = server->getFriendsListCmd(clientId);
                send(sockClient, friendList.c_str(), (int)friendList.length(), 0);
                continue;
            }

            if (rawMsg == "CMD:REQ_GROUP_LIST") {
                std::string groupList = server->m_groupMgr.getMyGroupListCmd(clientName);
                groupList += "\n";
                send(sockClient, groupList.c_str(), (int)groupList.length(), 0);
                continue;
            }

            if (rawMsg.find("CMD:REQ_GROUP_MEMBERS|") == 0) {
                std::string gidStr = rawMsg.substr(22);
                int gid = std::stoi(gidStr);
                std::vector<std::string> members = server->m_groupMgr.getGroupMembers(gid);
                std::string resp = "CMD:GROUP_MEMBERS|";
                std::lock_guard<std::mutex> lock(server->m_userMutex);
                std::vector<User> all = server->m_userMgr.getAllUsers();
                for (const auto& memName : members) {
                    int memId = 0;
                    for (auto& u : all) { if (u.getUsername() == memName) { memId = u.getId(); break; } }
                    int status = (server->m_onlineUsers.count(memName) > 0) ? 1 : 0;
                    int role = server->m_groupMgr.getUserRole(gid, memName);
                    if (memId != 0) resp += std::to_string(memId) + "," + memName + "," + std::to_string(status) + "," + std::to_string(role) + ";";
                }
                resp += "\n";
                send(sockClient, resp.c_str(), (int)resp.length(), 0);
                continue;
            }

            if (rawMsg.find("CMD:ENTER_FRIEND|") == 0) {
                std::string sId = rawMsg.substr(17);
                int targetId = std::stoi(sId);
                server->setClientFriendState(sockClient, targetId);
                server->logToGui(clientName + " entered friend chat with ID " + sId);
                std::vector<std::string> history = server->loadFriendHistory(clientId, targetId, clientId);
                for (const auto& line : history) {
                    std::stringstream ss(line);
                    std::string item;
                    std::vector<std::string> parts;
                    while (std::getline(ss, item, '|')) parts.push_back(item);

                    if (parts.size() >= 5) {
                        std::string packet = "MSG:" + std::to_string(targetId) + "|" + parts[2] + "|" + parts[3] + "|0|" + parts[0] + "|" + parts[1] + "|" + parts[4] + "\n";
                        send(sockClient, packet.c_str(), (int)packet.length(), 0);
                    }
                }
                continue;
            }

            if (rawMsg.find("CMD:LEAVE_FRIEND") == 0) {
                server->setClientFriendState(sockClient, -1);
                continue;
            }

            if (rawMsg.find("CMD:ENTER_GROUP|") == 0) {
                std::string sId = rawMsg.substr(16);
                int gid = std::stoi(sId);
                server->setClientGroupState(sockClient, gid);
                server->logToGui(clientName + " entered group " + sId);
                std::string gName = server->m_groupMgr.getGroupName(gid);
                if (gName != "Unknown") {
                    std::vector<std::string> history = server->loadGroupHistory(gName, clientId);
                    for (const auto& line : history) {
                        std::stringstream ss(line);
                        std::string item;
                        std::vector<std::string> parts;
                        while (std::getline(ss, item, '|')) parts.push_back(item);

                        if (parts.size() >= 5) {
                            std::string packet = "MSG:" + std::to_string(gid) + "|" + parts[2] + "|" + parts[3] + "|1|" + parts[0] + "|" + parts[1] + "|" + parts[4] + "\n";
                            send(sockClient, packet.c_str(), (int)packet.length(), 0);
                        }
                    }
                }
                continue;
            }

            if (rawMsg.find("CMD:LEAVE_GROUP") == 0) {
                server->setClientGroupState(sockClient, -1);
                continue;
            }

            if (rawMsg.find("SEND:") == 0) {
                std::string body = rawMsg.substr(5);
                std::stringstream ss(body);
                std::string seg;
                std::vector<std::string> parts;
                while (std::getline(ss, seg, '|')) parts.push_back(seg);

                if (parts.size() >= 3) {
                    int type = std::stoi(parts[0]);
                    int targetId = std::stoi(parts[1]);
                    std::string content = parts[2];
                    std::string timeStr = getCurrentTimeStr();

                    std::string uuid = generateUUID();

                    // 【核心】增加成员检查逻辑
                    if (type == 1) { // 群聊
                        std::vector<std::string> members = server->m_groupMgr.getGroupMembers(targetId);
                        bool isMember = false;
                        for (const auto& m : members) {
                            if (m == clientName) {
                                isMember = true;
                                break;
                            }
                        }

                        if (!isMember) {
                            // 不在群里，发送错误提示
                            std::string errorMsg = "[System] Failed to send: You are not a member of this group.\n";
                            send(sockClient, errorMsg.c_str(), (int)errorMsg.length(), 0);

                            /* 强制客户端退出界面
                            std::string kickCmd = "CMD:KICKED_FROM_GROUP|" + std::to_string(targetId) + "\n";
                            send(sockClient, kickCmd.c_str(), (int)kickCmd.length(), 0);*/

                            continue; // 跳过后续保存和转发
                        }
                    }

                    if (type == 0) { // 私聊
                        std::string targetName = "";
                        std::vector<User> all = server->m_userMgr.getAllUsers();
                        for (auto& u : all) if (u.getId() == targetId) { targetName = u.getUsername(); break; }
                        server->saveFriendMessageToFile(clientId, targetId, clientId, clientName, content, timeStr);
                        if (!targetName.empty()) {
                            std::lock_guard<std::mutex> lock(server->m_userMutex);
                            if (server->m_onlineUsers.count(targetName)) {
                                std::string packetToTarget = "MSG:" + std::to_string(clientId) + "|" + clientName + "|" + content + "|0|" + timeStr + "|" + std::to_string(clientId) + "|" + uuid + "\n";
                                send(server->m_onlineUsers[targetName], packetToTarget.c_str(), (int)packetToTarget.length(), 0);
                            }
                        }
                        std::string packetToMe = "MSG:" + std::to_string(targetId) + "|" + clientName + "|" + content + "|0|" + timeStr + "|" + std::to_string(clientId) + "|" + uuid + "\n";
                        send(sockClient, packetToMe.c_str(), (int)packetToMe.length(), 0);
                        server->logToGui("[Chat] " + clientName + " -> " + (targetName.empty() ? "Offline" : targetName) + ": " + content);
                    }
                    else if (type == 1) { // 群聊 (验证通过)
                        std::string gName = server->m_groupMgr.getGroupName(targetId);
                        server->saveGroupMessageToFile(gName, clientId, clientName, content, timeStr);
                        std::string packet = "MSG:" + std::to_string(targetId) + "|" + clientName + "|" + content + "|1|" + timeStr + "|" + std::to_string(clientId) + "|" + uuid + "\n";
                        std::lock_guard<std::mutex> lock(server->m_userMutex);
                        for (auto const& [uName, uSock] : server->m_onlineUsers) {
                            if (server->getClientGroupState(uSock) == targetId) {
                                send(uSock, packet.c_str(), (int)packet.length(), 0);
                            }
                        }
                        server->logToGui("[Group " + std::to_string(targetId) + "] " + clientName + ": " + content);
                    }
                }
                continue;
            }

            std::string fullMsg = "[" + clientName + "]: " + rawMsg;
            server->logToGui(fullMsg);
            {
                std::lock_guard<std::mutex> lock(server->m_userMutex);
                for (auto& pair : server->m_onlineUsers) {
                    send(pair.second, fullMsg.c_str(), (int)fullMsg.length(), 0);
                }
            }
        }
        else {
            server->logToGui("User [" + clientName + "] disconnected.");
            break;
        }
    }

    {
        std::lock_guard<std::mutex> lock(server->m_userMutex);
        server->m_onlineUsers.erase(clientName);
    }
    server->setClientGroupState(sockClient, -1);
    server->setClientFriendState(sockClient, -1);
    {
        std::lock_guard<std::mutex> lock(server->m_viewingReqMutex);
        server->m_viewingRequestSockets.erase(sockClient);
    }
    server->broadcastStatusChange(clientId, clientName, 0);

    std::string newUserList = server->buildUserList();
    {
        std::lock_guard<std::mutex> lock(server->m_userMutex);
        for (auto const& [name, sock] : server->m_onlineUsers) {
            send(sock, newUserList.c_str(), (int)newUserList.length(), 0);
        }
    }

    closesocket(sockClient);
    delete info;
    return 0;
}

void ServerThread::executeConsoleCommand(QString cmd) {
    QString raw = cmd.trimmed();
    if (raw.isEmpty()) return;
    if (!raw.startsWith("/")) raw.prepend("/");

    std::string strMsg = raw.toStdString();
    std::stringstream ss(strMsg);
    std::string command, arg1, arg2;
    ss >> command >> arg1 >> arg2;

    if (command == "/op") {
        if (arg1.empty()) {
            logToGui("[Error] Usage: op <username>");
        }
        else {
            AdminManager::setUserGroup(arg1, GROUP_ADMIN);
            logToGui("[System] Success: User [" + arg1 + "] is now an Admin.");
            std::lock_guard<std::mutex> lock(m_userMutex);
            if (m_onlineUsers.count(arg1)) {
                std::string grantMsg = "CMD:GRANT_ADMIN\n";
                send(m_onlineUsers[arg1], grantMsg.c_str(), (int)grantMsg.length(), 0);
                std::string notice = "[System] Server console granted you Admin permissions.\n";
                send(m_onlineUsers[arg1], notice.c_str(), (int)notice.length(), 0);
            }
        }
    }
    else if (command == "/deop") {
        if (arg1.empty()) {
            logToGui("[Error] Usage: deop <username>");
        }
        else {
            AdminManager::setUserGroup(arg1, GROUP_USER);
            logToGui("[System] Success: User [" + arg1 + "] is no longer an Admin.");
            std::lock_guard<std::mutex> lock(m_userMutex);
            if (m_onlineUsers.count(arg1)) {
                std::string revokeMsg = "CMD:REVOKE_ADMIN\n";
                send(m_onlineUsers[arg1], revokeMsg.c_str(), (int)revokeMsg.length(), 0);
                std::string notice = "[System] Your Admin permissions have been revoked by Server Console.\n";
                send(m_onlineUsers[arg1], notice.c_str(), (int)notice.length(), 0);
            }
        }
    }
    else if (command == "/kick") {
        if (arg1.empty()) {
            logToGui("[Error] Usage: kick <username>");
        }
        else {
            std::lock_guard<std::mutex> lock(m_userMutex);
            if (m_onlineUsers.count(arg1)) {
                SOCKET s = m_onlineUsers[arg1];
                std::string notice = "[System] You have been kicked by Server Console.\n";
                send(s, notice.c_str(), (int)notice.length(), 0);
                closesocket(s);
                m_onlineUsers.erase(arg1);
                logToGui("[System] User [" + arg1 + "] has been kicked.");
            }
            else {
                logToGui("[Error] User [" + arg1 + "] not found.");
            }
        }
    }
    else if (command == "/who") {
        std::lock_guard<std::mutex> lock(m_userMutex);
        QString listMsg = "--- Online Users (" + QString::number(m_onlineUsers.size()) + ") ---\n";
        for (auto& pair : m_onlineUsers) {
            std::string role = AdminManager::getUserGroup(pair.first);
            listMsg += " * " + QString::fromStdString(pair.first) + " [" + QString::fromStdString(role) + "]\n";
        }
        emit logMessage(listMsg);
    }
    else if (command == "/all") {
        std::string content;
        size_t pos = strMsg.find(' ');
        if (pos != std::string::npos) content = strMsg.substr(pos + 1);
        if (!content.empty()) {
            std::string broadcastMsg = "\n[Server Console]: " + content + "\n";
            {
                std::lock_guard<std::mutex> lock(m_userMutex);
                for (auto& pair : m_onlineUsers) {
                    send(pair.second, broadcastMsg.c_str(), (int)broadcastMsg.length(), 0);
                }
            }
            logToGui("[Broadcast] " + content);
        }
    }
    else if (command == "/create_group") {
        if (arg1.empty()) {
            logToGui("[Error] Usage: /create_group <Name> [Owner]");
        }
        else {
            std::string owner = arg2.empty() ? "ServerConsole" : arg2;
            int gid = m_groupMgr.createGroup(arg1, owner);
            if (gid != -1) {
                m_groupMgr.save();
                logToGui("[System] Group [" + arg1 + "] created. ID: " + std::to_string(gid));
            }
            else {
                logToGui("[Error] Group name already exists.");
            }
        }
    }
    else if (command == "/help") {
        std::string helpMsg =
            "--- Server Console Help ---\n"
            " /op <User>      - Set Admin\n"
            " /deop <User>    - Revoke Admin\n"
            " /kick <User>    - Kick User\n"
            " /who            - List Users\n"
            " /all <Msg>      - Broadcast\n"
            " /create_group <Name>\n";
        logToGui(helpMsg);
    }
    else {
        logToGui("[Error] Unknown command: " + command);
    }
}
