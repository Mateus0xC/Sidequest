#include "WindowFinder.h"
#include "Utf.h"
#include <cwctype>
#include <algorithm>

namespace sq {
namespace {
struct Ctx {
    std::wstring title, cls;
    HWND best = nullptr;
    long long bestArea = -1;
    DWORD self = 0;
};

std::wstring lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

BOOL CALLBACK enumProc(HWND h, LPARAM lp) {
    auto* c = (Ctx*)lp;
    if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == c->self) return TRUE;
    wchar_t buf[512];
    int n = GetWindowTextW(h, buf, 512);
    if (n <= 0) return TRUE;
    if (!c->title.empty() && lower(std::wstring(buf, n)).find(c->title) == std::wstring::npos) return TRUE;
    if (!c->cls.empty()) {
        wchar_t cn[256];
        GetClassNameW(h, cn, 256);
        if (lower(cn) != c->cls) return TRUE;
    }
    RECT r;
    GetWindowRect(h, &r);
    long long area = (long long)(r.right - r.left) * (r.bottom - r.top);
    if (area > c->bestArea) { c->best = h; c->bestArea = area; }
    return TRUE;
}
}  // namespace

HWND findWindow(const std::string& titleSub, const std::string& cls) {
    if (titleSub.empty() && cls.empty()) return nullptr;
    Ctx c;
    c.title = lower(widen(titleSub));
    c.cls = lower(widen(cls));
    c.self = GetCurrentProcessId();
    EnumWindows(enumProc, (LPARAM)&c);
    return c.best;
}
}  // namespace sq
