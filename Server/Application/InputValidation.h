#pragma once

#include <string>

namespace chat
{
namespace validation
{
    bool IsValidUsername(const std::string& username);
    bool IsValidPassword(const std::string& password);
    bool IsValidMessage(const std::string& message);
}
}
