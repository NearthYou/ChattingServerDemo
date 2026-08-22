#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "Application.h"
#include "ChatMessageLayout.h"
#include "RegistrationValidation.h"
#include "../ImGui/imgui.h"
#include "../ImGui/imgui_impl_win32.h"
#include "../ImGui/imgui_impl_dx11.h"

#include <algorithm>

namespace
{
constexpr ImU32 AccentColor = IM_COL32(109, 94, 252, 255);
constexpr ImU32 AccentHoverColor = IM_COL32(128, 113, 255, 255);
constexpr ImU32 SuccessColor = IM_COL32(52, 211, 153, 255);
constexpr ImU32 MutedTextColor = IM_COL32(150, 160, 181, 255);
constexpr ImU32 PanelColor = IM_COL32(20, 24, 36, 248);
constexpr ImU32 PanelBorderColor = IM_COL32(255, 255, 255, 25);

struct ChatFonts
{
    ImFont* Body = nullptr;
    ImFont* Label = nullptr;
    ImFont* Heading = nullptr;
    ImFont* Display = nullptr;
};

ChatFonts gChatFonts;

ImFont* AddFontFromFileIfPresent(
    ImGuiIO& io,
    const char* path,
    float size,
    ImFontConfig* config,
    const ImWchar* ranges = nullptr)
{
    const DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return nullptr;
    return io.Fonts->AddFontFromFileTTF(path, size, config, ranges);
}

ChatFonts LoadChatFonts()
{
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig config;
    config.OversampleH = 3;
    config.OversampleV = 2;
    config.PixelSnapH = false;
    const ImWchar* koreanRanges = io.Fonts->GetGlyphRangesKorean();

    ChatFonts fonts;
    fonts.Body = AddFontFromFileIfPresent(io,
        "C:\\Windows\\Fonts\\malgun.ttf", 17.0f, &config, koreanRanges);
    if (!fonts.Body)
        fonts.Body = AddFontFromFileIfPresent(io,
            "C:\\Windows\\Fonts\\gulim.ttc", 17.0f, &config, koreanRanges);
    fonts.Label = AddFontFromFileIfPresent(io,
        "C:\\Windows\\Fonts\\seguisb.ttf", 15.0f, &config);
    fonts.Heading = AddFontFromFileIfPresent(io,
        "C:\\Windows\\Fonts\\malgunbd.ttf", 27.0f, &config, koreanRanges);
    fonts.Display = AddFontFromFileIfPresent(io,
        "C:\\Windows\\Fonts\\seguisb.ttf", 38.0f, &config);

    if (!fonts.Body)
        fonts.Body = AddFontFromFileIfPresent(io,
            "C:\\Windows\\Fonts\\segoeui.ttf", 17.0f, &config);
    if (!fonts.Body) fonts.Body = io.Fonts->AddFontDefault();
    if (!fonts.Label) fonts.Label = fonts.Body;
    if (!fonts.Heading) fonts.Heading = fonts.Body;
    if (!fonts.Display) fonts.Display = fonts.Heading;
    io.FontDefault = fonts.Body;
    return fonts;
}

void ApplyChatStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 20.0f;
    style.ChildRounding = 16.0f;
    style.FrameRounding = 11.0f;
    style.PopupRounding = 18.0f;
    style.ScrollbarRounding = 10.0f;
    style.GrabRounding = 10.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(28.0f, 26.0f);
    style.FramePadding = ImVec2(14.0f, 11.0f);
    style.ItemSpacing = ImVec2(12.0f, 12.0f);
    style.DisabledAlpha = 0.42f;

    style.Colors[ImGuiCol_Text] = ImVec4(0.95f, 0.96f, 0.99f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.59f, 0.63f, 0.71f, 1.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.09f, 0.135f, 0.98f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.065f, 0.075f, 0.11f, 0.96f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.075f, 0.09f, 0.135f, 0.99f);
    style.Colors[ImGuiCol_Border] = ImVec4(1.0f, 1.0f, 1.0f, 0.10f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.11f, 0.13f, 0.19f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.17f, 0.24f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.20f, 0.28f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImGui::ColorConvertU32ToFloat4(AccentColor);
    style.Colors[ImGuiCol_ButtonHovered] = ImGui::ColorConvertU32ToFloat4(AccentHoverColor);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.36f, 0.30f, 0.86f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.04f, 0.05f, 0.08f, 0.6f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.26f, 0.28f, 0.38f, 1.0f);
    style.Colors[ImGuiCol_Separator] = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
}

