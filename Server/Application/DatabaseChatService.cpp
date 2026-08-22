#include "DatabaseChatService.h"

#include "InputValidation.h"
#include "../Security/PasswordHasher.h"

#include <Windows.h>

#include <iostream>
#include <utility>

namespace
{
    ChatServiceStatus MapStatus(DatabaseStatus status)
    {
        switch (status)
        {
        case DatabaseStatus::Succeeded:
            return ChatServiceStatus::Succeeded;
        case DatabaseStatus::Conflict:
            return ChatServiceStatus::Conflict;
        case DatabaseStatus::NotFound:
            return ChatServiceStatus::NotFound;
        case DatabaseStatus::InvalidData:
        case DatabaseStatus::Unavailable:
            return ChatServiceStatus::Unavailable;
        }
        return ChatServiceStatus::Unavailable;
    }
}

DatabaseChatService::DatabaseChatService(DatabaseConfig databaseConfig)
    : config(std::move(databaseConfig))
{
}

ChatServiceStatus DatabaseChatService::Start()
{
    std::string diagnostic;
    DatabaseStatus status = DatabaseStatus::Unavailable;
    try
    {
        status = database.Connect(config, diagnostic);
    }
    catch (...)
    {
        diagnostic = "Database connection failed.";
    }
    if (!config.password.empty())
    {
        SecureZeroMemory(config.password.data(), config.password.size());
        config.password.clear();
    }
    if (status != DatabaseStatus::Succeeded)
    {
        std::cerr << diagnostic << '\n';
    }
    return MapStatus(status);
}

void DatabaseChatService::Stop()
{
    database.Disconnect();
}

ChatServiceStatus DatabaseChatService::RegisterUser(
    const std::string& username,
    const std::string& password)
{
    if (!chat::validation::IsValidUsername(username) ||
        !chat::validation::IsValidPassword(password))
    {
        return ChatServiceStatus::Rejected;
    }

    PasswordRecord credential;
    if (!PasswordHasher::Hash(password, credential))
    {
        return ChatServiceStatus::Unavailable;
    }
    return MapStatus(database.InsertUser(username, credential));
}

LoginResult DatabaseChatService::Login(
    const std::string& username,
    const std::string& password,
    std::size_t historyLimit)
{
    LoginResult result;
    if (!chat::validation::IsValidUsername(username) ||
        !chat::validation::IsValidPassword(password))
    {
        result.status = ChatServiceStatus::Rejected;
        return result;
    }

    const CredentialLookupResult credential = database.LoadCredential(username);
    if (credential.status == DatabaseStatus::NotFound)
    {
        PasswordHasher::DummyVerify(password);
        result.status = ChatServiceStatus::Rejected;
        return result;
    }
    if (credential.status != DatabaseStatus::Succeeded)
    {
        result.status = ChatServiceStatus::Unavailable;
        return result;
    }
    if (!PasswordHasher::Verify(password, credential.credential))
    {
        result.status = ChatServiceStatus::Rejected;
        return result;
    }

    const DatabaseHistoryResult history = database.LoadRecentHistory(historyLimit);
    if (history.status != DatabaseStatus::Succeeded)
    {
        result.status = ChatServiceStatus::Unavailable;
        return result;
    }

    result.status = ChatServiceStatus::Succeeded;
    result.history.reserve(history.messages.size());
    for (const auto& message : history.messages)
    {
        result.history.push_back({
            message.username,
            message.message,
            message.timestampMilliseconds
        });
    }
    return result;
}

ChatServiceStatus DatabaseChatService::StoreMessage(
    const std::string& username,
    const std::string& message)
{
    if (!chat::validation::IsValidUsername(username) ||
        !chat::validation::IsValidMessage(message))
    {
        return ChatServiceStatus::Rejected;
    }
    return MapStatus(database.InsertMessage(username, message));
}
