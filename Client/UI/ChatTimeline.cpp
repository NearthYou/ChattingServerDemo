#include "ChatTimeline.h"

#include <cstdio>
#include <ctime>

namespace chat::ui
{
    bool TryGetLocalChatTime(
        std::int64_t timestampMilliseconds,
        LocalChatTime& result)
    {
        if (timestampMilliseconds < 0)
        {
            return false;
        }

        const std::time_t seconds = static_cast<std::time_t>(timestampMilliseconds / 1000);
        std::tm local{};
        if (localtime_s(&local, &seconds) != 0)
        {
            return false;
        }
        result = {
            local.tm_year + 1900,
            local.tm_mon + 1,
            local.tm_mday,
            local.tm_hour,
            local.tm_min
        };
        return true;
    }

    std::string FormatChatClock(const LocalChatTime& value)
    {
        char text[6]{};
        sprintf_s(text, "%02d:%02d", value.hour, value.minute);
        return text;
    }

    std::string FormatChatDate(const LocalChatTime& value)
    {
        char text[11]{};
        sprintf_s(text, "%04d-%02d-%02d", value.year, value.month, value.day);
        return text;
    }

    bool IsSameChatDate(const LocalChatTime& left, const LocalChatTime& right)
    {
        return left.year == right.year &&
            left.month == right.month &&
            left.day == right.day;
    }
}
