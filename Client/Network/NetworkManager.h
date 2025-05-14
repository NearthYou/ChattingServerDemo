// NetworkManager.h
#pragma once
#include <WinSock2.h>
#include <string>
#include <thread>
#include <mutex>
#include <vector>
#include "../../Common/PacketDefine.h"

class NetworkManager
{
public:
    NetworkManager();
    ~NetworkManager();

    bool Connect(const std::string& address, int port);
    void Disconnect();
    bool IsConnected() const;

    bool SendLoginRequest(const std::string& username, const std::string& password);
    bool SendRegisterRequest(const std::string& username, const std::string& password);
    bool SendChatMessage(const std::string& username, const std::string& message);
    std::vector<ChatPacket> GetPendingMessages();

private:
    void ReceiveLoop();

    SOCKET clientSocket;
    bool isConnected;
    std::thread receiveThread;
    std::vector<ChatPacket> pendingMessages;
    std::mutex messagesMutex;
};