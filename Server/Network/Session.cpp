#include "Session.h"

#include <utility>

Session::Session(SOCKET socketHandle, std::size_t maxQueuedSendBytes)
    : socketId(socketHandle),
      socket(socketHandle),
      maxQueuedBytes(maxQueuedSendBytes)
{
}

Session::~Session()
{
    Close();
}

SOCKET Session::Id() const
{
    return socketId;
}

SOCKET Session::Socket() const
{
    return socket.load();
}

bool Session::IsClosed() const
{
    return closed.load();
}

bool Session::Close()
{
    bool expected = false;
    if (!closed.compare_exchange_strong(expected, true))
    {
        return false;
    }

    const SOCKET socketHandle = socket.exchange(INVALID_SOCKET);
    if (socketHandle != INVALID_SOCKET)
    {
        shutdown(socketHandle, SD_BOTH);
        closesocket(socketHandle);
    }

    std::lock_guard<std::mutex> lock(sendMutex);
    sendQueue.clear();
    queuedSendBytes = inFlightSendBytes;
    return true;
}

bool Session::TryBeginReceive()
{
    if (closed.load())
    {
        return false;
    }

    bool expected = false;
    return receivePending.compare_exchange_strong(expected, true);
}

void Session::EndReceive()
{
    receivePending.store(false);
}

chat::protocol::DecodeResult Session::Decode(const std::uint8_t* bytes, std::size_t size)
{
    return decoder.Push(bytes, size);
}

bool Session::IsAuthenticated() const
{
    std::lock_guard<std::mutex> lock(authenticationMutex);
    return !authenticatedUsername.empty();
}

bool Session::TrySetAuthenticated(const std::string& username)
{
    std::lock_guard<std::mutex> lock(authenticationMutex);
    if (closed.load() || !authenticatedUsername.empty() || username.empty())
    {
        return false;
    }

    authenticatedUsername = username;
    return true;
}

std::string Session::Username() const
{
    std::lock_guard<std::mutex> lock(authenticationMutex);
    return authenticatedUsername;
}

Session::EnqueueResult Session::EnqueueSend(
    std::vector<std::uint8_t> frame,
    std::vector<std::uint8_t>& firstFrame)
{
    std::lock_guard<std::mutex> lock(sendMutex);
    if (closed.load())
    {
        return EnqueueResult::Closed;
    }
    if (frame.size() > maxQueuedBytes || queuedSendBytes > maxQueuedBytes - frame.size())
    {
        return EnqueueResult::Overflow;
    }

    queuedSendBytes += frame.size();
    if (!sendInProgress)
    {
        sendInProgress = true;
        inFlightSendBytes = frame.size();
        firstFrame = std::move(frame);
        return EnqueueResult::Start;
    }

    sendQueue.push_back(std::move(frame));
    return EnqueueResult::Queued;
}

bool Session::CompleteSend(std::vector<std::uint8_t>& nextFrame)
{
    std::lock_guard<std::mutex> lock(sendMutex);
    if (queuedSendBytes >= inFlightSendBytes)
    {
        queuedSendBytes -= inFlightSendBytes;
    }
    else
    {
        queuedSendBytes = 0;
    }
    inFlightSendBytes = 0;

    if (closed.load())
    {
        sendQueue.clear();
        queuedSendBytes = 0;
        sendInProgress = false;
        return false;
    }
    if (sendQueue.empty())
    {
        sendInProgress = false;
        return false;
    }

    nextFrame = std::move(sendQueue.front());
    sendQueue.pop_front();
    inFlightSendBytes = nextFrame.size();
    return true;
}

bool Session::AdvanceSendOffset(
    std::size_t totalBytes,
    std::size_t bytesTransferred,
    std::size_t& offset)
{
    if (bytesTransferred == 0 || offset > totalBytes || bytesTransferred > totalBytes - offset)
    {
        return false;
    }

    offset += bytesTransferred;
    return true;
}
