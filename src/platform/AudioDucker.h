#pragma once
#include <windows.h>
#include <filesystem>

namespace sq {
// Abaixa/restaura o volume da sessão de áudio do processo `pid` (WASAPI, por aplicativo).
// Guarda o volume original em disco para restaurar mesmo após um crash.
class AudioDucker {
public:
    explicit AudioDucker(std::filesystem::path pendingFile) : pending_(std::move(pendingFile)) {}
    void duck(DWORD pid, float factor);
    void restore();
    // Se um crash anterior deixou o volume baixo, tenta restaurar (precisa do processo no ar).
    void recoverPending(DWORD pid);
    bool ducked() const { return ducked_; }

private:
    bool apply(DWORD pid, float* originalOut, float setTo, bool setAbsolute, float factor);
    std::filesystem::path pending_;
    bool ducked_ = false;
    DWORD pid_ = 0;
    float original_ = 1.0f;
};
}  // namespace sq
