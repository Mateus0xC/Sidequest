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
// Ponta da linha do HUD de baixo, sob o contador de munição (só existe com o jogador vivo). Medida num quadro
// 1920x1079: a ponta termina em 3-4 pixels de BRANCO PURO (247..255) em y = 1054, x = 1379..1382; a cena nunca
// é tão branca ali (na tela de morto fica em ~161). Leitura absoluta, sem comparar com pixels vizinhos.
constexpr float kTipX0 = 0.7182f, kTipX1 = 0.7208f, kTipY = 0.9768f;
constexpr int kTipWhite = 245;
// Botão claro "CLOSE SETTINGS" do menu de pausa: caixa x 855..1064, y 993..1034 (1920x1079). Linhas internas
// y = 1000, 1010, 1020, 1028 e x 862..1058; em frações: x 0.4490..0.5510; y 0.9268, 0.9361, 0.9453, 0.9527.
constexpr float kPauseX0 = 0.4490f, kPauseX1 = 0.5510f;
constexpr float kPauseY[4] = {0.9268f, 0.9361f, 0.9453f, 0.9527f};
// Linha fina do cartão do jogador no menu de compra: 1 px em y = 120, x = 30..382 (1920x1079), cinza ~157.
// Em frações do quadro, com margem: x 0.0208..0.1927, y 0.1112 (3 linhas candidatas).
constexpr float kCardX0 = 0.0208f, kCardX1 = 0.1927f, kCardY = 0.1112f;
// Ícone do mouse "SWITCH PLAYER" do modo espectador (canto inferior esquerdo), medido em 1919x1079: botão
// esquerdo menta (169,237,224) e corpo branco (251,252,253). 9 pontos fixos, leitura absoluta de cor.
constexpr int kSpecMint[4][2] = {{136, 854}, {137, 858}, {137, 860}, {136, 862}};
constexpr int kSpecWhite[5][2] = {{137, 870}, {139, 870}, {141, 870}, {143, 870}, {145, 870}};
constexpr float kSpecRefW = 1919.f, kSpecRefH = 1079.f;
}  // namespace

namespace sq {

struct CaptureSampler::Impl {
    winrt::com_ptr<ID3D11Device> dev;
    winrt::com_ptr<ID3D11DeviceContext> ctx;
    winrt::com_ptr<ID3D11Texture2D> stg, stg2, stg3, stg4;
    UINT stgW = 0, stgH = 0, stg2W = 0, stg2H = 0, stg3W = 0, stg3H = 0, stg4W = 0, stg4H = 0;
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

    // Copia só uma faixa (x0..x1, y0..y0+h) da GPU para um staging e entrega as linhas a `fn`.
    template <class F>
    bool band(ID3D11Texture2D* tex, const D3D11_TEXTURE2D_DESC& d, winrt::com_ptr<ID3D11Texture2D>& st, UINT& stW,
              UINT& stH, UINT x0, UINT x1, UINT y0, UINT h, F&& fn) {
        UINT w = x1 > x0 ? x1 - x0 : 0;
        if (!w || !h || x1 > d.Width || y0 + h > d.Height) return false;
        if (!st || stW != w || stH != h) {
            D3D11_TEXTURE2D_DESC sd = d;
            sd.Width = w; sd.Height = h;
            sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.MiscFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            st = nullptr;
            if (FAILED(dev->CreateTexture2D(&sd, nullptr, st.put()))) return false;
            stW = w; stH = h;
        }
        D3D11_BOX box{x0, y0, 0, x1, y0 + h, 1};
        ctx->CopySubresourceRegion(st.get(), 0, 0, 0, 0, tex, 0, &box);
        D3D11_MAPPED_SUBRESOURCE mp{};
        if (FAILED(ctx->Map(st.get(), 0, D3D11_MAP_READ, 0, &mp))) return false;
        fn((const BYTE*)mp.pData, mp.RowPitch, w);
        ctx->Unmap(st.get(), 0);
        return true;
    }

