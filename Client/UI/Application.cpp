#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "Application.h"
#include "../ImGui/imgui.h"
#include "../ImGui/imgui_impl_win32.h"
#include "../ImGui/imgui_impl_dx11.h"

bool Application::Init(HINSTANCE hInstance, const std::string& ip, int port)
{
    ServerIp = ip;
    ServerPort = port;
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

    if (Network.Connect(ServerIp, ServerPort))
    {
        ConnectionStatus = "Connected.";
    }
    else
    {
        ConnectionStatus = "Disconnected. Start the server and reconnect.";
    }

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

        if (!ClientState.IsLoggedIn())
            DrawLoginUI();
        else
            DrawChatUI();

        for (auto& event : Network.GetPendingEvents())
        {
            if (event.kind == NetworkEvent::Kind::Packet)
            {
                switch (event.packet.type)
                {
                case PACKET_TYPE_LOGIN_SUCCESS:
                    ClientState.Apply(event.packet);
                    break;
                case PACKET_TYPE_LOGIN_FAILED:
                    showLoginFailedPopup = true;
                    break;
                case PACKET_TYPE_REGISTER_SUCCESS:
                    registerResultMessage = "Registration succeeded.";
                    showRegisterResultPopup = true;
                    break;
                case PACKET_TYPE_REGISTER_FAILED:
                    registerResultMessage = "Registration failed.";
                    showRegisterResultPopup = true;
                    break;
                case PACKET_TYPE_CHAT:
                    ClientState.Apply(event.packet);
                    break;
                case PACKET_TYPE_LOGIN:
                case PACKET_TYPE_REGISTER:
                    break;
                }
            }
            else if (event.status == NetworkStatus::Disconnected ||
                event.status == NetworkStatus::ProtocolError)
            {
                ClientState.Disconnect();
                ConnectionStatus = event.message.empty() ? "Disconnected." : event.message;
            }
            else if (event.status == NetworkStatus::QueueFull)
            {
                AddChatMessage("System", event.message, false);
            }
        }

        ImGuiUI.EndFrame();
        D3D.EndFrame();
    }

    return 0;
}

void Application::Shutdown()
{
    SecureZeroMemory(LoginPassword, sizeof(LoginPassword));
    SecureZeroMemory(RegisterPassword, sizeof(RegisterPassword));
    Network.Disconnect();
    ImGuiUI.Shutdown();
    D3D.Cleanup();

    DestroyWindow(hWnd);
    UnregisterClass(L"ImGui Chat Client", GetModuleHandle(NULL));
}

void Application::DrawLoginUI()
{
    ImGui::Begin("Login", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextUnformatted(ConnectionStatus.c_str());
    if (!Network.IsConnected())
    {
        if (ImGui::Button("Reconnect"))
        {
            if (Network.Connect(ServerIp, ServerPort))
            {
                ConnectionStatus = "Connected. Log in to reload recent history.";
            }
            else
            {
                ConnectionStatus = "Reconnect failed. Check the server and try again.";
            }
        }
        ImGui::Separator();
    }
    ImGui::InputText("Nickname", Nickname, IM_ARRAYSIZE(Nickname));
    ImGui::InputText("Password", LoginPassword, IM_ARRAYSIZE(LoginPassword), ImGuiInputTextFlags_Password);

    if (Network.IsConnected() && ImGui::Button("Login"))
    {
        if (strlen(Nickname) > 0 && strlen(LoginPassword) > 0)
        {
            const bool queued = Network.SendLoginRequest(Nickname, LoginPassword);
            SecureZeroMemory(LoginPassword, sizeof(LoginPassword));
            if (!queued)
            {
                showLoginFailedPopup = true;
            }
        }
    }
    ImGui::SameLine();
    if (Network.IsConnected() && ImGui::Button("Register"))
    {
        showRegisterPopup = true;
        RegisterNickname[0] = 0;
        RegisterPassword[0] = 0;
    }

    // 회원가입 팝업
    if (showRegisterPopup)
    {
        ImGui::OpenPopup("Register");
        showRegisterPopup = false;
    }
    if (ImGui::BeginPopupModal("Register", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputText("Nickname", RegisterNickname, IM_ARRAYSIZE(RegisterNickname));
        ImGui::InputText("Password", RegisterPassword, IM_ARRAYSIZE(RegisterPassword), ImGuiInputTextFlags_Password);
        if (ImGui::Button("Register"))
        {
            if (strlen(RegisterNickname) == 0 || strlen(RegisterPassword) == 0)
            {
                registerResultMessage = "Nickname and password are required.";
                showRegisterResultPopup = true;
            }
            else if (!Network.SendRegisterRequest(RegisterNickname, RegisterPassword))
            {
                registerResultMessage = "The registration request could not be queued.";
                showRegisterResultPopup = true;
            }
            SecureZeroMemory(RegisterPassword, sizeof(RegisterPassword));
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            SecureZeroMemory(RegisterPassword, sizeof(RegisterPassword));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // 로그인 실패 팝업
    if (showLoginFailedPopup)
        ImGui::OpenPopup("Login Failed");

    if (ImGui::BeginPopupModal("Login Failed", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Login/Register failed. Please try again.");
        if (ImGui::Button("OK")) {
            ImGui::CloseCurrentPopup();
            showLoginFailedPopup = false;
        }
        ImGui::EndPopup();
    }

    // 회원가입 결과 팝업
    if (showRegisterResultPopup)
    {
        ImGui::OpenPopup("Register Result");
        showRegisterResultPopup = false;
    }
    if (ImGui::BeginPopupModal("Register Result", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("%s", registerResultMessage.c_str());
        if (ImGui::Button("OK"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
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

    for (const auto& msg : ClientState.ChatMessages())
    {
        if (msg.isMine)
        {
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() * 0.5f);
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "[Me] %s", msg.text.c_str());
        }
        else
        {
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "[%s] %s", msg.sender.c_str(), msg.text.c_str());
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
        if (SubmitCurrentMessage())
        {
            ImGui::SetKeyboardFocusHere(-1);
        }
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();

    if (ImGui::Button("Send"))
    {
        if (SubmitCurrentMessage())
        {
            ImGui::SetKeyboardFocusHere(-1);
        }
    }

    ImGui::End();
}

bool Application::SubmitCurrentMessage()
{
    if (strlen(InputBuffer) == 0)
    {
        return false;
    }
    if (!Network.SendChatMessage(InputBuffer))
    {
        AddChatMessage("System", "The message could not be queued.", false);
    }
    InputBuffer[0] = '\0';
    return true;
}

void Application::AddChatMessage(const std::string& sender, const std::string& message, bool isMine)
{
    ClientState.AppendChat(sender, message, isMine);
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

