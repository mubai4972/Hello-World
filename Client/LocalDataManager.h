#pragma once
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include "ChatMessage.h"

using namespace std;

class LocalDataManager {
public:
    LocalDataManager() = default;

    // 【核心修复】带去重功能的保存
    // 只有当文件中不存在该消息时，才执行追加
    static void saveMessage(const QString& filePath, const ChatMessage& msg) {
        // 1. 先检查是否存在
        QFile fileRead(filePath);
        QString newMsgStr = QString::fromStdString(msg.toString());
        bool exists = false;

        if (fileRead.exists() && fileRead.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&fileRead);
            // 逐行比对，防止重复
            // (注意：对于超大聊天记录，这里可以优化为只读最后N行，但当前规模全读更安全)
            while (!in.atEnd()) {
                QString line = in.readLine();
                if (line.trimmed() == newMsgStr.trimmed()) {
                    exists = true;
                    break;
                }
            }
            fileRead.close();
        }

        // 2. 如果存在，直接返回，不再保存
        if (exists) {
            return;
        }

        // 3. 不存在则追加写入
        QFile fileWrite(filePath);
        if (fileWrite.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&fileWrite);
            out << newMsgStr << "\n";
            fileWrite.close();
        }
    }

    static vector<ChatMessage> loadHistory(const QString& filePath, int limit = 50) {
        vector<ChatMessage> history;
        QFile file(filePath);
        if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return history;
        }

        QTextStream in(&file);

        vector<string> allLines;
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (!line.trimmed().isEmpty()) {
                allLines.push_back(line.toStdString());
            }
        }
        file.close();

        int start = 0;
        if (allLines.size() > limit) {
            start = allLines.size() - limit;
        }

        for (int i = start; i < allLines.size(); ++i) {
            history.push_back(ChatMessage::fromString(allLines[i]));
        }

        return history;
    }
};