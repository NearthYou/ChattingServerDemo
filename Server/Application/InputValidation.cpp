#include "InputValidation.h"

#include "../../Common/Protocol/ChatProtocol.h"

#include <cstdint>

namespace
{
    std::size_t CountUtf8CodePoints(const std::string& value)
    {
        std::size_t count = 0;
        for (const unsigned char byte : value)
        {
            if ((byte & 0xc0) != 0x80)
            {
                ++count;
            }
        }
        return count;
    }
}

namespace chat
{
namespace validation
{
    bool IsValidUsername(const std::string& username)
    {
        if (username.size() < 3 || username.size() > 20 ||
            !protocol::IsValidUtf8(username))
        {
            return false;
        }

        const std::size_t codePoints = CountUtf8CodePoints(username);
        if (codePoints < 3 || codePoints > 20)
        {
            return false;
        }

        for (const unsigned char byte : username)
        {
            if (byte <= 0x20 || byte == 0x7f)
            {
                return false;
            }
        }
        return true;
    }

    bool IsValidPassword(const std::string& password)
    {
        return password.size() >= 8 && password.size() <= 128 &&
            protocol::IsValidUtf8(password);
    }

    bool IsValidMessage(const std::string& message)
    {
        return !message.empty() && message.size() <= 1000 &&
            protocol::IsValidUtf8(message);
    }
}
}
