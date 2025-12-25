#pragma once

#include <string>
#include <map>
#include <mutex>
#include <winsock2.h> 
#include "Group.h"    
#include "DataManager.h" // 【新增】需要访问用户数据来获取ID

// ==========================================
// 全局配置常量
// ==========================================

// 管理员密钥 (用于 /op 指令)
#define ADMIN_SECRET_KEY "123456" 

// 权限组标识 (字符串常量)
#define GROUP_ADMIN "ADMIN"
#define GROUP_USER  "USER"

// ==========================================
// AdminManager 类定义
// ==========================================
class AdminManager {
public:
    // 【核心】处理客户端指令
    // 注意：这里必须接收 GroupManager& 引用，否则 cpp 会报错
    static bool processClientCommand(
        std::string clientName,
        std::string rawMsg,
        SOCKET currentSock,
        std::map<std::string, SOCKET>& onlineUsers,
        std::mutex& userMutex,
        GroupManager& groupMgr,
        DataManager& userMgr // 【新增】传入 UserMgr 以便查询所有注册用户
    );

    // --- 权限管理 (全局权限) ---
    // 设置用户的全局权限 (比如设为 ADMIN)
    static void setUserGroup(std::string username, std::string group);

    // 获取用户的全局权限 (返回 "ADMIN" 或 "USER")
    static std::string getUserGroup(std::string username);

private:
    // --- 内部辅助函数 ---

    // 发送系统消息 (封装 send)
    static void sendSystemMsg(SOCKET sock, std::string msg);

    // 显示在线用户列表 (/who)
    static void cmdList(SOCKET currentSock, std::map<std::string, SOCKET>& onlineUsers, std::mutex& userMutex);

    // --- 静态数据成员 ---
    // 用来在内存中存储谁是管理员 (简单的 Map)
    static std::map<std::string, std::string> m_userPermissions;
    static std::mutex m_permMutex;
};