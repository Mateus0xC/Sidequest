#include "App.h"
#include "../core/Text.h"
#include "../platform/Utf.h"
#include "../platform/WindowFinder.h"
#include <shellapi.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace fs = std::filesystem;

namespace sq {
namespace {
constexpr UINT WM_TRAY = WM_APP + 1;
enum Timer : UINT_PTR { T_DEATH = 1, T_PEEK, T_BUY, T_BUYDONE, T_EXIT, T_WATCH, T_TEACH };
constexpr UINT WM_SAMPLE = WM_APP + 3;
enum Action { A_DIE = 1, A_REVIVE, A_ROUND, A_END, A_PEEK, A_NOTNOW, A_CH = 10, A_CONFIRM = 20 };
enum Group { G_GLOBAL = 0, G_ANSWER = 1 };
enum Menu { ID_REPORT = 1, ID_EXIT = 2 };
constexpr wchar_t kCtlClass[] = L"SidequestControl";
AudioDucker* g_ducker = nullptr;

int64_t nowSec() { return (int64_t)time(nullptr); }

fs::path g_logPath;
void logf(const char* fmt, ...) {
    if (g_logPath.empty()) return;
    FILE* f = nullptr;
    if (_wfopen_s(&f, g_logPath.c_str(), L"a") || !f) return;
    time_t t = time(nullptr);
    tm lt;
    localtime_s(&lt, &t);
    fprintf(f, "%02d:%02d:%02d ", lt.tm_hour, lt.tm_min, lt.tm_sec);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

LONG WINAPI crashFilter(EXCEPTION_POINTERS*) {
    if (g_ducker) g_ducker->restore();   // devolve o volume do Valorant mesmo se o app cair
    return EXCEPTION_EXECUTE_HANDLER;
}
}  // namespace

App::App(HINSTANCE hi, fs::path configPath)
    : hi_(hi), configPath_(fs::absolute(configPath)), ducker_(fs::absolute(configPath).parent_path() / "volume_pendente.txt") {}

App::~App() { g_ducker = nullptr; }

bool App::init() {
    cfg_ = fs::exists(configPath_) ? Config::load(configPath_, warnings_) : Config::defaults(configPath_.parent_path());
    if (!fs::exists(configPath_)) warnings_.push_back("config.json não encontrado; usando padrões");

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = &App::proc;
    wc.hInstance = hi_;
    wc.lpszClassName = kCtlClass;
    RegisterClassExW(&wc);
    ctl_ = CreateWindowExW(WS_EX_TOOLWINDOW, kCtlClass, L"Sidequest", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, hi_, this);
    if (!ctl_) return false;
    taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");

    hk_ = std::make_unique<HotkeyManager>(ctl_);
    struct { const std::string& key; int act; } globals[] = {
        {cfg_.keys.die, A_DIE}, {cfg_.keys.revive, A_REVIVE}, {cfg_.keys.roundStart, A_ROUND},
        {cfg_.keys.matchEnd, A_END}, {cfg_.keys.peek, A_PEEK}, {cfg_.keys.notNow, A_NOTNOW}};
    for (auto& g : globals) {
        HotkeySpec s;
        if (parseHotkey(g.key, s)) hk_->add(G_GLOBAL, g.act, s);
        else warnings_.push_back("tecla inválida: " + g.key);
    }
    for (int i = 0; i < 6; ++i) {
        HotkeySpec s;
        parseHotkey(std::to_string(i + 1), s);
        hk_->add(G_ANSWER, A_CH + i, s);
    }
    HotkeySpec enter;
    parseHotkey("Enter", enter);
    hk_->add(G_ANSWER, A_CONFIRM, enter);
    hk_->setGroup(G_GLOBAL, true);
    g_logPath = configPath_.parent_path() / "sidequest.log";
    logf("--- iniciado; teclas globais registradas, falhas=%zu", hk_->failures.size());
    for (auto& f : hk_->failures) logf("falha de tecla: %s", f.c_str());

    bank_.load(cfg_.questionsDir);
    for (auto& w : bank_.warnings) warnings_.push_back(w);
    std::string err;
    if (!store_.load(cfg_.statePath, err)) warnings_.push_back(err);
    session_ = std::make_unique<Session>(bank_, store_, cfg_);

    calibPath_ = cfg_.baseDir / "calibracao.json";
    det_.load(calibPath_);
    if (cfg_.detectEnabled) {
        logf("detecção: aprendido vivo=%d morto=%d", det_.taughtAlive(), det_.taughtDead());
        watchTick();
    }

    g_ducker = &ducker_;
    SetUnhandledExceptionFilter(crashFilter);
    if (HWND t = target()) {
        DWORD pid = 0;
        GetWindowThreadProcessId(t, &pid);
        ducker_.recoverPending(pid);
    }

    NOTIFYICONDATAW nid{sizeof nid};
    nid.hWnd = ctl_;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"Sidequest");
    Shell_NotifyIconW(NIM_ADD, &nid);

