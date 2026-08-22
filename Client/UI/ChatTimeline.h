#pragma once

#include <cstdint>
#include <string>

namespace chat::ui
{
    struct LocalChatTime
    {
        int year = 0;
        int month = 0;
        int day = 0;
        int hour = 0;
        int minute = 0;
    };

    bool TryGetLocalChatTime(
        std::int64_t timestampMilliseconds,
        LocalChatTime& result);
    std::string FormatChatClock(const LocalChatTime& value);
    std::string FormatChatDate(const LocalChatTime& value);
    bool IsSameChatDate(const LocalChatTime& left, const LocalChatTime& right);
}
