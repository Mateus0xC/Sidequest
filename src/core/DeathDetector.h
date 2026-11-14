#pragma once
#include <deque>
#include <optional>

namespace sq {

// Leitura do HUD: fração (0..1) do trecho da linha do HUD de baixo (sob o contador de munição) que está
// presente. Com o jogador vivo a linha existe; morto, o HUD some. Sem OCR, sem IA, sem calibração.
struct Features {
    float hud = 0;      // presença da ponta da linha do HUD
    float card = 0;     // linha branca do cartão do jogador no menu de compra (y≈120, canto esquerdo)
    float pause = 0;    // botão claro "CLOSE SETTINGS" do menu de pausa/configurações (base da tela)
};


enum class LifeState { Unknown, Alive, Dead };

// Regra: linha do HUD presente (>= limiar) = vivo; ausente = morto. Votação por maioria dos últimos 5 e
// confirmação em duas leituras seguidas iguais. Nunca emite "morto" sem antes ter visto o HUD vivo
// (assim lobby, menus e telas de carregamento não contam como morte). Com um menu aberto, reconhecido por
// elementos fixos dele (cartão do menu de compra, botão da pausa), a leitura é ignorada.
class DeathDetector {
public:
    explicit DeathDetector(float threshold = 0.8f) : threshold_(threshold) {}
    static bool menuOpen(const Features& f) { return f.card >= 0.85f || f.pause >= 0.85f; }

    // Uma leitura (~4x/s). Devolve o novo estado confirmado quando ele muda.
    std::optional<LifeState> observe(const Features& f);
    LifeState state() const { return confirmed_; }
    void setState(LifeState s);   // sincroniza com eventos manuais (F1/F2/F3)
    void clearVotes() { votes_.clear(); }   // ex.: jogo sem foco, leitura descartada

private:
    float threshold_;
    std::deque<LifeState> votes_;
    LifeState confirmed_ = LifeState::Unknown;
};
}  // namespace sq
