#include "NetworkManager.h"

#include <Ws2tcpip.h>

#include <chrono>
#include <utility>

namespace
{
    constexpr DWORD kConnectTimeoutMilliseconds = 5000;

    enum class ConnectOutcome
    {
        Connected,
        Failed,
        Stopped
    };

    ConnectOutcome ConnectSocket(
        SOCKET socketHandle,
        const std::string& address,
        int port,
        HANDLE stopEvent,
        WSAEVENT connectEvent)
    {
        sockaddr_in serverAddress{};
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port = htons(static_cast<u_short>(port));
        if (InetPtonA(AF_INET, address.c_str(), &serverAddress.sin_addr) != 1)
        {
            return ConnectOutcome::Failed;
        }

        if (WSAEventSelect(socketHandle, connectEvent, FD_CONNECT) == SOCKET_ERROR)
        {
            return ConnectOutcome::Failed;
        }

        const int connectResult = connect(
            socketHandle,
            reinterpret_cast<const sockaddr*>(&serverAddress),
            sizeof(serverAddress));
        if (connectResult == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
        {
            return ConnectOutcome::Failed;
        }

        if (connectResult == SOCKET_ERROR)
        {
            const HANDLE waitHandles[] = { stopEvent, connectEvent };
            const DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, kConnectTimeoutMilliseconds);
            if (waitResult == WAIT_OBJECT_0)
            {
                return ConnectOutcome::Stopped;
            }
            if (waitResult != WAIT_OBJECT_0 + 1)
            {
                return ConnectOutcome::Failed;
            }

            WSANETWORKEVENTS networkEvents{};
            if (WSAEnumNetworkEvents(socketHandle, connectEvent, &networkEvents) == SOCKET_ERROR ||
                (networkEvents.lNetworkEvents & FD_CONNECT) == 0 ||
                networkEvents.iErrorCode[FD_CONNECT_BIT] != 0)
            {
                return ConnectOutcome::Failed;
            }
        }

        if (WSAEventSelect(socketHandle, nullptr, 0) == SOCKET_ERROR)
        {
            return ConnectOutcome::Failed;
        }

        u_long nonBlocking = 0;
        if (ioctlsocket(socketHandle, FIONBIO, &nonBlocking) == SOCKET_ERROR)
        {
            return ConnectOutcome::Failed;
        }

        return ConnectOutcome::Connected;
    }

    NetworkEvent StatusEvent(NetworkStatus status, std::string message)
    {
        NetworkEvent event;
        event.kind = NetworkEvent::Kind::Status;
        event.status = status;
        event.message = std::move(message);
        return event;
    }
}

NetworkManager::NetworkManager()
    : outboundCommands(kOutboundQueueCapacity),
      inboundEvents(kInboundQueueCapacity)
{
}

NetworkManager::~NetworkManager()
{
    Disconnect();
}

bool NetworkManager::Connect(const std::string& address, int port)
{
    if (IsConnected())
    {
        return true;
    }
    if (!BeginConnect(address, port))
    {
        return false;
    }

    std::unique_lock<std::mutex> resultLock(connectionResultMutex);
    const bool receivedResult = connectionResultCondition.wait_for(
        resultLock,
        std::chrono::milliseconds(kConnectTimeoutMilliseconds + 1000),
        [this]() { return connectionResultReady; });
    const bool succeeded = receivedResult && connectionResult;
    resultLock.unlock();

    if (!succeeded)
    {
        std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex);
        StopWorkerLocked();
    }
    return succeeded;
}

