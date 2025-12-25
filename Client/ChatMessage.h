#pragma once
#include <string>
#include <sstream>
#include <vector>

using namespace std;

class ChatMessage {
public:
    ChatMessage() : m_sessionId(-1), m_type(0) {}

    // 构造函数
    ChatMessage(int sessionId, string senderId, string senderName, string content, string time, int type, string uuid = "", string del = "")
        : m_sessionId(sessionId), m_senderId(senderId), m_senderName(senderName), m_content(content), m_time(time), m_type(type), m_uuid(uuid), m_deletedBy(del) {
    }

    // 序列化：虽然客户端一般不存这个格式，但保持对称性
    string toString() const {
        return m_time + "|" + m_senderId + "|" + m_senderName + "|" + m_content + "|" + m_uuid + "|" + m_deletedBy;
    }

    // 反序列化：解析服务器发来的 6 列数据
    static ChatMessage fromString(const string& line) {
        stringstream ss(line);
        string item;
        vector<string> parts;
        while (getline(ss, item, '|')) {
            parts.push_back(item);
        }

        ChatMessage msg;
        // 兼容处理：至少要有前 4 个字段
        if (parts.size() >= 4) {
            msg.m_time = parts[0];
            msg.m_senderId = parts[1];
            msg.m_senderName = parts[2];
            msg.m_content = parts[3];

            if (parts.size() >= 5) msg.m_uuid = parts[4];
            if (parts.size() >= 6) msg.m_deletedBy = parts[5];
        }
        return msg;
    }

    int m_sessionId; // 运行时使用
    int m_type;      // 运行时使用

    string m_time;
    string m_senderId;
    string m_senderName;
    string m_content;
    string m_uuid;
    string m_deletedBy;
};