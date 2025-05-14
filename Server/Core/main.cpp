#include "Server.h"
#include <iostream>
#include <windows.h>

int main()
{
    int port = 8888;
    std::cout << "Enter server port (default: 8888): ";
    std::string inputPort;
    std::getline(std::cin, inputPort);
    if (!inputPort.empty()) port = std::stoi(inputPort);

    Server server;
    if (!server.Init(port))
        return 1;
    server.Run();
    server.Shutdown();
    return 0;
}