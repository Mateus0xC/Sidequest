#pragma once
#include <deque>
#include <filesystem>
#include <optional>

namespace sq {

// Resumo de uma faixa do HUD (frações 0..1 de pixels): ciano das barras de habilidade,
// branco do texto/ícones e luminância média. Sem OCR, sem IA.
struct Features {
    float teal = 0, white = 0, mean = 0;
};

enum class LifeState { Unknown, Alive, Dead };

// Classifica "vivo/morto" por centróides aprendidos com as hotkeys (F1 = morto; F2/F3 = vivo),
// com votação por maioria e confirmação em duas leituras seguidas.
class DeathDetector {
public:
    void teach(LifeState s, const Features& f);        // média incremental
    bool ready(int minEach = 3) const { return nAlive_ >= minEach && nDead_ >= minEach; }
    int taughtAlive() const { return nAlive_; }
    int taughtDead() const { return nDead_; }

    // Uma leitura (~4x/s). Devolve o novo estado confirmado quando ele muda.
    std::optional<LifeState> observe(const Features& f);
    LifeState state() const { return confirmed_; }
    void setState(LifeState s);                         // sincroniza com eventos manuais

    bool load(const std::filesystem::path& p);
    bool save(const std::filesystem::path& p) const;

private:
    LifeState classify(const Features& f) const;
    Features alive_, dead_;
    int nAlive_ = 0, nDead_ = 0;
    std::deque<LifeState> votes_;
    LifeState confirmed_ = LifeState::Unknown;
};
}  // namespace sq
