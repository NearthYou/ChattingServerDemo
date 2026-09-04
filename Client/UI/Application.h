#pragma once

#include "D3D11Manager.h"
#include "ClientPacketReducer.h"
#include "../Core/ImGuiManager.h"
#include "../Network/NetworkManager.h"
#include <string>

class Application
{
public:
    bool Init(HINSTANCE hInstance, const std::string& ip, int port);
    int Run();
    void Shutdown();

private:
    void DrawLoginUI();
    void DrawChatUI();
    void LogOut();
    void AddChatMessage(const std::string& sender, const std::string& message, bool isMine);
    bool SubmitCurrentMessage();

    static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    HWND hWnd = nullptr;
    UINT PendingResizeWidth = 0;
    UINT PendingResizeHeight = 0;
    bool Running = true;
    bool showLoginFailedPopup = false;
    bool showRegisterPopup = false;
    char RegisterNickname[32] = {};
    char RegisterPassword[129] = {};
    std::string registerValidationMessage;
    bool showRegisterResultPopup = false;
    std::string registerResultMessage;
    std::string ServerIp;
    int ServerPort = 0;

    char Nickname[32] = {};
    char LoginPassword[129] = {};
    char InputBuffer[256] = {};  // 한글 대응: char[]로 유지
    ClientPacketReducer ClientState;

    D3D11Manager D3D;
    ImGuiManager ImGuiUI;
    NetworkManager Network;
};
