#include "DataManager.h"
#include <sstream>

// 构造函数
DataManager::DataManager(string filename, FileType type)
    : m_filename(filename), m_type(type), m_autoSaveEnabled(true), m_maxId(1000)
{
    // 构造时直接根据类型加载数据
    load();
}

// 析构函数
DataManager::~DataManager() {
    if (m_autoSaveEnabled) {
        cout << "[AutoSave] Saving " << m_filename << "..." << endl;
        save();
    }
}

void DataManager::setAutoSave(bool enable) {
    m_autoSaveEnabled = enable;
}

// --- 用户操作 ---
void DataManager::addUser(const User& user) {
    if (m_type != TYPE_USER) {
        cout << "Warning: Trying to add User to a Log file!" << endl;
        return;
    }
    if (user.getId() > m_maxId) {
        m_maxId = user.getId();
    }
    m_users.push_back(user);

    // 【修改点】每次更新数据立即保存
    if (m_autoSaveEnabled) {
        save();
    }
}

vector<User> DataManager::getAllUsers() const {
    return m_users;
}

int DataManager::getNextId() {
    m_maxId++; // 自增
    return m_maxId;
}

// --- 日志操作 ---
void DataManager::addLog(string logInfo) {
    if (m_type != TYPE_LOG) {
        cout << "Warning: Trying to add Log to a User file!" << endl;
        return;
    }
    m_logs.push_back(logInfo);

    // 【修改点】每次更新数据立即保存
    if (m_autoSaveEnabled) {
        save();
    }
}

// ----------------------------------------
// --- 核心保存逻辑 ---
// ----------------------------------------

bool DataManager::save() {
    // 1. 打开文件 (覆盖模式 user 用，追加模式 log 用？这里为了简单统一用覆盖/截断)
    ofstream ofs(m_filename, ios::out | ios::trunc);

    if (!ofs.is_open()) return false;

    // 2. 根据类型写入不同的数据
    if (m_type == TYPE_USER) {
        // --- 写用户模式 ---
        for (const auto& user : m_users) {
            // User::toString 现在已经包含了密码
            ofs << user.toString() << endl;
        }
    }
    else if (m_type == TYPE_LOG) {
        // --- 写日志模式 ---
        for (const auto& log : m_logs) {
            ofs << log << endl;
        }
    }

    ofs.close();
    // cout << ">> Saved data to " << m_filename << endl; // 注释掉避免刷屏
    return true;
}

// ----------------------------------------
// --- 核心读取逻辑 ---
// ----------------------------------------

bool DataManager::load() {
    ifstream ifs(m_filename);
    if (!ifs.is_open()) return true; // 文件不存在

    // 清空内存
    m_users.clear();
    m_logs.clear();

    string line;
    while (getline(ifs, line)) {
        if (line.empty()) continue;

        if (m_type == TYPE_USER) {
            // --- 读用户模式：需要解析 ---
            stringstream ss(line);
            int id;
            string u, p, i;
            // 格式：ID Name Password IP
            // 确保能读到 4 个字段
            if (ss >> id >> u >> p >> i) {
                m_users.push_back(User(id, u, p, i));
            }
            if (id > m_maxId) {
                m_maxId = id;
            }
        }
        else if (m_type == TYPE_LOG) {
            // --- 读日志模式：直接存 ---
            m_logs.push_back(line);
        }
    }

    ifs.close();

    // 打印加载信息
    if (m_type == TYPE_USER) cout << ">> Loaded " << m_users.size() << " users." << endl;
    else cout << ">> Loaded " << m_logs.size() << " log entries." << endl;

    return true;
}