    void onFrame(Direct3D11CaptureFramePool const& sender) {
        std::lock_guard<std::mutex> lock(m);
        auto frame = sender.TryGetNextFrame();
        if (!frame || !alive) return;
        winrt::com_ptr<ID3D11Texture2D> tex;
        if (FAILED(frame.Surface().as<IDirect3DDxgiInterfaceAccess>()->GetInterface(winrt::guid_of<ID3D11Texture2D>(), tex.put_void())))
            return;
        D3D11_TEXTURE2D_DESC d{};
        tex->GetDesc(&d);

        // Todas as leituras são ABSOLUTAS: olham a cor de pixels fixos, nunca comparam com os vizinhos
        // (comparar com a vizinhança falha em cena clara e viraria "morte" falsa).
        auto lumAt = [](const BYTE* row, UINT x) { const BYTE* p = row + (size_t)x * 4; return (p[0] + p[1] + p[2]) / 3; };
        auto satAt = [](const BYTE* row, UINT x) {
            const BYTE* p = row + (size_t)x * 4;
            return std::max({(int)p[0], (int)p[1], (int)p[2]}) - std::min({(int)p[0], (int)p[1], (int)p[2]});
        };

        // ponta da linha do HUD: pixels de branco puro; 2 ou mais numa das 3 linhas candidatas = linha presente
        float best = 0;
        {
            UINT tx0 = (UINT)(d.Width * kTipX0), tx1 = (UINT)(d.Width * kTipX1), ty = (UINT)(d.Height * kTipY + 0.5f);
            if (ty >= 1) {
                band(tex.get(), d, stg, stgW, stgH, tx0, tx1, ty - 1, 3, [&](const BYTE* base, UINT pitch, UINT w) {
                    for (UINT c = 0; c < 3; ++c) {
                        UINT n = 0;
                        for (UINT x = 0; x < w; ++x)
                            if (lumAt(base + (size_t)c * pitch, x) >= kTipWhite) ++n;
                        best = std::max(best, std::min(1.f, n / 2.f));
                    }
                });
            }
        }

        // botão claro "CLOSE SETTINGS" (pausa): as 4 linhas internas quase brancas em quase toda a largura
        float pause = 0;
        {
            UINT px0 = (UINT)(d.Width * kPauseX0), px1 = (UINT)(d.Width * kPauseX1);
            UINT ya = (UINT)(d.Height * kPauseY[0] + 0.5f), yb = (UINT)(d.Height * kPauseY[3] + 0.5f);
            if (yb > ya) {
                band(tex.get(), d, stg2, stg2W, stg2H, px0, px1, ya, yb - ya + 1, [&](const BYTE* base, UINT pitch, UINT w) {
                    float worst = 1.f;
                    for (float fy : {kPauseY[0], kPauseY[3]}) {   // linhas acima e abaixo do texto do botão
                        UINT y = (UINT)(d.Height * fy + 0.5f);
                        UINT hit = 0;
                        for (UINT x = 0; x < w; ++x)
                            if (lumAt(base + (size_t)(y - ya) * pitch, x) > 225) ++hit;
                        worst = std::min(worst, (float)hit / w);
                    }
                    pause = worst;   // as duas linhas têm de estar quase todas brancas
                });
            }
        }

        // linha fina do cartão do jogador (menu de compra): uniforme, cinza-claro, ao longo de toda a largura
        float card = 0;
        {
            UINT cy = (UINT)(d.Height * kCardY + 0.5f), cx0 = (UINT)(d.Width * kCardX0), cx1 = (UINT)(d.Width * kCardX1);
            if (cy >= 1) {
                band(tex.get(), d, stg3, stg3W, stg3H, cx0, cx1, cy - 1, 3, [&](const BYTE* base, UINT pitch, UINT w) {
                    for (UINT c = 0; c < 3; ++c) {
                        const BYTE* r = base + (size_t)c * pitch;
                        UINT hit = 0;
                        for (UINT x = 0; x < w; ++x) {
                            int l = lumAt(r, x);
                            if (l >= 140 && l <= 175 && satAt(r, x) < 25) ++hit;
                        }
                        card = std::max(card, (float)hit / w);
                    }
                });
            }
        }

        // ícone de espectador: 9 pontos fixos com a cor exata (menta + branco); espectando = você está morto
        float spectate = 0;
        {
            auto sx = [&](int px) { return (UINT)(px / kSpecRefW * d.Width + 0.5f); };
            auto sy = [&](int py) { return (UINT)(py / kSpecRefH * d.Height + 0.5f); };
            UINT x0 = sx(136), x1 = sx(145) + 1, y0 = sy(854), y1 = sy(870) + 1;
            if (x1 > x0 && y1 > y0) {
                band(tex.get(), d, stg4, stg4W, stg4H, x0, x1, y0, y1 - y0, [&](const BYTE* base, UINT pitch, UINT w) {
                    auto match = [&](int px, int py, int r, int g, int b, int tol) {
                        UINT x = sx(px) - x0, y = sy(py) - y0;
                        if (x >= w || y >= y1 - y0) return false;
                        const BYTE* p = base + (size_t)y * pitch + (size_t)x * 4;   // BGRA
                        return std::abs(p[2] - r) <= tol && std::abs(p[1] - g) <= tol && std::abs(p[0] - b) <= tol;
                    };
                    int n = 0;
                    for (auto& m : kSpecMint) if (match(m[0], m[1], 169, 237, 224, 10)) ++n;
                    for (auto& m : kSpecWhite) if (match(m[0], m[1], 251, 252, 253, 8)) ++n;
                    spectate = n / 9.f;
                });
            }
        }

        frame.Close();
        last = Features{best, card, pause, spectate};
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
    s.stg = nullptr; s.stg2 = nullptr; s.stg3 = nullptr; s.stg4 = nullptr; s.ctx = nullptr; s.dev = nullptr;
    s.stgW = s.stgH = s.stg2W = s.stg2H = s.stg3W = s.stg3H = s.stg4W = s.stg4H = 0;
}
}  // namespace sq
