#pragma once
#include <deque>
#include <optional>

namespace sq {

// Leitura do HUD: fração (0..1) de pixels brancos numa linha fixa do painel "Combat Report",
// que só aparece quando o jogador está morto. Sem OCR, sem IA, sem calibração.
struct Features {
    float line = 0;
};

enum class LifeState { Unknown, Alive, Dead };

// Regra: linha branca presente (>= limiar) = morto. Votação por maioria dos últimos 5 e confirmação
// em duas leituras seguidas iguais antes de mudar de estado.
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
