#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <iostream>

using namespace std;

// --- 权限等级 ---
enum GroupRole {
    ROLE_MEMBER = 1,
    ROLE_ADMIN = 2,
    ROLE_OWNER = 3
};

class Group {
public:
    Group() = default;
    Group(int id, string name, string time);

    int getId() const { return m_id; }
    string getName() const { return m_name; }
    vector<string> getMembers() const { return m_members; }

    void addMember(const string& username);
    void removeMember(const string& username);
    bool hasMember(const string& username) const;
    string toString() const;
    void parseMembers(const string& memberStr);

private:
    int m_id;
    string m_name;
    string m_createTime;
    vector<string> m_members;
};

class GroupManager {
public:
    GroupManager();
    ~GroupManager();

    void load();
    void save();

    // 业务操作
    int createGroup(string groupName, string ownerName);
    bool joinGroup(int groupId, string username);
    bool leaveGroup(int groupId, string username);

    // 权限管理
    void setUserRole(int groupId, string username, GroupRole role);
    bool checkPermission(int groupId, string username, GroupRole requiredRole);
    // 【新增】获取用户的具体权限等级，用于比较
    int getUserRole(int groupId, string username);

    string getGroupName(int groupId);
    int getGroupIdByName(string name);

    // 获取群成员
    vector<string> getGroupMembers(int groupId);

    // 获取群组列表协议字符串 (CMD:GROUP_LIST|...)
    string getGroupListCmd();

    // 获取“我”加入的群组列表
    string getMyGroupListCmd(string username);

private:
    string m_fileBasic = "Group1.txt";
    string m_filePerm = "Group2.txt";

    map<int, Group> m_groups;
    map<int, map<string, int>> m_groupRoles;
    mutex m_mutex;
    int m_maxGroupId = 0;
};