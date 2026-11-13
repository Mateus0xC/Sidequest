#pragma once
#include <string>
#include <vector>

namespace sq {
struct ViewChoice {
    enum Mark { None, Correct, Wrong, Missed };
    std::wstring text;
    bool selected = false;
    Mark mark = None;
};

// Descrição pura do que o cartão mostra; o App monta, o CardRenderer desenha.
struct View {
    std::wstring tag, title;
    std::vector<ViewChoice> choices;
    bool gap = false;                    // mostra a caixa de resposta digitada
    std::wstring input, placeholder;
    bool inputActive = false;
    std::wstring verdict;
    bool verdictOk = false;
    std::wstring expected, explanation, hint;
    std::vector<std::wstring> lines;     // corpo livre (relatório)
    std::vector<char> lineMark;          // 0 normal, 1 acerto, 2 erro (paralelo a lines)
    bool compact = false;                // cartão pequeno de canto
};
}  // namespace sq
