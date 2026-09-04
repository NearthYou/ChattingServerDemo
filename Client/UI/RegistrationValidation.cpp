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
            return "닉네임과 비밀번호를 입력해 주세요.";
        }
        if (!chat::protocol::IsValidUtf8(nickname) ||
            !chat::protocol::IsValidUtf8(password))
        {
            return "입력한 내용을 확인해 주세요.";
        }

        const std::size_t nicknameCharacters = Utf8CodePointCount(nickname);
        if (nickname.size() < 3 || nickname.size() > 20 ||
            nicknameCharacters < 3 || nicknameCharacters > 20)
        {
            return "닉네임은 영문 3~20자, 한글 3~6자로 입력해 주세요.";
        }
        for (const unsigned char byte : nickname)
        {
            if (byte <= 0x20 || byte == 0x7f)
            {
                return "닉네임에 공백은 사용할 수 없습니다.";
            }
        }
        if (password.size() < 8 || password.size() > 128)
        {
            return "비밀번호는 영문 8자 또는 한글 3자 이상 입력해 주세요.";
        }
        return nullptr;
    }
}
