#include "app/App.h"
#include <objbase.h>
#include <shellapi.h>
#include <filesystem>

namespace fs = std::filesystem;

static fs::path findConfig() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    fs::path p;
    if (argv && argc > 1) p = argv[1];
    if (argv) LocalFree(argv);
    if (!p.empty()) return p;
    if (fs::exists("config.json")) return fs::absolute("config.json");
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return fs::path(exe).parent_path() / "config.json";
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\SidequestSingleton");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"O Sidequest já está aberto (veja o ícone na bandeja).", L"Sidequest", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int rc = 1;
    {
        sq::App app(hi, findConfig());
        if (app.init()) rc = app.run();
        else MessageBoxW(nullptr, L"Falha ao iniciar o Sidequest.", L"Sidequest", MB_OK | MB_ICONERROR);
    }
    CoUninitialize();
    CloseHandle(mutex);
    return rc;
}
