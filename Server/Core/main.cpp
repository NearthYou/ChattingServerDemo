#include "Server.h"

#include "../Application/DatabaseChatService.h"
#include "../../Common/Utils/StringUtils.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <utility>

namespace
{
    std::atomic<Server*> activeServer{ nullptr };

    BOOL WINAPI HandleConsoleControl(DWORD controlType)
    {
        switch (controlType)
        {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            if (Server* server = activeServer.load())
            {
                server->RequestStop();
                return TRUE;
            }
            return FALSE;
        default:
            return FALSE;
        }
    }

    std::string Environment(const char* name)
    {
        char* value = nullptr;
        std::size_t valueBytes = 0;
        if (_dupenv_s(&value, &valueBytes, name) != 0 || value == nullptr)
        {
            return {};
        }

        std::string result(value);
        SecureZeroMemory(value, valueBytes);
        std::free(value);
        return result;
    }

    bool ParseUnsigned(const std::string& text, unsigned long maximum, unsigned long& value)
    {
        if (text.empty())
        {
            return false;
        }
        try
        {
            std::size_t consumed = 0;
            const unsigned long parsed = std::stoul(text, &consumed);
            if (consumed != text.size() || parsed == 0 || parsed > maximum)
            {
                return false;
            }
            value = parsed;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool ReadPort(int argumentCount, char** arguments, std::uint16_t& port)
    {
        std::string value = Environment("CHAT_SERVER_PORT");
        if (value.empty())
        {
            value = "8888";
        }

        for (int index = 1; index < argumentCount; ++index)
        {
            const std::string argument = arguments[index];
            if (argument == "--port" && index + 1 < argumentCount)
            {
                value = arguments[++index];
            }
            else if (argument.rfind("--port=", 0) == 0)
            {
                value = argument.substr(7);
            }
            else
            {
                return false;
            }
        }

        unsigned long parsed = 0;
        if (!ParseUnsigned(value, 65535, parsed))
        {
            return false;
        }
        port = static_cast<std::uint16_t>(parsed);
        return true;
    }

    bool ReadDatabaseConfig(DatabaseConfig& config)
    {
        config.host = Environment("CHAT_DB_HOST");
        if (config.host.empty())
        {
            config.host = "127.0.0.1";
        }
        config.database = Environment("CHAT_DB_NAME");
        if (config.database.empty())
        {
            config.database = "chatdb";
        }
        config.username = Environment("CHAT_DB_USER");
        config.password = Environment("CHAT_DB_PASSWORD");
        if (config.username.empty() || config.password.empty())
        {
            return false;
        }

        const std::string driver = Environment("CHAT_DB_DRIVER");
        if (!driver.empty() && !StringUtils::TryStringToWString(driver, config.driverOverride))
        {
            return false;
        }

        const std::string port = Environment("CHAT_DB_PORT");
        unsigned long parsed = 3307;
        if (!port.empty() && !ParseUnsigned(port, 65535, parsed))
        {
            return false;
        }
        config.port = static_cast<std::uint16_t>(parsed);

        const std::string loginTimeout = Environment("CHAT_DB_LOGIN_TIMEOUT_SECONDS");
        if (!loginTimeout.empty() &&
            !ParseUnsigned(loginTimeout, (std::numeric_limits<std::uint32_t>::max)(), parsed))
        {
            return false;
        }
        if (!loginTimeout.empty())
        {
            config.loginTimeoutSeconds = static_cast<std::uint32_t>(parsed);
        }

        const std::string statementTimeout = Environment("CHAT_DB_STATEMENT_TIMEOUT_SECONDS");
        if (!statementTimeout.empty() &&
            !ParseUnsigned(statementTimeout, (std::numeric_limits<std::uint32_t>::max)(), parsed))
        {
            return false;
        }
        if (!statementTimeout.empty())
        {
            config.statementTimeoutSeconds = static_cast<std::uint32_t>(parsed);
        }
        return true;
    }
}

int main(int argumentCount, char** arguments)
{
    std::uint16_t port = 0;
    if (!ReadPort(argumentCount, arguments, port))
    {
        std::cerr << "Usage: Server.exe [--port <1-65535>]" << std::endl;
        return 1;
    }

    DatabaseConfig databaseConfig;
    if (!ReadDatabaseConfig(databaseConfig))
    {
        std::cerr << "Set valid CHAT_DB_USER, CHAT_DB_PASSWORD, and optional CHAT_DB_* settings." << std::endl;
        return 1;
    }
    if (_putenv_s("CHAT_DB_PASSWORD", "") != 0)
    {
        if (!databaseConfig.password.empty())
        {
            SecureZeroMemory(databaseConfig.password.data(), databaseConfig.password.size());
            databaseConfig.password.clear();
        }
        std::cerr << "Could not remove CHAT_DB_PASSWORD from the server environment." << std::endl;
        return 1;
    }

    HANDLE externalStopEvent = nullptr;
    const std::string externalStopEventUtf8 = Environment("CHAT_SERVER_STOP_EVENT");
    if (!externalStopEventUtf8.empty())
    {
        std::wstring externalStopEventName;
        if (!StringUtils::TryStringToWString(externalStopEventUtf8, externalStopEventName))
        {
            std::cerr << "CHAT_SERVER_STOP_EVENT is not valid UTF-8." << std::endl;
            return 1;
        }
        externalStopEvent = CreateEventW(nullptr, TRUE, FALSE, externalStopEventName.c_str());
        if (externalStopEvent == nullptr)
        {
            std::cerr << "Could not create the external stop event." << std::endl;
            return 1;
        }
    }

    DatabaseChatService service(std::move(databaseConfig));
    Server server(service);
    if (!server.Init(port))
    {
        std::cerr << "Server initialization failed." << std::endl;
        if (externalStopEvent != nullptr)
        {
            CloseHandle(externalStopEvent);
        }
        return 1;
    }

    std::cout << "Server is listening on 127.0.0.1:" << server.GetBoundPort() << std::endl;
    activeServer.store(&server);
    if (!SetConsoleCtrlHandler(HandleConsoleControl, TRUE))
    {
        activeServer.store(nullptr);
        std::cerr << "Console control handler registration failed." << std::endl;
        server.Shutdown();
        if (externalStopEvent != nullptr)
        {
            CloseHandle(externalStopEvent);
        }
        return 1;
    }

    std::thread externalStopMonitor;
    if (externalStopEvent != nullptr)
    {
        try
        {
            externalStopMonitor = std::thread([&server, externalStopEvent] {
                if (WaitForSingleObject(externalStopEvent, INFINITE) == WAIT_OBJECT_0)
                {
                    server.RequestStop();
                }
            });
        }
        catch (...)
        {
            activeServer.store(nullptr);
            SetConsoleCtrlHandler(HandleConsoleControl, FALSE);
            server.Shutdown();
            CloseHandle(externalStopEvent);
            std::cerr << "Could not create the external stop monitor." << std::endl;
            return 1;
        }
    }
    server.Run();
    if (externalStopEvent != nullptr)
    {
        SetEvent(externalStopEvent);
        externalStopMonitor.join();
        CloseHandle(externalStopEvent);
    }
    activeServer.store(nullptr);
    SetConsoleCtrlHandler(HandleConsoleControl, FALSE);
    server.Shutdown();
    return 0;
}
