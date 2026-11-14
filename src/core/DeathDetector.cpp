#include "DeathDetector.h"

namespace sq {

std::optional<LifeState> DeathDetector::observe(const Features& f) {
    if (menuOpen(f)) {   // algum menu aberto: não conclui nada e esquece os votos parciais
        votes_.clear();
        return std::nullopt;
    }
    votes_.push_back(f.hud >= threshold_ ? LifeState::Alive : LifeState::Dead);
    if (votes_.size() > 8) votes_.pop_front();
    auto lastAll = [&](LifeState s, size_t k) {
        if (votes_.size() < k) return false;
        for (size_t i = votes_.size() - k; i < votes_.size(); ++i)
            if (votes_[i] != s) return false;
        return true;
    };
    // Morte: de propósito muito conservador (nunca interromper o jogo sem ter certeza): exige kDeadReadings
    // leituras seguidas sem o HUD. A volta do HUD é prova forte (pixels brancos exatos), então 2 bastam.
    LifeState next;
    if (lastAll(LifeState::Dead, kDeadReadings)) next = LifeState::Dead;
    else if (lastAll(LifeState::Alive, 2)) next = LifeState::Alive;
    else return std::nullopt;
    if (next == confirmed_) return std::nullopt;
    if (next == LifeState::Dead && confirmed_ != LifeState::Alive) {
        confirmed_ = LifeState::Dead;   // sem HUD desde o começo (lobby/menu): não é morte
        return std::nullopt;
    }
    confirmed_ = next;
    return next;
}

void DeathDetector::setState(LifeState s) {
    confirmed_ = s;
    votes_.clear();
}
}  // namespace sq
