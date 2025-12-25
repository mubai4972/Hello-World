#pragma once

#include <string>
#include <iostream>

// 为了方便，我们在头文件中使用了命名空间
using namespace std;

class User {
public:
    // 默认构造函数
    User() = default;
    int getId() const { return m_id; }
    // 带参数的构造函数
    User(int id,string uName, string pwd, string ip);

    // Getters
    string getUsername() const { return m_username; }
    string getPassword() const { return m_password; }
    string getServerIP() const { return m_serverIP; }

    // 序列化：将对象转为字符串
    string toString() const;

private:
    int m_id;
    string m_username;
    string m_password;
    string m_serverIP;
};