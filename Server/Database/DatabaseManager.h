#pragma once

#include "../Security/PasswordHasher.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum class DatabaseStatus
{
    Succeeded,
    Conflict,
    NotFound,
    InvalidData,
    Unavailable
};

struct DatabaseConfig
{
    std::wstring driverOverride;
    std::string host = "127.0.0.1";
    std::uint16_t port = 3307;
    std::string database = "chatdb";
    std::string username;
    std::string password;
    std::uint32_t loginTimeoutSeconds = 5;
    std::uint32_t statementTimeoutSeconds = 5;
};

struct CredentialLookupResult
{
    DatabaseStatus status = DatabaseStatus::Unavailable;
    PasswordRecord credential;
};

struct StoredChatMessage
{
    std::string username;
    std::string message;
    std::int64_t timestampMilliseconds = 0;
};

struct DatabaseHistoryResult
{
    DatabaseStatus status = DatabaseStatus::Unavailable;
    std::vector<StoredChatMessage> messages;
};

class DatabaseManager
{
public:
    DatabaseManager();
    ~DatabaseManager();

    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    DatabaseStatus Connect(const DatabaseConfig& config, std::string& diagnostic);
    void Disconnect();

    DatabaseStatus InsertUser(const std::string& username, const PasswordRecord& credential);
    CredentialLookupResult LoadCredential(const std::string& username);
    DatabaseStatus InsertMessage(const std::string& username, const std::string& message);
    DatabaseHistoryResult LoadRecentHistory(std::size_t limit);

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};
