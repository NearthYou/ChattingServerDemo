#pragma once
#include <string>
#include <vector>
#include "../Network/NetworkManager.h"
#include "../../Common/PacketDefine.h"

class Application
{
public:
    Application();
    ~Application();

    bool Init();
    void Run();
    void Shutdown();

private:
    void DrawLoginUI();
    void DrawRegisterUI();
    void DrawChatUI();
    void AddChatMessage(const ChatPacket& packet);

    NetworkManager networkManager;
    bool isRunning;
    bool isLoggedIn;
    std::string username;
    std::string password;
    char chatInput[1024];
    std::vector<ChatPacket> chatLog;
}; 