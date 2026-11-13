#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace sq {
struct HotkeySpec { UINT mods = 0, vk = 0; };

// "F1", "Ctrl+Alt+P", "Enter", "1", "Esc"...
bool parseHotkey(const std::string& text, HotkeySpec& out);

// RegisterHotKey agrupado: o grupo de respostas só fica registrado enquanto a tela está aberta.
class HotkeyManager {
public:
    explicit HotkeyManager(HWND hwnd) : hwnd_(hwnd) {}
    ~HotkeyManager();
    void add(int group, int action, const HotkeySpec& spec);
    void setGroup(int group, bool on);
    bool lookup(int id, int& action) const;
    std::vector<std::string> failures;  // teclas que outro programa já usa

private:
    struct Entry { int id, group, action; HotkeySpec spec; bool registered; };
    HWND hwnd_;
    int nextId_ = 100;
    std::vector<Entry> entries_;
};
}  // namespace sq
