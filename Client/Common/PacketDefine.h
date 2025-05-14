#pragma once
#include <cstdint>

struct PacketHeader
{
    uint16_t size;
    uint16_t type;
};

enum PacketType : uint16_t
{
    PACKET_CHAT = 1,
    PACKET_LOGIN,
    PACKET_LOGIN_OK,
    PACKET_LOGIN_FAIL,
    PACKET_RANK,
    PACKET_RANK_RESULT
};