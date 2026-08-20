#include "../Server/Application/IChatService.h"
#include "../Server/Core/Server.h"
#include "../Server/Network/Session.h"
#include "../Common/Protocol/ChatProtocol.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    using namespace std::chrono_literals;
    using chat::protocol::CodecError;
    using chat::protocol::Message;
    using chat::protocol::MessageType;

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    template <typename Predicate>
    void WaitUntil(Predicate predicate, std::chrono::milliseconds timeout, const std::string& message)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate())
            {
                return;
            }
            std::this_thread::sleep_for(5ms);
        }
        Require(predicate(), message);
    }

    class FakeChatService final : public IChatService
    {
    public:
        void AddUser(const std::string& username, const std::string& password)
        {
            std::lock_guard<std::mutex> lock(mutex);
            users[username] = password;
        }

        bool RegisterUser(const std::string& username, const std::string& password) override
        {
            std::lock_guard<std::mutex> lock(mutex);
            return users.emplace(username, password).second;
        }

        bool Authenticate(const std::string& username, const std::string& password) override
        {
            std::unique_lock<std::mutex> lock(mutex);
            if (blockNextAuthentication)
            {
                blockNextAuthentication = false;
                authenticationEntered = true;
                authenticationCondition.notify_all();
                authenticationCondition.wait(lock, [this] { return authenticationReleased; });
            }

            const auto found = users.find(username);
            return found != users.end() && found->second == password;
        }

        bool StoreMessage(const std::string& username, const std::string& message) override
        {
            std::lock_guard<std::mutex> lock(mutex);
            storedMessages.emplace_back(username, message);
            return true;
        }

        void BlockNextAuthentication()
        {
            std::lock_guard<std::mutex> lock(mutex);
            blockNextAuthentication = true;
            authenticationReleased = false;
            authenticationEntered = false;
        }

        void WaitForAuthenticationEntry()
        {
            std::unique_lock<std::mutex> lock(mutex);
            const bool entered = authenticationCondition.wait_for(
                lock,
                2s,
                [this] { return authenticationEntered; });
            Require(entered, "authentication call did not block");
        }

        void ReleaseAuthentication()
        {
            std::lock_guard<std::mutex> lock(mutex);
            authenticationReleased = true;
            authenticationCondition.notify_all();
        }

        std::vector<std::pair<std::string, std::string>> Messages() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return storedMessages;
        }

    private:
        mutable std::mutex mutex;
        std::condition_variable authenticationCondition;
        std::unordered_map<std::string, std::string> users;
        std::vector<std::pair<std::string, std::string>> storedMessages;
        bool blockNextAuthentication = false;
        bool authenticationEntered = false;
        bool authenticationReleased = false;
    };

    std::vector<std::uint8_t> Encode(const Message& message)
    {
        auto encoded = chat::protocol::EncodeMessage(message);
        Require(encoded.error == CodecError::None, "test message did not encode");
        return encoded.bytes;
    }

    void SendAll(SOCKET socket, const std::uint8_t* bytes, std::size_t size)
    {
        std::size_t offset = 0;
        while (offset < size)
        {
            const int sent = send(
                socket,
                reinterpret_cast<const char*>(bytes + offset),
                static_cast<int>(std::min<std::size_t>(size - offset, 64 * 1024)),
                0);
            Require(sent > 0, "send failed");
            offset += static_cast<std::size_t>(sent);
        }
    }

    void SendAll(SOCKET socket, const std::vector<std::uint8_t>& bytes)
    {
        SendAll(socket, bytes.data(), bytes.size());
    }

    std::vector<Message> ReceiveMessages(SOCKET socket, std::size_t count, DWORD timeoutMs = 10000)
    {
        setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

        chat::protocol::StreamingDecoder decoder;
        std::vector<Message> messages;
        std::vector<std::uint8_t> bytes(64 * 1024);
        while (messages.size() < count)
        {
            const int received = recv(
                socket,
                reinterpret_cast<char*>(bytes.data()),
                static_cast<int>(bytes.size()),
                0);
            Require(received > 0, "connection closed before expected messages arrived");

            auto decoded = decoder.Push(bytes.data(), static_cast<std::size_t>(received));
            Require(decoded.error == CodecError::None, "server produced an invalid frame");
            messages.insert(
                messages.end(),
                std::make_move_iterator(decoded.messages.begin()),
                std::make_move_iterator(decoded.messages.end()));
        }
        return messages;
    }

    SOCKET Connect(std::uint16_t port)
    {
        SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        Require(socketHandle != INVALID_SOCKET, "client socket creation failed");

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);
        if (connect(socketHandle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
        {
            closesocket(socketHandle);
            throw std::runtime_error("loopback connect failed");
        }
        return socketHandle;
    }

    void CloseSocket(SOCKET& socketHandle)
    {
        if (socketHandle != INVALID_SOCKET)
        {
            shutdown(socketHandle, SD_BOTH);
            closesocket(socketHandle);
            socketHandle = INVALID_SOCKET;
        }
    }

    void Login(SOCKET socketHandle, const std::string& username, std::uint32_t requestId)
    {
        SendAll(socketHandle, Encode({ MessageType::LoginRequest, requestId, { username, "pw" } }));
        const auto replies = ReceiveMessages(socketHandle, 1);
        Require(replies[0].type == MessageType::LoginSucceeded, "login failed");
        Require(replies[0].requestId == requestId, "login request id was not preserved");
    }

    void RequireDrained(const ServerDiagnostics& diagnostics)
    {
        Require(diagnostics.outstandingOperations == 0, "IO operation remained outstanding");
        Require(diagnostics.operationsCreated == diagnostics.operationsRetired, "IO operation was not retired exactly once");
        Require(diagnostics.activeSessions == 0, "session remained active after shutdown");
        Require(diagnostics.pendingAccepts == 0, "AcceptEx remained pending after shutdown");
    }

    void TestPrepostedAcceptPoolAndShutdownCounters()
    {
        FakeChatService service;
        ServerOptions options;
        options.acceptPrepostCount = 6;
        options.workerCount = 2;
        Server server(service, options);
        Require(server.Init(0), "server init failed");

        auto diagnostics = server.GetDiagnostics();
        Require(diagnostics.workerCount == 2, "configured worker count was not used");
        Require(diagnostics.pendingAccepts == 6, "AcceptEx pool was not preposted");

        std::vector<SOCKET> clients;
        for (int index = 0; index < 3; ++index)
        {
            clients.push_back(Connect(server.GetBoundPort()));
        }
        WaitUntil(
            [&server] {
                const auto state = server.GetDiagnostics();
                return state.activeSessions == 3 && state.pendingAccepts == 6;
            },
            2s,
            "accepted sockets were not associated and replenished");

        server.Shutdown();
        RequireDrained(server.GetDiagnostics());
        for (auto& client : clients)
        {
            CloseSocket(client);
        }
    }

    void TestFragmentedAndCoalescedFramesPreserveIdentityAndRequestIds()
    {
        FakeChatService service;
        service.AddUser("alice", "pw");
        service.AddUser("bob", "pw");
        Server server(service);
        Require(server.Init(0), "server init failed");

        SOCKET peer = Connect(server.GetBoundPort());
        SOCKET origin = Connect(server.GetBoundPort());
        Login(peer, "bob", 10);

        auto login = Encode({ MessageType::LoginRequest, 20, { "alice", "pw" } });
        auto chat = Encode({ MessageType::ChatSend, 21, { "hello" } });
        std::vector<std::uint8_t> coalesced = login;
        coalesced.insert(coalesced.end(), chat.begin(), chat.end());
        SendAll(origin, coalesced.data(), 5);
        SendAll(origin, coalesced.data() + 5, coalesced.size() - 5);

        const auto originMessages = ReceiveMessages(origin, 2);
        Require(originMessages[0].type == MessageType::LoginSucceeded, "fragmented login was not decoded");
        Require(originMessages[0].requestId == 20, "login response lost request id");
        Require(originMessages[1].type == MessageType::ChatDelivered, "origin did not receive delivered chat");
        Require(originMessages[1].requestId == 21, "origin chat response lost request id");
        Require(originMessages[1].fields == std::vector<std::string>({ "alice", "hello" }), "origin chat identity was not server-derived");

        const auto peerMessages = ReceiveMessages(peer, 1);
        Require(peerMessages[0].type == MessageType::ChatDelivered, "peer did not receive delivered chat");
        Require(peerMessages[0].requestId == 0, "peer broadcast request id was not zero");
        Require(peerMessages[0].fields == std::vector<std::string>({ "alice", "hello" }), "peer chat identity was not server-derived");

        const auto stored = service.Messages();
        Require(stored == std::vector<std::pair<std::string, std::string>>({ { "alice", "hello" } }), "service saw an untrusted sender");

        CloseSocket(origin);
        CloseSocket(peer);
        server.Shutdown();
        RequireDrained(server.GetDiagnostics());
    }

    std::vector<std::uint8_t> MakeSpoofedChatFrame()
    {
        std::vector<std::uint8_t> bytes;
        const auto append16 = [&bytes](std::uint16_t value) {
            bytes.push_back(static_cast<std::uint8_t>(value >> 8));
            bytes.push_back(static_cast<std::uint8_t>(value));
        };
        const auto append32 = [&bytes](std::uint32_t value) {
            bytes.push_back(static_cast<std::uint8_t>(value >> 24));
            bytes.push_back(static_cast<std::uint8_t>(value >> 16));
            bytes.push_back(static_cast<std::uint8_t>(value >> 8));
            bytes.push_back(static_cast<std::uint8_t>(value));
        };
        const std::string sender = "mallory";
        const std::string message = "spoof";
        append32(static_cast<std::uint32_t>(8 + sender.size() + message.size()));
        append16(chat::protocol::kProtocolVersion);
        append16(static_cast<std::uint16_t>(MessageType::ChatSend));
        append32(99);
        append32(static_cast<std::uint32_t>(sender.size()));
        bytes.insert(bytes.end(), sender.begin(), sender.end());
        append32(static_cast<std::uint32_t>(message.size()));
        bytes.insert(bytes.end(), message.begin(), message.end());
        return bytes;
    }

    void RequireConnectionClosed(SOCKET socketHandle)
    {
        DWORD timeoutMs = 2000;
        setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
        char byte = 0;
        const int result = recv(socketHandle, &byte, 1, 0);
        Require(result == 0 || (result == SOCKET_ERROR && WSAGetLastError() != WSAETIMEDOUT), "protocol failure did not close the session");
    }

    void TestUnauthenticatedAndSpoofedChatAreRejected()
    {
        FakeChatService service;
        service.AddUser("alice", "pw");
        Server server(service);
        Require(server.Init(0), "server init failed");

        SOCKET unauthenticated = Connect(server.GetBoundPort());
        SendAll(unauthenticated, Encode({ MessageType::ChatSend, 1, { "no-auth" } }));
        RequireConnectionClosed(unauthenticated);
        CloseSocket(unauthenticated);

        SOCKET spoofing = Connect(server.GetBoundPort());
        Login(spoofing, "alice", 2);
        SendAll(spoofing, MakeSpoofedChatFrame());
        RequireConnectionClosed(spoofing);
        CloseSocket(spoofing);

        Require(service.Messages().empty(), "rejected chat reached storage");
        server.Shutdown();
        RequireDrained(server.GetDiagnostics());
    }

    void TestDisconnectDuringReceiveBlockedAuthenticationAndQueuedSend()
    {
        FakeChatService service;
        service.AddUser("alice", "pw");
        service.BlockNextAuthentication();
        ServerOptions options;
        options.workerCount = 1;
        options.maxQueuedSendBytes = 512;
        Server server(service, options);
        Require(server.Init(0), "server init failed");

        SOCKET receiving = Connect(server.GetBoundPort());
        CloseSocket(receiving);

        SOCKET blocked = Connect(server.GetBoundPort());
        SendAll(blocked, Encode({ MessageType::LoginRequest, 3, { "alice", "pw" } }));
        service.WaitForAuthenticationEntry();
        CloseSocket(blocked);
        service.ReleaseAuthentication();

        SOCKET queued = Connect(server.GetBoundPort());
        Login(queued, "alice", 4);
        const std::string queuedMessage(256, 'x');
        std::vector<std::uint8_t> coalesced;
        for (std::uint32_t requestId = 5; requestId < 15; ++requestId)
        {
            auto frame = Encode({ MessageType::ChatSend, requestId, { queuedMessage } });
            coalesced.insert(coalesced.end(), frame.begin(), frame.end());
        }
        SendAll(queued, coalesced);

        WaitUntil(
            [&server] { return server.GetDiagnostics().sendQueueOverflows > 0; },
            3s,
            "bounded send queue did not reject excess data");
        CloseSocket(queued);

        server.Shutdown();
        const auto diagnostics = server.GetDiagnostics();
        Require(diagnostics.sendQueueOverflows > 0, "queued-send pressure was not observed");
        RequireDrained(diagnostics);
    }

    void TestPurePartialSendOffset()
    {
        std::size_t offset = 3;
        Require(Session::AdvanceSendOffset(10, 4, offset), "valid partial send was rejected");
        Require(offset == 7, "partial send offset was not advanced");
        Require(Session::AdvanceSendOffset(10, 3, offset), "final send was rejected");
        Require(offset == 10, "final send offset was not exact");
        Require(!Session::AdvanceSendOffset(10, 1, offset), "overflowing send completion was accepted");
        Require(offset == 10, "invalid completion changed send offset");
    }

    void TestRequestStopWakesRunAndShutdownStaysIdempotent()
    {
        FakeChatService service;
        Server server(service);
        Require(server.Init(0), "server init failed");
        SOCKET client = Connect(server.GetBoundPort());

        std::atomic<bool> runReturned{ false };
        std::thread runner([&] {
            server.Run();
            runReturned.store(true);
        });

        server.RequestStop();
        const auto deadline = std::chrono::steady_clock::now() + 1s;
        while (!runReturned.load() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(5ms);
        }
        const bool returnedBeforeDeadline = runReturned.load();
        if (!returnedBeforeDeadline)
        {
            server.Shutdown();
        }
        runner.join();
        Require(returnedBeforeDeadline, "RequestStop did not wake Run under deadline");

        server.RequestStop();
        server.RequestStop();
        server.Shutdown();
        server.Shutdown();
        server.RequestStop();
        RequireDrained(server.GetDiagnostics());
        CloseSocket(client);
    }

    void TestShutdownDeadlineAndStressAccounting()
    {
        constexpr int clientCount = 100;
        constexpr int messagesPerClient = 10;
        constexpr int deliveriesPerClient = clientCount * messagesPerClient;

        FakeChatService service;
        for (int index = 0; index < clientCount; ++index)
        {
            service.AddUser("user" + std::to_string(index), "pw");
        }

        ServerOptions options;
        options.acceptPrepostCount = 32;
        options.workerCount = 8;
        options.maxQueuedSendBytes = 16 * 1024 * 1024;
        Server server(service, options);
        Require(server.Init(0), "server init failed");

        std::vector<SOCKET> clients;
        clients.reserve(clientCount);
        for (int index = 0; index < clientCount; ++index)
        {
            clients.push_back(Connect(server.GetBoundPort()));
            Login(clients.back(), "user" + std::to_string(index), static_cast<std::uint32_t>(1000 + index));
        }

        std::vector<int> receivedCounts(clientCount, 0);
        std::vector<std::thread> readers;
        readers.reserve(clientCount);
        for (int index = 0; index < clientCount; ++index)
        {
            readers.emplace_back([&, index] {
                try
                {
                    const auto messages = ReceiveMessages(clients[index], deliveriesPerClient, 30000);
                    const bool allDelivered = std::all_of(
                        messages.begin(),
                        messages.end(),
                        [](const Message& message) { return message.type == MessageType::ChatDelivered; });
                    receivedCounts[index] = allDelivered ? static_cast<int>(messages.size()) : -1;
                }
                catch (...)
                {
                    receivedCounts[index] = -1;
                }
            });
        }

        for (int clientIndex = 0; clientIndex < clientCount; ++clientIndex)
        {
            for (int messageIndex = 0; messageIndex < messagesPerClient; ++messageIndex)
            {
                SendAll(
                    clients[clientIndex],
                    Encode({
                        MessageType::ChatSend,
                        static_cast<std::uint32_t>(2000 + clientIndex * messagesPerClient + messageIndex),
                        { "message-" + std::to_string(clientIndex) + "-" + std::to_string(messageIndex) }
                    }));
            }
        }

        for (auto& reader : readers)
        {
            reader.join();
        }

        const auto exactClients = std::count(receivedCounts.begin(), receivedCounts.end(), deliveriesPerClient);
        Require(exactClients == clientCount, "100-client delivery accounting was not exact");
        Require(service.Messages().size() == static_cast<std::size_t>(deliveriesPerClient), "storage accounting was not exact");

        const auto shutdownStarted = std::chrono::steady_clock::now();
        server.Shutdown();
        const auto shutdownDuration = std::chrono::steady_clock::now() - shutdownStarted;
        Require(shutdownDuration < 3s, "server shutdown exceeded deadline");
        RequireDrained(server.GetDiagnostics());
        for (auto& client : clients)
        {
            CloseSocket(client);
        }
    }

    void Run(const char* name, const std::function<void()>& test)
    {
        test();
        std::cout << "PASS " << name << '\n';
    }
}

int main()
{
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "FAIL test WSAStartup failed\n";
        return 1;
    }

    try
    {
        Run("preposted accepts and shutdown counters", TestPrepostedAcceptPoolAndShutdownCounters);
        Run("fragmented and coalesced real frames", TestFragmentedAndCoalescedFramesPreserveIdentityAndRequestIds);
        Run("authentication identity and spoof rejection", TestUnauthenticatedAndSpoofedChatAreRejected);
        Run("disconnect and bounded queued send", TestDisconnectDuringReceiveBlockedAuthenticationAndQueuedSend);
        Run("pure partial-send offset", TestPurePartialSendOffset);
        Run("request stop and idempotent shutdown", TestRequestStopWakesRunAndShutdownStaysIdempotent);
        Run("100 clients exact delivery and shutdown deadline", TestShutdownDeadlineAndStressAccounting);
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        WSACleanup();
        return 1;
    }
    WSACleanup();
    return 0;
}
