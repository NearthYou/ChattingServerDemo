#include "StringUtils.h"
#include <Windows.h>

#include <limits>

namespace StringUtils
{
    bool TryStringToWString(const std::string& str, std::wstring& result)
    {
        result.clear();
        if (str.empty())
        {
            return true;
        }
        if (str.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        {
            return false;
        }

        const int inputBytes = static_cast<int>(str.size());
        const int outputCharacters = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            str.data(),
            inputBytes,
            nullptr,
            0);
        if (outputCharacters <= 0)
        {
            return false;
        }

        result.resize(static_cast<std::size_t>(outputCharacters));
        if (MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                str.data(),
                inputBytes,
                result.data(),
                outputCharacters) != outputCharacters)
        {
            result.clear();
            return false;
        }
        return true;
    }

    bool TryWStringToString(const std::wstring& wstr, std::string& result)
    {
        result.clear();
        if (wstr.empty())
        {
            return true;
        }
        if (wstr.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        {
            return false;
        }

        const int inputCharacters = static_cast<int>(wstr.size());
        const int outputBytes = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            wstr.data(),
            inputCharacters,
            nullptr,
            0,
            nullptr,
            nullptr);
        if (outputBytes <= 0)
        {
            return false;
        }

        result.resize(static_cast<std::size_t>(outputBytes));
        if (WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                wstr.data(),
                inputCharacters,
                result.data(),
                outputBytes,
                nullptr,
                nullptr) != outputBytes)
        {
            result.clear();
            return false;
        }
        return true;
    }

    std::wstring StringToWString(const std::string& str)
    {
        std::wstring result;
        TryStringToWString(str, result);
        return result;
    }

    std::string WStringToString(const std::wstring& wstr)
    {
        std::string result;
        TryWStringToString(wstr, result);
        return result;
    }
}
