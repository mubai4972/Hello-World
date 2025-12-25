#include "Group.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <vector>

// --- Group 实现 ---
Group::Group(int id, string name, string time) : m_id(id), m_name(name), m_createTime(time) {}

void Group::addMember(const string& username) {
    if (!hasMember(username)) m_members.push_back(username);
}

void Group::removeMember(const string& username) {
    auto it = remove(m_members.begin(), m_members.end(), username);
    if (it != m_members.end()) m_members.erase(it, m_members.end());
}

bool Group::hasMember(const string& username) const {
    for (const auto& m : m_members) if (m == username) return true;
    return false;
}

string Group::toString() const {
    stringstream ss;
    ss << m_id << " " << m_name << " " << m_createTime << " ";
    if (m_members.empty()) ss << "None";
    else {
        for (size_t i = 0; i < m_members.size(); ++i) {
            ss << m_members[i] << (i < m_members.size() - 1 ? "," : "");
        }
    }
    return ss.str();
}

void Group::parseMembers(const string& memberStr) {
    m_members.clear();
    if (memberStr == "None") return;
    stringstream ss(memberStr);
    string seg;
    while (getline(ss, seg, ',')) {
        if (!seg.empty()) {
            string cleanName = seg;
            size_t p = cleanName.find('(');
            if (p != string::npos) {
                cleanName = cleanName.substr(0, p);
            }
            m_members.push_back(cleanName);
        }
    }
}

// --- GroupManager 实现 ---
GroupManager::GroupManager() { load(); }
GroupManager::~GroupManager() { save(); }

void GroupManager::load() {
    lock_guard<mutex> lock(m_mutex);
    m_groups.clear(); m_groupRoles.clear(); m_maxGroupId = 0;

    ifstream ifs1(m_fileBasic);
    string line;
    while (getline(ifs1, line)) {
        if (line.empty()) continue;
        stringstream ss(line);
        int id; string name, time, mems;
        if (ss >> id >> name >> time >> mems) {
            Group g(id, name, time);

            if (mems != "None") {
                stringstream mss(mems);
                string seg;
                while (getline(mss, seg, ',')) {
                    if (seg.empty()) continue;

                    string uName = seg;
                    int role = ROLE_MEMBER;

                    if (uName.find("(Owner)") != string::npos) {
                        role = ROLE_OWNER;
                        uName = uName.substr(0, uName.find("(Owner)"));
                    }
                    else if (uName.find("(Admin)") != string::npos) {
                        role = ROLE_ADMIN;
                        uName = uName.substr(0, uName.find("(Admin)"));
                    }

                    g.addMember(uName);
                    if (role != ROLE_MEMBER) {
                        m_groupRoles[id][uName] = role;
                    }
                }
            }

            m_groups[id] = g;
            if (id > m_maxGroupId) m_maxGroupId = id;
        }
    }
    ifs1.close();

    ifstream ifs2(m_filePerm);
    while (getline(ifs2, line)) {
        if (line.empty()) continue;
        stringstream ss(line);
        int gid, role; string u;
        if (ss >> gid >> u >> role) m_groupRoles[gid][u] = role;
    }
    ifs2.close();
}

void GroupManager::save() {
    lock_guard<mutex> lock(m_mutex);
    ofstream ofs1(m_fileBasic, ios::trunc);

    for (const auto& pair : m_groups) {
        int gid = pair.first;
        const Group& g = pair.second;
        vector<string> members = g.getMembers();

        string ownerStr = "";
        string adminStr = "";
        string memberStr = "";

        for (const auto& m : members) {
            int role = ROLE_MEMBER;
            if (m_groupRoles.count(gid) && m_groupRoles[gid].count(m)) {
                role = m_groupRoles[gid][m];
            }

            if (role == ROLE_OWNER) {
                ownerStr = m + "(Owner)";
            }
            else if (role == ROLE_ADMIN) {
                if (!adminStr.empty()) adminStr += ",";
                adminStr += m + "(Admin)";
            }
            else {
                if (!memberStr.empty()) memberStr += ",";
                memberStr += m;
            }
        }

        string finalMems = ownerStr;
        if (!adminStr.empty()) {
            if (!finalMems.empty()) finalMems += ",";
            finalMems += adminStr;
        }
        if (!memberStr.empty()) {
            if (!finalMems.empty()) finalMems += ",";
            finalMems += memberStr;
        }
        if (finalMems.empty()) finalMems = "None";

        ofs1 << g.getId() << " " << g.getName() << " " << "2025-01-01" << " " << finalMems << endl;
    }

    ofstream ofs2(m_filePerm, ios::trunc);
    for (const auto& gp : m_groupRoles) {
        for (const auto& up : gp.second) {
            if (up.second > ROLE_MEMBER)
                ofs2 << gp.first << " " << up.first << " " << up.second << endl;
        }
    }
}

