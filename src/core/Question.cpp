#include "Question.h"
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using nlohmann::json;

namespace sq {
const char* typeLabel(QType t) {
    switch (t) {
        case QType::VF: return "verdadeiro/falso";
        case QType::Single: return "escolha única";
        case QType::Multi: return "várias corretas";
        case QType::Gap: return "lacuna";
    }
    return "";
}
const char* diffLabel(Diff d) {
    switch (d) {
        case Diff::Easy: return "fácil";
        case Diff::Medium: return "média";
        case Diff::Hard: return "difícil";
    }
    return "";
}

static bool parseQuestion(const json& j, Question& q, std::string& err) {
    q.id = j.value("id", "");
    if (q.id.empty()) { err = "sem id"; return false; }
    std::string t = j.value("tipo", ""), d = j.value("dificuldade", "media");
    if (t == "vf") q.type = QType::VF;
    else if (t == "escolha_unica") q.type = QType::Single;
    else if (t == "multipla_correta") q.type = QType::Multi;
    else if (t == "lacuna") q.type = QType::Gap;
    else { err = "tipo desconhecido: " + t; return false; }
    q.diff = d == "facil" ? Diff::Easy : d == "dificil" ? Diff::Hard : Diff::Medium;
    q.prompt = j.value("enunciado", "");
    q.explanation = j.value("explicacao", "");
    q.origin = j.value("origem", "");
    if (q.prompt.empty()) { err = "sem enunciado"; return false; }
    if (q.type == QType::Gap) {
        if (j.contains("respostas_aceitas"))
            for (auto& r : j["respostas_aceitas"])
                if (r.is_string()) q.accepted.push_back(r.get<std::string>());
        if (q.accepted.empty()) { err = "lacuna sem respostas_aceitas"; return false; }
    } else {
        if (j.contains("alternativas"))
            for (auto& a : j["alternativas"]) q.alts.push_back({a.value("texto", ""), a.value("correta", false)});
        int nc = (int)std::count_if(q.alts.begin(), q.alts.end(), [](const Alt& a) { return a.correct; });
        if (q.alts.size() < 2 || nc == 0) { err = "alternativas inválidas"; return false; }
        if (q.alts.size() > 6) { err = "mais de 6 alternativas"; return false; }
        if (q.type != QType::Multi && nc != 1) { err = "esperava exatamente 1 correta"; return false; }
    }
    return true;
}

bool QuestionBank::load(const fs::path& dir) {
    qs_.clear(); index_.clear(); titles_.clear(); warnings.clear();
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        warnings.push_back("pasta de questões não encontrada: " + dir.string());
        return false;
    }
    std::vector<fs::path> files;
    for (auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file() && e.path().extension() == ".json" && e.path().filename().string()[0] != '_')
            files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (auto& f : files) {
        try {
            std::ifstream in(f, std::ios::binary);
            json j = json::parse(in, nullptr, true, true);
            std::string stem = f.stem().string();
            titles_[stem] = j.value("conteudo", stem);
            for (auto& jq : j.at("questoes")) {
                Question q;
                q.content = stem;
                q.contentTitle = titles_[stem];
                std::string err;
                if (!parseQuestion(jq, q, err)) { warnings.push_back(stem + ": questão ignorada (" + err + ")"); continue; }
                if (index_.count(q.key())) { warnings.push_back(stem + ": id duplicado " + q.id); continue; }
                index_[q.key()] = qs_.size();
                qs_.push_back(std::move(q));
            }
        } catch (const std::exception& e) {
            warnings.push_back(f.filename().string() + ": " + e.what());
        }
    }
    return !qs_.empty();
}

const Question* QuestionBank::find(const std::string& key) const {
    auto it = index_.find(key);
    return it == index_.end() ? nullptr : &qs_[it->second];
}

std::string QuestionBank::titleOf(const std::string& content) const {
    auto it = titles_.find(content);
    return it == titles_.end() ? content : it->second;
}
}  // namespace sq
