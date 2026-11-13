#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace sq {
enum class Display { Full, Corner, Off };

struct KeyNames {
    std::string die = "F1", revive = "F2", roundStart = "F3", matchEnd = "F4", peek = "F5", notNow = "F6";
};

struct Config {
    std::filesystem::path baseDir;
    std::filesystem::path questionsDir = "questoes";
    std::filesystem::path statePath = "estado.json";

    // janela cuja miniatura aparece na prévia (substring do título, não diferencia maiúsculas)
    std::string targetTitle = "VALORANT";
    std::string targetClass = "VALORANTUnrealWindow";

    bool previewEnabled = true;
    int previewWidthPct = 22;                 // % da largura do monitor
    std::string previewCorner = "bottom-right";
    int previewMargin = 24;

    Display deathDisplay = Display::Full;
    Display buyDisplay = Display::Corner;
    std::string buyCorner = "top-right";

    int deathDelayMs = 1500;
    int peekSeconds = 4;
    int buySeconds = 20;
    int pillSeconds = 12;
    int minGapSeconds = 0;                    // intervalo mínimo entre perguntas da morte (deathmatch)

    float volumeOnDeath = 0.5f;               // 1.0 desliga o abaixar volume
    int retryAfterCards = 3;
    int failLimit = 3;
    std::vector<int> leitnerDays = {0, 1, 3, 7, 14, 30};

    KeyNames keys;

    // Lê o JSON; chaves ausentes mantêm o padrão. Problemas viram avisos.
    static Config load(const std::filesystem::path& file, std::vector<std::string>& warnings);
    static Config defaults(const std::filesystem::path& baseDir);
};
}  // namespace sq
