#include "D3D11Manager.h"

bool D3D11Manager::Init(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[1] = { D3D_FEATURE_LEVEL_11_0 };

    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 1, D3D11_SDK_VERSION, &sd,
        &SwapChain, &Device, &featureLevel, &DeviceContext);

    if (FAILED(res))
        return false;

    CreateRenderTarget();
    return true;
}

void D3D11Manager::BeginFrame()
{
    const float clear_color_with_alpha[4] = { 0.45f, 0.55f, 0.60f, 1.00f };
    DeviceContext->OMSetRenderTargets(1, &RenderTargetView, NULL);
    DeviceContext->ClearRenderTargetView(RenderTargetView, clear_color_with_alpha);
}

void D3D11Manager::EndFrame()
{
    SwapChain->Present(1, 0);
}

void D3D11Manager::Cleanup()
{
    CleanupRenderTarget();
    if (SwapChain) { SwapChain->Release(); SwapChain = nullptr; }
    if (DeviceContext) { DeviceContext->Release(); DeviceContext = nullptr; }
    if (Device) { Device->Release(); Device = nullptr; }
}

void D3D11Manager::CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    SwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    Device->CreateRenderTargetView(pBackBuffer, NULL, &RenderTargetView);
    pBackBuffer->Release();
}

void D3D11Manager::CleanupRenderTarget()
{
    if (RenderTargetView) { RenderTargetView->Release(); RenderTargetView = nullptr; }
}
