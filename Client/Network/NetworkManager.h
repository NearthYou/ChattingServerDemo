#pragma once

#include <WinSock2.h>
#include <Windows.h>

#include "../../Common/PacketDefine.h"
#include "../../Common/Protocol/ChatProtocol.h"
#include "NetworkPrimitives.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class NetworkStatus
{
    Connecting,
    Connected,
    ConnectFailed,
    Disconnected,
    ProtocolError,
    QueueFull
};

struct NetworkEvent
{
    enum class Kind
    {
        Packet,
        Status
    };

    Kind kind = Kind::Status;
    ChatPacket packet{};
    NetworkStatus status = NetworkStatus::Disconnected;
    std::string message;
};

class NetworkManager
{
public:
    NetworkManager();
    ~NetworkManager();

    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;

    bool Connect(const std::string& address, int port);
    void Disconnect();
    bool IsConnected() const;

    bool SendLoginRequest(const std::string& username, const std::string& password);
    bool SendRegisterRequest(const std::string& username, const std::string& password);
    bool SendChatMessage(const std::string& message);
    std::vector<NetworkEvent> GetPendingEvents();

private:
    static constexpr std::size_t kOutboundQueueCapacity = 128;
    static constexpr std::size_t kInboundQueueCapacity = 256;

    bool QueueCommand(
        chat::protocol::MessageType type,
        std::vector<std::string> fields);
    void NetworkLoop(std::string address, int port);
    void SignalConnectionResult(bool succeeded);
    bool PublishEvent(NetworkEvent event);
    void StopWorkerLocked();

    std::atomic<bool> isConnected{ false };
    std::atomic<std::uint32_t> nextRequestId{ 1 };

    BoundedQueue<chat::protocol::Message> outboundCommands;
    BoundedQueue<NetworkEvent> inboundEvents;

    std::mutex lifecycleMutex;
    std::thread networkThread;
    HANDLE stopEvent = nullptr;
    HANDLE wakeEvent = nullptr;

    std::mutex connectionResultMutex;
    std::condition_variable connectionResultCondition;
    bool connectionResultReady = false;
    bool connectionResult = false;
};