    std::wstring msg = std::to_wstring(bank_.all().size()) + L" questões em " + std::to_wstring(bank_.contentCount()) +
                       L" conteúdos. " + widen(cfg_.keys.die) + L" morri · " + widen(cfg_.keys.revive) + L" revivi · " +
                       widen(cfg_.keys.roundStart) + L" round · " + widen(cfg_.keys.matchEnd) + L" fim.";
    if (cfg_.detectEnabled && !det_.ready())
        msg += L"\nCalibrando a detecção: aperte " + widen(cfg_.keys.die) + L" ao morrer e " + widen(cfg_.keys.roundStart) +
               L" no início do round (3 de cada).";
    if (bank_.all().empty()) msg =L"Nenhuma questão carregada em " + cfg_.questionsDir.wstring();
    for (auto& f : hk_->failures) warnings_.push_back(f);
    if (!warnings_.empty()) msg += L"\nAviso: " + widen(warnings_.front()) + (warnings_.size() > 1 ? L" (+" + std::to_wstring(warnings_.size() - 1) + L")" : L"");
    balloon(L"Sidequest pronto", msg);
    return true;
}

int App::run() {
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    restoreAudio();
    NOTIFYICONDATAW nid{sizeof nid};
    nid.hWnd = ctl_;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    return 0;
}

// ---------------------------------------------------------------- mensagens

LRESULT CALLBACK App::proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCREATE) {
        auto* a = (App*)((CREATESTRUCTW*)l)->lpCreateParams;
        a->ctl_ = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)a);
    }
    auto* self = (App*)GetWindowLongPtrW(h, GWLP_USERDATA);
    return self ? self->handle(m, w, l) : DefWindowProcW(h, m, w, l);
}

LRESULT App::handle(UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_HOTKEY: {
            int act;
            if (hk_ && hk_->lookup((int)w, act)) onAction(act);
            return 0;
        }
        case WM_TIMER: onTimer(w); return 0;
        case WM_TRAY: onTray(l); return 0;
        case WM_SAMPLE: onSample(); return 0;
        case WM_COMMAND:
            if (LOWORD(w) == ID_REPORT) onMatchEnd();
            else if (LOWORD(w) == ID_EXIT) requestExit();
            return 0;
        case WM_CLOSE: closeAttempt(); return 0;
        case WM_QUERYENDSESSION: return TRUE;
        case WM_ENDSESSION:
            if (w) quit();
            return 0;
        default:
            if (taskbarCreated_ && m == taskbarCreated_) {
                NOTIFYICONDATAW nid{sizeof nid};
                nid.hWnd = ctl_; nid.uID = 1;
                nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
                nid.uCallbackMessage = WM_TRAY;
                nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
                wcscpy_s(nid.szTip, L"Sidequest");
                Shell_NotifyIconW(NIM_ADD, &nid);
                return 0;
            }
    }
    return DefWindowProcW(ctl_, m, w, l);
}