bool NetworkManager::BeginConnect(const std::string& address, int port)
{
    if (address.empty() || port <= 0 || port > 65535)
    {
        return false;
    }

    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex);
    if (isConnected.load(std::memory_order_acquire))
    {
        return true;
    }
    if (isConnecting.load(std::memory_order_acquire))
    {
        return false;
    }

    StopWorkerLocked();
    outboundCommands.Clear();
    inboundEvents.Clear();
    nextRequestId.store(1, std::memory_order_relaxed);

    stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (stopEvent == nullptr || wakeEvent == nullptr)
    {
        StopWorkerLocked();
        PublishEvent(StatusEvent(NetworkStatus::ConnectFailed, "Network worker initialization failed."));
        return false;
    }

    {
        std::lock_guard<std::mutex> resultLock(connectionResultMutex);
        connectionResultReady = false;
        connectionResult = false;
    }

    isConnecting.store(true, std::memory_order_release);
    PublishEvent(StatusEvent(NetworkStatus::Connecting, "Connecting to the server."));
    try
    {
        networkThread = std::thread(&NetworkManager::NetworkLoop, this, address, port);
    }
    catch (...)
    {
        StopWorkerLocked();
        PublishEvent(StatusEvent(NetworkStatus::ConnectFailed, "Network worker creation failed."));
        return false;
    }
    return true;
}

void NetworkManager::Disconnect()
{
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex);
    StopWorkerLocked();
}

bool NetworkManager::IsConnected() const
{
    return isConnected.load(std::memory_order_acquire);
}

bool NetworkManager::IsConnecting() const
{
    return isConnecting.load(std::memory_order_acquire);
}

bool NetworkManager::SendLoginRequest(const std::string& username, const std::string& password)
{
    return QueueCommand(chat::protocol::MessageType::LoginRequest, { username, password });
}

bool NetworkManager::SendRegisterRequest(const std::string& username, const std::string& password)
{
    return QueueCommand(chat::protocol::MessageType::RegisterRequest, { username, password });
}

bool NetworkManager::SendChatMessage(const std::string& message)
{
    return QueueCommand(chat::protocol::MessageType::ChatSend, { message });
}

std::vector<NetworkEvent> NetworkManager::GetPendingEvents()
{
    return inboundEvents.Drain();
}

bool NetworkManager::QueueCommand(
    chat::protocol::MessageType type,
    std::vector<std::string> fields)
{
    if (!isConnected.load(std::memory_order_acquire))
    {
        return false;
    }

    std::uint32_t requestId = nextRequestId.fetch_add(1, std::memory_order_relaxed);
    if (requestId == 0)
    {
        requestId = nextRequestId.fetch_add(1, std::memory_order_relaxed);
    }

    chat::protocol::Message command{ type, requestId, std::move(fields) };
    if (chat::protocol::ValidateMessage(command) != chat::protocol::CodecError::None ||
        !outboundCommands.TryPush(std::move(command)))
    {
        return false;
    }

    return SetEvent(wakeEvent) != FALSE;
}