int GroupManager::createGroup(string groupName, string ownerName) {
    lock_guard<mutex> lock(m_mutex);

    for (const auto& pair : m_groups) {
        if (pair.second.getName() == groupName) {
            return -1;
        }
    }

    int newId = ++m_maxGroupId;
    time_t now = time(0); char buf[80];
    struct tm tstruct; localtime_s(&tstruct, &now);
    strftime(buf, sizeof(buf), "%Y-%m-%d", &tstruct);

    Group g(newId, groupName, string(buf));
    g.addMember(ownerName);
    m_groups[newId] = g;
    m_groupRoles[newId][ownerName] = ROLE_OWNER;
    return newId;
}

bool GroupManager::joinGroup(int groupId, string username) {
    lock_guard<mutex> lock(m_mutex);
    if (m_groups.find(groupId) == m_groups.end()) return false;
    if (m_groups[groupId].getMembers().size() >= 100) return false;
    m_groups[groupId].addMember(username);
    return true;
}

bool GroupManager::leaveGroup(int groupId, string username) {
    lock_guard<mutex> lock(m_mutex);
    if (m_groups.find(groupId) == m_groups.end()) return false;
    m_groups[groupId].removeMember(username);
    if (m_groupRoles[groupId].count(username)) m_groupRoles[groupId].erase(username);
    return true;
}

void GroupManager::setUserRole(int groupId, string username, GroupRole role) {
    lock_guard<mutex> lock(m_mutex);
    m_groupRoles[groupId][username] = (int)role;
}

bool GroupManager::checkPermission(int groupId, string username, GroupRole requiredRole) {
    lock_guard<mutex> lock(m_mutex);
    if (m_groups.find(groupId) == m_groups.end()) return false;
    if (!m_groups[groupId].hasMember(username)) return false;

    int currentLevel = ROLE_MEMBER;
    if (m_groupRoles.count(groupId) && m_groupRoles[groupId].count(username)) {
        currentLevel = m_groupRoles[groupId][username];
    }
    return currentLevel >= (int)requiredRole;
}

// 【新增】获取具体权限等级
int GroupManager::getUserRole(int groupId, string username) {
    lock_guard<mutex> lock(m_mutex);
    if (m_groups.find(groupId) == m_groups.end()) return 0;

    int currentLevel = ROLE_MEMBER; // 默认为成员 (1)
    if (m_groupRoles.count(groupId) && m_groupRoles[groupId].count(username)) {
        currentLevel = m_groupRoles[groupId][username];
    }
    return currentLevel;
}

string GroupManager::getGroupName(int groupId) {
    lock_guard<mutex> lock(m_mutex);
    if (m_groups.count(groupId)) return m_groups[groupId].getName();
    return "Unknown";
}

int GroupManager::getGroupIdByName(string name) {
    lock_guard<mutex> lock(m_mutex);
    for (const auto& pair : m_groups) {
        if (pair.second.getName() == name) {
            return pair.first;
        }
    }
    return -1;
}

vector<string> GroupManager::getGroupMembers(int groupId) {
    lock_guard<mutex> lock(m_mutex);
    if (m_groups.find(groupId) != m_groups.end()) {
        return m_groups[groupId].getMembers();
    }
    return vector<string>();
}

string GroupManager::getGroupListCmd() {
    string cmd = "CMD:GROUP_LIST|";
    lock_guard<mutex> lock(m_mutex);
    for (const auto& pair : m_groups) {
        cmd += to_string(pair.first) + "," + pair.second.getName() + ";";
    }
    return cmd;
}

string GroupManager::getMyGroupListCmd(string username) {
    string cmd = "CMD:GROUP_LIST|";
    lock_guard<mutex> lock(m_mutex);
    for (const auto& pair : m_groups) {
        if (pair.second.hasMember(username)) {
            cmd += to_string(pair.first) + "," + pair.second.getName() + ";";
        }
    }
    return cmd;
}