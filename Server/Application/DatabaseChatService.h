#pragma once

#include "IChatService.h"
#include "../Database/DatabaseManager.h"

class DatabaseChatService final : public IChatService
{
public:
    explicit DatabaseChatService(DatabaseConfig config);

    ChatServiceStatus Start() override;
    void Stop() override;
    ChatServiceStatus RegisterUser(const std::string& username, const std::string& password) override;
    LoginResult Login(
        const std::string& username,
        const std::string& password,
        std::size_t historyLimit) override;
    ChatServiceStatus StoreMessage(const std::string& username, const std::string& message) override;

private:
    DatabaseConfig config;
    DatabaseManager database;
};
