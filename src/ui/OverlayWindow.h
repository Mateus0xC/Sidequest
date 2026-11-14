#pragma once
#include "../platform/DwmPreview.h"
#include "CardRenderer.h"
#include "Gfx.h"
#include "View.h"
#include <functional>
#include <string>

namespace sq {
enum class OverlayMode { Full, Corner, Report };   // Report: janela normal, não fica por cima

struct PreviewParams {
    HWND src = nullptr;
    std::string corner = "bottom-right";
    int widthPct = 22;
    int margin = 24;
    bool background = false;   // true: jogo ao vivo borrado/escurecido atrás do cartão (sem miniatura)
};

// Janela sempre no topo, sem borda, sem ativação. Todo o desenho do cartão acontece uma vez por
// mudança de conteúdo; animações (fade/zoom/barra) rodam no compositor (DirectComposition).
class OverlayWindow {
public:
    OverlayWindow(Gfx& g, OverlayMode mode, std::string corner, int margin);
    ~OverlayWindow();
    bool create(HINSTANCE hi);

    void show(const View& v, HMONITOR mon, const PreviewParams* pv, double barSeconds);
    void update(const View& v);       // redesenha o cartão, sem animação
    void hide(bool animate);          // animação de ~100 ms
    void hideInstant();               // "espiar": some sem destruir nada
    void showInstant();
    bool visible() const { return visible_; }
    HWND hwnd() const { return hwnd_; }

    void setInteractive(bool on);     // digitação/relatório: aceita foco e cliques
    void activate();                  // SetForegroundWindow (só chamar de dentro de um WM_HOTKEY)

    std::function<void(wchar_t)> onChar;
    std::function<void()> onClick, onCloseAttempt, onHidden;
    std::function<void(int kind, int index)> onItem;   // clique: kind 0 = alternativa, 1 = barra de digitar

private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT, WPARAM, LPARAM);
    void renderCard(const View& v);
    void place();
    ComPtr<IDCompositionAnimation> anim(float from, float to, double dur, bool easeOut);
    ComPtr<IDCompositionSurface> solidSurface(D2D1_COLOR_F c);
    void releaseContent();
    void setBackdropBlur(bool on);

    Gfx& g_;
    CardRenderer renderer_;
    OverlayMode mode_;
    std::string corner_;
    int margin_;
    HWND hwnd_ = nullptr;
    bool visible_ = false, interactive_ = false, hiding_ = false;

    RECT mon_{};
    float scale_ = 1.f;
    float cardW_ = 0, cardH_ = 0, cardX_ = 0, cardY_ = 0;
    PreviewParams pv_;
    bool hasPv_ = false;
    DwmPreview preview_;
    double barSeconds_ = 0;

    ComPtr<IDCompositionTarget> target_;
    ComPtr<IDCompositionVisual2> root_, bgV_, cardV_, barV_;
    ComPtr<IDCompositionEffectGroup> rootFx_, bgFx_, cardFx_;
    ComPtr<IDCompositionScaleTransform> cardScale_, bgScale_, barScale_;
    ComPtr<IDCompositionSurface> cardSurf_, bgSolid_, bgDim_, barSurf_;
    bool blurOn_ = false;
    struct Hit { D2D1_RECT_F r; int kind, index; };
    std::vector<Hit> hits_;
    bool hitAt(LPARAM screenPtOrClient, bool isScreen, int* kind, int* index) const;
};
}  // namespace sq
