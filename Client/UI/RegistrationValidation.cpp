#include "RegistrationValidation.h"

#include "../../Common/Protocol/ChatProtocol.h"

namespace
{
    std::size_t Utf8CodePointCount(const std::string& value)
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

namespace chat::ui
{
    const char* RegistrationValidationMessage(
        const std::string& nickname,
        const std::string& password)
    {
        if (nickname.empty() || password.empty())
        {
            return "Nickname and password are required.";
        }
        if (!chat::protocol::IsValidUtf8(nickname) ||
            !chat::protocol::IsValidUtf8(password))
        {
            return "Nickname or password contains invalid text.";
        }

        const std::size_t nicknameCharacters = Utf8CodePointCount(nickname);
        if (nickname.size() < 3 || nickname.size() > 20 ||
            nicknameCharacters < 3 || nicknameCharacters > 20)
        {
            return "Nickname must be 3-20 bytes. Korean nicknames can be 3-6 characters.";
        }
        for (const unsigned char byte : nickname)
        {
            if (byte <= 0x20 || byte == 0x7f)
            {
                return "Nickname cannot contain spaces or control characters.";
            }
        }
        if (password.size() < 8 || password.size() > 128)
        {
            return "Password must be 8-128 bytes. Three Korean characters meet the minimum.";
        }
        return nullptr;
    }
}
