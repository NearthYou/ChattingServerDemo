#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "Application.h"
#include "ChatMessageLayout.h"
#include "ChatTimeline.h"
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
        "C:\\Windows\\Fonts\\malgunbd.ttf", 15.0f, &config, koreanRanges);
    if (!fonts.Label)
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

void DrawConnectionStatus(const std::string& ip, int port, bool connected, bool connecting)
{
    const char* state = connected ? "연결됨" : (connecting ? "연결 중" : "연결 끊김");
    const ImU32 color = connected
        ? SuccessColor
        : (connecting ? IM_COL32(250, 184, 74, 255) : IM_COL32(248, 113, 113, 255));
    const std::string line = std::string(state) + "  " + ip + ":" + std::to_string(port);
    CenteredText(line.c_str(), gChatFonts.Label, color);
}

void DrawDateSeparator(
    const std::string& label,
    float contentStartX,
    float availableWidth)
{
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGui::PushFont(gChatFonts.Label);
    const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
    ImGui::PopFont();
    const float textX = contentStartX + (availableWidth - textSize.x) * 0.5f;
    const float lineY = ImGui::GetCursorScreenPos().y + textSize.y * 0.5f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddLine(
        ImVec2(contentStartX, lineY),
        ImVec2((std::max)(contentStartX, textX - 14.0f), lineY),
        IM_COL32(255, 255, 255, 24));
    drawList->AddLine(
        ImVec2(textX + textSize.x + 14.0f, lineY),
        ImVec2(contentStartX + availableWidth, lineY),
        IM_COL32(255, 255, 255, 24));
    ImGui::SetCursorPosX(textX);
    ImGui::PushFont(gChatFonts.Label);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(MutedTextColor), "%s", label.c_str());
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
}

