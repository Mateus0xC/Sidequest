#pragma once
#include "../core/DeathDetector.h"
#include <windows.h>
#include <memory>

namespace sq {
// Windows Graphics Capture da janela do jogo, a ~4 quadros/s (MinUpdateInterval). Lê só uma faixa
// pequena do HUD (copia apenas essa região da GPU) e acorda o app com `msg` a cada leitura.
// Só usa a API pública do Windows sobre a janela; não toca no processo do jogo.
class CaptureSampler {
public:
    CaptureSampler();
    ~CaptureSampler();
    bool start(HWND target, HWND notify, UINT msg);
    void stop();
    bool running() const;
    Features last() const;       // última leitura
    unsigned long long samples() const;
    int borderState() const;     // -1 desconhecido, 0 sem borda, 1 com borda amarela

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
}  // namespace sq
