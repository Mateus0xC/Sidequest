#include "CaptureSampler.h"
#include <d3d11.h>
#include <dxgi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <chrono>
#include <mutex>

using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

namespace {
struct __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1")) IDirect3DDxgiInterfaceAccess : ::IUnknown {
    virtual HRESULT __stdcall GetInterface(REFIID iid, void** object) = 0;
};
// Ponta da linha do HUD de baixo, sob o contador de munição (só existe com o jogador vivo). Medida num
// quadro 1920x1079: a linha tem 1 px em y = 1054 e vai de x = 1273 a 1382 (semitransparente, ~(170,170,178)
// sobre cenas escuras). Só os últimos ~13 px da ponta: x 1370..1382. Em frações do quadro: x 0.7135..0.7203.
constexpr float kX0 = 0.7135f, kX1 = 0.7203f, kYc = 0.9768f;
constexpr int kContrast = 20;   // a linha tem de ser mais clara que 2 px acima e abaixo
constexpr int kMinLum = 110;
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
        UINT yc = (UINT)(d.Height * kYc + 0.5f);
        // 7 linhas (yc-3..yc+3): 3 candidatas (yc-1..yc+1), cada uma comparada com os vizinhos a ±2 px
        if (yc < 3 || yc + 4 > d.Height) return;
        UINT x0 = (UINT)(d.Width * kX0), x1 = (UINT)(d.Width * kX1), y0 = yc - 3, y1 = yc + 4;
        UINT w = x1 - x0, h = y1 - y0;
        if (!w || h != 7) return;
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
        // fração do trecho onde a linha do HUD aparece (mais clara que os vizinhos); vale a melhor candidata
        auto lum = [&](UINT row, UINT x) {
            const BYTE* p = (const BYTE*)mp.pData + (size_t)row * mp.RowPitch + (size_t)x * 4;
            return (p[0] + p[1] + p[2]) / 3;
        };
        float best = 0;
        for (UINT c = 2; c <= 4; ++c) {   // linhas locais 2..4 = yc-1..yc+1
            UINT hit = 0;
            for (UINT x = 0; x < w; ++x) {
                const BYTE* p = (const BYTE*)mp.pData + (size_t)c * mp.RowPitch + (size_t)x * 4;
                int mx = std::max({(int)p[0], (int)p[1], (int)p[2]}), mn = std::min({(int)p[0], (int)p[1], (int)p[2]});
                bool whitish = mx - mn < 40;   // branco/cinza-claro, sem cor
                int l = lum(c, x);
                if (whitish && l > kMinLum && l - std::max(lum(c - 2, x), lum(c + 2, x)) > kContrast) ++hit;
            }
            best = std::max(best, (float)hit / w);
        }
        ctx->Unmap(stg.get(), 0);
        frame.Close();
        last = Features{best};
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