void DrawMessageBubble(
    const ClientChatMessage& message,
    int index,
    float contentStartX,
    float availableWidth,
    const std::string& clockText)
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
    if (!clockText.empty())
    {
        ImGui::PushFont(gChatFonts.Label);
        const float clockWidth = ImGui::CalcTextSize(clockText.c_str()).x;
        ImGui::PopFont();
        const float clockX = message.isMine
            ? bubbleMin.x - 8.0f - clockWidth
            : bubbleMax.x + 8.0f;
        drawList->AddText(
            gChatFonts.Label,
            13.0f,
            ImVec2(clockX, bubbleMax.y - 15.0f),
            MutedTextColor,
            clockText.c_str());
    }
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
        100, 100, 900, 700, NULL, NULL, wc.hInstance, this);

    if (!D3D.Init(hWnd))
        return false;

    ShowWindow(hWnd, SW_SHOWDEFAULT);
    UpdateWindow(hWnd);

    ImGuiUI.Init(hWnd, D3D.GetDevice(), D3D.GetDeviceContext());
    ImGui::GetIO().IniFilename = nullptr;
    gChatFonts = LoadChatFonts();
    ApplyChatStyle();

    Network.BeginConnect(ServerIp, ServerPort);

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

        if (PendingResizeWidth != 0 && PendingResizeHeight != 0)
        {
            if (!D3D.Resize(PendingResizeWidth, PendingResizeHeight))
                return 1;
            PendingResizeWidth = 0;
            PendingResizeHeight = 0;
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
                    registerResultMessage = "가입이 완료됐습니다.";
                    showRegisterResultPopup = true;
                    break;
                case PACKET_TYPE_REGISTER_FAILED:
                    registerResultMessage = "가입에 실패했습니다. 다시 시도해 주세요.";
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
            else if (event.status == NetworkStatus::ConnectFailed ||
                event.status == NetworkStatus::Disconnected ||
                event.status == NetworkStatus::ProtocolError)
            {
                ClientState.Disconnect();
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
    const bool connected = Network.IsConnected();
    const bool connecting = Network.IsConnecting();
    const float cardWidth = (std::max)(1.0f, (std::min)(500.0f, displaySize.x - 32.0f));
    const float desiredCardHeight = !connected && !connecting ? 470.0f : 420.0f;
    const float cardHeight = (std::max)(1.0f, (std::min)(desiredCardHeight, displaySize.y - 28.0f));
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(cardWidth, cardHeight), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(PanelColor));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(PanelBorderColor));
    ImGui::Begin("Login", nullptr, windowFlags);

    CenteredText("RELAY", gChatFonts.Display, AccentColor);
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    DrawConnectionStatus(ServerIp, ServerPort, connected, connecting);

    if (!connected && !connecting)
    {
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        if (SecondaryButton("다시 연결", ImVec2(ImGui::GetContentRegionAvail().x, 44.0f)))
        {
            Network.BeginConnect(ServerIp, ServerPort);
        }
    }

    ImGui::Dummy(ImVec2(0.0f, 18.0f));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##LoginNickname", "닉네임", Nickname, IM_ARRAYSIZE(Nickname));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##LoginPassword", "비밀번호", LoginPassword,
        IM_ARRAYSIZE(LoginPassword), ImGuiInputTextFlags_Password);

    ImGui::BeginDisabled(!connected);
    if (PrimaryButton("로그인", ImVec2(ImGui::GetContentRegionAvail().x, 48.0f)))
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
    if (SecondaryButton("회원가입", ImVec2(ImGui::GetContentRegionAvail().x, 44.0f)))
    {
        showRegisterPopup = true;
        RegisterNickname[0] = 0;
        RegisterPassword[0] = 0;
        registerValidationMessage.clear();
    }
    ImGui::EndDisabled();

    if (showRegisterPopup)
    {
        ImGui::OpenPopup("회원가입");
        showRegisterPopup = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(
        chat::ui::ClampedOverlayExtent(400.0f, displaySize.x),
        chat::ui::ClampedOverlayExtent(340.0f, displaySize.y)), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("회원가입", NULL,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##RegisterNickname", "닉네임", RegisterNickname, IM_ARRAYSIZE(RegisterNickname));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##RegisterPassword", "비밀번호", RegisterPassword,
            IM_ARRAYSIZE(RegisterPassword), ImGuiInputTextFlags_Password);
        if (!registerValidationMessage.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.45f, 0.45f, 1.0f));
            ImGui::TextWrapped("%s", registerValidationMessage.c_str());
            ImGui::PopStyleColor();
        }
        if (PrimaryButton("가입하기", ImVec2(ImGui::GetContentRegionAvail().x, 46.0f)))
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
                registerValidationMessage = "서버에 요청을 보내지 못했습니다.";
                SecureZeroMemory(RegisterPassword, sizeof(RegisterPassword));
            }
            else
            {
                registerValidationMessage.clear();
                SecureZeroMemory(RegisterPassword, sizeof(RegisterPassword));
                ImGui::CloseCurrentPopup();
            }
        }
        if (SecondaryButton("취소", ImVec2(ImGui::GetContentRegionAvail().x, 42.0f)))
        {
            SecureZeroMemory(RegisterPassword, sizeof(RegisterPassword));
            registerValidationMessage.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (showLoginFailedPopup)
        ImGui::OpenPopup("로그인 실패");

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(
        chat::ui::ClampedOverlayExtent(390.0f, displaySize.x),
        chat::ui::ClampedOverlayExtent(165.0f, displaySize.y)), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("로그인 실패", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(MutedTextColor), "닉네임과 비밀번호를 확인해 주세요.");
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        if (PrimaryButton("확인", ImVec2(ImGui::GetContentRegionAvail().x, 44.0f))) {
            ImGui::CloseCurrentPopup();
            showLoginFailedPopup = false;
        }
        ImGui::EndPopup();
    }

    if (showRegisterResultPopup)
    {
        ImGui::OpenPopup("가입 결과");
        showRegisterResultPopup = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(
        chat::ui::ClampedOverlayExtent(390.0f, displaySize.x),
        chat::ui::ClampedOverlayExtent(165.0f, displaySize.y)), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("가입 결과", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::TextWrapped("%s", registerResultMessage.c_str());
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        if (PrimaryButton("확인", ImVec2(ImGui::GetContentRegionAvail().x, 44.0f)))
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
    ImGui::TextUnformatted("전체 채팅");
    ImGui::PopFont();
    ImGui::SameLine();
    constexpr float logoutButtonWidth = 96.0f;
    ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - 28.0f - logoutButtonWidth));
    if (SecondaryButton("로그아웃", ImVec2(logoutButtonWidth, 34.0f)))
    {
        LogOut();
    }
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(MutedTextColor), "%s로 접속", Nickname);
    const std::string endpoint = ServerIp + ":" + std::to_string(ServerPort);
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
        CenteredText("아직 대화가 없습니다.", gChatFonts.Heading, MutedTextColor);
    }
    int messageIndex = 0;
    bool hasPreviousDate = false;
    chat::ui::LocalChatTime previousDate{};
    for (const auto& msg : ClientState.ChatMessages())
    {
        chat::ui::LocalChatTime localTime{};
        std::string clockText;
        if (msg.timestampMilliseconds > 0 &&
            chat::ui::TryGetLocalChatTime(msg.timestampMilliseconds, localTime))
        {
            if (!hasPreviousDate || !chat::ui::IsSameChatDate(previousDate, localTime))
            {
                DrawDateSeparator(
                    chat::ui::FormatChatDate(localTime),
                    contentStartX,
                    availableWidth);
                previousDate = localTime;
                hasPreviousDate = true;
            }
            clockText = chat::ui::FormatChatClock(localTime);
        }
        DrawMessageBubble(msg, messageIndex++, contentStartX, availableWidth, clockText);
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
    if (ImGui::InputTextWithHint("##Input", "메시지 입력", InputBuffer, IM_ARRAYSIZE(InputBuffer),
        ImGuiInputTextFlags_EnterReturnsTrue))
    {
        if (SubmitCurrentMessage())
        {
            ImGui::SetKeyboardFocusHere(-1);
        }
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();

    if (PrimaryButton("전송", ImVec2(96.0f, 42.0f)))
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
    Network.BeginConnect(ServerIp, ServerPort);
}

bool Application::SubmitCurrentMessage()
{
    if (strlen(InputBuffer) == 0)
    {
        return false;
    }
    if (!Network.SendChatMessage(InputBuffer))
    {
        AddChatMessage("System", "메시지를 보내지 못했습니다.", false);
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
    Application* application = reinterpret_cast<Application*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<CREATESTRUCT*>(lParam);
        application = static_cast<Application*>(create->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
    }

    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    if (msg == WM_SIZE && wParam != SIZE_MINIMIZED && application)
    {
        application->PendingResizeWidth = LOWORD(lParam);
        application->PendingResizeHeight = HIWORD(lParam);
        return 0;
    }
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