void DrawAppBackground()
{
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    drawList->AddRectFilledMultiColor(
        ImVec2(0.0f, 0.0f), size,
        IM_COL32(14, 17, 28, 255),
        IM_COL32(20, 17, 39, 255),
        IM_COL32(9, 13, 23, 255),
        IM_COL32(9, 15, 25, 255));
    drawList->AddCircleFilled(ImVec2(size.x * 0.78f, size.y * 0.12f), 210.0f, IM_COL32(109, 94, 252, 22), 64);
    drawList->AddCircleFilled(ImVec2(size.x * 0.20f, size.y * 0.92f), 260.0f, IM_COL32(41, 198, 172, 14), 64);
}

void CenterNextItem(float width)
{
    ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX(), (ImGui::GetWindowWidth() - width) * 0.5f));
}

void CenteredText(const char* text, ImFont* font, ImU32 color)
{
    ImGui::PushFont(font ? font : ImGui::GetFont());
    CenterNextItem(ImGui::CalcTextSize(text).x);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color), "%s", text);
    ImGui::PopFont();
}

bool PrimaryButton(const char* label, ImVec2 size)
{
    ImGui::PushFont(gChatFonts.Label);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopFont();
    return pressed;
}

bool SecondaryButton(const char* label, ImVec2 size)
{
    ImGui::PushFont(gChatFonts.Label);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.14f, 0.21f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.17f, 0.19f, 0.27f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.21f, 0.23f, 0.32f, 1.0f));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    ImGui::PopFont();
    return pressed;
}

