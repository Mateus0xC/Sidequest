#include "DeathDetector.h"
#include <algorithm>

namespace sq {

std::optional<LifeState> DeathDetector::observe(const Features& f) {
    votes_.push_back(f.hud >= threshold_ ? LifeState::Alive : LifeState::Dead);
    if (votes_.size() > 5) votes_.pop_front();
    if (votes_.size() < 3) return std::nullopt;
    int dead = (int)std::count(votes_.begin(), votes_.end(), LifeState::Dead);
    int alive = (int)votes_.size() - dead;
    LifeState maj = dead > alive ? LifeState::Dead : LifeState::Alive;
    // maioria dos últimos 5 + as duas leituras mais recentes iguais
    if (votes_[votes_.size() - 1] != maj || votes_[votes_.size() - 2] != maj) return std::nullopt;
    if (maj == confirmed_) return std::nullopt;
    if (maj == LifeState::Dead && confirmed_ != LifeState::Alive) {
        confirmed_ = LifeState::Dead;   // sem HUD desde o começo (lobby/menu): não é morte
        return std::nullopt;
    }
    confirmed_ = maj;
    return maj;
}

void DeathDetector::setState(LifeState s) {
    confirmed_ = s;
    votes_.clear();
}
}  // namespace sq
