﻿#pragma once

#include "D3D11Manager.h"
#include "ImGuiManager.h"
#include "NetworkManager.h"
#include <vector>
#include <string>

class Application
{
public:
    bool Init(HINSTANCE hInstance);
    int Run();
    void Shutdown();

private:
    void DrawLoginUI();
    void DrawChatUI();
    void AddChatMessage(const std::string& sender, const std::string& message, bool isMine);

    static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    HWND hWnd = nullptr;
    bool Running = true;
    bool LoggedIn = false;

    char Nickname[32] = {};
    char InputBuffer[256] = {};  // 한글 대응: char[]로 유지

    struct ChatMessage
    {
        std::string Sender;
        std::string Text;
        bool IsMine = false;
    };

    std::vector<ChatMessage> ChatLog;

    D3D11Manager D3D;
    ImGuiManager ImGuiUI;
    NetworkManager Network;
};
