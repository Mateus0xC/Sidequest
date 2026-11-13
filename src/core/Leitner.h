#pragma once
#include "Progress.h"
#include <algorithm>
#include <vector>

namespace sq {
// Caixas 0..N-1; days[i] = intervalo da caixa i.
inline void leitnerApply(CardState& c, bool correct, int64_t now, const std::vector<int>& days) {
    int top = (int)days.size() - 1;
    if (correct) {
        c.box = std::min(c.box + 1, top);
        c.due = now + (int64_t)days[c.box] * 86400;
        c.right++;
    } else {
        c.box = std::min(1, top);   // volta à caixa 1, mas vence já (revista na próxima sessão)
        c.due = now;
        c.wrong++;
    }
    c.last = now;
}
}  // namespace sq
