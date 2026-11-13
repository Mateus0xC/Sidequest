#include "Progress.h"
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using nlohmann::json;

namespace sq {

bool ProgressStore::load(const fs::path& p, std::string& err) {
    path_ = p;
    cards.clear(); interrupted.clear(); studyOutside.clear(); sessions.clear(); midExits = 0;
    std::error_code ec;
    if (!fs::exists(p, ec)) return true;  // primeira execução
    try {
        std::ifstream in(p, std::ios::binary);
        json j = json::parse(in);
        if (j.contains("cartoes"))
            for (auto& [k, v] : j["cartoes"].items()) {
                CardState c;
                c.box = v.value("caixa", 0);
                c.due = v.value("vence", (int64_t)0);
                c.right = v.value("acertos", 0);
                c.wrong = v.value("erros", 0);
                c.last = v.value("ultima", (int64_t)0);
                cards[k] = c;
            }
        if (j.contains("interrompidas")) interrupted = j["interrompidas"].get<std::vector<std::string>>();
        if (j.contains("estudar_fora"))
            for (auto& s : j["estudar_fora"]) studyOutside.insert(s.get<std::string>());
        if (j.contains("sessoes"))
            for (auto& s : j["sessoes"]) {
                SessionLog l;
                l.start = s.value("inicio", (int64_t)0);
                l.end = s.value("fim", (int64_t)0);
                l.answered = s.value("respondidas", 0);
                l.correct = s.value("acertos", 0);
                l.wrong = s.value("erros", 0);
                l.interrupted = s.value("interrompidas", 0);
                l.ended = s.value("encerrada_por", "");
                sessions.push_back(l);
            }
        midExits = j.value("saidas_no_meio", 0);
    } catch (const std::exception& e) {
        err = std::string("estado.json ilegível: ") + e.what();
        // preserva o arquivo problemático para não perder dados
        std::error_code ec2;
        fs::rename(p, fs::path(p).replace_extension(".corrompido.json"), ec2);
        return false;
    }
    return true;
}

bool ProgressStore::save() {
    json j;
    j["versao"] = 1;
    json cs = json::object();
    for (auto& [k, c] : cards)
        cs[k] = {{"caixa", c.box}, {"vence", c.due}, {"acertos", c.right}, {"erros", c.wrong}, {"ultima", c.last}};
    j["cartoes"] = cs;
    j["interrompidas"] = interrupted;
    j["estudar_fora"] = studyOutside;
    json ss = json::array();
    for (auto& s : sessions)
        ss.push_back({{"inicio", s.start}, {"fim", s.end}, {"respondidas", s.answered}, {"acertos", s.correct},
                      {"erros", s.wrong}, {"interrompidas", s.interrupted}, {"encerrada_por", s.ended}});
    j["sessoes"] = ss;
    j["saidas_no_meio"] = midExits;

    fs::path tmp = path_;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << j.dump(1);
        if (!out.good()) return false;
    }
    std::error_code ec;
    fs::rename(tmp, path_, ec);
    return !ec;
}
}  // namespace sq
