#pragma once
#include <windows.h>
#include <filesystem>

namespace sq {
// Abaixa/restaura o volume da sessão de áudio do processo `pid` (WASAPI, por aplicativo).
// O volume original fica gravado em disco até a restauração dar certo: o Windows guarda o volume por
// aplicativo, então se o jogo fechar (ou o app cair) com o volume baixo, ele precisa ser recuperado depois.
class AudioDucker {
public:
    explicit AudioDucker(std::filesystem::path pendingFile) : pending_(std::move(pendingFile)) {}
    void duck(DWORD pid, float factor);
    bool restore();                        // false se não achou a sessão de áudio (o registro continua)
    // Se sobrou volume baixo de uma execução anterior, tenta restaurar (precisa do jogo com áudio no ar).
    void recoverPending(DWORD pid);
    bool setVolume(DWORD pid, float v);    // força o volume do aplicativo (ex.: "restaurar para 100%")
    bool ducked() const { return ducked_; }

private:
    bool apply(DWORD pid, float* originalOut, float setTo, bool setAbsolute, float factor);
    std::filesystem::path pending_;
    bool ducked_ = false;
    DWORD pid_ = 0;
    float original_ = 1.0f;
};
}  // namespace sq
