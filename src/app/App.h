#pragma once
#include "../core/Config.h"
#include "../core/Progress.h"
#include "../core/Question.h"
#include "../core/Session.h"
#include "../platform/AudioDucker.h"
#include "../platform/Hotkeys.h"
#include "../ui/Gfx.h"
#include "../ui/OverlayWindow.h"
#include <windows.h>
#include <memory>
#include <set>
#include <string>

namespace sq {

// Máquina de estados do app: ocioso/vivo -> morto (pergunta em tela cheia) -> fase de compra -> relatório.
// Tudo orientado a eventos (hotkeys, timers de uma vez só); nenhum laço de redesenho.
class App {
public:
    App(HINSTANCE hi, std::filesystem::path configPath);
    ~App();
    bool init();
    int run();

private:
    enum class Ctx { None, Ask, GapPrompt, GapTyping, Reveal, Report };
    enum class BuyCtx { None, Pill, Question, Answered };
    struct Active {
        const Question* q = nullptr;
        std::set<int> sel;
        std::wstring typed;
        bool correct = false, committed = false;
    };

    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT, WPARAM, LPARAM);
    void onAction(int action);
    void onTimer(UINT_PTR id);
    void onTray(LPARAM lp);

    // morte
    void onDie();
    void onRevive();
    void onRound();
    void onMatchEnd();
    void onPeek();
    void onNotNow();
    void onChoiceKey(int n);
    void onConfirm();
    void onOverlayChar(wchar_t c);
    void onItemClick(int kind, int index);
    void showDeathQuestion(const Question* q, bool fresh);
    void nextDeathCard();
    void leaveDeath();
    void beginTyping();
    void submitGap();
    void commitDeath();
    void endPeek();
    void dismissReport();
    void showReport();

    // compra
    void startBuy();
    void endBuy(bool animate);
    void onBuyChoice(int n);

    // helpers
    View questionView(const Active& a, bool compact, bool reveal, bool typing, bool buy) const;
    static bool evaluate(const Question& q, const std::set<int>& sel);
    bool ensureGfx();
    void ensureSession();
    HWND target() const;
    HMONITOR monitorFor(HWND t) const;
    void focusTarget();
    void duckAudio();
    void restoreAudio();
    void closeAttempt();
    void balloon(const std::wstring& title, const std::wstring& text);
    void requestExit();
    void quit();
    std::wstring keyHint() const;

    HINSTANCE hi_;
    std::filesystem::path configPath_;
    Config cfg_;
    std::vector<std::string> warnings_;
    HWND ctl_ = nullptr;
    UINT taskbarCreated_ = 0;

    QuestionBank bank_;
    ProgressStore store_;
    std::unique_ptr<Session> session_;
    std::unique_ptr<HotkeyManager> hk_;
    AudioDucker ducker_;

    Gfx gfx_;
    std::unique_ptr<OverlayWindow> deathWin_, buyWin_;

    bool dead_ = false, deathPending_ = false, peeking_ = false, exiting_ = false;
    Ctx ctx_ = Ctx::None;
    Active death_;
    BuyCtx buyCtx_ = BuyCtx::None;
    Active buy_;
    int64_t lastQuestionEnd_ = 0;
};
}  // namespace sq
