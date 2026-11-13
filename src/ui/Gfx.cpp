#include "Gfx.h"

namespace sq {
bool Gfx::init() {
    if (ready) return true;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
                                   &d3d, nullptr, nullptr);
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &d3d,
                               nullptr, nullptr);
    if (FAILED(hr) || FAILED(d3d.As(&dxgi))) return false;
    D2D1_FACTORY_OPTIONS fo{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &fo,
                                 (void**)d2dFactory.GetAddressOf())))
        return false;
    if (FAILED(d2dFactory->CreateDevice(dxgi.Get(), &d2dDevice))) return false;
    if (FAILED(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   (IUnknown**)dwrite.GetAddressOf())))
        return false;
    if (FAILED(DCompositionCreateDevice3(dxgi.Get(), IID_PPV_ARGS(&dcomp)))) return false;
    ready = true;
    return true;
}
}  // namespace sq
