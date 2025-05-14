#include "StringUtils.h"
#include <Windows.h>

namespace StringUtils
{
    std::wstring StringToWString(const std::string& str)
    {
        int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
        std::wstring wstr(len - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], len);
        return wstr;
    }

    std::string WStringToString(const std::wstring& wstr)
    {
        int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
        std::string str(len - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], len, NULL, NULL);
        return str;
    }
}; 