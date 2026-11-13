#include "Hotkeys.h"
#include <algorithm>
#include <cctype>

namespace sq {

bool parseHotkey(const std::string& text, HotkeySpec& out) {
    out = {};
    std::string tok;
    std::vector<std::string> parts;
    for (char c : text + "+") {
        if (c == '+') { if (!tok.empty()) parts.push_back(tok); tok.clear(); }
        else if (c != ' ') tok.push_back((char)std::tolower((unsigned char)c));
    }
    if (parts.empty()) return false;
    for (size_t i = 0; i < parts.size(); ++i) {
        const std::string& p = parts[i];
        if (i + 1 < parts.size()) {
            if (p == "ctrl") out.mods |= MOD_CONTROL;
            else if (p == "alt") out.mods |= MOD_ALT;
            else if (p == "shift") out.mods |= MOD_SHIFT;
            else if (p == "win") out.mods |= MOD_WIN;
            else return false;
            continue;
        }
        if (p.size() == 1 && std::isalnum((unsigned char)p[0])) out.vk = (UINT)std::toupper((unsigned char)p[0]);
        else if (p[0] == 'f' && p.size() <= 3 && std::isdigit((unsigned char)p[1])) {
            int n = std::atoi(p.c_str() + 1);
            if (n < 1 || n > 24) return false;
            out.vk = VK_F1 + n - 1;
        }
        else if (p == "enter") out.vk = VK_RETURN;
        else if (p == "esc") out.vk = VK_ESCAPE;
        else if (p == "space") out.vk = VK_SPACE;
        else if (p == "tab") out.vk = VK_TAB;
        else if (p == "pause") out.vk = VK_PAUSE;
        else if (p == "insert") out.vk = VK_INSERT;
        else if (p == "delete") out.vk = VK_DELETE;
        else if (p == "home") out.vk = VK_HOME;
        else if (p == "end") out.vk = VK_END;
        else return false;
    }
    return out.vk != 0;
}

HotkeyManager::~HotkeyManager() {
    for (auto& e : entries_)
        if (e.registered) UnregisterHotKey(hwnd_, e.id);
}

void HotkeyManager::add(int group, int action, const HotkeySpec& spec) {
    entries_.push_back({nextId_++, group, action, spec, false});
}

void HotkeyManager::setGroup(int group, bool on) {
    for (auto& e : entries_) {
        if (e.group != group || e.registered == on) continue;
        if (on) {
            if (RegisterHotKey(hwnd_, e.id, e.spec.mods | MOD_NOREPEAT, e.spec.vk)) e.registered = true;
            else failures.push_back("tecla em uso por outro programa (ação " + std::to_string(e.action) + ")");
        } else {
            UnregisterHotKey(hwnd_, e.id);
            e.registered = false;
        }
    }
}

bool HotkeyManager::lookup(int id, int& action) const {
    for (auto& e : entries_)
        if (e.id == id) { action = e.action; return true; }
    return false;
}
}  // namespace sq
