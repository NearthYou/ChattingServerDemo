#pragma once

#include <string>

struct OdbcDriverDiscoveryResult
{
    bool found = false;
    std::wstring driver;
    std::string diagnostic;
};

OdbcDriverDiscoveryResult DiscoverMySqlUnicodeDriver(const std::wstring& exactOverride = {});