// Verifica a cada ~25 s se a janela do jogo existe e liga/desliga a captura (nada roda sem o jogo).
void App::watchTick() {
    if (!cfg_.detectEnabled) return;
    HWND t = target();
    if (t && !cap_.running()) {
        bool ok = cap_.start(t, ctl_, WM_SAMPLE);
        logf("captura iniciada ok=%d borda=%d", (int)ok, cap_.borderState());
    } else if (!t && cap_.running()) {
        cap_.stop();
        logf("captura parada (janela do jogo sumiu)");
    }
    SetTimer(ctl_, T_WATCH, 25000, nullptr);
}

void App::onSample() {
    Features f = cap_.last();
    unsigned long long n = cap_.samples();
    if (n % 40 == 1)
        logf("amostra teal=%.4f white=%.4f mean=%.3f | estado=%d aprendido vivo=%d morto=%d", f.teal, f.white, f.mean,
             (int)det_.state(), det_.taughtAlive(), det_.taughtDead());
    auto ev = det_.observe(f);
    if (!ev) return;
    logf("detector: %s (dead_=%d)", *ev == LifeState::Dead ? "MORTO" : "VIVO", (int)dead_);
    if (*ev == LifeState::Dead && !dead_) onDie();
    else if (*ev == LifeState::Alive && dead_) onRevive();
}

// As hotkeys manuais são o "gabarito": 0,9 s depois (o HUD já mudou) a leitura vira exemplo.
void App::scheduleTeach(LifeState s) {
    if (!cfg_.detectEnabled || !cap_.running()) return;
    teachLabel_ = s;
    SetTimer(ctl_, T_TEACH, 900, nullptr);
}

void App::onAction(int a) {
    logf("hotkey acao=%d dead=%d ctx=%d", a, (int)dead_, (int)ctx_);
    if (a == A_DIE) { det_.setState(LifeState::Dead); scheduleTeach(LifeState::Dead); }
    else if (a == A_REVIVE || a == A_ROUND) { det_.setState(LifeState::Alive); scheduleTeach(LifeState::Alive); }
    if (a == A_DIE) onDie();
    else if (a == A_REVIVE) onRevive();
    else if (a == A_ROUND) onRound();
    else if (a == A_END) onMatchEnd();
    else if (a == A_PEEK) onPeek();
    else if (a == A_NOTNOW) onNotNow();
    else if (a >= A_CH && a < A_CH + 6) onChoiceKey(a - A_CH);
    else if (a == A_CONFIRM) onConfirm();
}

void App::onTimer(UINT_PTR id) {
    KillTimer(ctl_, id);   // todos os timers são de uma vez só
    switch (id) {
        case T_DEATH:
            deathPending_ = false;
            if (!dead_) return;
            duckAudio();
            if (ctx_ != Ctx::None || cfg_.deathDisplay == Display::Off) return;
            if (cfg_.minGapSeconds > 0 && nowSec() - lastQuestionEnd_ < cfg_.minGapSeconds) return;
            if (ensureGfx()) {
                ensureSession();
                const Question* q = session_->nextQuestion(Slot::Death, nowSec());
                HWND t = target();
                logf("morte: pergunta=%s alvo=%p", q ? q->key().c_str() : "(nenhuma)", (void*)t);
                if (q) {
                    showDeathQuestion(q, true);
                    logf("overlay visivel=%d", (int)deathWin_->visible());
                }
            } else {
                logf("morte: falha ao iniciar GPU");
            }
            break;
        case T_WATCH: watchTick(); break;
        case T_TEACH: {
            if (!cap_.running()) break;
            bool wasReady = det_.ready();
            det_.teach(teachLabel_, cap_.last());
            det_.setState(teachLabel_);
            det_.save(calibPath_);
            Features f = cap_.last();
            logf("aprendeu %s: teal=%.4f white=%.4f mean=%.3f | vivo=%d morto=%d", teachLabel_ == LifeState::Dead ? "MORTO" : "VIVO",
                 f.teal, f.white, f.mean, det_.taughtAlive(), det_.taughtDead());
            if (!wasReady && det_.ready()) balloon(L"Sidequest", L"Detecção automática de morte calibrada e ativa.");
            break;
        }
        case T_PEEK: endPeek(); break;
        case T_BUY: endBuy(true); break;
        case T_BUYDONE: endBuy(true); break;
        case T_EXIT:
            if (MessageBoxW(nullptr, L"Confirmar: sair do Sidequest agora?\n(se houver sessão em andamento, a saída fica registrada)",
                            L"Sidequest", MB_YESNO | MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND) == IDYES)
                quit();
            exiting_ = false;
            break;
    }
}

