#pragma once

#include <Windows.h>
#include <sql.h>
#include <sqlext.h>

#include <cstddef>
#include <string>

struct OdbcWideText
{
    std::wstring value;
    SQLLEN indicatorBytes = 0;
    SQLSMALLINT cType = SQL_C_WCHAR;
    SQLSMALLINT sqlType = SQL_WVARCHAR;
};

bool TryMakeOdbcWideText(const std::string& utf8, bool longValue, OdbcWideText& result);
bool TryReadOdbcWideText(
    const SQLWCHAR* buffer,
    std::size_t capacityCharacters,
    SQLLEN indicatorBytes,
    std::string& utf8);
