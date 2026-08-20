#include "ClientPacketReducer.h"

void ClientPacketReducer::Apply(const ChatPacket& packet)
{
    switch (packet.type)
    {
    case PACKET_TYPE_LOGIN_SUCCESS:
        loggedIn = true;
        chatMessages.clear();
        break;
    case PACKET_TYPE_CHAT:
        AppendChat(packet.sender, packet.message, packet.isMine);
        break;
    case PACKET_TYPE_LOGIN:
    case PACKET_TYPE_REGISTER:
    case PACKET_TYPE_LOGIN_FAILED:
    case PACKET_TYPE_REGISTER_SUCCESS:
    case PACKET_TYPE_REGISTER_FAILED:
        break;
    }
}

void ClientPacketReducer::AppendChat(
    const std::string& sender,
    const std::string& message,
    bool isMine)
{
    chatMessages.push_back({ sender, message, isMine });
}

void ClientPacketReducer::Disconnect()
{
    loggedIn = false;
}

bool ClientPacketReducer::IsLoggedIn() const
{
    return loggedIn;
}

std::size_t ClientPacketReducer::ChatCount() const
{
    return chatMessages.size();
}

const std::vector<ClientChatMessage>& ClientPacketReducer::ChatMessages() const
{
    return chatMessages;
}
