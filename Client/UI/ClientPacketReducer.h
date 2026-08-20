#pragma once

#include "../../Common/PacketDefine.h"

#include <cstddef>
#include <string>
#include <vector>

struct ClientChatMessage
{
    std::string sender;
    std::string text;
    bool isMine = false;
};

class ClientPacketReducer
{
public:
    void Apply(const ChatPacket& packet);
    void AppendChat(const std::string& sender, const std::string& message, bool isMine);
    void Disconnect();

    bool IsLoggedIn() const;
    std::size_t ChatCount() const;
    const std::vector<ClientChatMessage>& ChatMessages() const;

private:
    bool loggedIn = false;
    std::vector<ClientChatMessage> chatMessages;
};
