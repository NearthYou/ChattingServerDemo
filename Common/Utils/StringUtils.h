#pragma once
#include <string>

namespace StringUtils
{
    std::wstring StringToWString(const std::string& str);
    std::string WStringToString(const std::wstring& wstr);
}; 