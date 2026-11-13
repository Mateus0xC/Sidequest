#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <string>

namespace sq {
// Miniatura ao vivo de outra janela, desenhada pelo próprio DWM (sem copiar frames).
class DwmPreview {
public:
    ~DwmPreview() { detach(); }
    // Posiciona num canto do cliente de `dest`, com largura `widthPx`. Retorna false se não deu.
    bool attach(HWND dest, HWND src, const std::string& corner, int widthPx, int margin, int clientW, int clientH);
    void detach();
    bool active() const { return thumb_ != nullptr; }

private:
    HTHUMBNAIL thumb_ = nullptr;
};
}  // namespace sq
