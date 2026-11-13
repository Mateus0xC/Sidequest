// Teste de risco da Etapa 1: o Windows Graphics Capture entrega frames reais da janela do Valorant?
// Uso: sq_wgc_probe.exe [classe-da-janela]   (padrão: VALORANTUnrealWindow)
// Só usa a API pública do Windows sobre a janela; não toca no processo do jogo.
#include <windows.h>
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
#include <cstdio>

using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

struct __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1")) IDirect3DDxgiInterfaceAccess : ::IUnknown {
    virtual HRESULT __stdcall GetInterface(REFIID iid, void** object) = 0;
};

int wmain(int argc, wchar_t** argv) {
    init_apartment(apartment_type::multi_threaded);
    const wchar_t* cls = argc > 1 ? argv[1] : L"VALORANTUnrealWindow";
    HWND hwnd = FindWindowW(cls, nullptr);
    if (!hwnd) { wprintf(L"FALHA: janela de classe '%s' não encontrada\n", cls); return 2; }
    if (!GraphicsCaptureSession::IsSupported()) { wprintf(L"FALHA: WGC não suportado neste Windows\n"); return 3; }

    auto interop = get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item{nullptr};
    HRESULT hr = interop->CreateForWindow(hwnd, guid_of<GraphicsCaptureItem>(), put_abi(item));
    if (FAILED(hr)) { wprintf(L"FALHA: CreateForWindow hr=0x%08X\n", (unsigned)hr); return 4; }
    auto size = item.Size();
    wprintf(L"item criado: %dx%d\n", size.Width, size.Height);

    com_ptr<ID3D11Device> dev;
    com_ptr<ID3D11DeviceContext> ctx;
    check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                    nullptr, 0, D3D11_SDK_VERSION, dev.put(), nullptr, ctx.put()));
    com_ptr<IDXGIDevice> dxgi = dev.as<IDXGIDevice>();
    com_ptr<::IInspectable> insp;
    check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), insp.put()));
    auto d3dDev = insp.as<IDirect3DDevice>();

    auto pool = Direct3D11CaptureFramePool::CreateFreeThreaded(d3dDev, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
    auto session = pool.CreateCaptureSession(item);
    std::atomic<int> frames{0};
    pool.FrameArrived([&](auto&&, auto&&) { frames++; });
    session.StartCapture();

    // pega um frame para analisar os pixels
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    Direct3D11CaptureFrame frame{nullptr};
    while (clock::now() - t0 < std::chrono::seconds(3)) {
        frame = pool.TryGetNextFrame();
        if (frame) break;
        Sleep(20);
    }
    if (!frame) { wprintf(L"FALHA: nenhum frame em 3 s\n"); return 5; }

    com_ptr<ID3D11Texture2D> tex;
    check_hresult(frame.Surface().as<IDirect3DDxgiInterfaceAccess>()->GetInterface(guid_of<ID3D11Texture2D>(), tex.put_void()));
    D3D11_TEXTURE2D_DESC d{};
    tex->GetDesc(&d);
    D3D11_TEXTURE2D_DESC sd = d;
    sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.MiscFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    com_ptr<ID3D11Texture2D> stg;
    check_hresult(dev->CreateTexture2D(&sd, nullptr, stg.put()));
    ctx->CopyResource(stg.get(), tex.get());
    D3D11_MAPPED_SUBRESOURCE m{};
    check_hresult(ctx->Map(stg.get(), 0, D3D11_MAP_READ, 0, &m));
    int total = 0, lit = 0;
    double sum = 0;
    for (UINT y = d.Height / 40; y < d.Height; y += d.Height / 20)
        for (UINT x = d.Width / 40; x < d.Width; x += d.Width / 20) {
            const BYTE* p = (const BYTE*)m.pData + (size_t)y * m.RowPitch + (size_t)x * 4;
            int v = p[0] + p[1] + p[2];
            sum += v / 3.0;
            if (v > 30) lit++;
            total++;
        }
    ctx->Unmap(stg.get(), 0);

    // taxa de entrega: consome e libera frames durante 3 s (o WGC só entrega quando a imagem muda)
    frame.Close();
    int got = 0;
    auto t1 = clock::now();
    while (clock::now() - t1 < std::chrono::seconds(3)) {
        if (auto f = pool.TryGetNextFrame()) { ++got; f.Close(); }
        else Sleep(4);
    }
    session.Close();
    pool.Close();
    wprintf(L"frame %ux%u | amostras não-pretas: %d/%d | brilho médio: %.1f/255 | frames entregues em 3 s: %d (~%.1f fps)\n",
            d.Width, d.Height, lit, total, sum / total, got, got / 3.0);
    wprintf(lit * 4 >= total ? L"RESULTADO: OK (frames reais)\n" : L"RESULTADO: SUSPEITO (imagem quase toda preta)\n");
    return lit * 4 >= total ? 0 : 1;
}
