#include "Config.h"
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using nlohmann::json;

namespace sq {

Config Config::defaults(const fs::path& baseDir) {
    Config c;
    c.baseDir = baseDir;
    c.questionsDir = baseDir / "questoes";
    c.statePath = baseDir / "estado.json";
    return c;
}

static Display parseDisplay(const std::string& s, Display def, std::vector<std::string>& w, const char* key) {
    if (s == "tela_cheia") return Display::Full;
    if (s == "canto") return Display::Corner;
    if (s == "desligado") return Display::Off;
    if (s == "segundo_monitor") { w.push_back(std::string(key) + ": segundo_monitor chega na Etapa 3; usando canto"); return Display::Corner; }
    w.push_back(std::string(key) + ": valor inválido '" + s + "'");
    return def;
}

static bool validCorner(const std::string& c) {
    return c == "top-left" || c == "top-right" || c == "bottom-left" || c == "bottom-right";
}

static fs::path resolve(const fs::path& base, const std::string& s) {
    fs::path p = fs::u8path(s);
    return p.is_absolute() ? p : base / p;
}

Config Config::load(const fs::path& file, std::vector<std::string>& w) {
    fs::path base = fs::absolute(file).parent_path();
    Config c = defaults(base);
    json j;
    try {
        std::ifstream in(file, std::ios::binary);
        j = json::parse(in, nullptr, true, true);
    } catch (const std::exception& e) {
        w.push_back(std::string("config.json inválido: ") + e.what());
        return c;
    }
    try {
        if (j.contains("pasta_questoes")) c.questionsDir = resolve(base, j["pasta_questoes"].get<std::string>());
        if (j.contains("estado")) c.statePath = resolve(base, j["estado"].get<std::string>());
        if (j.contains("janela_alvo")) {
            auto& t = j["janela_alvo"];
            c.targetTitle = t.value("titulo", c.targetTitle);
            c.targetClass = t.value("classe", c.targetClass);
        }
        if (j.contains("previa")) {
            auto& p = j["previa"];
            c.previewEnabled = p.value("ativa", c.previewEnabled);
            c.previewWidthPct = p.value("largura_pct", c.previewWidthPct);
            c.previewCorner = p.value("canto", c.previewCorner);
            c.previewMargin = p.value("margem", c.previewMargin);
            c.previewMode = p.value("modo", c.previewMode);
        }
        if (j.contains("exibicao")) {
            auto& e = j["exibicao"];
            if (e.contains("morte")) c.deathDisplay = parseDisplay(e["morte"].get<std::string>(), c.deathDisplay, w, "exibicao.morte");
            if (e.contains("compra")) c.buyDisplay = parseDisplay(e["compra"].get<std::string>(), c.buyDisplay, w, "exibicao.compra");
            c.buyCorner = e.value("canto_compra", c.buyCorner);
        }
        if (j.contains("tempos")) {
            auto& t = j["tempos"];
            c.deathDelayMs = t.value("espera_morte_ms", c.deathDelayMs);
            c.peekSeconds = t.value("espiar_s", c.peekSeconds);
            c.buySeconds = t.value("fase_compra_s", c.buySeconds);
            c.pillSeconds = t.value("pilula_s", c.pillSeconds);
            c.minGapSeconds = t.value("intervalo_minimo_s", c.minGapSeconds);
        }
        c.volumeOnDeath = j.value("volume_na_morte", c.volumeOnDeath);
        if (j.contains("estudo")) {
            auto& s = j["estudo"];
            c.retryAfterCards = s.value("voltar_apos_cartoes", c.retryAfterCards);
            c.failLimit = s.value("limite_erros_conteudo", c.failLimit);
            if (s.contains("caixas_dias")) c.leitnerDays = s["caixas_dias"].get<std::vector<int>>();
        }
        if (j.contains("teclas")) {
            auto& k = j["teclas"];
            c.keys.die = k.value("morri", c.keys.die);
            c.keys.revive = k.value("revivi", c.keys.revive);
            c.keys.roundStart = k.value("round_comecou", c.keys.roundStart);
            c.keys.matchEnd = k.value("fim_de_partida", c.keys.matchEnd);
            c.keys.peek = k.value("espiar", c.keys.peek);
            c.keys.notNow = k.value("agora_nao", c.keys.notNow);
        }
    } catch (const std::exception& e) {
        w.push_back(std::string("config.json: ") + e.what());
    }
    if (!validCorner(c.previewCorner)) { w.push_back("previa.canto inválido; usando bottom-right"); c.previewCorner = "bottom-right"; }
    if (!validCorner(c.buyCorner)) { w.push_back("exibicao.canto_compra inválido; usando top-right"); c.buyCorner = "top-right"; }
    if (c.leitnerDays.size() < 2) c.leitnerDays = {0, 1, 3, 7, 14, 30};
    if (c.previewWidthPct < 5 || c.previewWidthPct > 50) c.previewWidthPct = 22;
    return c;
}
}  // namespace sq
