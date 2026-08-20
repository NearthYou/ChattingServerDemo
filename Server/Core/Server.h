#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

class IChatService;

struct ServerOptions
{
    std::size_t acceptPrepostCount = 16;
    std::size_t workerCount = 0;
    std::size_t maxQueuedSendBytes = 4 * 1024 * 1024;
};

struct ServerDiagnostics
{
    std::size_t workerCount = 0;
    std::size_t pendingAccepts = 0;
    std::size_t activeSessions = 0;
    std::uint64_t operationsCreated = 0;
    std::uint64_t operationsRetired = 0;
    std::uint64_t outstandingOperations = 0;
    std::uint64_t sendQueueOverflows = 0;
};

class Server
{
public:
    explicit Server(IChatService& service, ServerOptions options = {});
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool Init(std::uint16_t port = 8888);
    void Run();
    void RequestStop();
    void Shutdown();

    std::uint16_t GetBoundPort() const;
    ServerDiagnostics GetDiagnostics() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};
