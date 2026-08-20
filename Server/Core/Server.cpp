#include "Server.h"

#include "../Application/IChatService.h"
#include "../Application/DatabaseExecutor.h"
#include "../Application/InputValidation.h"
#include "../Network/Session.h"

#include <WinSock2.h>
#include <MSWSock.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    constexpr std::size_t kReceiveBufferBytes = 16 * 1024;
    constexpr DWORD kAcceptAddressBytes = sizeof(sockaddr_in) + 16;

    enum class IoOperationType
    {
        Accept,
        Receive,
        Send
    };

    struct IoOperation
    {
        OVERLAPPED overlapped{};
        IoOperationType type;
        std::vector<std::uint8_t> buffer;
        WSABUF socketBuffer{};
        std::shared_ptr<Session> session;
        std::size_t offset = 0;

        IoOperation(
            IoOperationType operationType,
            std::shared_ptr<Session> operationSession,
            std::size_t bufferBytes)
            : type(operationType),
              buffer(bufferBytes),
              session(std::move(operationSession))
        {
        }
    };
}

class Server::Impl
{
public:
    Impl(IChatService& chatService, ServerOptions serverOptions)
        : options(serverOptions),
          databaseExecutor(chatService, serverOptions.maxQueuedDatabaseJobs)
    {
        options.acceptPrepostCount = std::max<std::size_t>(1, options.acceptPrepostCount);
        options.maxQueuedSendBytes = std::max<std::size_t>(
            chat::protocol::kFrameHeaderBytes + 8,
            options.maxQueuedSendBytes);
        options.maxQueuedDatabaseJobs = std::max<std::size_t>(1, options.maxQueuedDatabaseJobs);
    }

    ~Impl()
    {
        Stop();
    }

    bool Initialize(std::uint16_t requestedPort)
    {
        if (started.load())
        {
            return false;
        }

        started.store(true);
        stopping.store(false);
        if (!databaseExecutor.Start())
        {
            started.store(false);
            return false;
        }

        WSADATA wsaData{};
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            Stop();
            return false;
        }
        wsaStarted = true;

        completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        operationsDrainedEvent = CreateEventW(nullptr, TRUE, TRUE, nullptr);
        stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (completionPort == nullptr || operationsDrainedEvent == nullptr || stopEvent == nullptr)
        {
            Stop();
            return false;
        }

        const SOCKET listenSocket = WSASocketW(
            AF_INET,
            SOCK_STREAM,
            IPPROTO_TCP,
            nullptr,
            0,
            WSA_FLAG_OVERLAPPED);
        if (listenSocket == INVALID_SOCKET)
        {
            Stop();
            return false;
        }
        listener.store(listenSocket);

