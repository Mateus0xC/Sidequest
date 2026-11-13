#include "DeathDetector.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace sq {

void DeathDetector::teach(LifeState s, const Features& f) {
    Features& c = s == LifeState::Dead ? dead_ : alive_;
    int& n = s == LifeState::Dead ? nDead_ : nAlive_;
    if (s == LifeState::Unknown) return;
    ++n;
    c.teal += (f.teal - c.teal) / n;
    c.white += (f.white - c.white) / n;
    c.mean += (f.mean - c.mean) / n;
}

LifeState DeathDetector::classify(const Features& f) const {
    // distância a cada centróide, com cada característica escalada pela separação entre os dois
    auto span = [](float a, float b) { return std::max(std::fabs(a - b), 0.005f); };
    float st = span(alive_.teal, dead_.teal), sw = span(alive_.white, dead_.white), sm = span(alive_.mean, dead_.mean);
    auto dist = [&](const Features& c) {
        float a = (f.teal - c.teal) / st, b = (f.white - c.white) / sw, d = (f.mean - c.mean) / sm;
        return a * a + b * b + d * d;
    };
    return dist(alive_) <= dist(dead_) ? LifeState::Alive : LifeState::Dead;
}

std::optional<LifeState> DeathDetector::observe(const Features& f) {
    if (!ready()) return std::nullopt;
    votes_.push_back(classify(f));
    if (votes_.size() > 5) votes_.pop_front();
    int dead = (int)std::count(votes_.begin(), votes_.end(), LifeState::Dead);
    int alive = (int)votes_.size() - dead;
    // maioria dos últimos 5 + as duas leituras mais recentes iguais
    if (votes_.size() < 3) return std::nullopt;
    LifeState maj = dead > alive ? LifeState::Dead : LifeState::Alive;
    if (votes_[votes_.size() - 1] != maj || votes_[votes_.size() - 2] != maj) return std::nullopt;
    if (maj == confirmed_) return std::nullopt;
    confirmed_ = maj;
    return maj;
}

void DeathDetector::setState(LifeState s) {
    confirmed_ = s;
    votes_.clear();
}

bool DeathDetector::load(const std::filesystem::path& p) {
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) return false;
    try {
        std::ifstream in(p, std::ios::binary);
        json j = json::parse(in);
        auto rd = [](const json& o, Features& f, int& n) {
            f.teal = o.value("teal", 0.f);
            f.white = o.value("white", 0.f);
            f.mean = o.value("mean", 0.f);
            n = o.value("n", 0);
        };
        rd(j.at("vivo"), alive_, nAlive_);
        rd(j.at("morto"), dead_, nDead_);
        return true;
    } catch (...) {
        return false;
    }
}

bool DeathDetector::save(const std::filesystem::path& p) const {
    json j;
    j["vivo"] = {{"teal", alive_.teal}, {"white", alive_.white}, {"mean", alive_.mean}, {"n", nAlive_}};
    j["morto"] = {{"teal", dead_.teal}, {"white", dead_.white}, {"mean", dead_.mean}, {"n", nDead_}};
    std::filesystem::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << j.dump(1);
    }
    std::error_code ec;
    std::filesystem::rename(tmp, p, ec);
    return !ec;
}
}  // namespace sq
