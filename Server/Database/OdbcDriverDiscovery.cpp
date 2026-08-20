#include "OdbcDriverDiscovery.h"

#include <Windows.h>
#include <sql.h>
#include <sqlext.h>

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace
{
    std::wstring Upper(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
            return static_cast<wchar_t>(std::towupper(character));
        });
        return value;
    }

    std::vector<unsigned long> VersionParts(const std::wstring& value)
    {
        std::vector<unsigned long> parts;
        unsigned long current = 0;
        bool inNumber = false;
        for (const wchar_t character : value)
        {
            if (character >= L'0' && character <= L'9')
            {
                current = current * 10 + static_cast<unsigned long>(character - L'0');
                inNumber = true;
            }
            else if (inNumber)
            {
                parts.push_back(current);
                current = 0;
                inNumber = false;
            }
        }
        if (inNumber)
        {
            parts.push_back(current);
        }
        return parts;
    }
}

OdbcDriverDiscoveryResult DiscoverMySqlUnicodeDriver(const std::wstring& exactOverride)
{
    OdbcDriverDiscoveryResult result;
    SQLHENV environment = SQL_NULL_HENV;
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &environment)))
    {
        result.diagnostic = "Could not enumerate ODBC drivers.";
        return result;
    }

    if (!SQL_SUCCEEDED(SQLSetEnvAttr(
            environment,
            SQL_ATTR_ODBC_VERSION,
            reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3),
            0)))
    {
        SQLFreeHandle(SQL_HANDLE_ENV, environment);
        result.diagnostic = "Could not enumerate ODBC drivers.";
        return result;
    }

    std::vector<std::wstring> candidates;
    SQLWCHAR description[512]{};
    SQLWCHAR attributes[2048]{};
    SQLSMALLINT descriptionLength = 0;
    SQLSMALLINT attributesLength = 0;
    SQLUSMALLINT direction = SQL_FETCH_FIRST;
    for (;;)
    {
        const SQLRETURN status = SQLDriversW(
            environment,
            direction,
            description,
            static_cast<SQLSMALLINT>(std::size(description)),
            &descriptionLength,
            attributes,
            static_cast<SQLSMALLINT>(std::size(attributes)),
            &attributesLength);
        if (!SQL_SUCCEEDED(status))
        {
            break;
        }
        direction = SQL_FETCH_NEXT;

        const std::wstring driver(
            reinterpret_cast<const wchar_t*>(description),
            static_cast<std::size_t>(descriptionLength));
        if (!exactOverride.empty())
        {
            if (driver == exactOverride)
            {
                candidates.push_back(driver);
            }
        }
        else
        {
            const std::wstring upper = Upper(driver);
            if (upper.find(L"MYSQL ODBC") != std::wstring::npos &&
                upper.find(L"UNICODE") != std::wstring::npos)
            {
                candidates.push_back(driver);
            }
        }
    }
    SQLFreeHandle(SQL_HANDLE_ENV, environment);

    if (candidates.empty())
    {
        result.diagnostic =
            "Install the x64 MySQL Connector/ODBC Unicode driver, or set CHAT_DB_DRIVER to its exact registered name.";
        return result;
    }

    result.driver = *std::max_element(
        candidates.begin(),
        candidates.end(),
        [](const std::wstring& left, const std::wstring& right) {
            return VersionParts(left) < VersionParts(right);
        });
    result.found = true;
    return result;
}
