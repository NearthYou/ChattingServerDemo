#pragma once

#include <Windows.h>
#include <sql.h>
#include <sqlext.h>
#include <string>
#include <vector>
#include <utility>
#include "../../Common/Utils/StringUtils.h"

class DatabaseManager
{
public:
    static DatabaseManager& GetInstance();

    bool Init();
    bool Connect(const std::string& connectionString);
    void Cleanup();

    bool RegisterUser(const std::string& username, const std::string& password);
    bool ValidateUser(const std::string& username, const std::string& password);
    bool SaveChatMessage(const std::string& username, const std::string& message);
    std::vector<std::pair<std::string, std::string>> GetChatHistory(int limit);

private:
    DatabaseManager();
    ~DatabaseManager();

    void HandleError(SQLHANDLE handle, SQLSMALLINT type);

    SQLHENV hEnv;
    SQLHDBC hDbc;
    bool isConnected;
}; 