#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include "User.h"

using namespace std;

// 定义文件模式：是存用户数据，还是存普通日志？
enum FileType {
    TYPE_USER,  // 只有 User 对象
    TYPE_LOG    // 只有 纯文本日志
};

class DataManager {
public:
    // 构造函数：必须指定 文件名 和 文件类型
    // 比如：DataManager("Users.txt", TYPE_USER);
    // 比如：DataManager("Log.txt", TYPE_LOG);
    DataManager(string filename, FileType type);
    void testsave() { save(); };
    ~DataManager();
    int getNextId();
    // --- 通用设置 ---
    void setAutoSave(bool enable);

    // --- 用户专用操作 (TYPE_USER 时使用) ---
    void addUser(const User& user);
    vector<User> getAllUsers() const;

    // --- 日志专用操作 (TYPE_LOG 时使用) ---
    void addLog(string logInfo);
    // 这里可以加一个 getLogs() ...

    // --- 核心文件操作 ---
    // 这两个函数内部会自动判断 m_type
    bool save();
    bool load();

private:
    string m_filename;
    FileType m_type;       // 记住当前实例是管理哪种文件的
    bool m_autoSaveEnabled;
    int m_maxId = 1000;
    // 数据存储区
    vector<User> m_users;  // 专门存用户
    vector<string> m_logs; // 专门存日志
};