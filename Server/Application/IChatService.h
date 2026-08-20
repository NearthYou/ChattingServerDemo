#pragma once

#include <string>

class IChatService
{
public:
    virtual ~IChatService() = default;

    virtual bool RegisterUser(const std::string& username, const std::string& password) = 0;
    virtual bool Authenticate(const std::string& username, const std::string& password) = 0;
    virtual bool StoreMessage(const std::string& username, const std::string& message) = 0;
};