void App::onTray(LPARAM lp) {
    if (LOWORD(lp) != WM_RBUTTONUP && LOWORD(lp) != WM_LBUTTONUP) return;
    HMENU menu = CreatePopupMenu();
    std::wstring status = session_ && session_->active()
        ? L"Sessão: " + std::to_wstring(session_->stats().answered) + L" respondidas, " + std::to_wstring(session_->stats().correct) + L" acertos"
        : L"Nenhuma sessão ativa";
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, status.c_str());
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, (L"Saídas no meio de sessão: " + std::to_wstring(store_.midExits)).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_REPORT, L"Encerrar sessão e ver relatório");
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"Sair...");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(ctl_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, ctl_, nullptr);
    PostMessageW(ctl_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

// ---------------------------------------------------------------- morte

void App::onDie() {
    if (ctx_ == Ctx::Report) dismissReport();
    if (dead_) return;
    ensureSession();
    endBuy(false);
    dead_ = true;
    deathPending_ = true;   // o volume só baixa depois da espera (junto com a pergunta)
    SetTimer(ctl_, T_DEATH, (UINT)std::max(0, cfg_.deathDelayMs), nullptr);
}

void App::onRevive() {
    if (ctx_ == Ctx::Report) dismissReport();
    leaveDeath();
}

void App::onRound() {
    if (ctx_ == Ctx::Report) dismissReport();
    leaveDeath();
    ensureSession();
    if (HWND t = target()) {
        DWORD pid = 0;
        GetWindowThreadProcessId(t, &pid);
        ducker_.recoverPending(pid);
    }
    startBuy();
}

void App::leaveDeath() {
    KillTimer(ctl_, T_DEATH);
    KillTimer(ctl_, T_PEEK);
    if (ctx_ == Ctx::Ask || ctx_ == Ctx::GapPrompt || ctx_ == Ctx::GapTyping) {
        if (death_.q) session_->interrupted(*death_.q);       // sem penalidade, volta à frente da fila
        lastQuestionEnd_ = nowSec();
    } else if (ctx_ == Ctx::Reveal) {
        if (!death_.committed) commitDeath();
    }
    hk_->setGroup(G_ANSWER, false);
    bool fg = deathWin_ && GetForegroundWindow() == deathWin_->hwnd();
    if (deathWin_ && ctx_ != Ctx::None) deathWin_->hide(true);
    if (fg) focusTarget();
    ctx_ = Ctx::None;
    dead_ = deathPending_ = peeking_ = false;
    restoreAudio();
}

void App::showDeathQuestion(const Question* q, bool fresh) {
    death_ = Active{};
    death_.q = q;
    ctx_ = q->type == QType::Gap ? Ctx::GapPrompt : Ctx::Ask;
    View v = questionView(death_, false, false, false, false);
    if (fresh) {
        HWND t = target();
        PreviewParams pv;
        pv.src = cfg_.previewEnabled ? t : nullptr;
        pv.corner = cfg_.previewCorner;
        pv.widthPct = cfg_.previewWidthPct;
        pv.margin = cfg_.previewMargin;
        pv.background = cfg_.previewMode != "canto";
        deathWin_->show(v, monitorFor(t), &pv, 0);
    } else {
        deathWin_->update(v);
    }
    hk_->setGroup(G_ANSWER, true);
}

void App::nextDeathCard() {
    if (const Question* q = session_->nextQuestion(Slot::Death, nowSec())) {
        showDeathQuestion(q, false);
        return;
    }
    hk_->setGroup(G_ANSWER, false);
    deathWin_->hide(true);
    ctx_ = Ctx::None;
}

void App::commitDeath() {
    if (!death_.q || death_.committed) return;
    session_->answered(*death_.q, death_.correct, true, nowSec());
    death_.committed = true;
    lastQuestionEnd_ = nowSec();
}

void App::onChoiceKey(int n) {
    if (buyCtx_ == BuyCtx::Question && !dead_) { onBuyChoice(n); return; }
    if (ctx_ == Ctx::Ask && death_.q && n < (int)death_.q->alts.size()) {
        if (death_.q->type == QType::Multi) {
            if (!death_.sel.erase(n)) death_.sel.insert(n);
        } else {
            death_.sel = {n};
        }
        deathWin_->update(questionView(death_, false, false, false, false));
    } else if (ctx_ == Ctx::Reveal && death_.q && death_.q->type == QType::Gap && !death_.committed && n < 2) {
        death_.correct = (n == 0);                              // "eu acertei" / "eu errei"
        commitDeath();
        deathWin_->update(questionView(death_, false, true, false, false));
    }
}

void App::onConfirm() {
    if (ctx_ == Ctx::Report) { dismissReport(); return; }
    if (ctx_ == Ctx::Ask && death_.q) {
        if (death_.sel.empty()) return;
        death_.correct = evaluate(*death_.q, death_.sel);
        ctx_ = Ctx::Reveal;
        commitDeath();
        deathWin_->update(questionView(death_, false, true, false, false));
    } else if (ctx_ == Ctx::GapPrompt) {
        beginTyping();
    } else if (ctx_ == Ctx::Reveal) {
        commitDeath();
        nextDeathCard();
    }
}

void App::beginTyping() {
    hk_->setGroup(G_ANSWER, false);   // dígitos e Enter voltam a ser teclas normais
    ctx_ = Ctx::GapTyping;
    deathWin_->update(questionView(death_, false, false, true, false));
    deathWin_->activate();            // o único caso em que o app pega o foco
}

void App::submitGap() {
    death_.correct = answerMatches(narrow(death_.typed), death_.q->accepted);
    ctx_ = Ctx::Reveal;
    deathWin_->setInteractive(false);
    focusTarget();                    // devolve o foco ao jogo
    hk_->setGroup(G_ANSWER, true);
    deathWin_->update(questionView(death_, false, true, false, false));
}

// Clique numa alternativa ou na barra de digitar.
void App::onItemClick(int kind, int index) {
    if (buyCtx_ == BuyCtx::Question && !dead_) {
        if (kind == 0) onBuyChoice(index);
        return;
    }
    if (ctx_ == Ctx::Ask && death_.q && kind == 0) {
        onChoiceKey(index);
        if (death_.q->type != QType::Multi) onConfirm();   // única/VF: clicar já responde; várias: marca/desmarca e Enter confirma
    } else if (ctx_ == Ctx::GapPrompt && kind == 1) {
        beginTyping();
    }
}

void App::onOverlayChar(wchar_t c) {
    if (ctx_ == Ctx::Report) {
        if (c == 13 || c == 27) dismissReport();
        return;
    }
    if (ctx_ != Ctx::GapTyping) return;
    if (c == 8) { if (!death_.typed.empty()) death_.typed.pop_back(); }
    else if (c == 13) { submitGap(); return; }
    else if (c >= 32 && death_.typed.size() < 80) death_.typed.push_back(c);
    deathWin_->update(questionView(death_, false, false, true, false));
}

void App::onPeek() {
    if (peeking_) { KillTimer(ctl_, T_PEEK); endPeek(); return; }
    if (!dead_ || ctx_ == Ctx::None || ctx_ == Ctx::Report || !deathWin_ || !deathWin_->visible()) return;
    peeking_ = true;
    if (ctx_ == Ctx::GapTyping) {     // sai da digitação para o jogo receber as teclas
        ctx_ = Ctx::GapPrompt;
        deathWin_->setInteractive(false);
        focusTarget();
    }
    hk_->setGroup(G_ANSWER, false);
    deathWin_->hideInstant();
    SetTimer(ctl_, T_PEEK, (UINT)(std::max(1, cfg_.peekSeconds) * 1000), nullptr);
}

void App::endPeek() {
    if (!peeking_) return;
    peeking_ = false;
    if (!dead_ || ctx_ == Ctx::None) return;
    deathWin_->showInstant();
    hk_->setGroup(G_ANSWER, true);
}

void App::onNotNow() {
    if (buyCtx_ != BuyCtx::None && !dead_) { endBuy(true); return; }
    if (!dead_ || ctx_ == Ctx::None || ctx_ == Ctx::Report) return;
    KillTimer(ctl_, T_PEEK);
    peeking_ = false;
    if (ctx_ == Ctx::Ask || ctx_ == Ctx::GapPrompt || ctx_ == Ctx::GapTyping) {
        if (death_.q) session_->interrupted(*death_.q);
    } else if (ctx_ == Ctx::Reveal) {
        commitDeath();
    }
    lastQuestionEnd_ = nowSec();
    hk_->setGroup(G_ANSWER, false);
    bool fg = GetForegroundWindow() == deathWin_->hwnd();
    deathWin_->hide(true);
    if (fg) focusTarget();
    ctx_ = Ctx::None;
}

void App::onMatchEnd() {
    if (ctx_ == Ctx::Report) return;
    leaveDeath();
    endBuy(false);
    ensureSession();
    session_->end("fim_partida", nowSec());
    showReport();
}

void App::showReport() {
    if (!ensureGfx()) return;
    View v;
    v.tag = L"RELATÓRIO DA SESSÃO";
    v.title = L"Fim de partida";
    for (auto& l : session_->reportLines(nowSec())) v.lines.push_back(widen(l));
    v.lineMark.assign(v.lines.size(), 0);

    // lista das perguntas respondidas (as últimas, para caber na tela)
    const auto& recs = session_->records();
    if (!recs.empty()) {
        constexpr size_t kMax = 14;
        size_t from = recs.size() > kMax ? recs.size() - kMax : 0;
        v.lines.push_back(L"");
        v.lineMark.push_back(0);
        v.lines.push_back(from ? L"Perguntas respondidas (últimas " + std::to_wstring(kMax) + L" de " + std::to_wstring(recs.size()) + L"):"
                               : L"Perguntas respondidas:");
        v.lineMark.push_back(0);
        for (size_t i = from; i < recs.size(); ++i) {
            const Question* q = bank_.find(recs[i].key);
            std::wstring p = q ? widen(q->prompt) : widen(recs[i].key);
            if (p.size() > 85) p = p.substr(0, 85) + L"…";
            v.lines.push_back((recs[i].correct ? L"✓ acertou   " : L"✗ errou      ") + p);
            v.lineMark.push_back(recs[i].correct ? 1 : 2);
        }
    }
    v.hint = L"Enter para fechar";
    ctx_ = Ctx::Report;
    deathWin_->show(v, secondMonitor(target()), nullptr, 0);   // sem tomar o foco do jogo
    hk_->setGroup(G_ANSWER, true);                             // Enter (hotkey) fecha o relatório
}

static BOOL CALLBACK collectMonitors(HMONITOR m, HDC, LPRECT, LPARAM lp) {
    ((std::vector<HMONITOR>*)lp)->push_back(m);
    return TRUE;
}

// Um monitor diferente do jogo, se houver; senão o próprio monitor do jogo.
HMONITOR App::secondMonitor(HWND t) const {
    std::vector<HMONITOR> mons;
    EnumDisplayMonitors(nullptr, nullptr, collectMonitors, (LPARAM)&mons);
    HMONITOR cur = monitorFor(t);
    for (HMONITOR m : mons)
        if (m != cur) return m;
    return cur;
}

void App::dismissReport() {
    if (ctx_ != Ctx::Report) return;
    ctx_ = Ctx::None;
    hk_->setGroup(G_ANSWER, false);
    deathWin_->hide(true);
    focusTarget();
}

// ---------------------------------------------------------------- fase de compra

void App::startBuy() {
    if (cfg_.buyDisplay == Display::Off || !ensureGfx()) return;
    endBuy(false);
    View v;
    double secs = cfg_.buySeconds;
    buy_ = Active{};
    if (session_->hasPendingPill()) {
        auto p = session_->nextPill();
        v.compact = true;
        v.tag = L"PÍLULA  ·  " + widen(p->title);
        v.title = widen(p->text);
        buyCtx_ = BuyCtx::Pill;
        secs = cfg_.pillSeconds;
    } else if (const Question* q = session_->nextQuestion(Slot::Buy, nowSec())) {
        buy_.q = q;
        buyCtx_ = BuyCtx::Question;
        v = questionView(buy_, true, false, false, true);
        hk_->setGroup(G_ANSWER, true);
    } else if (auto p = session_->nextPill()) {
        v.compact = true;
        v.tag = L"PÍLULA  ·  " + widen(p->title);
        v.title = widen(p->text);
        buyCtx_ = BuyCtx::Pill;
        secs = cfg_.pillSeconds;
    } else {
        return;
    }
    PreviewParams* none = nullptr;
    buyWin_->show(v, monitorFor(target()), none, std::max(1.0, secs));
    SetTimer(ctl_, T_BUY, (UINT)(std::max(1.0, secs) * 1000), nullptr);
}

void App::endBuy(bool animate) {
    if (buyCtx_ == BuyCtx::None) return;
    KillTimer(ctl_, T_BUY);
    KillTimer(ctl_, T_BUYDONE);
    if (buyCtx_ == BuyCtx::Question && buy_.q) session_->interrupted(*buy_.q);   // sem penalidade
    if (ctx_ == Ctx::None) hk_->setGroup(G_ANSWER, false);
    if (buyWin_) buyWin_->hide(animate);
    buyCtx_ = BuyCtx::None;
}

void App::onBuyChoice(int n) {
    if (!buy_.q || n >= (int)buy_.q->alts.size()) return;
    buy_.sel = {n};
    buy_.correct = evaluate(*buy_.q, buy_.sel);
    session_->answered(*buy_.q, buy_.correct, false, nowSec());   // sem tempo: explicação vira pílula se errar
    buy_.committed = true;
    buyCtx_ = BuyCtx::Answered;
    hk_->setGroup(G_ANSWER, false);
    View v = questionView(buy_, true, true, false, true);
    v.explanation.clear();
    if (!buy_.correct && !buy_.q->explanation.empty()) v.explanation = L"A explicação aparece na próxima fase de compra.";
    buyWin_->update(v);
    KillTimer(ctl_, T_BUY);
    SetTimer(ctl_, T_BUYDONE, 2200, nullptr);
}

// ---------------------------------------------------------------- views e helpers

std::wstring App::keyHint() const {
    return widen(cfg_.keys.peek) + L" espiar · " + widen(cfg_.keys.notNow) + L" agora não";
}

bool App::evaluate(const Question& q, const std::set<int>& sel) {
    for (size_t i = 0; i < q.alts.size(); ++i)
        if (q.alts[i].correct != (sel.count((int)i) > 0)) return false;
    return !sel.empty();
}

View App::questionView(const Active& a, bool compact, bool reveal, bool typing, bool buy) const {
    View v;
    const Question& q = *a.q;
    v.compact = compact;
    v.tag = widen(q.contentTitle) + L"  ·  " + widen(diffLabel(q.diff)) + L"  ·  " + widen(typeLabel(q.type));
    v.title = widen(q.prompt);
    if (q.type == QType::Gap) {
        v.gap = true;
        v.input = a.typed;
        v.inputActive = typing;
        v.placeholder = L"(Enter para digitar a resposta)";
    } else {
        for (size_t i = 0; i < q.alts.size(); ++i) {
            ViewChoice c;
            c.text = widen(q.alts[i].text);
            c.selected = a.sel.count((int)i) > 0;
            if (reveal) {
                bool corr = q.alts[i].correct;
                if (corr) c.mark = c.selected ? ViewChoice::Correct : ViewChoice::Missed;
                else if (c.selected) c.mark = ViewChoice::Wrong;
            }
            v.choices.push_back(c);
        }
    }
    if (reveal) {
        v.verdict = a.correct ? L"Correto!" : L"Errado";
        v.verdictOk = a.correct;
        if (q.type == QType::Gap) {
            std::wstring exp = L"Resposta esperada: ";
            for (size_t i = 0; i < q.accepted.size() && i < 3; ++i) exp += (i ? L" / " : L"") + widen(q.accepted[i]);
            v.expected = exp;
        }
        v.explanation = widen(q.explanation);
        if (!buy) {
            if (q.type == QType::Gap && !a.committed) v.hint = L"1 = eu acertei  ·  2 = eu errei  ·  Enter: próxima";
            else v.hint = L"Enter: próxima pergunta  ·  " + keyHint();
        }
    } else if (typing) {
        v.hint = L"Enter confirma  ·  Backspace apaga  ·  " + keyHint();
    } else if (q.type == QType::Gap) {
        v.hint = L"Enter para digitar  ·  " + keyHint();
    } else if (buy) {
        v.hint = L"tecla 1–" + std::to_wstring(q.alts.size()) + L" responde";
    } else {
        v.hint = (q.type == QType::Multi ? L"marque todas as corretas: 1–" : L"1–") + std::to_wstring(q.alts.size()) +
                 L" marca  ·  Enter confirma  ·  " + keyHint();
    }
    return v;
}

bool App::ensureGfx() {
    if (!gfx_.init()) {
        balloon(L"Sidequest", L"Não foi possível iniciar a GPU/DirectComposition.");
        return false;
    }
    if (!deathWin_) {
        deathWin_ = std::make_unique<OverlayWindow>(gfx_, OverlayMode::Full, "", 0);
        if (!deathWin_->create(hi_)) { deathWin_.reset(); return false; }
        deathWin_->onChar = [this](wchar_t c) { onOverlayChar(c); };
        deathWin_->onCloseAttempt = [this] { closeAttempt(); };
    }
    if (!buyWin_) {
        buyWin_ = std::make_unique<OverlayWindow>(gfx_, OverlayMode::Corner, cfg_.buyCorner, 24);
        if (!buyWin_->create(hi_)) { buyWin_.reset(); return false; }
        buyWin_->onCloseAttempt = [this] { closeAttempt(); };
    }
    return true;
}

void App::ensureSession() {
    if (!session_->active()) session_->begin(nowSec());
}

HWND App::target() const { return findWindow(cfg_.targetTitle, cfg_.targetClass); }

HMONITOR App::monitorFor(HWND t) const {
    if (t) return MonitorFromWindow(t, MONITOR_DEFAULTTONEAREST);
    return MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
}

void App::focusTarget() {
    if (HWND t = target()) SetForegroundWindow(t);
}

void App::duckAudio() {
    HWND t = target();
    if (!t || cfg_.volumeOnDeath >= 0.999f) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(t, &pid);
    ducker_.duck(pid, cfg_.volumeOnDeath);
}

void App::restoreAudio() { ducker_.restore(); }

void App::closeAttempt() {
    balloon(L"Sidequest", L"Alt+F4 não fecha o Sidequest. Para sair, use o ícone da bandeja → Sair...");
}

void App::balloon(const std::wstring& title, const std::wstring& text) {
    NOTIFYICONDATAW nid{sizeof nid};
    nid.hWnd = ctl_;
    nid.uID = 1;
    nid.uFlags = NIF_INFO;
    wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, text.c_str(), _TRUNCATE);
    nid.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void App::requestExit() {
    if (exiting_) return;
    if (MessageBoxW(nullptr, L"Sair do Sidequest?", L"Sidequest", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND) != IDYES)
        return;
    exiting_ = true;   // confirmação lenta: segunda pergunta só depois de 3 s
    SetTimer(ctl_, T_EXIT, 3000, nullptr);
}

void App::quit() {
    leaveDeath();
    endBuy(false);
    if (session_ && session_->active()) {
        if (session_->stats().cards > 0 || session_->stats().answered > 0) store_.midExits++;   // fechou no meio da sessão
        session_->end("fechado", nowSec());
    }
    store_.save();
    restoreAudio();
    PostQuitMessage(0);
}
}  // namespace sq
