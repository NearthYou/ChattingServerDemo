#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <WinSock2.h>

class Server
{
public:
    bool Init(int port);
    void Run();

private:
    void AcceptLoop();
    void ClientLoop(SOCKET clientSocket);

private:
    SOCKET listenSocket;
    std::vector<SOCKET> clients;
    std::unordered_map<SOCKET, std::string> nicknames; // 소켓별 닉네임
    std::mutex clientsMutex;
};
