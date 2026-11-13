#include "CaptureSampler.h"
#include <d3d11.h>
#include <dxgi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <atomic>
#include <chrono>
#include <mutex>

using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

namespace {
struct __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1")) IDirect3DDxgiInterfaceAccess : ::IUnknown {
    virtual HRESULT __stdcall GetInterface(REFIID iid, void** object) = 0;
};
// faixa do HUD (fração do quadro): ícones/barras de habilidade e vida, na base da tela
constexpr float kX0 = 0.25f, kX1 = 0.65f, kY0 = 0.92f, kY1 = 0.99f;
}  // namespace

namespace sq {

struct CaptureSampler::Impl {
    winrt::com_ptr<ID3D11Device> dev;
    winrt::com_ptr<ID3D11DeviceContext> ctx;
    winrt::com_ptr<ID3D11Texture2D> stg;
    UINT stgW = 0, stgH = 0;
    IDirect3DDevice d3dDev{nullptr};
    GraphicsCaptureItem item{nullptr};
    Direct3D11CaptureFramePool pool{nullptr};
    GraphicsCaptureSession session{nullptr};
    winrt::event_token arrived{}, closed{};
    HWND notify = nullptr;
    UINT msg = 0;
    std::mutex m;
    Features last;
    std::atomic<unsigned long long> count{0};
    std::atomic<bool> alive{false};
    std::atomic<int> border{-1};

    void onFrame(Direct3D11CaptureFramePool const& sender) {
        std::lock_guard<std::mutex> lock(m);
        auto frame = sender.TryGetNextFrame();
        if (!frame || !alive) return;
        winrt::com_ptr<ID3D11Texture2D> tex;
        if (FAILED(frame.Surface().as<IDirect3DDxgiInterfaceAccess>()->GetInterface(winrt::guid_of<ID3D11Texture2D>(), tex.put_void())))
            return;
        D3D11_TEXTURE2D_DESC d{};
        tex->GetDesc(&d);
        UINT x0 = (UINT)(d.Width * kX0), x1 = (UINT)(d.Width * kX1), y0 = (UINT)(d.Height * kY0), y1 = (UINT)(d.Height * kY1);
        UINT w = x1 - x0, h = y1 - y0;
        if (!w || !h) return;
        if (!stg || stgW != w || stgH != h) {
            D3D11_TEXTURE2D_DESC sd = d;
            sd.Width = w; sd.Height = h;
            sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.MiscFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            stg = nullptr;
            if (FAILED(dev->CreateTexture2D(&sd, nullptr, stg.put()))) return;
            stgW = w; stgH = h;
        }
        D3D11_BOX box{x0, y0, 0, x1, y1, 1};
        ctx->CopySubresourceRegion(stg.get(), 0, 0, 0, 0, tex.get(), 0, &box);   // só a faixa, não o quadro todo
        D3D11_MAPPED_SUBRESOURCE mp{};
        if (FAILED(ctx->Map(stg.get(), 0, D3D11_MAP_READ, 0, &mp))) return;
        unsigned long long teal = 0, white = 0, sum = 0, n = 0;
        for (UINT y = 0; y < h; y += 2) {
            const BYTE* row = (const BYTE*)mp.pData + (size_t)y * mp.RowPitch;
            for (UINT x = 0; x < w; x += 2) {
                int b = row[x * 4], g = row[x * 4 + 1], r = row[x * 4 + 2];
                if (g > 140 && b > 110 && r < 120 && g > r + 40) ++teal;
                if (r > 215 && g > 215 && b > 215) ++white;
                sum += (r + g + b) / 3;
                ++n;
            }
        }
        ctx->Unmap(stg.get(), 0);
        frame.Close();
        if (!n) return;
        last = {(float)teal / n, (float)white / n, (float)sum / n / 255.f};
        ++count;
        if (notify) PostMessageW(notify, msg, 0, 0);
    }
};

CaptureSampler::CaptureSampler() : p_(new Impl) {}
CaptureSampler::~CaptureSampler() { stop(); }

bool CaptureSampler::running() const { return p_->alive; }
Features CaptureSampler::last() const {
    std::lock_guard<std::mutex> lock(p_->m);
    return p_->last;
}
unsigned long long CaptureSampler::samples() const { return p_->count; }
int CaptureSampler::borderState() const { return p_->border; }

bool CaptureSampler::start(HWND target, HWND notify, UINT msg) {
    stop();
    try {
        if (!GraphicsCaptureSession::IsSupported() || !target) return false;
        Impl& s = *p_;
        s.notify = notify;
        s.msg = msg;
        auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        winrt::check_hresult(interop->CreateForWindow(target, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(s.item)));
        winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                               nullptr, 0, D3D11_SDK_VERSION, s.dev.put(), nullptr, s.ctx.put()));
        auto dxgi = s.dev.as<IDXGIDevice>();
        winrt::com_ptr<::IInspectable> insp;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), insp.put()));
        s.d3dDev = insp.as<IDirect3DDevice>();
        s.pool = Direct3D11CaptureFramePool::CreateFreeThreaded(s.d3dDev, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
                                                                s.item.Size());
        s.session = s.pool.CreateCaptureSession(s.item);
        try { s.session.MinUpdateInterval(std::chrono::milliseconds(250)); } catch (...) {}   // Windows 11 22H2+
        s.border = 1;
        try { s.session.IsBorderRequired(false); s.border = 0; } catch (...) {}              // sem a borda amarela
        s.alive = true;
        s.arrived = s.pool.FrameArrived([this](auto const& sender, auto const&) { p_->onFrame(sender); });
        s.closed = s.item.Closed([this](auto const&, auto const&) { p_->alive = false; });
        s.session.StartCapture();
        return true;
    } catch (...) {
        stop();
        return false;
    }
}

void CaptureSampler::stop() {
    Impl& s = *p_;
    s.alive = false;
    try {
        if (s.pool && s.arrived) s.pool.FrameArrived(s.arrived);   // remove o handler
        s.arrived = {};
        if (s.session) s.session.Close();
        if (s.pool) s.pool.Close();
    } catch (...) {}
    std::lock_guard<std::mutex> lock(s.m);
    s.session = nullptr; s.pool = nullptr; s.item = nullptr; s.d3dDev = nullptr;
    s.stg = nullptr; s.ctx = nullptr; s.dev = nullptr;
    s.stgW = s.stgH = 0;
}
}  // namespace sq