void DrawConnectionCard(const std::string& status, const std::string& ip, int port, bool connected, bool connecting)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.095f, 0.11f, 0.16f, 1.0f));
    ImGui::BeginChild("ConnectionCard", ImVec2(-1.0f, 92.0f), true, ImGuiWindowFlags_NoScrollbar);
    const ImU32 indicatorColor = connected ? SuccessColor : (connecting ? IM_COL32(250, 184, 74, 255) : IM_COL32(248, 113, 113, 255));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 indicator(ImGui::GetWindowPos().x + 19.0f, ImGui::GetWindowPos().y + 24.0f);
    drawList->AddCircleFilled(indicator, 5.0f, indicatorColor, 20);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 17.0f);
    ImGui::PushFont(gChatFonts.Label);
    ImGui::TextUnformatted(connected ? "ONLINE" : (connecting ? "CONNECTING" : "OFFLINE"));
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(MutedTextColor), "  %s:%d", ip.c_str(), port);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 17.0f);
    ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - 14.0f);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(MutedTextColor), "%s", status.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void DrawMessageBubble(const ClientChatMessage& message, int index, float contentStartX, float availableWidth)
{
    ImGui::PushID(index);
    const float maxBubbleWidth = chat::ui::MessageBubbleMaxWidth(availableWidth);
    const float wrapWidth = (std::max)(1.0f, maxBubbleWidth - 24.0f);
    ImGui::PushFont(gChatFonts.Body);
    const ImVec2 textSize = ImGui::CalcTextSize(message.text.c_str(), nullptr, false, wrapWidth);
    ImGui::PopFont();

    const float bubbleWidth = chat::ui::MessageBubbleWidth(availableWidth, textSize.x);
    const float bubbleHeight = textSize.y + 20.0f;
    const bool isSystemMessage = !message.isMine && message.sender == "System";
    if (isSystemMessage)
    {
        const float systemWidth = (std::min)(bubbleWidth, availableWidth);
        ImGui::SetCursorPosX(contentStartX + (availableWidth - systemWidth) * 0.5f);
        ImGui::Dummy(ImVec2(systemWidth, bubbleHeight));
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        drawList->AddRectFilled(min, max, IM_COL32(38, 42, 54, 220), 12.0f);
        drawList->AddText(gChatFonts.Body, gChatFonts.Body->FontSize,
            ImVec2(min.x + 12.0f, min.y + 10.0f),
            MutedTextColor, message.text.c_str(), nullptr, wrapWidth);
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::PopID();
        return;
    }

    if (!message.isMine)
    {
        ImGui::SetCursorPosX(contentStartX + 4.0f);
        ImGui::PushFont(gChatFonts.Body);
        ImGui::TextColored(ImVec4(0.58f, 0.92f, 0.85f, 1.0f), "@%s", message.sender.c_str());
        ImGui::PopFont();
        ImGui::SetCursorPosX(contentStartX);
    }
    const float bubbleX = message.isMine
        ? chat::ui::RightAlignedMessageX(contentStartX, availableWidth, bubbleWidth)
        : contentStartX;
    ImGui::SetCursorPosX(bubbleX);
    ImGui::Dummy(ImVec2(bubbleWidth, bubbleHeight));

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 bubbleMin = ImGui::GetItemRectMin();
    const ImVec2 bubbleMax(bubbleMin.x + bubbleWidth, bubbleMin.y + bubbleHeight);
    const ImU32 bubbleColor = message.isMine ? IM_COL32(96, 81, 226, 245) : IM_COL32(34, 39, 53, 250);
    drawList->AddRectFilled(bubbleMin, bubbleMax, bubbleColor, 16.0f);
    drawList->AddRect(bubbleMin, bubbleMax, message.isMine ? IM_COL32(157, 145, 255, 95) : PanelBorderColor, 16.0f, 0, 1.0f);
    drawList->AddText(gChatFonts.Body, gChatFonts.Body->FontSize,
        ImVec2(bubbleMin.x + 12.0f, bubbleMin.y + 10.0f),
        IM_COL32(247, 248, 252, 255), message.text.c_str(), nullptr, wrapWidth);
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGui::PopID();
}
}

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
    ImGui::GetIO().IniFilename = nullptr;
    gChatFonts = LoadChatFonts();
    ApplyChatStyle();

    if (Network.BeginConnect(ServerIp, ServerPort))
    {
        ConnectionStatus = "Connecting to the server.";
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
        DrawAppBackground();

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
                    ClientState.Apply(event.packet);
                    showLoginFailedPopup = true;
                    break;
                case PACKET_TYPE_REGISTER_SUCCESS:
                    registerResultMessage = "Registration succeeded.";
                    showRegisterResultPopup = true;
                    break;
                case PACKET_TYPE_REGISTER_FAILED:
                    registerResultMessage =
                        "Registration failed. The nickname may already be in use, or the server rejected the request.";
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
            else if (event.status == NetworkStatus::Connecting)
            {
                ConnectionStatus = event.message;
            }
            else if (event.status == NetworkStatus::Connected)
            {
                ConnectionStatus = "Connected. Log in to reload recent history.";
            }
            else if (event.status == NetworkStatus::ConnectFailed ||
                event.status == NetworkStatus::Disconnected ||
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
    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings;
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const float cardWidth = (std::max)(1.0f, (std::min)(500.0f, displaySize.x - 32.0f));
    const float cardHeight = (std::max)(1.0f, (std::min)(630.0f, displaySize.y - 28.0f));
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(cardWidth, cardHeight), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(PanelColor));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(PanelBorderColor));
    ImGui::Begin("Login", nullptr, windowFlags);

    CenteredText("RELAY", gChatFonts.Display, AccentColor);
    CenteredText("A focused real-time chat client", gChatFonts.Body, MutedTextColor);
    ImGui::Dummy(ImVec2(0.0f, 12.0f));
    DrawConnectionCard(ConnectionStatus, ServerIp, ServerPort, Network.IsConnected(), Network.IsConnecting());

    if (!Network.IsConnected() && !Network.IsConnecting())
    {
        if (SecondaryButton("Reconnect to server", ImVec2(ImGui::GetContentRegionAvail().x, 44.0f)))
        {
            if (Network.BeginConnect(ServerIp, ServerPort))
            {
                ConnectionStatus = "Connecting to the server.";
            }
            else
            {
                ConnectionStatus = "Reconnect failed. Check the server and try again.";
            }
        }
    }

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::PushFont(gChatFonts.Label);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(MutedTextColor), "NICKNAME");
    ImGui::PopFont();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##LoginNickname", "Enter your nickname", Nickname, IM_ARRAYSIZE(Nickname));
    ImGui::PushFont(gChatFonts.Label);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(MutedTextColor), "PASSWORD");
    ImGui::PopFont();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##LoginPassword", "Enter your password", LoginPassword,
        IM_ARRAYSIZE(LoginPassword), ImGuiInputTextFlags_Password);

    ImGui::BeginDisabled(!Network.IsConnected());
    if (PrimaryButton("Sign in", ImVec2(ImGui::GetContentRegionAvail().x, 48.0f)))
    {
        if (strlen(Nickname) > 0 && strlen(LoginPassword) > 0)
        {
            const bool queued = Network.SendLoginRequest(Nickname, LoginPassword);
            if (queued)
            {
                ClientState.BeginLogin(Nickname);
            }
            SecureZeroMemory(LoginPassword, sizeof(LoginPassword));
            if (!queued)
            {
                showLoginFailedPopup = true;
            }
        }
    }
    if (SecondaryButton("Create account", ImVec2(ImGui::GetContentRegionAvail().x, 44.0f)))
    {
        showRegisterPopup = true;
        RegisterNickname[0] = 0;
        RegisterPassword[0] = 0;
        registerValidationMessage.clear();
    }
    ImGui::EndDisabled();
    CenteredText("Password is cleared after the request is queued", gChatFonts.Label, MutedTextColor);

    if (showRegisterPopup)
    {
        ImGui::OpenPopup("Register");
        showRegisterPopup = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(
        chat::ui::ClampedOverlayExtent(430.0f, displaySize.x),
        chat::ui::ClampedOverlayExtent(400.0f, displaySize.y)), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Register", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::PushFont(gChatFonts.Heading);
        ImGui::TextUnformatted("Create account");
        ImGui::PopFont();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(MutedTextColor), "Choose credentials for this chat server.");
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##RegisterNickname", "Nickname", RegisterNickname, IM_ARRAYSIZE(RegisterNickname));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##RegisterPassword", "Password", RegisterPassword,
            IM_ARRAYSIZE(RegisterPassword), ImGuiInputTextFlags_Password);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(MutedTextColor));
        ImGui::TextWrapped("Nickname: 3-20 bytes (Korean: 3-6 characters), no spaces");
        ImGui::TextWrapped("Password: 8-128 bytes (Korean: 3+ characters)");
        ImGui::PopStyleColor();
        if (!registerValidationMessage.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.45f, 0.45f, 1.0f));
            ImGui::TextWrapped("%s", registerValidationMessage.c_str());
            ImGui::PopStyleColor();
        }
        if (PrimaryButton("Register", ImVec2(ImGui::GetContentRegionAvail().x, 46.0f)))
        {
            const char* validationMessage = chat::ui::RegistrationValidationMessage(
                RegisterNickname,
                RegisterPassword);
            if (validationMessage != nullptr)
            {
                registerValidationMessage = validationMessage;
            }
            else if (!Network.SendRegisterRequest(RegisterNickname, RegisterPassword))
            {
                registerValidationMessage = "The registration request could not be queued.";
                SecureZeroMemory(RegisterPassword, sizeof(RegisterPassword));
            }
            else
            {
                registerValidationMessage.clear();
                SecureZeroMemory(RegisterPassword, sizeof(RegisterPassword));
                ImGui::CloseCurrentPopup();
            }
        }
        if (SecondaryButton("Cancel", ImVec2(ImGui::GetContentRegionAvail().x, 42.0f)))
        {
            SecureZeroMemory(RegisterPassword, sizeof(RegisterPassword));
            registerValidationMessage.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (showLoginFailedPopup)
        ImGui::OpenPopup("Login Failed");

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(
        chat::ui::ClampedOverlayExtent(390.0f, displaySize.x),
        chat::ui::ClampedOverlayExtent(215.0f, displaySize.y)), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Login Failed", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::PushFont(gChatFonts.Heading);
        ImGui::TextUnformatted("Unable to sign in");
        ImGui::PopFont();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(MutedTextColor), "Check your credentials and try again.");
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        if (PrimaryButton("Try again", ImVec2(ImGui::GetContentRegionAvail().x, 44.0f))) {
            ImGui::CloseCurrentPopup();
            showLoginFailedPopup = false;
        }
        ImGui::EndPopup();
    }

    if (showRegisterResultPopup)
    {
        ImGui::OpenPopup("Register Result");
        showRegisterResultPopup = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(
        chat::ui::ClampedOverlayExtent(390.0f, displaySize.x),
        chat::ui::ClampedOverlayExtent(215.0f, displaySize.y)), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Register Result", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::PushFont(gChatFonts.Heading);
        ImGui::TextUnformatted("Registration");
        ImGui::PopFont();
        ImGui::TextWrapped("%s", registerResultMessage.c_str());
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        if (PrimaryButton("Continue", ImVec2(ImGui::GetContentRegionAvail().x, 44.0f)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
    ImGui::PopStyleColor(2);
}

void Application::DrawChatUI()
{
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(displaySize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 22.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.05f, 0.08f, 1.0f));
    ImGui::Begin("Chat Room", nullptr, windowFlags);

    static bool AutoScroll = true;
    static bool ScrollToBottom = false;

    ImGui::PushFont(gChatFonts.Heading);
    ImGui::TextUnformatted("General chat");
    ImGui::PopFont();
    ImGui::SameLine();
    constexpr float logoutButtonWidth = 96.0f;
    ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - 28.0f - logoutButtonWidth));
    if (SecondaryButton("Log out", ImVec2(logoutButtonWidth, 34.0f)))
    {
        LogOut();
    }
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(MutedTextColor), "Connected as %s", Nickname);
    const std::string endpoint = "ONLINE  " + ServerIp + ":" + std::to_string(ServerPort);
    ImGui::SameLine();
    const float statusWidth = ImGui::CalcTextSize(endpoint.c_str()).x + 24.0f;
    ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - 28.0f - statusWidth));
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(SuccessColor), "%s", endpoint.c_str());
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 18.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.065f, 0.095f, 0.72f));
    ImGui::BeginChild("ChatLog", ImVec2(0.0f, -78.0f), true,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNavInputs);

    const float contentStartX = ImGui::GetCursorPosX();
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    if (ClientState.ChatMessages().empty())
    {
        ImGui::Dummy(ImVec2(0.0f, 36.0f));
        CenteredText("No messages yet", gChatFonts.Heading, MutedTextColor);
        CenteredText("Start the conversation below.", gChatFonts.Body, MutedTextColor);
    }
    int messageIndex = 0;
    for (const auto& msg : ClientState.ChatMessages())
    {
        DrawMessageBubble(msg, messageIndex++, contentStartX, availableWidth);
    }

    if (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ScrollToBottom = true;

    if (ScrollToBottom)
        ImGui::SetScrollHereY(1.0f);

    ScrollToBottom = false;

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::PushItemWidth(-108.0f);
    if (ImGui::InputTextWithHint("##Input", "Write a message...", InputBuffer, IM_ARRAYSIZE(InputBuffer),
        ImGuiInputTextFlags_EnterReturnsTrue))
    {
        if (SubmitCurrentMessage())
        {
            ImGui::SetKeyboardFocusHere(-1);
        }
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();

    if (PrimaryButton("Send", ImVec2(96.0f, 42.0f)))
    {
        if (SubmitCurrentMessage())
        {
            ImGui::SetKeyboardFocusHere(-1);
        }
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void Application::LogOut()
{
    SecureZeroMemory(LoginPassword, sizeof(LoginPassword));
    InputBuffer[0] = '\0';
    Network.Disconnect();
    ClientState.Disconnect();
    if (Network.BeginConnect(ServerIp, ServerPort))
    {
        ConnectionStatus = "Connecting to the server.";
    }
    else
    {
        ConnectionStatus = "Disconnected. Start the server and reconnect.";
    }
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
    if (msg == WM_GETMINMAXINFO)
    {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        limits->ptMinTrackSize.x = 720;
        limits->ptMinTrackSize.y = 560;
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

