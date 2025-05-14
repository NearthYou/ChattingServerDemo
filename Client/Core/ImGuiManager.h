#pragma once
#include <d3d11.h>
#include <functional>

class ImGuiManager
{
public:
    void Init(HWND hWnd, ID3D11Device* device, ID3D11DeviceContext* context);
    void BeginFrame();
    void EndFrame();
    void Shutdown();

private:
    HWND WindowHandle = nullptr;
};