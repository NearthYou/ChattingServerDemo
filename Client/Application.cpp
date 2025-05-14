#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "Application.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_win32.h"
#include "ImGui/imgui_impl_dx11.h"

bool Application::Init(HINSTANCE hInstance)
{
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, Application::WndProc, 0L, 0L,
        hInstance, NULL, NULL, NULL, NULL, L"ImGui Chat Client", NULL };
    RegisterClassEx(&wc);

    hWnd = CreateWindow(wc.lpszClassName, L"ImGui Chat Client (DX11)", WS_OVERLAPPEDWINDOW,
        100, 100, 900, 700, NULL, NULL, wc.hInstance, NULL);

    if (!D3D.Init(hWnd))
        return false;

    ShowWindow(hWnd, SW_SHOWDEFAULT);
    UpdateWindow(hWnd);

    ImGuiUI.Init(hWnd, D3D.GetDevice(), D3D.GetDeviceContext());

    if (!Network.Init())
        return false;

    return true;
}

int Application::Run()
{
    MSG msg;
    ZeroMemory(&msg, sizeof(msg));

    while (Running)
    {
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);

            if (msg.message == WM_QUIT)
                Running = false;
        }

        D3D.BeginFrame();
        ImGuiUI.BeginFrame();

        if (!LoggedIn)
            DrawLoginUI();
        else
            DrawChatUI();

        for (auto& packet : Network.GetPendingMessages())
        {
            AddChatMessage(packet.Sender, packet.Message, packet.IsMine);
        }

        ImGuiUI.EndFrame();
        D3D.EndFrame();
    }

    return 0;
}

void Application::Shutdown()
{
    Network.Shutdown();
    ImGuiUI.Shutdown();
    D3D.Cleanup();

    DestroyWindow(hWnd);
    UnregisterClass(L"ImGui Chat Client", GetModuleHandle(NULL));
}

void Application::DrawLoginUI()
{
    ImGui::Begin("Login", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::InputText("Nickname", Nickname, IM_ARRAYSIZE(Nickname));

    if (ImGui::Button("Login"))
    {
        if (strlen(Nickname) > 0)
        {
            Network.SendLogin(Nickname);
            LoggedIn = true;
        }
    }
    ImGui::End();
}

void Application::DrawChatUI()
{
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin("Chat Room", nullptr, ImGuiWindowFlags_NoCollapse);

    static bool AutoScroll = true;
    static bool ScrollToBottom = false;

    ImGui::BeginChild("ChatLog", ImVec2(0, -50), true,
        ImGuiWindowFlags_HorizontalScrollbar |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoNavInputs |
        ImGuiWindowFlags_NoScrollWithMouse
    );

    for (auto& msg : ChatLog)
    {
        if (msg.IsMine)
        {
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() * 0.5f);
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "[Me] %s", msg.Text.c_str());
        }
        else
        {
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "[%s] %s", msg.Sender.c_str(), msg.Text.c_str());
        }
    }

    if (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ScrollToBottom = true;

    if (ScrollToBottom)
        ImGui::SetScrollHereY(1.0f);

    ScrollToBottom = false;

    ImGui::EndChild();

    ImGui::PushItemWidth(-70);
    if (ImGui::InputText("##Input", InputBuffer, IM_ARRAYSIZE(InputBuffer),
        ImGuiInputTextFlags_EnterReturnsTrue))
    {
        if (strlen(InputBuffer) > 0)
        {
            Network.SendChatMessage(InputBuffer);
            InputBuffer[0] = '\0';
            ImGui::SetKeyboardFocusHere(-1);
        }
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();

    if (ImGui::Button("Send"))
    {
        if (strlen(InputBuffer) > 0)
        {
            Network.SendChatMessage(InputBuffer);
            InputBuffer[0] = '\0';
            ImGui::SetKeyboardFocusHere(-1);
        }
    }

    ImGui::End();
}

void Application::AddChatMessage(const std::string& sender, const std::string& message, bool isMine)
{
    ChatLog.push_back({ sender, message, isMine });
}

LRESULT WINAPI Application::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    if (msg == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
