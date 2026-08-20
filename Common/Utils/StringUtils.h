#pragma once
#include <string>

namespace StringUtils
{
    bool TryStringToWString(const std::string& str, std::wstring& result);
    bool TryWStringToString(const std::wstring& wstr, std::string& result);
    std::wstring StringToWString(const std::string& str);
    std::string WStringToString(const std::wstring& wstr);
}
