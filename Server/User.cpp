#include "User.h"

// 构造函数实现
User::User(int id, string uName, string pwd, string ip)
    : m_id(id), m_username(uName), m_password(pwd), m_serverIP(ip)
{
}

// 转换为字符串实现
string User::toString() const {
    // 格式变更为：ID 账号 密码 IP
    // 例如：1001 Admin 123456 127.0.0.1
    return to_string(m_id) + " " + m_username + " " + m_password + " " + m_serverIP;
}