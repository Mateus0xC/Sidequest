#pragma once
#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi.h>
#include <wrl/client.h>

namespace sq {
using Microsoft::WRL::ComPtr;

// Dispositivos compartilhados (D3D11 -> Direct2D/DirectWrite -> DirectComposition).
struct Gfx {
    ComPtr<ID3D11Device> d3d;
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<ID2D1Factory1> d2dFactory;
    ComPtr<ID2D1Device> d2dDevice;
    ComPtr<ID2D1DeviceContext> dc;
    ComPtr<IDWriteFactory> dwrite;
    ComPtr<IDCompositionDesktopDevice> dcomp;
    bool ready = false;
    bool init();
};
}  // namespace sq
