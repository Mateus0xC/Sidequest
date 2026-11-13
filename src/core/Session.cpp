#include "Session.h"
#include "Leitner.h"
#include <algorithm>
#include <chrono>

namespace sq {

Session::Session(const QuestionBank& bank, ProgressStore& store, const Config& cfg)
    : bank_(bank), store_(store), cfg_(cfg),
      rng_((unsigned)std::chrono::steady_clock::now().time_since_epoch().count()) {}

void Session::begin(int64_t now) {
    active_ = true;
    start_ = now;
    shownCount_ = 0;
    stats_ = {};
    retries_.clear();
    pendingPills_.clear();
    seen_.clear();
    wrongByContent_.clear();
    flagged_ = store_.studyOutside;  // conteúdos já marcados continuam só com pílulas
    interruptedQ_.assign(store_.interrupted.begin(), store_.interrupted.end());
}

void Session::end(const std::string& reason, int64_t now) {
    if (!active_) return;
    active_ = false;
    SessionLog l;
    l.start = start_; l.end = now;
    l.answered = stats_.answered; l.correct = stats_.correct; l.wrong = stats_.wrong;
    l.interrupted = stats_.interrupted; l.ended = reason;
    store_.sessions.push_back(l);
    save();
}

bool Session::eligible(Slot s, const Question& q) {
    if (s == Slot::Death) return true;
    return (q.type == QType::VF || q.type == QType::Single) && q.diff == Diff::Easy;
}

const Question* Session::shown(const Question* q) {
    ++shownCount_;
    ++stats_.cards;
    seen_.insert(q->key());
    return q;
}

const Question* Session::nextQuestion(Slot slot, int64_t now) {
    // 1) interrompidas
    for (size_t i = 0; i < interruptedQ_.size(); ++i) {
        const Question* q = bank_.find(interruptedQ_[i]);
        if (!q) { interruptedQ_.erase(interruptedQ_.begin() + i); --i; continue; }
        if (eligible(slot, *q)) { interruptedQ_.erase(interruptedQ_.begin() + i); return shown(q); }
    }
    // 2) erradas que voltam (~N cartões depois)
    for (size_t i = 0; i < retries_.size(); ++i) {
        if (shownCount_ < retries_[i].dueShown) continue;
        const Question* q = bank_.find(retries_[i].key);
        if (!q) { retries_.erase(retries_.begin() + i); --i; continue; }
        if (eligible(slot, *q)) { retries_.erase(retries_.begin() + i); return shown(q); }
    }
    // 3) vencidas (mais atrasada primeiro) e 4) novas (sorteio)
    const Question* overdue = nullptr;
    int64_t overdueDue = 0;
    std::vector<const Question*> fresh;
    for (const Question& q : bank_.all()) {
        if (!eligible(slot, q) || seen_.count(q.key()) || flagged_.count(q.content)) continue;
        const CardState* st = store_.find(q.key());
        if (st) {
            if (st->due <= now && (!overdue || st->due < overdueDue)) { overdue = &q; overdueDue = st->due; }
        } else {
            fresh.push_back(&q);
        }
    }
    if (overdue) return shown(overdue);
    if (!fresh.empty()) return shown(fresh[rng_() % fresh.size()]);
    return nullptr;
}

std::optional<Pill> Session::nextPill() {
    if (!pendingPills_.empty()) {
        Pill p = pendingPills_.front();
        pendingPills_.pop_front();
        ++shownCount_; ++stats_.pills;
        return p;
    }
    std::vector<const Question*> pool, pref;
    for (const Question& q : bank_.all()) {
        if (q.explanation.empty()) continue;
        bool isFlagged = flagged_.count(q.content) > 0;
        bool known = seen_.count(q.key()) || store_.find(q.key());
        if (!known && !isFlagged) continue;
        if (q.key() == lastPill_) continue;
        pool.push_back(&q);
        if (isFlagged) pref.push_back(&q);
    }
    auto& from = pref.empty() ? pool : pref;
    if (from.empty()) return std::nullopt;
    const Question* q = from[rng_() % from.size()];
    lastPill_ = q->key();
    ++shownCount_; ++stats_.pills;
    return Pill{q->contentTitle, q->explanation};
}

void Session::answered(const Question& q, bool correct, bool explanationShown, int64_t now) {
    leitnerApply(store_.get(q.key()), correct, now, cfg_.leitnerDays);
    store_.interrupted.erase(std::remove(store_.interrupted.begin(), store_.interrupted.end(), q.key()),
                             store_.interrupted.end());
    ++stats_.answered;
    auto& t = stats_.byContent[q.content];
    ++t.answered;
    if (correct) {
        ++stats_.correct; ++t.correct;
    } else {
        ++stats_.wrong; ++t.wrong;
        retries_.push_back({q.key(), shownCount_ + cfg_.retryAfterCards});
        if (!explanationShown && !q.explanation.empty())
            pendingPills_.push_back({q.contentTitle, q.explanation});
        if (++wrongByContent_[q.content] >= cfg_.failLimit && !flagged_.count(q.content)) {
            flagged_.insert(q.content);
            store_.studyOutside.insert(q.content);
        }
    }
    save();
}

void Session::interrupted(const Question& q) {
    ++stats_.interrupted;
    interruptedQ_.push_front(q.key());
    auto& v = store_.interrupted;
    v.erase(std::remove(v.begin(), v.end(), q.key()), v.end());
    v.insert(v.begin(), q.key());
    seen_.erase(q.key());  // volta a ser elegível
    save();
}

std::vector<std::string> Session::reportLines(int64_t now) const {
    std::vector<std::string> L;
    int64_t dur = std::max<int64_t>(0, now - start_);
    char buf[160];
    snprintf(buf, sizeof buf, "Duração: %lld min %02lld s", (long long)(dur / 60), (long long)(dur % 60));
    L.push_back(buf);
    int pct = stats_.answered ? (100 * stats_.correct) / stats_.answered : 0;
    snprintf(buf, sizeof buf, "Respondidas: %d   Acertos: %d   Erros: %d   (%d%%)", stats_.answered, stats_.correct,
             stats_.wrong, pct);
    L.push_back(buf);
    snprintf(buf, sizeof buf, "Interrompidas: %d   Pílulas vistas: %d", stats_.interrupted, stats_.pills);
    L.push_back(buf);
    if (!stats_.byContent.empty()) {
        L.push_back("");
        L.push_back("Por conteúdo:");
        for (auto& [c, t] : stats_.byContent) {
            snprintf(buf, sizeof buf, "  %d/%d  ", t.correct, t.answered);
            L.push_back(buf + bank_.titleOf(c));
        }
    }
    std::vector<std::string> out;
    for (auto& c : flagged_) out.push_back(bank_.titleOf(c));
    if (!out.empty()) {
        L.push_back("");
        L.push_back("Estudar fora do jogo:");
        for (auto& s : out) L.push_back("  " + s);
    }
    size_t pending = store_.interrupted.size();
    if (pending) {
        snprintf(buf, sizeof buf, "Pendentes para a próxima sessão: %zu", pending);
        L.push_back("");
        L.push_back(buf);
    }
    return L;
}
}  // namespace sq
