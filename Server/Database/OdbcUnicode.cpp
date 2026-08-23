#include "OdbcUnicode.h"

#include "../../Common/Utils/StringUtils.h"

#include <limits>

bool TryMakeOdbcWideText(const std::string& utf8, bool longValue, OdbcWideText& result)
{
    result = {};
    result.sqlType = longValue ? SQL_WLONGVARCHAR : SQL_WVARCHAR;
    if (!StringUtils::TryStringToWString(utf8, result.value) ||
        result.value.size() > static_cast<std::size_t>((std::numeric_limits<SQLLEN>::max)()) / sizeof(SQLWCHAR))
    {
        result = {};
        return false;
    }
    result.indicatorBytes = static_cast<SQLLEN>(result.value.size() * sizeof(SQLWCHAR));
    return true;
}

bool TryReadOdbcWideText(
    const SQLWCHAR* buffer,
    std::size_t capacityCharacters,
    SQLLEN indicatorBytes,
    std::string& utf8)
{
    utf8.clear();
    if (buffer == nullptr || capacityCharacters == 0 || indicatorBytes < 0 ||
        indicatorBytes % static_cast<SQLLEN>(sizeof(SQLWCHAR)) != 0)
    {
        return false;
    }

    const std::size_t characters = static_cast<std::size_t>(
        indicatorBytes / static_cast<SQLLEN>(sizeof(SQLWCHAR)));
    if (characters >= capacityCharacters)
    {
        return false;
    }

    std::wstring wide;
    wide.reserve(characters);
    for (std::size_t index = 0; index < characters; ++index)
    {
        wide.push_back(static_cast<wchar_t>(buffer[index]));
    }
    return StringUtils::TryWStringToString(wide, utf8);
}
