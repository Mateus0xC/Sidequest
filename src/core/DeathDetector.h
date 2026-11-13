#pragma once
#include <deque>
#include <optional>

namespace sq {

// Leitura do HUD: fração (0..1) do trecho da linha do HUD de baixo (sob o contador de munição) que está
// presente. Com o jogador vivo a linha existe; morto, o HUD some. Sem OCR, sem IA, sem calibração.
struct Features {
    float hud = 0;
};

enum class LifeState { Unknown, Alive, Dead };

// Regra: linha do HUD presente (>= limiar) = vivo; ausente = morto. Votação por maioria dos últimos 5 e
// confirmação em duas leituras seguidas iguais. Nunca emite "morto" sem antes ter visto o HUD vivo
// (assim lobby, menus e telas de carregamento não contam como morte).
class DeathDetector {
public:
    explicit DeathDetector(float threshold = 0.8f) : threshold_(threshold) {}

    // Uma leitura (~4x/s). Devolve o novo estado confirmado quando ele muda.
    std::optional<LifeState> observe(const Features& f);
    LifeState state() const { return confirmed_; }
    void setState(LifeState s);   // sincroniza com eventos manuais (F1/F2/F3)

private:
    float threshold_;
    std::deque<LifeState> votes_;
    LifeState confirmed_ = LifeState::Unknown;
};
}  // namespace sq
