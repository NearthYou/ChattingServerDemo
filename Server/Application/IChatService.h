#pragma once

#include <cstddef>
#include <string>
#include <vector>

enum class ChatServiceStatus
{
    Succeeded,
    Conflict,
    NotFound,
    Rejected,
    Unavailable
};

struct ChatHistoryEntry
{
    std::string username;
    std::string message;
};

struct LoginResult
{
    ChatServiceStatus status = ChatServiceStatus::Unavailable;
    std::vector<ChatHistoryEntry> history;
};

class IChatService
{
public:
    virtual ~IChatService() = default;

    virtual ChatServiceStatus Start() = 0;
    virtual void Stop() = 0;
    virtual ChatServiceStatus RegisterUser(const std::string& username, const std::string& password) = 0;
    virtual LoginResult Login(
        const std::string& username,
        const std::string& password,
        std::size_t historyLimit) = 0;
    virtual ChatServiceStatus StoreMessage(const std::string& username, const std::string& message) = 0;
};
