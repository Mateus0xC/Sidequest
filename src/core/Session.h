#pragma once
#include "Config.h"
#include "Progress.h"
#include "Question.h"
#include <deque>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace sq {
enum class Slot { Death, Buy };

struct Pill { std::string title, text; };

struct ContentTally { int answered = 0, correct = 0, wrong = 0; };

struct Stats {
    int answered = 0, correct = 0, wrong = 0, interrupted = 0, pills = 0, cards = 0;
    std::map<std::string, ContentTally> byContent;  // chave = arquivo
};

// Fila da sessão + escolha de cartão por espaço disponível.
class Session {
public:
    Session(const QuestionBank& bank, ProgressStore& store, const Config& cfg);

    void begin(int64_t now);
    bool active() const { return active_; }
    void end(const std::string& reason, int64_t now);

    // Ordem: interrompidas -> erradas que voltam -> vencidas -> novas.
    const Question* nextQuestion(Slot slot, int64_t now);
    bool hasPendingPill() const { return !pendingPills_.empty(); }
    std::optional<Pill> nextPill();

    // explanationShown=false (ex.: fase de compra) transforma a explicação em pílula futura.
    void answered(const Question& q, bool correct, bool explanationShown, int64_t now, const std::string& answer = {});
    void interrupted(const Question& q);

    static bool eligible(Slot s, const Question& q);
    const Stats& stats() const { return stats_; }
    struct Record { std::string key; bool correct; std::string answer; };   // answer: o que o usuário respondeu
    const std::vector<Record>& records() const { return records_; }
    bool flagged(const std::string& content) const { return flagged_.count(content) > 0; }
    std::vector<std::string> reportLines(int64_t now) const;

private:
    const Question* shown(const Question* q);
    void save() { store_.save(); }

    const QuestionBank& bank_;
    ProgressStore& store_;
    const Config& cfg_;
    bool active_ = false;
    int64_t start_ = 0;
    int shownCount_ = 0;
    Stats stats_;
    std::vector<Record> records_;
    std::deque<std::string> interruptedQ_;
    struct Retry { std::string key; int dueShown; };
    std::vector<Retry> retries_;
    std::deque<Pill> pendingPills_;
    std::set<std::string> seen_;
    std::map<std::string, int> wrongByContent_;
    std::set<std::string> flagged_;
    std::string lastPill_;
    std::mt19937 rng_;
};
}  // namespace sq
