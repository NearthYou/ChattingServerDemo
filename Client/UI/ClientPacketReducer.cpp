#include "ClientPacketReducer.h"

void ClientPacketReducer::BeginLogin(const std::string& username)
{
    pendingUsername = username;
}

void ClientPacketReducer::Apply(const ChatPacket& packet)
{
    switch (packet.type)
    {
    case PACKET_TYPE_LOGIN_SUCCESS:
        loggedIn = true;
        currentUsername = pendingUsername;
        pendingUsername.clear();
        chatMessages.clear();
        break;
    case PACKET_TYPE_CHAT:
        AppendChat(
            packet.sender,
            packet.message,
            packet.isMine || (!currentUsername.empty() && packet.sender == currentUsername));
        break;
    case PACKET_TYPE_LOGIN:
    case PACKET_TYPE_REGISTER:
    case PACKET_TYPE_REGISTER_SUCCESS:
    case PACKET_TYPE_REGISTER_FAILED:
        break;
    case PACKET_TYPE_LOGIN_FAILED:
        pendingUsername.clear();
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
    pendingUsername.clear();
    currentUsername.clear();
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
