// NetworkManager.h
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <Ws2tcpip.h>
#include <Windows.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <iostream>

#pragma comment(lib, "ws2_32.lib")

struct ChatPacket
{
    std::string Sender;
    std::string Message;
    bool IsMine;
};

class NetworkManager
{
public:
    bool Init();
    void Shutdown();
    void SendLogin(const std::string& nickname);
    void SendChatMessage(const std::string& message);
    std::vector<ChatPacket> GetPendingMessages();

    void SetNickname(const std::string& nickname) { Nickname = nickname; }
    const std::string& GetNickname() const { return Nickname; }

private:
    void RecvLoop();

private:
    SOCKET ClientSocket = INVALID_SOCKET;
    bool Running = false;
    std::thread RecvThread;

    std::string Nickname;

    std::mutex Mutex;
    std::vector<ChatPacket> PendingMessages;
};