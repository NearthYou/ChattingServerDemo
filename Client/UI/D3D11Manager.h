#pragma once
#include <d3d11.h>

class D3D11Manager
{
public:
    bool Init(HWND hWnd);
    void BeginFrame();
    void EndFrame();
    void Cleanup();

    ID3D11Device* GetDevice() { return Device; }
    ID3D11DeviceContext* GetDeviceContext() { return DeviceContext; }

private:
    void CreateRenderTarget();
    void CleanupRenderTarget();

    IDXGISwapChain* SwapChain = nullptr;
    ID3D11Device* Device = nullptr;
    ID3D11DeviceContext* DeviceContext = nullptr;
    ID3D11RenderTargetView* RenderTargetView = nullptr;
};