#include "OverlayWindow.h"
#include <algorithm>
#include <cmath>

namespace sq {
namespace {
constexpr wchar_t kClass[] = L"SidequestOverlay";
constexpr UINT_PTR kHideTimer = 1;
constexpr double kEnter = 0.25, kExit = 0.10;
}  // namespace

OverlayWindow::OverlayWindow(Gfx& g, OverlayMode mode, std::string corner, int margin)
    : g_(g), renderer_(g), mode_(mode), corner_(std::move(corner)), margin_(margin) {}

OverlayWindow::~OverlayWindow() {
    preview_.detach();
    if (hwnd_) DestroyWindow(hwnd_);
}

bool OverlayWindow::create(HINSTANCE hi) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = &OverlayWindow::proc;
        wc.hInstance = hi;
        wc.lpszClassName = kClass;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        if (!RegisterClassExW(&wc)) return false;
        registered = true;
    }
    const bool report = mode_ == OverlayMode::Report;
    // Relatório: janela normal (aparece na barra de tarefas/Alt+Tab), nunca por cima do jogo
    DWORD ex = WS_EX_NOREDIRECTIONBITMAP | (report ? WS_EX_APPWINDOW : (WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE));
    hwnd_ = CreateWindowExW(ex, kClass, report ? L"Sidequest — Relatório da sessão" : L"Sidequest", WS_POPUP, 0, 0, 100, 100,
                            nullptr, nullptr, hi, this);
    if (!hwnd_) return false;
    if (report) interactive_ = true;   // aceita foco quando você for até ela
    if (FAILED(g_.dcomp->CreateTargetForHwnd(hwnd_, TRUE, &target_))) return false;
    g_.dcomp->CreateVisual(&root_);
    g_.dcomp->CreateVisual(&bgV_);
    g_.dcomp->CreateVisual(&cardV_);
    g_.dcomp->CreateVisual(&barV_);
    g_.dcomp->CreateEffectGroup(&rootFx_);
    g_.dcomp->CreateEffectGroup(&bgFx_);
    g_.dcomp->CreateEffectGroup(&cardFx_);
    g_.dcomp->CreateScaleTransform(&cardScale_);
    g_.dcomp->CreateScaleTransform(&bgScale_);
    g_.dcomp->CreateScaleTransform(&barScale_);
    if (mode_ == OverlayMode::Full) root_->AddVisual(bgV_.Get(), TRUE, nullptr);
    root_->AddVisual(cardV_.Get(), TRUE, mode_ == OverlayMode::Full ? bgV_.Get() : nullptr);
    root_->AddVisual(barV_.Get(), TRUE, cardV_.Get());
    target_->SetRoot(root_.Get());
    bgV_->SetBitmapInterpolationMode(DCOMPOSITION_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    barV_->SetBitmapInterpolationMode(DCOMPOSITION_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    return true;
}

ComPtr<IDCompositionAnimation> OverlayWindow::anim(float from, float to, double dur, bool easeOut) {
    ComPtr<IDCompositionAnimation> a;
    g_.dcomp->CreateAnimation(&a);
    float d = to - from, T = (float)dur;
    if (easeOut) a->AddCubic(0, from, 2 * d / T, -d / (T * T), 0);   // f(u) = 2u - u^2
    else a->AddCubic(0, from, d / T, 0, 0);
    a->End(dur, to);
    return a;
}

ComPtr<IDCompositionSurface> OverlayWindow::solidSurface(D2D1_COLOR_F c) {
    ComPtr<IDCompositionSurface> s;
    if (FAILED(g_.dcomp->CreateSurface(4, 4, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &s))) return nullptr;
    POINT off{};
    ComPtr<IDXGISurface> ds;
    if (FAILED(s->BeginDraw(nullptr, IID_PPV_ARGS(&ds), &off))) return nullptr;
    ComPtr<ID2D1Bitmap1> bmp;
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    g_.dc->CreateBitmapFromDxgiSurface(ds.Get(), &bp, &bmp);
    g_.dc->SetTarget(bmp.Get());
    g_.dc->BeginDraw();
    g_.dc->SetTransform(D2D1::Matrix3x2F::Translation((float)off.x, (float)off.y));
    g_.dc->PushAxisAlignedClip(D2D1::RectF(0, 0, 4, 4), D2D1_ANTIALIAS_MODE_ALIASED);   // já sob a translação do offset
    g_.dc->Clear(c);
    g_.dc->PopAxisAlignedClip();
    g_.dc->EndDraw();
    g_.dc->SetTarget(nullptr);
    s->EndDraw();
    return s;
}

void OverlayWindow::renderCard(const View& v) {
    float usableW = (float)(mon_.right - mon_.left), H = (float)(mon_.bottom - mon_.top);
    float x0 = 0;
    if (mode_ == OverlayMode::Full) {
        float pvCol = 0;
        if (hasPv_) {
            pvCol = usableW * pv_.widthPct / 100.f + 2 * pv_.margin;
            if (pv_.corner.find("left") != std::string::npos) x0 = pvCol;
            usableW -= pvCol;
        }
        cardW_ = std::min(1000.f * scale_, usableW - 80 * scale_);
    } else if (mode_ == OverlayMode::Report) {
        cardW_ = std::min(1500.f * scale_, usableW - 120 * scale_);
    } else {
        cardW_ = 440.f * scale_;
    }
    CardLayout L = renderer_.layout(v, cardW_, scale_);
    cardH_ = L.h;
    hits_.clear();
    for (size_t i = 0; i < L.choiceRects.size(); ++i) hits_.push_back({L.choiceRects[i], 0, (int)i});
    if (L.hasInput) hits_.push_back({L.inputRect, 1, 0});
    int w = (int)std::ceil(L.w), h = (int)std::ceil(L.h);

    ComPtr<IDCompositionSurface> surf;
    if (FAILED(g_.dcomp->CreateSurface(w, h, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &surf))) return;
    POINT off{};
    ComPtr<IDXGISurface> ds;
    if (FAILED(surf->BeginDraw(nullptr, IID_PPV_ARGS(&ds), &off))) return;
    ComPtr<ID2D1Bitmap1> bmp;
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    g_.dc->CreateBitmapFromDxgiSurface(ds.Get(), &bp, &bmp);
    g_.dc->SetTarget(bmp.Get());
    g_.dc->BeginDraw();
    g_.dc->SetTransform(D2D1::Matrix3x2F::Translation((float)off.x, (float)off.y));
    g_.dc->PushAxisAlignedClip(D2D1::RectF(0, 0, (float)w, (float)h), D2D1_ANTIALIAS_MODE_ALIASED);
    g_.dc->Clear(D2D1::ColorF(0, 0));
    renderer_.draw(g_.dc.Get(), L);
    g_.dc->PopAxisAlignedClip();
    g_.dc->EndDraw();
    g_.dc->SetTarget(nullptr);
    surf->EndDraw();
    cardSurf_ = surf;
    cardV_->SetContent(cardSurf_.Get());

    if (mode_ == OverlayMode::Full) {
        cardX_ = x0 + (usableW - cardW_) / 2;
        cardY_ = (H - cardH_) / 2;
    } else {
        cardX_ = cardY_ = 0;
    }
    cardV_->SetOffsetX(cardX_);
    cardV_->SetOffsetY(cardY_);
    cardScale_->SetCenterX(cardW_ / 2);
    cardScale_->SetCenterY(cardH_ / 2);
    cardV_->SetTransform(cardScale_.Get());
}

void OverlayWindow::place() {
    HWND after = HWND_TOPMOST;
    if (mode_ == OverlayMode::Full) {
        SetWindowPos(hwnd_, after, mon_.left, mon_.top, mon_.right - mon_.left, mon_.bottom - mon_.top,
                     SWP_NOACTIVATE);
    } else if (mode_ == OverlayMode::Report) {
        int w = (int)std::ceil(cardW_), h = (int)std::ceil(cardH_);
        int x = mon_.left + ((mon_.right - mon_.left) - w) / 2;
        int y = std::max<int>(mon_.top, mon_.top + ((mon_.bottom - mon_.top) - h) / 2);
        SetWindowPos(hwnd_, HWND_NOTOPMOST, x, y, w, h, SWP_NOACTIVATE);
    } else {
        int w = (int)std::ceil(cardW_), h = (int)std::ceil(cardH_);
        int m = (int)(margin_ * scale_);
        int x = corner_.find("left") != std::string::npos ? mon_.left + m : mon_.right - m - w;
        int y = corner_.find("top") != std::string::npos ? mon_.top + m : mon_.bottom - m - h;
        SetWindowPos(hwnd_, after, x, y, w, h, SWP_NOACTIVATE);
    }
}

void OverlayWindow::show(const View& v, HMONITOR mon, const PreviewParams* pv, double barSeconds) {
    KillTimer(hwnd_, kHideTimer);
    hiding_ = false;
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    mon_ = mi.rcMonitor;
    scale_ = std::clamp((float)(mon_.bottom - mon_.top) / 1080.f, 0.6f, 2.5f);
    const bool backdrop = pv && pv->src && pv->background && mode_ == OverlayMode::Full;
    hasPv_ = pv && pv->src && mode_ == OverlayMode::Full && !pv->background;
    if (hasPv_) pv_ = *pv;
    barSeconds_ = barSeconds;
    setBackdropBlur(backdrop);

    root_->SetEffect(nullptr);
    if (mode_ == OverlayMode::Full) {
        // jogo ao vivo atrás: camada escura translúcida (alfa pré-multiplicado) sobre o blur do DWM
        if (backdrop) {
            if (!bgDim_) bgDim_ = solidSurface(D2D1::ColorF(0.043f * 0.74f, 0.051f * 0.74f, 0.071f * 0.74f, 0.74f));
        } else if (!bgSolid_) {
            bgSolid_ = solidSurface(D2D1::ColorF(0x0B0D12, 1.f));
        }
        bgV_->SetContent(backdrop ? bgDim_.Get() : bgSolid_.Get());
        bgScale_->SetScaleX((mon_.right - mon_.left) / 4.f);
        bgScale_->SetScaleY((mon_.bottom - mon_.top) / 4.f);
        bgV_->SetTransform(bgScale_.Get());
        bgFx_->SetOpacity(anim(0.f, 1.f, 0.15, false).Get());
        bgV_->SetEffect(bgFx_.Get());
    }
    renderCard(v);
    cardFx_->SetOpacity(anim(0.f, 1.f, kEnter, true).Get());
    cardV_->SetEffect(cardFx_.Get());
    cardScale_->SetScaleX(anim(0.94f, 1.f, kEnter, true).Get());
    cardScale_->SetScaleY(anim(0.94f, 1.f, kEnter, true).Get());

    if (barSeconds > 0 && mode_ == OverlayMode::Corner) {
        if (!barSurf_) barSurf_ = solidSurface(D2D1::ColorF(0x5B8CFF, 1.f));
        float bw = cardW_ - 2 * 18.f * scale_, bh = 3.f * scale_;
        barV_->SetContent(barSurf_.Get());
        barV_->SetOffsetX(18.f * scale_);
        barV_->SetOffsetY(cardH_ - 9.f * scale_);
        barScale_->SetCenterX(0.f);
        barScale_->SetCenterY(0.f);
        barScale_->SetScaleY(bh / 4.f);
        barScale_->SetScaleX(anim(bw / 4.f, 0.f, barSeconds, false).Get());
        barV_->SetTransform(barScale_.Get());
    } else {
        barV_->SetContent(nullptr);
    }
    g_.dcomp->Commit();

    place();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    // relatório: vai para o fundo (atrás do jogo); as outras janelas ficam por cima de tudo
    SetWindowPos(hwnd_, mode_ == OverlayMode::Report ? HWND_BOTTOM : HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    visible_ = true;
    if (hasPv_) {
        int pw = (int)((mon_.right - mon_.left) * pv_.widthPct / 100.f);
        preview_.attach(hwnd_, pv_.src, pv_.corner, pw, (int)(pv_.margin), mon_.right - mon_.left,
                        mon_.bottom - mon_.top);
    }
}

void OverlayWindow::update(const View& v) {
    if (!visible_ || hiding_) return;
    renderCard(v);
    if (mode_ == OverlayMode::Corner) {
        barV_->SetOffsetY(cardH_ - 9.f * scale_);
        place();
    }
    g_.dcomp->Commit();
}

void OverlayWindow::hide(bool animate) {
    if (!visible_ || hiding_) return;
    if (!animate) { hideInstant(); releaseContent(); if (onHidden) onHidden(); return; }
    hiding_ = true;
    rootFx_->SetOpacity(anim(1.f, 0.f, kExit, false).Get());
    root_->SetEffect(rootFx_.Get());
    g_.dcomp->Commit();
    SetTimer(hwnd_, kHideTimer, (UINT)(kExit * 1000) + 25, nullptr);
}

void OverlayWindow::hideInstant() {
    if (!visible_) return;
    ShowWindow(hwnd_, SW_HIDE);
    visible_ = false;
}

void OverlayWindow::showInstant() {
    if (visible_ || !hwnd_) return;
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    visible_ = true;
}

// Blur-behind do DWM (borra o que está atrás da janela, ao vivo). API não documentada do user32,
// usada por vários apps; se faltar, só fica o escurecimento sem blur.
void OverlayWindow::setBackdropBlur(bool on) {
    if (blurOn_ == on || !hwnd_) return;
    struct Accent { int state, flags; unsigned color; int anim; };
    struct Data { int attr; void* data; SIZE_T size; };
    using Fn = BOOL(WINAPI*)(HWND, Data*);
    static Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute");
    if (!fn) return;
    Accent a{on ? 3 /*ACCENT_ENABLE_BLURBEHIND*/ : 0, 0, 0, 0};
    Data d{19 /*WCA_ACCENT_POLICY*/, &a, sizeof a};
    fn(hwnd_, &d);
    blurOn_ = on;
}

void OverlayWindow::releaseContent() {
    setBackdropBlur(false);
    preview_.detach();
    cardV_->SetContent(nullptr);
    barV_->SetContent(nullptr);
    cardSurf_.Reset();
    if (mode_ != OverlayMode::Report) setInteractive(false);
    hiding_ = false;
    g_.dcomp->Commit();
}

void OverlayWindow::setInteractive(bool on) {
    if (interactive_ == on || !hwnd_) return;
    interactive_ = on;
    LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    ex = on ? (ex & ~WS_EX_NOACTIVATE) : (ex | WS_EX_NOACTIVATE);
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex);
}

void OverlayWindow::activate() {
    setInteractive(true);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);
}

