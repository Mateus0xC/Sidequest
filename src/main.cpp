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
    std::error_code ec;
    if (fs::exists("config.json", ec)) return fs::absolute("config.json");
    // procura ao lado do .exe e nas pastas acima (ex.: .exe em ...\build, config.json em ...\)
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    fs::path dir = fs::path(exe).parent_path();
    for (int i = 0; i < 5 && !dir.empty(); ++i, dir = dir.parent_path()) {
        if (fs::exists(dir / "config.json", ec)) return dir / "config.json";
        if (dir == dir.parent_path()) break;
    }
    return fs::path(exe).parent_path() / "config.json";   // não existe: o app usa os padrões e avisa
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
