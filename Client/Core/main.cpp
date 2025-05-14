#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <iostream>
#include "../UI/Application.h"
#include "../../Common/Utils/Logger.h"

int main()
{
    Logger::GetInstance().SetLogFile("client.log");
    Logger::GetInstance().Log("클라이언트 시작");

    std::string serverIP = "127.0.0.1";
    int serverPort = 8888;

    std::cout << "Enter server IP (default: 127.0.0.1): ";
    std::string inputIP;
    std::getline(std::cin, inputIP);
    if (!inputIP.empty()) serverIP = inputIP;

    std::cout << "Enter server port (default: 8888): ";
    std::string inputPort;
    std::getline(std::cin, inputPort);
    if (!inputPort.empty()) serverPort = std::stoi(inputPort);

    Application app;
    if (!app.Init(GetModuleHandle(NULL), serverIP, serverPort)) {
        Logger::GetInstance().LogError("서버에 연결할 수 없습니다.");
        return 1;
    }

    app.Run();
    app.Shutdown();

    Logger::GetInstance().Log("클라이언트 종료");
    return 0;
}