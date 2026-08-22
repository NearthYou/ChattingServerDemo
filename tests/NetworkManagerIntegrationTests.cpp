#include "../Client/Network/NetworkManager.h"
#include "../Client/Network/NetworkPrimitives.h"

#include <Ws2tcpip.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    using namespace std::chrono_literals;

    int failures = 0;

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

    void Check(bool condition, const char* expression, const char* testName, int line)
    {
        if (condition)
        {
            return;
        }

        ++failures;
        std::cerr << testName << ":" << line << " check failed: " << expression << '\n';
    }

#define CHECK(expression) Check((expression), #expression, __func__, __LINE__)

    std::uint16_t ReadUint16(const std::uint8_t* bytes)
    {
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(bytes[0]) << 8) |
            static_cast<std::uint16_t>(bytes[1]));
    }

    std::uint32_t ReadUint32(const std::uint8_t* bytes)
    {
        return
            (static_cast<std::uint32_t>(bytes[0]) << 24) |
            (static_cast<std::uint32_t>(bytes[1]) << 16) |
            (static_cast<std::uint32_t>(bytes[2]) << 8) |
            static_cast<std::uint32_t>(bytes[3]);
    }

    void AppendUint16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
    {
        bytes.push_back(static_cast<std::uint8_t>(value >> 8));
        bytes.push_back(static_cast<std::uint8_t>(value));
    }

    void AppendUint32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
    {
        bytes.push_back(static_cast<std::uint8_t>(value >> 24));
        bytes.push_back(static_cast<std::uint8_t>(value >> 16));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8));
        bytes.push_back(static_cast<std::uint8_t>(value));
    }

    std::vector<std::uint8_t> BuildFrame(
        std::uint16_t messageType,
        std::uint32_t requestId,
        const std::vector<std::string>& fields)
    {
        std::uint32_t payloadBytes = 0;
        for (const auto& field : fields)
        {
            payloadBytes += static_cast<std::uint32_t>(sizeof(std::uint32_t) + field.size());
        }

        std::vector<std::uint8_t> bytes;
        AppendUint32(bytes, payloadBytes);
        AppendUint16(bytes, 1);
        AppendUint16(bytes, messageType);
        AppendUint32(bytes, requestId);
        for (const auto& field : fields)
        {
            AppendUint32(bytes, static_cast<std::uint32_t>(field.size()));
            bytes.insert(bytes.end(), field.begin(), field.end());
        }
        return bytes;
    }

    bool ReceiveExact(SOCKET socketHandle, std::uint8_t* bytes, std::size_t size)
    {
        std::size_t received = 0;
        while (received < size)
        {
            const int result = recv(
                socketHandle,
                reinterpret_cast<char*>(bytes + received),
                static_cast<int>(size - received),
                0);
            if (result <= 0)
            {
                return false;
            }
            received += static_cast<std::size_t>(result);
        }
        return true;
    }

    bool SendAll(SOCKET socketHandle, const std::uint8_t* bytes, std::size_t size)
    {
        std::size_t sent = 0;
        while (sent < size)
        {
            const int result = send(
                socketHandle,
                reinterpret_cast<const char*>(bytes + sent),
                static_cast<int>(size - sent),
                0);
            if (result <= 0)
            {
                return false;
            }
            sent += static_cast<std::size_t>(result);
        }
        return true;
    }

    SOCKET CreateLoopbackListener(std::uint16_t& port)
    {
        SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET)
        {
            return INVALID_SOCKET;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
            listen(listener, 1) == SOCKET_ERROR)
        {
            closesocket(listener);
            return INVALID_SOCKET;
        }

        int addressBytes = sizeof(address);
        if (getsockname(listener, reinterpret_cast<sockaddr*>(&address), &addressBytes) == SOCKET_ERROR)
        {
            closesocket(listener);
            return INVALID_SOCKET;
        }

        u_long nonBlocking = 1;
        if (ioctlsocket(listener, FIONBIO, &nonBlocking) == SOCKET_ERROR)
        {
            closesocket(listener);
            return INVALID_SOCKET;
        }

        port = ntohs(address.sin_port);
        return listener;
    }

    SOCKET AcceptWithDeadline(SOCKET listener, const std::atomic<bool>& stopRequested)
    {
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (!stopRequested.load() && std::chrono::steady_clock::now() < deadline)
        {
            SOCKET peer = accept(listener, nullptr, nullptr);
            if (peer != INVALID_SOCKET)
            {
                int timeoutMilliseconds = 3000;
                setsockopt(
                    peer,
                    SOL_SOCKET,
                    SO_RCVTIMEO,
                    reinterpret_cast<const char*>(&timeoutMilliseconds),
                    sizeof(timeoutMilliseconds));
                return peer;
            }
            if (WSAGetLastError() != WSAEWOULDBLOCK)
            {
                return INVALID_SOCKET;
            }
            std::this_thread::sleep_for(5ms);
        }
        return INVALID_SOCKET;
    }

    struct FakePeerResult
    {
        bool accepted = false;
        bool loginFrameValid = false;
        bool responsesSent = false;
    };

    void RunFakePeer(
        SOCKET listener,
        const std::atomic<bool>& stopRequested,
        FakePeerResult& result)
    {
        SOCKET peer = AcceptWithDeadline(listener, stopRequested);
        closesocket(listener);
        if (peer == INVALID_SOCKET)
        {
            return;
        }
        result.accepted = true;

        std::array<std::uint8_t, 12> header{};
        if (!ReceiveExact(peer, header.data(), header.size()))
        {
            closesocket(peer);
            return;
        }

        const std::uint32_t payloadBytes = ReadUint32(header.data());
        const std::uint32_t requestId = ReadUint32(header.data() + 8);
        std::vector<std::uint8_t> payload(payloadBytes);
        if (!ReceiveExact(peer, payload.data(), payload.size()))
        {
            closesocket(peer);
            return;
        }

        std::size_t offset = 0;
        auto readField = [&payload, &offset]()
        {
            if (payload.size() - offset < sizeof(std::uint32_t))
            {
                return std::string();
            }
            const std::size_t fieldBytes = ReadUint32(payload.data() + offset);
            offset += sizeof(std::uint32_t);
            if (fieldBytes > payload.size() - offset)
            {
                return std::string();
            }
            std::string field(reinterpret_cast<const char*>(payload.data() + offset), fieldBytes);
            offset += fieldBytes;
            return field;
        };

        const std::string username = readField();
        const std::string password = readField();
        result.loginFrameValid =
            ReadUint16(header.data() + 4) == 1 &&
            ReadUint16(header.data() + 6) == 1 &&
            requestId != 0 &&
            username == "alice" &&
            password == "secret" &&
            offset == payload.size();

        const auto loginSucceeded = BuildFrame(4, requestId, {});
        if (!SendAll(peer, loginSucceeded.data(), 3))
        {
            closesocket(peer);
            return;
        }
        std::this_thread::sleep_for(25ms);
        if (!SendAll(peer, loginSucceeded.data() + 3, 5))
        {
            closesocket(peer);
            return;
        }
        std::this_thread::sleep_for(25ms);
        if (!SendAll(peer, loginSucceeded.data() + 8, loginSucceeded.size() - 8))
        {
            closesocket(peer);
            return;
        }

        auto coalesced = BuildFrame(8, 0, { "bob", "hello" });
        const auto registerFailed = BuildFrame(7, 77, {});
        coalesced.insert(coalesced.end(), registerFailed.begin(), registerFailed.end());
        result.responsesSent = SendAll(peer, coalesced.data(), coalesced.size());

        std::array<char, 1> ignored{};
        recv(peer, ignored.data(), static_cast<int>(ignored.size()), 0);
        closesocket(peer);
    }

    void PendingRequestTrackerStaysBounded()
    {
        BoundedRequestTracker tracker(128);
        for (std::uint32_t requestId = 1; requestId <= 128; ++requestId)
        {
            CHECK(tracker.TryTrack(requestId));
        }
        CHECK(!tracker.TryTrack(129));
        CHECK(tracker.Size() == 128u);
        CHECK(tracker.Consume(1));
        CHECK(!tracker.Consume(999));
        CHECK(tracker.TryTrack(129));
        CHECK(tracker.Size() == 128u);
    }

    std::vector<NetworkStatus> WaitForTerminalConnectionStatus(
        NetworkManager& manager,
        NetworkStatus terminal)
    {
        std::vector<NetworkStatus> statuses;
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            for (const auto& event : manager.GetPendingEvents())
            {
                if (event.kind == NetworkEvent::Kind::Status)
                {
                    statuses.push_back(event.status);
                }
            }
            if (!statuses.empty() && statuses.back() == terminal)
            {
                break;
            }
            std::this_thread::sleep_for(5ms);
        }
        return statuses;
    }

    void BeginConnectReturnsPromptlyAndPublishesOrderedStatus()
    {
        std::uint16_t successPort = 0;
        SOCKET listener = CreateLoopbackListener(successPort);
        CHECK(listener != INVALID_SOCKET);
        if (listener == INVALID_SOCKET)
        {
            return;
        }

        std::atomic<bool> stopPeer{ false };
        std::thread peerThread([listener, &stopPeer] {
            SOCKET peer = AcceptWithDeadline(listener, stopPeer);
            closesocket(listener);
            if (peer != INVALID_SOCKET)
            {
                char ignored = 0;
                recv(peer, &ignored, 1, 0);
                closesocket(peer);
            }
        });

        NetworkManager successManager;
        const auto successStart = std::chrono::steady_clock::now();
        CHECK(successManager.BeginConnect("127.0.0.1", successPort));
        CHECK(std::chrono::steady_clock::now() - successStart < 250ms);
        const auto successStatuses = WaitForTerminalConnectionStatus(
            successManager,
            NetworkStatus::Connected);
        CHECK(successStatuses ==
            std::vector<NetworkStatus>({ NetworkStatus::Connecting, NetworkStatus::Connected }));
        successManager.Disconnect();
        stopPeer.store(true);
        peerThread.join();

        std::uint16_t closedPort = 0;
        SOCKET closedListener = CreateLoopbackListener(closedPort);
        CHECK(closedListener != INVALID_SOCKET);
        if (closedListener == INVALID_SOCKET)
        {
            return;
        }
        closesocket(closedListener);

        NetworkManager failureManager;
        const auto failureStart = std::chrono::steady_clock::now();
        CHECK(failureManager.BeginConnect("127.0.0.1", closedPort));
        CHECK(std::chrono::steady_clock::now() - failureStart < 250ms);
        const auto failureStatuses = WaitForTerminalConnectionStatus(
            failureManager,
            NetworkStatus::ConnectFailed);
        CHECK(failureStatuses ==
            std::vector<NetworkStatus>({ NetworkStatus::Connecting, NetworkStatus::ConnectFailed }));
        failureManager.Disconnect();
    }

    void FailedConnectCanBeStoppedAndRetriedWithoutAStuckWorker()
    {
        std::uint16_t closedPort = 0;
        SOCKET listener = CreateLoopbackListener(closedPort);
        CHECK(listener != INVALID_SOCKET);
        if (listener == INVALID_SOCKET)
        {
            return;
        }
        closesocket(listener);

        NetworkManager manager;
        const auto firstStart = std::chrono::steady_clock::now();
        CHECK(!manager.Connect("127.0.0.1", closedPort));
        CHECK(!manager.IsConnected());
        const auto firstDuration = std::chrono::steady_clock::now() - firstStart;
        CHECK(firstDuration < 6s);

        const auto disconnectStart = std::chrono::steady_clock::now();
        manager.Disconnect();
        manager.Disconnect();
        CHECK(std::chrono::steady_clock::now() - disconnectStart < 500ms);

        const auto retryStart = std::chrono::steady_clock::now();
        CHECK(!manager.Connect("127.0.0.1", closedPort));
        CHECK(!manager.IsConnected());
        const auto retryDuration = std::chrono::steady_clock::now() - retryStart;
        CHECK(retryDuration < 6s);
        manager.Disconnect();
    }

    void LoopbackPeerExercisesNetworkManagerFramingAndShutdown()
    {
        std::uint16_t port = 0;
        SOCKET listener = CreateLoopbackListener(port);
        CHECK(listener != INVALID_SOCKET);
        if (listener == INVALID_SOCKET)
        {
            return;
        }

        std::atomic<bool> stopPeer{ false };
        FakePeerResult peerResult;
        std::thread peerThread(RunFakePeer, listener, std::cref(stopPeer), std::ref(peerResult));

        NetworkManager manager;
        const bool connected = manager.Connect("127.0.0.1", port);
        CHECK(connected);
        if (connected)
        {
            CHECK(manager.SendLoginRequest("alice", "secret"));
        }

        std::vector<ChatPacket> packets;
        const auto eventDeadline = std::chrono::steady_clock::now() + 3s;
        while (packets.size() < 3 && std::chrono::steady_clock::now() < eventDeadline)
        {
            for (auto& event : manager.GetPendingEvents())
            {
                if (event.kind == NetworkEvent::Kind::Packet)
                {
                    packets.push_back(std::move(event.packet));
                }
            }
            std::this_thread::sleep_for(5ms);
        }

        CHECK(packets.size() == 3u);
        if (packets.size() == 3u)
        {
            CHECK(packets[0].type == PACKET_TYPE_LOGIN_SUCCESS);
            CHECK(packets[0].message == "Login/Register successful");
            CHECK(packets[1].type == PACKET_TYPE_CHAT);
            CHECK(packets[1].sender == "bob");
            CHECK(packets[1].message == "hello");
            CHECK(!packets[1].isMine);
            CHECK(packets[2].type == PACKET_TYPE_REGISTER_FAILED);
            CHECK(packets[2].message == "Login/Register failed");
        }

        const auto disconnectStart = std::chrono::steady_clock::now();
        manager.Disconnect();
        CHECK(std::chrono::steady_clock::now() - disconnectStart < 1500ms);
        CHECK(!manager.IsConnected());

        stopPeer.store(true);
        peerThread.join();
        CHECK(peerResult.accepted);
        CHECK(peerResult.loginFrameValid);
        CHECK(peerResult.responsesSent);
    }

    std::vector<ChatPacket> WaitForLivePackets(
        NetworkManager& manager,
        const std::function<bool(const std::vector<ChatPacket>&)>& complete)
    {
        std::vector<ChatPacket> packets;
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            for (auto& event : manager.GetPendingEvents())
            {
                if (event.kind == NetworkEvent::Kind::Packet)
                {
                    packets.push_back(std::move(event.packet));
                }
            }
            if (complete(packets))
            {
                break;
            }
            std::this_thread::sleep_for(10ms);
        }
        return packets;
    }

    bool ContainsChat(
        const std::vector<ChatPacket>& packets,
        const std::string& sender,
        const std::string& message)
    {
        return std::any_of(
            packets.begin(),
            packets.end(),
            [&](const ChatPacket& packet) {
                return packet.type == PACKET_TYPE_CHAT &&
                       packet.sender == sender &&
                       packet.message == message;
            });
    }

    void LiveLanServiceWriteOrRead()
    {
        const std::string address = Environment("CHAT_LIVE_E2E_ADDRESS");
        const std::string phase = Environment("CHAT_LIVE_E2E_PHASE");
        const std::string runId = Environment("CHAT_LIVE_E2E_RUN_ID");
        if (address.empty() || phase.empty() || runId.empty())
        {
            return;
        }

        const int port = std::stoi(Environment("CHAT_LIVE_E2E_PORT"));
        const std::string aliceName = "lana" + runId;
        const std::string bobName = "lanb" + runId;
        const std::string password = "LiveTestPassword123!";
        const std::string aliceMessage = "history-a-" + runId;
        const std::string bobMessage = "history-b-" + runId;

        NetworkManager alice;
        CHECK(alice.Connect(address, port));
        if (!alice.IsConnected()) return;

        if (phase == "write")
        {
            NetworkManager bob;
            CHECK(bob.Connect(address, port));
            if (!bob.IsConnected()) return;

            CHECK(alice.SendRegisterRequest(aliceName, password));
            const auto aliceRegistration = WaitForLivePackets(
                alice,
                [](const std::vector<ChatPacket>& packets) {
                    return std::any_of(packets.begin(), packets.end(), [](const ChatPacket& packet) {
                        return packet.type == PACKET_TYPE_REGISTER_SUCCESS;
                    });
                });
            CHECK(std::any_of(aliceRegistration.begin(), aliceRegistration.end(), [](const ChatPacket& packet) {
                return packet.type == PACKET_TYPE_REGISTER_SUCCESS;
            }));

            CHECK(bob.SendRegisterRequest(bobName, password));
            const auto bobRegistration = WaitForLivePackets(
                bob,
                [](const std::vector<ChatPacket>& packets) {
                    return std::any_of(packets.begin(), packets.end(), [](const ChatPacket& packet) {
                        return packet.type == PACKET_TYPE_REGISTER_SUCCESS;
                    });
                });
            CHECK(std::any_of(bobRegistration.begin(), bobRegistration.end(), [](const ChatPacket& packet) {
                return packet.type == PACKET_TYPE_REGISTER_SUCCESS;
            }));

            CHECK(alice.SendLoginRequest(aliceName, password));
            CHECK(bob.SendLoginRequest(bobName, password));
            const auto aliceLogin = WaitForLivePackets(alice, [](const std::vector<ChatPacket>& packets) {
                return std::any_of(packets.begin(), packets.end(), [](const ChatPacket& packet) {
                    return packet.type == PACKET_TYPE_LOGIN_SUCCESS;
                });
            });
            const auto bobLogin = WaitForLivePackets(bob, [](const std::vector<ChatPacket>& packets) {
                return std::any_of(packets.begin(), packets.end(), [](const ChatPacket& packet) {
                    return packet.type == PACKET_TYPE_LOGIN_SUCCESS;
                });
            });
            CHECK(!aliceLogin.empty());
            CHECK(!bobLogin.empty());

            CHECK(alice.SendChatMessage(aliceMessage));
            const auto aliceDelivery = WaitForLivePackets(
                alice,
                [&](const std::vector<ChatPacket>& packets) { return ContainsChat(packets, aliceName, aliceMessage); });
            const auto bobReceivesAlice = WaitForLivePackets(
                bob,
                [&](const std::vector<ChatPacket>& packets) { return ContainsChat(packets, aliceName, aliceMessage); });
            CHECK(ContainsChat(aliceDelivery, aliceName, aliceMessage));
            CHECK(ContainsChat(bobReceivesAlice, aliceName, aliceMessage));

            CHECK(bob.SendChatMessage(bobMessage));
            const auto aliceReceivesBob = WaitForLivePackets(
                alice,
                [&](const std::vector<ChatPacket>& packets) { return ContainsChat(packets, bobName, bobMessage); });
            const auto bobDelivery = WaitForLivePackets(
                bob,
                [&](const std::vector<ChatPacket>& packets) { return ContainsChat(packets, bobName, bobMessage); });
            CHECK(ContainsChat(aliceReceivesBob, bobName, bobMessage));
            CHECK(ContainsChat(bobDelivery, bobName, bobMessage));
            bob.Disconnect();
        }
        else if (phase == "read")
        {
            CHECK(alice.SendLoginRequest(aliceName, password));
            const auto restored = WaitForLivePackets(
                alice,
                [&](const std::vector<ChatPacket>& packets) {
                    return ContainsChat(packets, aliceName, aliceMessage) &&
                           ContainsChat(packets, bobName, bobMessage);
                });
            CHECK(ContainsChat(restored, aliceName, aliceMessage));
            CHECK(ContainsChat(restored, bobName, bobMessage));
        }
        else
        {
            CHECK(false);
        }
        alice.Disconnect();
    }
}

int RunNetworkManagerIntegrationTests()
{
    WSADATA winsockData{};
    if (WSAStartup(MAKEWORD(2, 2), &winsockData) != 0)
    {
        std::cerr << "NetworkManagerIntegrationTests: WSAStartup failed\n";
        return 1;
    }

    PendingRequestTrackerStaysBounded();
    BeginConnectReturnsPromptlyAndPublishesOrderedStatus();
    FailedConnectCanBeStoppedAndRetriedWithoutAStuckWorker();
    LoopbackPeerExercisesNetworkManagerFramingAndShutdown();
    LiveLanServiceWriteOrRead();

    WSACleanup();
    return failures;
}
