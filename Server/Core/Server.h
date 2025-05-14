#pragma once

#include <WinSock2.h>
#include <string>
#include <thread>
#include <vector>
#include "../Network/Session.h"

class Server
{
public:
    Server();
    ~Server();

    bool Init(int port = 8888);
    void Run();
    void Shutdown();

private:
    void ClientLoop(SOCKET clientSocket);

    SOCKET serverSocket;
    bool isRunning;
    int listenPort;
    std::vector<SOCKET> connectedClients;
    std::vector<std::shared_ptr<Session>> sessions;
};
