#pragma once
#include <windows.h>
#include <string>

namespace sq {
// Maior janela visível, sem dono, cujo título contém `titleSub` (sem diferenciar maiúsculas)
// e cuja classe é `cls` (se não vazio). Ignora as janelas do próprio processo.
HWND findWindow(const std::string& titleSub, const std::string& cls);
}  // namespace sq
