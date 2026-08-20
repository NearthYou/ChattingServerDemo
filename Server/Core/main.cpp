#include "Server.h"

#include "../Application/IChatService.h"
#include "../Database/DatabaseManager.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>

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

    class DatabaseChatService final : public IChatService
    {
    public:
        bool RegisterUser(const std::string& username, const std::string& password) override
        {
            std::lock_guard<std::mutex> lock(databaseMutex);
            return DatabaseManager::GetInstance().RegisterUser(username, password);
        }

        bool Authenticate(const std::string& username, const std::string& password) override
        {
            std::lock_guard<std::mutex> lock(databaseMutex);
            return DatabaseManager::GetInstance().ValidateUser(username, password);
        }

        bool StoreMessage(const std::string& username, const std::string& message) override
        {
            std::lock_guard<std::mutex> lock(databaseMutex);
            return DatabaseManager::GetInstance().SaveChatMessage(username, message);
        }

    private:
        std::mutex databaseMutex;
    };

    std::uint16_t ReadPort()
    {
        std::cout << "Enter server port (default: 8888): ";
        std::string input;
        std::getline(std::cin, input);
        if (input.empty())
        {
            return 8888;
        }

        try
        {
            const unsigned long parsed = std::stoul(input);
            if (parsed > 0 && parsed <= 65535)
            {
                return static_cast<std::uint16_t>(parsed);
            }
        }
        catch (...)
        {
        }
        return 0;
    }
}

int main()
{
    const std::uint16_t port = ReadPort();
    if (port == 0)
    {
        std::cerr << "Invalid port" << std::endl;
        return 1;
    }

    auto& database = DatabaseManager::GetInstance();
    if (!database.Init())
    {
        std::cerr << "Database initialization failed" << std::endl;
        return 1;
    }

    const std::string connectionString =
        "DRIVER={SQL Server};SERVER=localhost;DATABASE=ChatDB;Trusted_Connection=yes;";
    if (!database.Connect(connectionString))
    {
        std::cerr << "Database connection failed" << std::endl;
        database.Cleanup();
        return 1;
    }

    DatabaseChatService service;
    Server server(service);
    if (!server.Init(port))
    {
        std::cerr << "Server initialization failed" << std::endl;
        database.Cleanup();
        return 1;
    }

    std::cout << "Server is listening on 127.0.0.1:" << server.GetBoundPort() << std::endl;
    activeServer.store(&server);
    if (!SetConsoleCtrlHandler(HandleConsoleControl, TRUE))
    {
        activeServer.store(nullptr);
        std::cerr << "Console control handler registration failed" << std::endl;
        server.Shutdown();
        database.Cleanup();
        return 1;
    }

    std::cout << "Press Ctrl+C to stop the server." << std::endl;
    server.Run();

    activeServer.store(nullptr);
    SetConsoleCtrlHandler(HandleConsoleControl, FALSE);

    server.Shutdown();
    database.Cleanup();
    return 0;
}
