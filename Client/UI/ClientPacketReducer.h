#pragma once

#include "../../Common/PacketDefine.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct ClientChatMessage
{
    std::string sender;
    std::string text;
    bool isMine = false;
    std::int64_t timestampMilliseconds = 0;
};

class ClientPacketReducer
{
public:
    void BeginLogin(const std::string& username);
    void Apply(const ChatPacket& packet);
    void AppendChat(
        const std::string& sender,
        const std::string& message,
        bool isMine,
        std::int64_t timestampMilliseconds = 0);
    void Disconnect();

    bool IsLoggedIn() const;
    std::size_t ChatCount() const;
    const std::vector<ClientChatMessage>& ChatMessages() const;

private:
    bool loggedIn = false;
    std::string pendingUsername;
    std::string currentUsername;
    std::vector<ClientChatMessage> chatMessages;
};
