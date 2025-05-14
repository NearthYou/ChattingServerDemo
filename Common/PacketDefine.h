#pragma once
#include <string>

enum PacketType
{
    PACKET_TYPE_LOGIN,
    PACKET_TYPE_REGISTER,
    PACKET_TYPE_CHAT,
    PACKET_TYPE_LOGIN_SUCCESS,
    PACKET_TYPE_LOGIN_FAILED,
    PACKET_TYPE_REGISTER_SUCCESS,
    PACKET_TYPE_REGISTER_FAILED
};

struct ChatPacket
{
    PacketType type;
    std::string sender;
    std::string message;
    bool isMine;
}; 