        const BOOL exclusiveAddress = TRUE;
        if (setsockopt(
                listenSocket,
                SOL_SOCKET,
                SO_EXCLUSIVEADDRUSE,
                reinterpret_cast<const char*>(&exclusiveAddress),
                sizeof(exclusiveAddress)) == SOCKET_ERROR)
        {
            Stop();
            return false;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(requestedPort);
        if (bind(
                listenSocket,
                reinterpret_cast<const sockaddr*>(&address),
                sizeof(address)) == SOCKET_ERROR ||
            listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
        {
            Stop();
            return false;
        }

        int addressBytes = sizeof(address);
        if (getsockname(
                listenSocket,
                reinterpret_cast<sockaddr*>(&address),
                &addressBytes) == SOCKET_ERROR)
        {
            Stop();
            return false;
        }
        boundPort.store(ntohs(address.sin_port));

        if (CreateIoCompletionPort(
                reinterpret_cast<HANDLE>(listenSocket),
                completionPort,
                0,
                0) == nullptr)
        {
            Stop();
            return false;
        }

        GUID acceptExGuid = WSAID_ACCEPTEX;
        DWORD bytesReturned = 0;
        if (WSAIoctl(
                listenSocket,
                SIO_GET_EXTENSION_FUNCTION_POINTER,
                &acceptExGuid,
                sizeof(acceptExGuid),
                &acceptEx,
                sizeof(acceptEx),
                &bytesReturned,
                nullptr,
                nullptr) == SOCKET_ERROR)
        {
            Stop();
            return false;
        }

        const unsigned int hardwareThreads = std::thread::hardware_concurrency();
        const std::size_t requestedWorkers = options.workerCount == 0
            ? std::max<std::size_t>(1, hardwareThreads)
            : options.workerCount;
        configuredWorkerCount = std::min<std::size_t>(8, requestedWorkers);

        try
        {
            workers.reserve(configuredWorkerCount);
            for (std::size_t index = 0; index < configuredWorkerCount; ++index)
            {
                workers.emplace_back([this] { WorkerLoop(); });
            }
        }
        catch (...)
        {
            Stop();
            return false;
        }

        for (std::size_t index = 0; index < options.acceptPrepostCount; ++index)
        {
            if (!PostAccept())
            {
                Stop();
                return false;
            }
        }
        return true;
    }

    void Wait()
    {
        const HANDLE eventHandle = stopEvent;
        if (started.load() && eventHandle != nullptr)
        {
            WaitForSingleObject(eventHandle, INFINITE);
        }
    }

    void RequestStop()
    {
        std::lock_guard<std::mutex> lock(stopEventMutex);
        if (stopEvent != nullptr)
        {
            SetEvent(stopEvent);
        }
    }

    void Stop()
    {
        if (!started.exchange(false))
        {
            return;
        }

        stopping.store(true);
        databaseExecutor.StopAccepting();
        {
            std::lock_guard<std::mutex> lock(operationRegistrationMutex);
        }
        RequestStop();

        const SOCKET listenSocket = listener.exchange(INVALID_SOCKET);
        if (listenSocket != INVALID_SOCKET)
        {
            closesocket(listenSocket);
        }

        const auto sessionSnapshot = SnapshotSessions(false);
        for (const auto& session : sessionSnapshot)
        {
            CloseSession(session);
        }

        databaseExecutor.Stop();

        if (operationsDrainedEvent != nullptr)
        {
            WaitForSingleObject(operationsDrainedEvent, INFINITE);
        }

        if (completionPort != nullptr)
        {
            for (std::size_t index = 0; index < workers.size(); ++index)
            {
                PostQueuedCompletionStatus(completionPort, 0, 0, nullptr);
            }
        }
        for (auto& worker : workers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
        workers.clear();

        {
            std::lock_guard<std::mutex> lock(sessionsMutex);
            sessions.clear();
        }

        if (completionPort != nullptr)
        {
            CloseHandle(completionPort);
            completionPort = nullptr;
        }
        if (operationsDrainedEvent != nullptr)
        {
            CloseHandle(operationsDrainedEvent);
            operationsDrainedEvent = nullptr;
        }
        {
            std::lock_guard<std::mutex> lock(stopEventMutex);
            if (stopEvent != nullptr)
            {
                CloseHandle(stopEvent);
                stopEvent = nullptr;
            }
        }
        if (wsaStarted)
        {
            WSACleanup();
            wsaStarted = false;
        }

    }

    std::uint16_t Port() const
    {
        return boundPort.load();
    }

    ServerDiagnostics Diagnostics() const
    {
        ServerDiagnostics diagnostics;
        diagnostics.workerCount = configuredWorkerCount;
        diagnostics.pendingAccepts = pendingAccepts.load();
        {
            std::lock_guard<std::mutex> lock(sessionsMutex);
            diagnostics.activeSessions = sessions.size();
        }
        diagnostics.operationsCreated = operationsCreated.load();
        diagnostics.operationsRetired = operationsRetired.load();
        diagnostics.outstandingOperations = outstandingOperations.load();
        diagnostics.sendQueueOverflows = sendQueueOverflows.load();
        diagnostics.databaseQueueOverflows = databaseQueueOverflows.load();
        return diagnostics;
    }

private:
    IoOperation* CreateOperation(
        IoOperationType type,
        const std::shared_ptr<Session>& session,
        std::size_t bufferBytes)
    {
        std::lock_guard<std::mutex> lock(operationRegistrationMutex);
        if (stopping.load())
        {
            return nullptr;
        }

        IoOperation* operation = nullptr;
        try
        {
            operation = new IoOperation(type, session, bufferBytes);
        }
        catch (...)
        {
            return nullptr;
        }

        operationsCreated.fetch_add(1);
        if (outstandingOperations.fetch_add(1) == 0 && operationsDrainedEvent != nullptr)
        {
            ResetEvent(operationsDrainedEvent);
        }
        return operation;
    }

    void RetireOperation(IoOperation* operation)
    {
        delete operation;
        operationsRetired.fetch_add(1);
        if (outstandingOperations.fetch_sub(1) == 1 && operationsDrainedEvent != nullptr)
        {
            SetEvent(operationsDrainedEvent);
        }
    }

    bool PostAccept()
    {
        if (stopping.load() || acceptEx == nullptr)
        {
            return false;
        }

        const SOCKET listenSocket = listener.load();
        if (listenSocket == INVALID_SOCKET)
        {
            return false;
        }

        const SOCKET acceptedSocket = WSASocketW(
            AF_INET,
            SOCK_STREAM,
            IPPROTO_TCP,
            nullptr,
            0,
            WSA_FLAG_OVERLAPPED);
        if (acceptedSocket == INVALID_SOCKET)
        {
            return false;
        }

        std::shared_ptr<Session> session;
        try
        {
            session = std::make_shared<Session>(acceptedSocket, options.maxQueuedSendBytes);
        }
        catch (...)
        {
            closesocket(acceptedSocket);
            return false;
        }

        IoOperation* operation = CreateOperation(
            IoOperationType::Accept,
            session,
            2 * kAcceptAddressBytes);
        if (operation == nullptr)
        {
            session->Close();
            return false;
        }

        pendingAccepts.fetch_add(1);
        DWORD receivedBytes = 0;
        const BOOL accepted = acceptEx(
            listenSocket,
            acceptedSocket,
            operation->buffer.data(),
            0,
            kAcceptAddressBytes,
            kAcceptAddressBytes,
            &receivedBytes,
            &operation->overlapped);
        if (!accepted)
        {
            const int error = WSAGetLastError();
            if (error != WSA_IO_PENDING)
            {
                pendingAccepts.fetch_sub(1);
                session->Close();
                RetireOperation(operation);
                return false;
            }
        }
        return true;
    }

    bool PostReceive(const std::shared_ptr<Session>& session)
    {
        if (stopping.load() || !session->TryBeginReceive())
        {
            return false;
        }

        IoOperation* operation = CreateOperation(
            IoOperationType::Receive,
            session,
            kReceiveBufferBytes);
        if (operation == nullptr)
        {
            session->EndReceive();
            CloseSession(session);
            return false;
        }

        operation->socketBuffer.buf = reinterpret_cast<char*>(operation->buffer.data());
        operation->socketBuffer.len = static_cast<ULONG>(operation->buffer.size());
        DWORD flags = 0;
        DWORD receivedBytes = 0;
        const int result = WSARecv(
            session->Socket(),
            &operation->socketBuffer,
            1,
            &receivedBytes,
            &flags,
            &operation->overlapped,
            nullptr);
        if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
        {
            session->EndReceive();
            RetireOperation(operation);
            CloseSession(session);
            return false;
        }
        return true;
    }

    bool SubmitSend(IoOperation* operation)
    {
        if (stopping.load() || operation->session->IsClosed() ||
            operation->offset >= operation->buffer.size())
        {
            return false;
        }

        const std::size_t remainingBytes = operation->buffer.size() - operation->offset;
        if (remainingBytes > (std::numeric_limits<ULONG>::max)())
        {
            return false;
        }

        operation->socketBuffer.buf = reinterpret_cast<char*>(
            operation->buffer.data() + operation->offset);
        operation->socketBuffer.len = static_cast<ULONG>(remainingBytes);
        DWORD sentBytes = 0;
        const int result = WSASend(
            operation->session->Socket(),
            &operation->socketBuffer,
            1,
            &sentBytes,
            0,
            &operation->overlapped,
            nullptr);
        return result == 0 || WSAGetLastError() == WSA_IO_PENDING;
    }

    bool SendMessage(
        const std::shared_ptr<Session>& session,
        const chat::protocol::Message& message)
    {
        if (stopping.load() || session->IsClosed())
        {
            return false;
        }

        auto encoded = chat::protocol::EncodeMessage(message);
        if (encoded.error != chat::protocol::CodecError::None)
        {
            CloseSession(session);
            return false;
        }

        std::vector<std::uint8_t> firstFrame;
        const Session::EnqueueResult enqueueResult = session->EnqueueSend(
            std::move(encoded.bytes),
            firstFrame);
        if (enqueueResult == Session::EnqueueResult::Queued)
        {
            return true;
        }
        if (enqueueResult == Session::EnqueueResult::Overflow)
        {
            sendQueueOverflows.fetch_add(1);
            CloseSession(session);
            return false;
        }
        if (enqueueResult == Session::EnqueueResult::Closed)
        {
            return false;
        }

        IoOperation* operation = CreateOperation(IoOperationType::Send, session, 0);
        if (operation == nullptr)
        {
            CloseSession(session);
            return false;
        }
        operation->buffer = std::move(firstFrame);
        if (!SubmitSend(operation))
        {
            RetireOperation(operation);
            CloseSession(session);
            return false;
        }
        return true;
    }

    void WorkerLoop()
    {
        for (;;)
        {
            DWORD bytesTransferred = 0;
            ULONG_PTR completionKey = 0;
            OVERLAPPED* overlapped = nullptr;
            const BOOL succeeded = GetQueuedCompletionStatus(
                completionPort,
                &bytesTransferred,
                &completionKey,
                &overlapped,
                INFINITE);
            static_cast<void>(completionKey);

            if (overlapped == nullptr)
            {
                if (stopping.load())
                {
                    return;
                }
                continue;
            }

            auto* operation = reinterpret_cast<IoOperation*>(overlapped);
            bool requeued = false;
            switch (operation->type)
            {
            case IoOperationType::Accept:
                HandleAccept(operation, succeeded != FALSE);
                break;
            case IoOperationType::Receive:
                HandleReceive(operation, succeeded != FALSE, bytesTransferred);
                break;
            case IoOperationType::Send:
                requeued = HandleSend(operation, succeeded != FALSE, bytesTransferred);
                break;
            }

            if (!requeued)
            {
                RetireOperation(operation);
            }
        }
    }

    void HandleAccept(IoOperation* operation, bool succeeded)
    {
        pendingAccepts.fetch_sub(1);
        const auto session = operation->session;
        bool accepted = succeeded && !stopping.load();
        const SOCKET listenSocket = listener.load();
        const SOCKET acceptedSocket = session->Socket();

        if (accepted)
        {
            accepted = listenSocket != INVALID_SOCKET && acceptedSocket != INVALID_SOCKET &&
                setsockopt(
                    acceptedSocket,
                    SOL_SOCKET,
                    SO_UPDATE_ACCEPT_CONTEXT,
                    reinterpret_cast<const char*>(&listenSocket),
                    sizeof(listenSocket)) != SOCKET_ERROR;
        }
        if (accepted)
        {
            accepted = CreateIoCompletionPort(
                reinterpret_cast<HANDLE>(acceptedSocket),
                completionPort,
                0,
                0) != nullptr;
        }
        if (accepted)
        {
            std::lock_guard<std::mutex> lock(sessionsMutex);
            if (stopping.load())
            {
                accepted = false;
            }
            else
            {
                sessions[session->Id()] = session;
            }
        }

        if (!accepted)
        {
            CloseSession(session);
        }
        else
        {
            PostReceive(session);
        }

        if (!stopping.load() && !PostAccept() && !stopping.load())
        {
            RequestStop();
        }
    }

    void HandleReceive(IoOperation* operation, bool succeeded, DWORD bytesTransferred)
    {
        const auto session = operation->session;
        session->EndReceive();
        if (!succeeded || bytesTransferred == 0 || stopping.load() || session->IsClosed())
        {
            CloseSession(session);
            return;
        }

        auto decoded = session->Decode(operation->buffer.data(), bytesTransferred);
        if (decoded.error != chat::protocol::CodecError::None)
        {
            CloseSession(session);
            return;
        }

        for (const auto& message : decoded.messages)
        {
            if (!QueueMessage(session, message))
            {
                return;
            }
        }

        if (!stopping.load() && !session->IsClosed())
        {
            PostReceive(session);
        }
    }

    bool HandleSend(IoOperation* operation, bool succeeded, DWORD bytesTransferred)
    {
        const auto session = operation->session;
        if (!succeeded || stopping.load() || session->IsClosed() ||
            !Session::AdvanceSendOffset(
                operation->buffer.size(),
                bytesTransferred,
                operation->offset))
        {
            CloseSession(session);
            return false;
        }

        if (operation->offset < operation->buffer.size())
        {
            ZeroMemory(&operation->overlapped, sizeof(operation->overlapped));
            if (SubmitSend(operation))
            {
                return true;
            }
            CloseSession(session);
            return false;
        }

        std::vector<std::uint8_t> nextFrame;
        if (!session->CompleteSend(nextFrame))
        {
            return false;
        }

        operation->buffer = std::move(nextFrame);
        operation->offset = 0;
        ZeroMemory(&operation->overlapped, sizeof(operation->overlapped));
        if (SubmitSend(operation))
        {
            return true;
        }

        CloseSession(session);
        return false;
    }

    bool QueueMessage(
        const std::shared_ptr<Session>& session,
        const chat::protocol::Message& message)
    {
        if (stopping.load() || session->IsClosed())
        {
            return false;
        }

        if (!databaseExecutor.TrySubmit(
                [this, session, message](IChatService& executorService) {
                    ProcessMessage(session, message, executorService);
                }))
        {
            databaseQueueOverflows.fetch_add(1);
            CloseSession(session);
            return false;
        }
        return true;
    }

    void ProcessMessage(
        const std::shared_ptr<Session>& session,
        const chat::protocol::Message& message,
        IChatService& executorService)
    {
        using chat::protocol::Message;
        using chat::protocol::MessageType;

        switch (message.type)
        {
        case MessageType::RegisterRequest:
        {
            ChatServiceStatus status = ChatServiceStatus::Rejected;
            if (!chat::validation::IsValidUsername(message.fields[0]) ||
                !chat::validation::IsValidPassword(message.fields[1]))
            {
                SendMessage(session, { MessageType::RegisterFailed, message.requestId, {} });
                return;
            }
            try
            {
                status = executorService.RegisterUser(message.fields[0], message.fields[1]);
            }
            catch (...)
            {
                status = ChatServiceStatus::Unavailable;
            }
            SendMessage(session, {
                status == ChatServiceStatus::Succeeded
                    ? MessageType::RegisterSucceeded
                    : MessageType::RegisterFailed,
                message.requestId,
                {}
            });
            return;
        }
        case MessageType::LoginRequest:
        {
            if (session->IsAuthenticated() ||
                !chat::validation::IsValidUsername(message.fields[0]) ||
                !chat::validation::IsValidPassword(message.fields[1]))
            {
                SendMessage(session, { MessageType::LoginFailed, message.requestId, {} });
                return;
            }

            LoginResult result;
            try
            {
                result = executorService.Login(message.fields[0], message.fields[1], 50);
            }
            catch (...)
            {
                result.status = ChatServiceStatus::Unavailable;
            }

            const bool authenticated = result.status == ChatServiceStatus::Succeeded &&
                session->TrySetAuthenticated(message.fields[0]);
            if (!authenticated)
            {
                SendMessage(session, { MessageType::LoginFailed, message.requestId, {} });
                return;
            }

            if (!SendMessage(session, { MessageType::LoginSucceeded, message.requestId, {} }))
            {
                return;
            }
            const std::size_t historyCount = std::min<std::size_t>(50, result.history.size());
            for (std::size_t index = 0; index < historyCount; ++index)
            {
                if (!SendMessage(session, {
                        MessageType::ChatDelivered,
                        0,
                        { result.history[index].username, result.history[index].message }
                    }))
                {
                    return;
                }
            }
            return;
        }
        case MessageType::ChatSend:
        {
            if (!session->IsAuthenticated() ||
                !chat::validation::IsValidMessage(message.fields[0]))
            {
                CloseSession(session);
                return;
            }

            const std::string username = session->Username();
            ChatServiceStatus status = ChatServiceStatus::Unavailable;
            try
            {
                status = executorService.StoreMessage(username, message.fields[0]);
            }
            catch (...)
            {
                status = ChatServiceStatus::Unavailable;
            }
            if (status != ChatServiceStatus::Succeeded || session->IsClosed() || stopping.load())
            {
                CloseSession(session);
                return;
            }

            const auto recipients = SnapshotSessions(true);
            for (const auto& recipient : recipients)
            {
                const std::uint32_t requestId = recipient == session ? message.requestId : 0;
                SendMessage(recipient, {
                    MessageType::ChatDelivered,
                    requestId,
                    { username, message.fields[0] }
                });
            }
            return;
        }
        default:
            CloseSession(session);
            return;
        }
    }

    std::vector<std::shared_ptr<Session>> SnapshotSessions(bool authenticatedOnly) const
    {
        std::vector<std::shared_ptr<Session>> snapshot;
        {
            std::lock_guard<std::mutex> lock(sessionsMutex);
            snapshot.reserve(sessions.size());
            for (const auto& entry : sessions)
            {
                snapshot.push_back(entry.second);
            }
        }

        if (authenticatedOnly)
        {
            snapshot.erase(
                std::remove_if(
                    snapshot.begin(),
                    snapshot.end(),
                    [](const std::shared_ptr<Session>& session) {
                        return session->IsClosed() || !session->IsAuthenticated();
                    }),
                snapshot.end());
        }
        return snapshot;
    }

    void CloseSession(const std::shared_ptr<Session>& session)
    {
        session->Close();
        std::lock_guard<std::mutex> lock(sessionsMutex);
        const auto found = sessions.find(session->Id());
        if (found != sessions.end() && found->second == session)
        {
            sessions.erase(found);
        }
    }

    ServerOptions options;
    DatabaseExecutor databaseExecutor;

    std::atomic<bool> stopping{ false };
    std::atomic<bool> started{ false };
    bool wsaStarted = false;
    std::atomic<SOCKET> listener{ INVALID_SOCKET };
    HANDLE completionPort = nullptr;
    HANDLE operationsDrainedEvent = nullptr;
    HANDLE stopEvent = nullptr;
    std::mutex stopEventMutex;
    LPFN_ACCEPTEX acceptEx = nullptr;
    std::atomic<std::uint16_t> boundPort{ 0 };
    std::size_t configuredWorkerCount = 0;
    std::vector<std::thread> workers;

    std::mutex operationRegistrationMutex;

    mutable std::mutex sessionsMutex;
    std::unordered_map<SOCKET, std::shared_ptr<Session>> sessions;

    std::atomic<std::uint64_t> operationsCreated{ 0 };
    std::atomic<std::uint64_t> operationsRetired{ 0 };
    std::atomic<std::uint64_t> outstandingOperations{ 0 };
    std::atomic<std::size_t> pendingAccepts{ 0 };
    std::atomic<std::uint64_t> sendQueueOverflows{ 0 };
    std::atomic<std::uint64_t> databaseQueueOverflows{ 0 };
};

Server::Server(IChatService& service, ServerOptions options)
    : impl(std::make_unique<Impl>(service, options))
{
}

Server::~Server() = default;

bool Server::Init(std::uint16_t port)
{
    return impl->Initialize(port);
}

void Server::Run()
{
    impl->Wait();
}

void Server::RequestStop()
{
    impl->RequestStop();
}

void Server::Shutdown()
{
    impl->Stop();
}

std::uint16_t Server::GetBoundPort() const
{
    return impl->Port();
}

ServerDiagnostics Server::GetDiagnostics() const
{
    return impl->Diagnostics();
}
