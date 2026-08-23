#include "../Server/Application/IChatService.h"
#include "../Server/Core/Server.h"
#include "../Server/Network/Session.h"
#include "../Common/Protocol/ChatProtocol.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
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

    void RequireDeliveredChat(
        const Message& message,
        const std::string& username,
        const std::string& body,
        const std::string& failure)
    {
        Require(message.fields.size() == 3, failure + " timestamp field was missing");
        Require(message.fields[0] == username && message.fields[1] == body, failure);
        std::int64_t timestamp = 0;
        const std::string& encoded = message.fields[2];
        const auto parsed = std::from_chars(
            encoded.data(), encoded.data() + encoded.size(), timestamp);
        Require(parsed.ec == std::errc() &&
            parsed.ptr == encoded.data() + encoded.size() &&
            timestamp > 0,
            failure + " timestamp was invalid");
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
        std::free(value);
        return result;
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
        ChatServiceStatus Start() override
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++startCalls;
            return startStatus;
        }

        void Stop() override
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++stopCalls;
        }

        void AddUser(const std::string& username, const std::string& password)
        {
            std::lock_guard<std::mutex> lock(mutex);
            users[username] = password;
        }

        void AddHistory(
            const std::string& username,
            const std::string& message,
            std::int64_t timestampMilliseconds)
        {
            std::lock_guard<std::mutex> lock(mutex);
            history.push_back({ username, message, timestampMilliseconds });
        }

        void SetUnavailable(bool unavailable)
        {
            std::lock_guard<std::mutex> lock(mutex);
            operationsUnavailable = unavailable;
        }

        ChatServiceStatus RegisterUser(const std::string& username, const std::string& password) override
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (operationsUnavailable)
            {
                return ChatServiceStatus::Unavailable;
            }
            return users.emplace(username, password).second
                ? ChatServiceStatus::Succeeded
                : ChatServiceStatus::Conflict;
        }

        LoginResult Login(
            const std::string& username,
            const std::string& password,
            std::size_t historyLimit) override
        {
            std::unique_lock<std::mutex> lock(mutex);
            if (blockNextAuthentication)
            {
                blockNextAuthentication = false;
                authenticationEntered = true;
                authenticationCondition.notify_all();
                authenticationCondition.wait(lock, [this] { return authenticationReleased; });
            }

            if (operationsUnavailable)
            {
                return { ChatServiceStatus::Unavailable, {} };
            }

            const auto found = users.find(username);
            if (found == users.end() || found->second != password)
            {
                return { ChatServiceStatus::Rejected, {} };
            }

            const std::size_t first = history.size() > historyLimit
                ? history.size() - historyLimit
                : 0;
            return {
                ChatServiceStatus::Succeeded,
                std::vector<ChatHistoryEntry>(history.begin() + first, history.end())
            };
        }

        ChatServiceStatus StoreMessage(const std::string& username, const std::string& message) override
        {
            std::unique_lock<std::mutex> lock(mutex);
            if (blockNextStore)
            {
                blockNextStore = false;
                storeEntered = true;
                storeCondition.notify_all();
                storeCondition.wait(lock, [this] { return storeReleased; });
            }
            if (operationsUnavailable)
            {
                return ChatServiceStatus::Unavailable;
            }
            storedMessages.emplace_back(username, message);
            history.push_back({ username, message });
            return ChatServiceStatus::Succeeded;
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

        void BlockNextStore()
        {
            std::lock_guard<std::mutex> lock(mutex);
            blockNextStore = true;
            storeReleased = false;
            storeEntered = false;
        }

        void WaitForStoreEntry()
        {
            std::unique_lock<std::mutex> lock(mutex);
            const bool entered = storeCondition.wait_for(lock, 2s, [this] { return storeEntered; });
            Require(entered, "message store call did not block");
        }

        void ReleaseStore()
        {
            std::lock_guard<std::mutex> lock(mutex);
            storeReleased = true;
            storeCondition.notify_all();
        }

        std::vector<std::pair<std::string, std::string>> Messages() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return storedMessages;
        }

        std::pair<int, int> LifecycleCalls() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return { startCalls, stopCalls };
        }

    private:
        mutable std::mutex mutex;
        std::condition_variable authenticationCondition;
        std::condition_variable storeCondition;
        std::unordered_map<std::string, std::string> users;
        std::vector<std::pair<std::string, std::string>> storedMessages;
        std::vector<ChatHistoryEntry> history;
        ChatServiceStatus startStatus = ChatServiceStatus::Succeeded;
        bool operationsUnavailable = false;
        int startCalls = 0;
        int stopCalls = 0;
        bool blockNextAuthentication = false;
        bool authenticationEntered = false;
        bool authenticationReleased = false;
        bool blockNextStore = false;
        bool storeEntered = false;
        bool storeReleased = false;
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

    SOCKET Connect(const std::string& host, std::uint16_t port)
    {
        SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        Require(socketHandle != INVALID_SOCKET, "client socket creation failed");

        sockaddr_in address{};
        address.sin_family = AF_INET;
        Require(InetPtonA(AF_INET, host.c_str(), &address.sin_addr) == 1, "test connect address was invalid");
        address.sin_port = htons(port);
        if (connect(socketHandle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
        {
            closesocket(socketHandle);
            throw std::runtime_error("IPv4 connect failed");
        }
        return socketHandle;
    }

    SOCKET Connect(std::uint16_t port)
    {
        return Connect("127.0.0.1", port);
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
        SendAll(socketHandle, Encode({ MessageType::LoginRequest, requestId, { username, "password" } }));
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

    void TestDefaultDatabaseQueueSupportsPlannedBurst()
    {
        constexpr std::size_t plannedClientCount = 100;
        constexpr std::size_t messagesPerClient = 10;
        const ServerOptions options;
        Require(
            options.maxQueuedDatabaseJobs >= plannedClientCount * messagesPerClient,
            "default database queue cannot hold the planned 100-client burst");
    }

    void RequireConnectionClosed(SOCKET socketHandle);

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

    void TestBindAddressConfiguration()
    {
        FakeChatService defaultService;
        Server defaultServer(defaultService);
        Require(defaultServer.Init(0), "default loopback server init failed");
        Require(defaultServer.GetBoundAddress() == "127.0.0.1", "default bind address must remain loopback");
        defaultServer.Shutdown();
        RequireDrained(defaultServer.GetDiagnostics());

        FakeChatService anyService;
        Server anyServer(anyService);
        Require(anyServer.Init("0.0.0.0", 0), "explicit any-address server init failed");
        Require(anyServer.GetBoundAddress() == "0.0.0.0", "explicit bind address was not preserved");
        anyServer.Shutdown();
        RequireDrained(anyServer.GetDiagnostics());

        FakeChatService invalidService;
        Server invalidServer(invalidService);
        Require(!invalidServer.Init("not-an-ipv4-address", 0), "invalid bind address must be rejected");
        invalidServer.Shutdown();
        RequireDrained(invalidServer.GetDiagnostics());
    }

    void TestConfiguredLanAddressSupportsTwoWayChat()
    {
        const std::string address = Environment("CHAT_TEST_LAN_ADDRESS");
        if (address.empty())
        {
            return;
        }

        FakeChatService service;
        service.AddUser("lan_alice", "password");
        service.AddUser("lan_bob", "password");
        Server server(service);
        Require(server.Init(address, 0), "LAN-address server init failed");

        SOCKET alice = Connect(address, server.GetBoundPort());
        SOCKET bob = Connect(address, server.GetBoundPort());
        Login(alice, "lan_alice", 901);
        Login(bob, "lan_bob", 902);

        SendAll(alice, Encode({ MessageType::ChatSend, 903, { "alice-to-bob" } }));
        const auto aliceFirst = ReceiveMessages(alice, 1);
        const auto bobFirst = ReceiveMessages(bob, 1);
        RequireDeliveredChat(
            aliceFirst[0], "lan_alice", "alice-to-bob", "LAN origin did not receive its first delivery");
        Require(bobFirst[0].fields == aliceFirst[0].fields, "LAN peer did not receive Alice's message");

        SendAll(bob, Encode({ MessageType::ChatSend, 904, { "bob-to-alice" } }));
        const auto aliceSecond = ReceiveMessages(alice, 1);
        const auto bobSecond = ReceiveMessages(bob, 1);
        RequireDeliveredChat(
            aliceSecond[0], "lan_bob", "bob-to-alice", "LAN peer did not receive Bob's message");
        Require(bobSecond[0].fields == aliceSecond[0].fields, "LAN origin did not receive its second delivery");

        CloseSocket(alice);
        CloseSocket(bob);
        server.Shutdown();
        RequireDrained(server.GetDiagnostics());
    }

    void TestFragmentedAndCoalescedFramesPreserveIdentityAndRequestIds()
    {
        FakeChatService service;
        service.AddUser("alice", "password");
        service.AddUser("bob", "password");
        Server server(service);
        Require(server.Init(0), "server init failed");

        SOCKET peer = Connect(server.GetBoundPort());
        SOCKET origin = Connect(server.GetBoundPort());
        Login(peer, "bob", 10);

        auto login = Encode({ MessageType::LoginRequest, 20, { "alice", "password" } });
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
        RequireDeliveredChat(
            originMessages[1], "alice", "hello", "origin chat identity was not server-derived");

        const auto peerMessages = ReceiveMessages(peer, 1);
        Require(peerMessages[0].type == MessageType::ChatDelivered, "peer did not receive delivered chat");
        Require(peerMessages[0].requestId == 0, "peer broadcast request id was not zero");
        RequireDeliveredChat(
            peerMessages[0], "alice", "hello", "peer chat identity was not server-derived");

        const auto stored = service.Messages();
        Require(stored == std::vector<std::pair<std::string, std::string>>({ { "alice", "hello" } }), "service saw an untrusted sender");

        CloseSocket(origin);
        CloseSocket(peer);
        server.Shutdown();
        RequireDrained(server.GetDiagnostics());
    }

    void TestPersistedChatReachesPeersAfterOriginDisconnects()
    {
        FakeChatService service;
        service.AddUser("alice", "password");
        service.AddUser("bob", "password");
        Server server(service);
        Require(server.Init(0), "server init failed");

        SOCKET origin = Connect(server.GetBoundPort());
        SOCKET peer = Connect(server.GetBoundPort());
        Login(origin, "alice", 31);
        Login(peer, "bob", 32);

        service.BlockNextStore();
        SendAll(origin, Encode({ MessageType::ChatSend, 33, { "persisted" } }));
        service.WaitForStoreEntry();
        CloseSocket(origin);
        WaitUntil(
            [&server] { return server.GetDiagnostics().activeSessions == 1; },
            2s,
            "closed origin remained active while storage was blocked");
        service.ReleaseStore();

        const auto delivered = ReceiveMessages(peer, 1);
        Require(delivered[0].type == MessageType::ChatDelivered, "peer did not receive persisted chat");
        Require(delivered[0].requestId == 0, "peer delivery reused the closed origin request id");
        RequireDeliveredChat(
            delivered[0], "alice", "persisted", "peer delivery changed the persisted chat");
        Require(service.Messages() ==
            std::vector<std::pair<std::string, std::string>>({ { "alice", "persisted" } }),
            "message was not persisted before peer delivery");

        CloseSocket(peer);
        server.Shutdown();
        RequireDrained(server.GetDiagnostics());
    }

    void TestLoginHistoryAndRegistrationState()
    {
        FakeChatService service;
        service.AddUser("alice", "password");
        service.AddHistory("bob", "older", 1763856000123LL);
        service.AddHistory("carol", "newer", 1763856060456LL);
        Server server(service);
        Require(server.Init(0), "server init failed");

        SOCKET login = Connect(server.GetBoundPort());
        SendAll(login, Encode({ MessageType::LoginRequest, 41, { "alice", "password" } }));
        const auto loginMessages = ReceiveMessages(login, 3);
        Require(loginMessages[0].type == MessageType::LoginSucceeded, "login success was not first");
        Require(loginMessages[0].requestId == 41, "login response request id changed");
        Require(loginMessages[1].type == MessageType::ChatDelivered && loginMessages[1].requestId == 0,
            "first history item was not an unsolicited delivery");
        Require(loginMessages[1].fields ==
            std::vector<std::string>({ "bob", "older", "1763856000123" }),
            "oldest history item was not first");
        Require(loginMessages[2].fields ==
            std::vector<std::string>({ "carol", "newer", "1763856060456" }),
            "history order changed");

        SOCKET registration = Connect(server.GetBoundPort());
        SendAll(registration, Encode({ MessageType::RegisterRequest, 42, { "dave", "password" } }));
        const auto registrationReply = ReceiveMessages(registration, 1);
        Require(registrationReply[0].type == MessageType::RegisterSucceeded, "registration failed");
        SendAll(registration, Encode({ MessageType::ChatSend, 43, { "not-authenticated" } }));
        RequireConnectionClosed(registration);

        CloseSocket(login);
        CloseSocket(registration);
        server.Shutdown();
        Require(service.LifecycleCalls() == std::pair<int, int>({ 1, 1 }), "service lifecycle count was not exact");
        RequireDrained(server.GetDiagnostics());
    }

    void TestDatabaseUnavailableAndBlockedAuthenticationKeepIocpResponsive()
    {
        FakeChatService service;
        service.AddUser("alice", "password");
        service.BlockNextAuthentication();

        ServerOptions options;
        options.workerCount = 1;
        Server server(service, options);
        Require(server.Init(0), "server init failed");

        SOCKET blocked = Connect(server.GetBoundPort());
        SendAll(blocked, Encode({ MessageType::LoginRequest, 51, { "alice", "password" } }));
        service.WaitForAuthenticationEntry();

        SOCKET responsive = Connect(server.GetBoundPort());
        WaitUntil(
            [&server] { return server.GetDiagnostics().activeSessions >= 2; },
            1s,
            "blocked authentication stalled IOCP accepts");

        service.ReleaseAuthentication();
        Require(ReceiveMessages(blocked, 1)[0].type == MessageType::LoginSucceeded,
            "released authentication did not finish");
        service.SetUnavailable(true);
        SendAll(responsive, Encode({ MessageType::LoginRequest, 52, { "alice", "password" } }));
        Require(ReceiveMessages(responsive, 1)[0].type == MessageType::LoginFailed,
            "database unavailability was not mapped to login failure");

        CloseSocket(blocked);
        CloseSocket(responsive);
        server.Shutdown();
        RequireDrained(server.GetDiagnostics());
    }

    void TestDatabaseQueueOverflowIsNonblockingAndDiagnostic()
    {
        FakeChatService service;
        service.AddUser("alice", "password");
        service.BlockNextAuthentication();

        ServerOptions options;
        options.workerCount = 2;
        options.maxQueuedDatabaseJobs = 1;
        Server server(service, options);
        Require(server.Init(0), "server init failed");

        SOCKET blocked = Connect(server.GetBoundPort());
        SOCKET firstQueued = Connect(server.GetBoundPort());
        SOCKET overflowing = Connect(server.GetBoundPort());
        WaitUntil(
            [&server] { return server.GetDiagnostics().activeSessions == 3; },
            1s,
            "overflow test sessions were not accepted");

        SendAll(blocked, Encode({ MessageType::LoginRequest, 61, { "alice", "password" } }));
        service.WaitForAuthenticationEntry();
        SendAll(firstQueued, Encode({ MessageType::LoginRequest, 62, { "alice", "password" } }));
        SendAll(overflowing, Encode({ MessageType::LoginRequest, 63, { "alice", "password" } }));
        WaitUntil(
            [&server] { return server.GetDiagnostics().databaseQueueOverflows == 1; },
            1s,
            "database queue overflow was not recorded without blocking IOCP");

        service.ReleaseAuthentication();
        CloseSocket(blocked);
        CloseSocket(firstQueued);
        CloseSocket(overflowing);
        server.Shutdown();
        Require(server.GetDiagnostics().databaseQueueOverflows == 1, "database queue overflow count changed");
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
        service.AddUser("alice", "password");
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
        service.AddUser("alice", "password");
        service.BlockNextAuthentication();
        ServerOptions options;
        options.workerCount = 1;
        options.maxQueuedSendBytes = 512;
        Server server(service, options);
        Require(server.Init(0), "server init failed");

        SOCKET receiving = Connect(server.GetBoundPort());
        CloseSocket(receiving);

        SOCKET blocked = Connect(server.GetBoundPort());
        SendAll(blocked, Encode({ MessageType::LoginRequest, 3, { "alice", "password" } }));
        service.WaitForAuthenticationEntry();
        CloseSocket(blocked);
        service.ReleaseAuthentication();

        SOCKET queued = Connect(server.GetBoundPort());
        Login(queued, "alice", 4);
        const std::string queuedMessage(1000, 'x');
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
            service.AddUser("user" + std::to_string(index), "password");
        }

        ServerOptions options;
        options.acceptPrepostCount = 32;
        options.workerCount = 8;
        options.maxQueuedSendBytes = 16 * 1024 * 1024;
        options.maxQueuedDatabaseJobs = 2048;
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
        Run("default database queue supports planned burst", TestDefaultDatabaseQueueSupportsPlannedBurst);
        Run("bind address configuration", TestBindAddressConfiguration);
        Run("configured LAN address two-way chat", TestConfiguredLanAddressSupportsTwoWayChat);
        Run("preposted accepts and shutdown counters", TestPrepostedAcceptPoolAndShutdownCounters);
        Run("fragmented and coalesced real frames", TestFragmentedAndCoalescedFramesPreserveIdentityAndRequestIds);
        Run("persisted chat survives origin disconnect", TestPersistedChatReachesPeersAfterOriginDisconnects);
        Run("login history and registration state", TestLoginHistoryAndRegistrationState);
        Run("database isolation and unavailable mapping", TestDatabaseUnavailableAndBlockedAuthenticationKeepIocpResponsive);
        Run("database queue overflow diagnostics", TestDatabaseQueueOverflowIsNonblockingAndDiagnostic);
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
