#include "op.h"
#include <sstream>
#include <iostream>
#include <vector>

using namespace std;
std::map<std::string, std::string> AdminManager::m_userPermissions;
std::mutex AdminManager::m_permMutex;

void AdminManager::setUserGroup(std::string username, std::string group) {
    std::lock_guard<std::mutex> lock(m_permMutex);
    m_userPermissions[username] = group;
}

std::string AdminManager::getUserGroup(std::string username) {
    std::lock_guard<std::mutex> lock(m_permMutex);
    if (m_userPermissions.count(username)) {
        return m_userPermissions[username];
    }
    return GROUP_USER;
}

void AdminManager::sendSystemMsg(SOCKET sock, string msg) {
    if (sock != INVALID_SOCKET) {
        send(sock, msg.c_str(), (int)msg.length(), 0);
    }
}

void AdminManager::cmdList(SOCKET currentSock, map<string, SOCKET>& onlineUsers, mutex& userMutex) {
    string msg = "--- Online Users ---\n";
    lock_guard<mutex> lock(userMutex);
    for (auto const& [name, socket] : onlineUsers) {
        string role = getUserGroup(name);
        msg += " * " + name + " [" + role + "]\n";
    }
    sendSystemMsg(currentSock, msg);
}

// ==========================================================
// 核心函数：处理客户端发来的指令
// ==========================================================
bool AdminManager::processClientCommand(
    string clientName,
    string rawMsg,
    SOCKET currentSock,
    map<string, SOCKET>& onlineUsers,
    mutex& userMutex,
    GroupManager& groupMgr,
    DataManager& userMgr
)
{
    if (rawMsg.empty() || rawMsg[0] != '/') {    return false; }

    stringstream ss(rawMsg);
    string command, arg1, arg2;
    ss >> command >> arg1 >> arg2;

    bool isGlobalAdmin = (getUserGroup(clientName) == GROUP_ADMIN);

    // --- 查看在线人数 (/who) ---
    if (command == "/who") {
        cmdList(currentSock, onlineUsers, userMutex);
        return true;
    }

    // --- 帮助 (/help) ---
    if (command == "/help") {
        string msg = "--- General Commands ---\n"
            " /who                  - 查看在线用户列表\n"
            " /friend_add <ID/Name> - 添加好友 (输入ID或用户名)\n"
            " /g_join <ID/Name>     - 加入群组 (输入群ID或群名)\n"
            " /g_create <Name>      - 创建新群组\n"
            "\n"
            "--- Group Admin ---\n"
            " /g_kick <GID> <User>  - 踢出群成员 (需群主/管理权限)\n"
            "\n"
            "--- Server Admin Only ---\n"
            " /op <Name>            - 提升某人为服务器管理员\n"
            " /kick <Name>          - 强制踢某人下线\n"
            " /all <Message>        - 发送全服广播\n";
        sendSystemMsg(currentSock, msg);
        return true;
    }

    // --- 添加好友 (/friend_add) ---
    if (command == "/friend_add") {
        if (arg1.empty()) {
            sendSystemMsg(currentSock, "Usage: /friend_add <ID or Name>\n");
        }
        else if (arg1 == clientName) {
            sendSystemMsg(currentSock, "[Error] You cannot add yourself.\n");
        }
        else {
            vector<User> all = userMgr.getAllUsers();
            User* targetUser = nullptr;
            User* myUser = nullptr;
            int searchId = atoi(arg1.c_str());

            for (auto& u : all) {
                if (u.getUsername() == clientName) myUser = &u;
                if (searchId != 0 && u.getId() == searchId) targetUser = &u;
                else if (targetUser == nullptr && u.getUsername() == arg1) targetUser = &u;
            }

            if (targetUser != nullptr) {
                string packetToMe = "CMD:FRIEND_ADD|" + to_string(targetUser->getId()) + "," + targetUser->getUsername() + "\n";
                sendSystemMsg(currentSock, packetToMe);
                sendSystemMsg(currentSock, "[System] Friend added successfully.\n");

                if (myUser != nullptr) {
                    lock_guard<mutex> lock(userMutex);
                    if (onlineUsers.count(targetUser->getUsername())) {
                        SOCKET targetSock = onlineUsers[targetUser->getUsername()];
                        string packetToTarget = "CMD:FRIEND_ADD|" + to_string(myUser->getId()) + "," + myUser->getUsername() + "\n";
                        sendSystemMsg(targetSock, packetToTarget);
                        string notice = "[System] " + clientName + " added you as friend.\n";
                        sendSystemMsg(targetSock, notice);
                    }
                }
            }
            else {
                sendSystemMsg(currentSock, "[Error] User [" + arg1 + "] not found in server database.\n");
            }
        }
        return true;
    }

    // --- 申请管理员 (/op) ---
    // 【修改点】废弃密钥，改为管理员提权他人并发送广播
    if (command == "/op") {
        if (isGlobalAdmin) {
            if (arg1.empty()) {
                sendSystemMsg(currentSock, "Usage: /op <Username>\n");
            }
            else {
                string targetName = arg1;
                setUserGroup(targetName, GROUP_ADMIN);
                sendSystemMsg(currentSock, "[System] You granted Admin to [" + targetName + "].\n");

                // 【核心】对目标用户广播：激活他的“输入指令”按钮
                lock_guard<mutex> lock(userMutex);
                if (onlineUsers.count(targetName)) {
                    SOCKET targetSock = onlineUsers[targetName];
                    string packet = "CMD:GRANT_ADMIN\n"; // 发送激活指令
                    sendSystemMsg(targetSock, packet);
                    sendSystemMsg(targetSock, "[System] You have been promoted to Server Admin!\n");
                }
            }
        }
        else {
            sendSystemMsg(currentSock, "[Permission Denied] Only Admin can use /op.\n");
        }
        return true;
    }

    // ======================================================
    //  B. 群组指令
    // ======================================================

    if (command == "/g_create") {
        if (arg1.empty()) {
            sendSystemMsg(currentSock, "Usage: /g_create <GroupName>\n");
        }
        else {
            int gid = groupMgr.createGroup(arg1, clientName);
            if (gid == -1) {
                sendSystemMsg(currentSock, "[Error] Group name '" + arg1 + "' already exists.\n");
            }
            else {
                groupMgr.save();
                sendSystemMsg(currentSock, "[Group] Created [" + arg1 + "] successfully! GroupID: " + to_string(gid) + "\n");
                string listCmd = groupMgr.getGroupListCmd() + "\n";
                send(currentSock, listCmd.c_str(), (int)listCmd.length(), 0);
            }
        }
        return true;
    }

    if (command == "/g_join") {
        if (arg1.empty()) {
            sendSystemMsg(currentSock, "Usage: /g_join <GroupID or GroupName>\n");
            return true;
        }

        int gid = atoi(arg1.c_str());
        bool joined = false;

        if (gid > 0) joined = groupMgr.joinGroup(gid, clientName);

        if (!joined) {
            int nameId = groupMgr.getGroupIdByName(arg1);
            if (nameId != -1) {
                gid = nameId;
                joined = groupMgr.joinGroup(nameId, clientName);
            }
        }

        if (joined) {
            groupMgr.save();
            sendSystemMsg(currentSock, "[Group] You joined Group " + to_string(gid) + ".\n");
            string listCmd = groupMgr.getGroupListCmd() + "\n";
            send(currentSock, listCmd.c_str(), (int)listCmd.length(), 0);
        }
        else {
            sendSystemMsg(currentSock, "[Error] Failed to join (Group full, not exist, or already joined).\n");
        }
        return true;
    }

    if (command == "/g_kick") {
        int gid = atoi(arg1.c_str());
        string target = arg2;
        if (gid <= 0 || target.empty()) return true;

        if (groupMgr.checkPermission(gid, clientName, ROLE_ADMIN)) {
            if (groupMgr.leaveGroup(gid, target)) {
                groupMgr.save();
                sendSystemMsg(currentSock, "[Group] Kicked " + target + ".\n");
            }
            else {
                sendSystemMsg(currentSock, "[Error] Target user is not in this group.\n");
            }
        }
        else {
            sendSystemMsg(currentSock, "[Permission Denied] Admin only.\n");
        }
        return true;
    }

    // ======================================================
    //  C. 服务器管理员指令
    // ======================================================

    if (isGlobalAdmin) {
        if (command == "/kick") {
            lock_guard<mutex> lock(userMutex);
            if (onlineUsers.count(arg1)) {
                SOCKET s = onlineUsers[arg1];
                string notice = "You have been kicked by Admin.\n";
                send(s, notice.c_str(), (int)notice.length(), 0);
                closesocket(s);
                onlineUsers.erase(arg1);
                sendSystemMsg(currentSock, "[System] User " + arg1 + " kicked.\n");
            }
            else {
                sendSystemMsg(currentSock, "[System] User not found.\n");
            }
            return true;
        }
        if (command == "/all") {
            string broadcastMsg;
            size_t pos = rawMsg.find(' ');
            if (pos != string::npos) broadcastMsg = rawMsg.substr(pos + 1);
            if (!broadcastMsg.empty()) {
                string finalMsg = "\n[Server Broadcast]: " + broadcastMsg + "\n";
                lock_guard<mutex> lock(userMutex);
                for (auto const& [name, sock] : onlineUsers) {
                    send(sock, finalMsg.c_str(), (int)finalMsg.length(), 0);
                }
            }
            return true;
        }
    }

    sendSystemMsg(currentSock, "[Error] Unknown command. Type /help for list.\n");
    return true;
}