#pragma once
#include <WinSock2.h>
#include <string>
#include <memory>
#include "../../Common/PacketDefine.h"

class Session
{
public:
    Session(SOCKET socket);
    ~Session();

    bool Send(const std::string& message);
    bool Send(const ChatPacket& packet);
    bool IsConnected() const { return isConnected; }
    void Disconnect();
    SOCKET GetSocket() const { return socket; }

private:
    SOCKET socket;
    bool isConnected;
    std::string username;
}; 