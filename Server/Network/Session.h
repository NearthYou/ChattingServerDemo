#pragma once

#include <WinSock2.h>

#include "../../Common/Protocol/ChatProtocol.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

class Session
{
public:
    enum class EnqueueResult
    {
        Start,
        Queued,
        Closed,
        Overflow
    };

    Session(SOCKET socketHandle, std::size_t maxQueuedSendBytes);
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    SOCKET Id() const;
    SOCKET Socket() const;
    bool IsClosed() const;
    bool Close();

    bool TryBeginReceive();
    void EndReceive();
    chat::protocol::DecodeResult Decode(const std::uint8_t* bytes, std::size_t size);

    bool IsAuthenticated() const;
    bool TrySetAuthenticated(const std::string& username);
    std::string Username() const;

    EnqueueResult EnqueueSend(
        std::vector<std::uint8_t> frame,
        std::vector<std::uint8_t>& firstFrame);
    bool CompleteSend(std::vector<std::uint8_t>& nextFrame);

    static bool AdvanceSendOffset(
        std::size_t totalBytes,
        std::size_t bytesTransferred,
        std::size_t& offset);

private:
    const SOCKET socketId;
    std::atomic<SOCKET> socket;
    std::atomic<bool> closed{ false };
    std::atomic<bool> receivePending{ false };
    const std::size_t maxQueuedBytes;

    chat::protocol::StreamingDecoder decoder;

    mutable std::mutex authenticationMutex;
    std::string authenticatedUsername;

    std::mutex sendMutex;
    std::deque<std::vector<std::uint8_t>> sendQueue;
    std::size_t queuedSendBytes = 0;
    std::size_t inFlightSendBytes = 0;
    bool sendInProgress = false;
};