LRESULT CALLBACK OverlayWindow::proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCREATE) {
        auto* s = (OverlayWindow*)((CREATESTRUCTW*)l)->lpCreateParams;
        s->hwnd_ = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)s);
    }
    auto* self = (OverlayWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    return self ? self->handle(m, w, l) : DefWindowProcW(h, m, w, l);
}

// ponto em coordenadas de tela (WM_NCHITTEST) ou de cliente (WM_LBUTTONDOWN)
bool OverlayWindow::hitAt(LPARAM lp, bool isScreen, int* kind, int* index) const {
    POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
    if (isScreen) ScreenToClient(hwnd_, &p);
    float x = (float)p.x - cardX_, y = (float)p.y - cardY_;
    for (const Hit& h : hits_)
        if (x >= h.r.left && x < h.r.right && y >= h.r.top && y < h.r.bottom) {
            if (kind) *kind = h.kind;
            if (index) *index = h.index;
            return true;
        }
    return false;
}

LRESULT OverlayWindow::handle(UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_NCHITTEST:   // a janela inteira engole o clique (nada vaza para o jogo); só alternativas/barra reagem
            return (interactive_ || (visible_ && !hiding_)) ? HTCLIENT : HTTRANSPARENT;
        case WM_MOUSEACTIVATE: return interactive_ ? MA_ACTIVATE : MA_NOACTIVATE;
        case WM_ERASEBKGND: return 1;
        case WM_CLOSE:   // Alt+F4 não fecha o app: a saída é deliberada, pelo menu da bandeja
            if (mode_ == OverlayMode::Report) { hide(false); return 0; }   // fechar o relatório é seguro
            if (onCloseAttempt) onCloseAttempt();
            return 0;
        case WM_CHAR:
            if (onChar) onChar((wchar_t)w);
            return 0;
        case WM_LBUTTONDOWN: {
            int kind = 0, index = 0;
            if (!hiding_ && hitAt(l, false, &kind, &index) && onItem) onItem(kind, index);
            else if (onClick) onClick();
            return 0;
        }
        case WM_TIMER:
            if (w == kHideTimer) {
                KillTimer(hwnd_, kHideTimer);
                ShowWindow(hwnd_, SW_HIDE);
                visible_ = false;
                releaseContent();
                if (onHidden) onHidden();
            }
            return 0;
    }
    return DefWindowProcW(hwnd_, m, w, l);
}
}  // namespace sq
