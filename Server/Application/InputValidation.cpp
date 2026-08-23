#include "InputValidation.h"

#include <cstdint>

namespace
{
    bool IsDeniedFormatControl(std::uint32_t codePoint)
    {
        return codePoint == 0x00ad ||
            (codePoint >= 0x0600 && codePoint <= 0x0605) ||
            codePoint == 0x061c || codePoint == 0x06dd || codePoint == 0x070f ||
            (codePoint >= 0x0890 && codePoint <= 0x0891) || codePoint == 0x08e2 ||
            codePoint == 0x180e ||
            (codePoint >= 0x200b && codePoint <= 0x200f) ||
            (codePoint >= 0x202a && codePoint <= 0x202e) ||
            (codePoint >= 0x2060 && codePoint <= 0x2064) ||
            (codePoint >= 0x2066 && codePoint <= 0x206f) ||
            codePoint == 0xfeff ||
            (codePoint >= 0xfff9 && codePoint <= 0xfffb) ||
            codePoint == 0x110bd || codePoint == 0x110cd ||
            (codePoint >= 0x13430 && codePoint <= 0x1343f) ||
            (codePoint >= 0x1bca0 && codePoint <= 0x1bca3) ||
            (codePoint >= 0x1d173 && codePoint <= 0x1d17a) ||
            codePoint == 0xe0001 ||
            (codePoint >= 0xe0020 && codePoint <= 0xe007f);
    }

    bool DecodeUtf8(
        const std::string& value,
        bool rejectFormatControls,
        std::size_t& codePointCount)
    {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(value.data());
        std::size_t index = 0;
        codePointCount = 0;
        while (index < value.size())
        {
            const std::uint8_t first = bytes[index];
            std::uint32_t codePoint = 0;
            std::size_t length = 0;
            if (first <= 0x7f)
            {
                codePoint = first;
                length = 1;
            }
            else if (first >= 0xc2 && first <= 0xdf)
            {
                codePoint = first & 0x1f;
                length = 2;
            }
            else if (first >= 0xe0 && first <= 0xef)
            {
                codePoint = first & 0x0f;
                length = 3;
            }
            else if (first >= 0xf0 && first <= 0xf4)
            {
                codePoint = first & 0x07;
                length = 4;
            }
            else
            {
                return false;
            }

            if (length > value.size() - index)
            {
                return false;
            }
            for (std::size_t continuation = 1; continuation < length; ++continuation)
            {
                const std::uint8_t byte = bytes[index + continuation];
                if (byte < 0x80 || byte > 0xbf)
                {
                    return false;
                }
                codePoint = (codePoint << 6) | (byte & 0x3f);
            }

            const bool overlong =
                (length == 2 && codePoint < 0x80) ||
                (length == 3 && codePoint < 0x800) ||
                (length == 4 && codePoint < 0x10000);
            if (overlong ||
                (codePoint >= 0xd800 && codePoint <= 0xdfff) ||
                codePoint > 0x10ffff ||
                (rejectFormatControls && IsDeniedFormatControl(codePoint)))
            {
                return false;
            }

            index += length;
            ++codePointCount;
        }
        return true;
    }
}

namespace chat
{
namespace validation
{
    bool IsValidUsername(const std::string& username)
    {
        if (username.size() < 3 || username.size() > 20)
        {
            return false;
        }

        std::size_t codePoints = 0;
        if (!DecodeUtf8(username, true, codePoints))
        {
            return false;
        }
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
        std::size_t ignoredCodePoints = 0;
        return password.size() >= 8 && password.size() <= 128 &&
            DecodeUtf8(password, false, ignoredCodePoints);
    }

    bool IsValidMessage(const std::string& message)
    {
        std::size_t ignoredCodePoints = 0;
        return !message.empty() && message.size() <= 1000 &&
            DecodeUtf8(message, false, ignoredCodePoints);
    }
}
}