void NetworkManager::NetworkLoop(std::string address, int port)
{
    WSADATA winsockData{};
    SOCKET socketHandle = INVALID_SOCKET;
    WSAEVENT connectEvent = WSA_INVALID_EVENT;
    WSAEVENT receiveEvent = WSA_INVALID_EVENT;
    WSAEVENT sendEvent = WSA_INVALID_EVENT;
    bool connectionSignaled = false;

    auto signalResult = [this, &connectionSignaled](bool succeeded)
    {
        if (!connectionSignaled)
        {
            SignalConnectionResult(succeeded);
            connectionSignaled = true;
        }
    };

    if (WSAStartup(MAKEWORD(2, 2), &winsockData) != 0)
    {
        signalResult(false);
        PublishEvent(StatusEvent(NetworkStatus::ConnectFailed, "Winsock initialization failed."));
        return;
    }

    socketHandle = WSASocketW(
        AF_INET,
        SOCK_STREAM,
        IPPROTO_TCP,
        nullptr,
        0,
        WSA_FLAG_OVERLAPPED);
    connectEvent = WSACreateEvent();
    if (socketHandle == INVALID_SOCKET || connectEvent == WSA_INVALID_EVENT)
    {
        signalResult(false);
        PublishEvent(StatusEvent(NetworkStatus::ConnectFailed, "Socket creation failed."));
        if (connectEvent != WSA_INVALID_EVENT)
        {
            WSACloseEvent(connectEvent);
        }
        if (socketHandle != INVALID_SOCKET)
        {
            closesocket(socketHandle);
        }
        WSACleanup();
        return;
    }

    const ConnectOutcome connectOutcome = ConnectSocket(
        socketHandle,
        address,
        port,
        stopEvent,
        connectEvent);
    WSACloseEvent(connectEvent);
    connectEvent = WSA_INVALID_EVENT;
    if (connectOutcome != ConnectOutcome::Connected)
    {
        signalResult(false);
        if (connectOutcome == ConnectOutcome::Failed)
        {
            PublishEvent(StatusEvent(NetworkStatus::ConnectFailed, "Unable to connect to the server."));
        }
        closesocket(socketHandle);
        WSACleanup();
        return;
    }

    receiveEvent = WSACreateEvent();
    sendEvent = WSACreateEvent();
    if (receiveEvent == WSA_INVALID_EVENT || sendEvent == WSA_INVALID_EVENT)
    {
        signalResult(false);
        PublishEvent(StatusEvent(NetworkStatus::ConnectFailed, "Network event creation failed."));
        if (receiveEvent != WSA_INVALID_EVENT)
        {
            WSACloseEvent(receiveEvent);
        }
        if (sendEvent != WSA_INVALID_EVENT)
        {
            WSACloseEvent(sendEvent);
        }
        closesocket(socketHandle);
        WSACleanup();
        return;
    }

    isConnected.store(true, std::memory_order_release);
    signalResult(true);
    bool keepRunning = PublishEvent(StatusEvent(NetworkStatus::Connected, "Connected to the server."));

    chat::protocol::StreamingDecoder decoder;
    SerializedSendQueue sendQueue(kOutboundQueueCapacity);
    BoundedRequestTracker pendingChatRequests(kOutboundQueueCapacity);
    std::vector<std::uint8_t> receiveBuffer(8192);
    WSAOVERLAPPED receiveOverlapped{};
    WSAOVERLAPPED sendOverlapped{};
    receiveOverlapped.hEvent = receiveEvent;
    sendOverlapped.hEvent = sendEvent;
    bool receivePending = false;
    bool sendPending = false;
    bool requestedStop = false;

    auto publishMessage = [this, &pendingChatRequests](const chat::protocol::Message& message)
    {
        NetworkEvent event;
        event.kind = NetworkEvent::Kind::Packet;

        switch (message.type)
        {
        case chat::protocol::MessageType::LoginSucceeded:
            event.packet.type = PACKET_TYPE_LOGIN_SUCCESS;
            event.packet.message = "Login/Register successful";
            break;
        case chat::protocol::MessageType::LoginFailed:
            event.packet.type = PACKET_TYPE_LOGIN_FAILED;
            event.packet.message = "Login/Register failed";
            break;
        case chat::protocol::MessageType::RegisterSucceeded:
            event.packet.type = PACKET_TYPE_REGISTER_SUCCESS;
            event.packet.message = "Login/Register successful";
            break;
        case chat::protocol::MessageType::RegisterFailed:
            event.packet.type = PACKET_TYPE_REGISTER_FAILED;
            event.packet.message = "Login/Register failed";
            break;
        case chat::protocol::MessageType::ChatDelivered:
            event.packet.type = PACKET_TYPE_CHAT;
            event.packet.sender = message.fields[0];
            event.packet.message = message.fields[1];
            event.packet.isMine = message.requestId != 0 && pendingChatRequests.Consume(message.requestId);
            break;
        default:
            return false;
        }

        return PublishEvent(std::move(event));
    };

    auto processReceivedBytes = [this, &decoder, &publishMessage](const std::uint8_t* bytes, std::size_t size)
    {
        const auto result = decoder.Push(bytes, size);
        if (result.error != chat::protocol::CodecError::None)
        {
            PublishEvent(StatusEvent(NetworkStatus::ProtocolError, "The server sent an invalid protocol frame."));
            return false;
        }

        for (const auto& message : result.messages)
        {
            if (message.type == chat::protocol::MessageType::LoginRequest ||
                message.type == chat::protocol::MessageType::RegisterRequest ||
                message.type == chat::protocol::MessageType::ChatSend)
            {
                PublishEvent(StatusEvent(NetworkStatus::ProtocolError, "The server sent an unexpected message type."));
                return false;
            }
            if (!publishMessage(message))
            {
                return false;
            }
        }
        return true;
    };

    auto postReceive = [&]()
    {
        while (true)
        {
            WSAResetEvent(receiveEvent);
            ZeroMemory(&receiveOverlapped, sizeof(receiveOverlapped));
            receiveOverlapped.hEvent = receiveEvent;
            WSABUF buffer{};
            buffer.buf = reinterpret_cast<char*>(receiveBuffer.data());
            buffer.len = static_cast<ULONG>(receiveBuffer.size());
            DWORD receivedBytes = 0;
            DWORD flags = 0;
            const int result = WSARecv(
                socketHandle,
                &buffer,
                1,
                &receivedBytes,
                &flags,
                &receiveOverlapped,
                nullptr);
            if (result == 0)
            {
                if (receivedBytes == 0 || !processReceivedBytes(receiveBuffer.data(), receivedBytes))
                {
                    return false;
                }
                continue;
            }
            if (WSAGetLastError() != WSA_IO_PENDING)
            {
                return false;
            }

            receivePending = true;
            return true;
        }
    };

    auto drainCommands = [&]()
    {
        for (auto& command : outboundCommands.Drain())
        {
            auto encoded = chat::protocol::EncodeMessage(command);
            if (encoded.error != chat::protocol::CodecError::None)
            {
                PublishEvent(StatusEvent(NetworkStatus::ProtocolError, "The outbound command was rejected."));
                continue;
            }
            const bool tracksChatDelivery = command.type == chat::protocol::MessageType::ChatSend;
            if (tracksChatDelivery && !pendingChatRequests.TryTrack(command.requestId))
            {
                PublishEvent(StatusEvent(
                    NetworkStatus::QueueFull,
                    "Too many chat messages are awaiting delivery."));
                return false;
            }
            if (!sendQueue.TryPush(std::move(encoded.bytes)))
            {
                if (tracksChatDelivery)
                {
                    pendingChatRequests.Consume(command.requestId);
                }
                PublishEvent(StatusEvent(NetworkStatus::QueueFull, "The outbound queue is full."));
                return false;
            }
        }
        return true;
    };

    auto startSend = [&]()
    {
        while (!sendPending && !sendQueue.Empty())
        {
            const ByteView bytes = sendQueue.Current();
            WSAResetEvent(sendEvent);
            ZeroMemory(&sendOverlapped, sizeof(sendOverlapped));
            sendOverlapped.hEvent = sendEvent;
            WSABUF buffer{};
            buffer.buf = reinterpret_cast<char*>(const_cast<std::uint8_t*>(bytes.data));
            buffer.len = static_cast<ULONG>(bytes.size);
            DWORD sentBytes = 0;
            const int result = WSASend(
                socketHandle,
                &buffer,
                1,
                &sentBytes,
                0,
                &sendOverlapped,
                nullptr);
            if (result == 0)
            {
                if (sentBytes == 0 || !sendQueue.Consume(sentBytes))
                {
                    return false;
                }
                continue;
            }
            if (WSAGetLastError() != WSA_IO_PENDING)
            {
                return false;
            }

            sendPending = true;
        }
        return true;
    };

    if (keepRunning)
    {
        keepRunning = postReceive();
    }

    while (keepRunning)
    {
        if (!drainCommands() || !startSend())
        {
            break;
        }

        const HANDLE waitHandles[] = { stopEvent, wakeEvent, receiveEvent, sendEvent };
        const DWORD waitResult = WaitForMultipleObjects(4, waitHandles, FALSE, INFINITE);
        if (waitResult == WAIT_OBJECT_0)
        {
            requestedStop = true;
            break;
        }
        if (waitResult == WAIT_OBJECT_0 + 1)
        {
            continue;
        }
        if (waitResult == WAIT_OBJECT_0 + 2)
        {
            if (!receivePending)
            {
                continue;
            }

            DWORD receivedBytes = 0;
            DWORD flags = 0;
            receivePending = false;
            if (!WSAGetOverlappedResult(
                    socketHandle,
                    &receiveOverlapped,
                    &receivedBytes,
                    FALSE,
                    &flags) ||
                receivedBytes == 0 ||
                !processReceivedBytes(receiveBuffer.data(), receivedBytes))
            {
                break;
            }
            if (!postReceive())
            {
                break;
            }
            continue;
        }
        if (waitResult == WAIT_OBJECT_0 + 3)
        {
            if (!sendPending)
            {
                continue;
            }

            DWORD sentBytes = 0;
            DWORD flags = 0;
            sendPending = false;
            if (!WSAGetOverlappedResult(
                    socketHandle,
                    &sendOverlapped,
                    &sentBytes,
                    FALSE,
                    &flags) ||
                sentBytes == 0 ||
                !sendQueue.Consume(sentBytes))
            {
                break;
            }
            continue;
        }

        break;
    }

    isConnected.store(false, std::memory_order_release);
    if (receivePending)
    {
        CancelIoEx(reinterpret_cast<HANDLE>(socketHandle), &receiveOverlapped);
        DWORD ignoredBytes = 0;
        DWORD ignoredFlags = 0;
        WSAGetOverlappedResult(
            socketHandle,
            &receiveOverlapped,
            &ignoredBytes,
            TRUE,
            &ignoredFlags);
    }
    if (sendPending)
    {
        CancelIoEx(reinterpret_cast<HANDLE>(socketHandle), &sendOverlapped);
        DWORD ignoredBytes = 0;
        DWORD ignoredFlags = 0;
        WSAGetOverlappedResult(
            socketHandle,
            &sendOverlapped,
            &ignoredBytes,
            TRUE,
            &ignoredFlags);
    }

    shutdown(socketHandle, SD_BOTH);
    closesocket(socketHandle);
    WSACloseEvent(receiveEvent);
    WSACloseEvent(sendEvent);
    WSACleanup();

    if (requestedStop)
    {
        PublishEvent(StatusEvent(NetworkStatus::Disconnected, "Disconnected from the server."));
    }
    else
    {
        PublishEvent(StatusEvent(NetworkStatus::Disconnected, "The server closed the connection."));
    }
}

void NetworkManager::SignalConnectionResult(bool succeeded)
{
    isConnecting.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(connectionResultMutex);
        connectionResult = succeeded;
        connectionResultReady = true;
    }
    connectionResultCondition.notify_all();
}

bool NetworkManager::PublishEvent(NetworkEvent event)
{
    return inboundEvents.TryPush(std::move(event));
}

void NetworkManager::StopWorkerLocked()
{
    if (stopEvent != nullptr)
    {
        SetEvent(stopEvent);
    }
    if (wakeEvent != nullptr)
    {
        SetEvent(wakeEvent);
    }
    if (networkThread.joinable())
    {
        networkThread.join();
    }

    isConnected.store(false, std::memory_order_release);
    isConnecting.store(false, std::memory_order_release);
    outboundCommands.Clear();
    if (stopEvent != nullptr)
    {
        CloseHandle(stopEvent);
        stopEvent = nullptr;
    }
    if (wakeEvent != nullptr)
    {
        CloseHandle(wakeEvent);
        wakeEvent = nullptr;
    }
}
