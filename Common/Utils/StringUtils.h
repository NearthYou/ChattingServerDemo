#pragma once
#include <string>

namespace StringUtils
{
    bool TryStringToWString(const std::string& str, std::wstring& result);
    bool TryWStringToString(const std::wstring& wstr, std::string& result);
}
