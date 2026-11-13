#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace sq {
struct CardState {
    int box = 0;
    int64_t due = 0;   // epoch (s)
    int right = 0, wrong = 0;
    int64_t last = 0;
};

struct SessionLog {
    int64_t start = 0, end = 0;
    int answered = 0, correct = 0, wrong = 0, interrupted = 0;
    std::string ended;  // "fim_partida" | "fechado"
};

// Progresso separado das questões: chave = "arquivo|id".
class ProgressStore {
public:
    bool load(const std::filesystem::path& p, std::string& err);
    bool save();  // atômico: grava .tmp e substitui
    CardState& get(const std::string& key) { return cards[key]; }
    const CardState* find(const std::string& key) const {
        auto it = cards.find(key);
        return it == cards.end() ? nullptr : &it->second;
    }

    std::map<std::string, CardState> cards;
    std::vector<std::string> interrupted;   // chaves, a primeira vai na frente
    std::set<std::string> studyOutside;     // conteúdos marcados "estudar fora do jogo"
    std::vector<SessionLog> sessions;
    int midExits = 0;                       // fechamentos no meio de uma sessão

private:
    std::filesystem::path path_;
};
}  // namespace sq
