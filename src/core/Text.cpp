#include "Text.h"
#include <algorithm>

namespace sq {

std::u32string decodeUtf8(std::string_view s) {
    std::u32string o;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        char32_t cp;
        int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 6) { cp = c & 0x1F; n = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0F; n = 3; }
        else if ((c >> 3) == 30) { cp = c & 7; n = 4; }
        else { cp = 0xFFFD; n = 1; }
        for (int k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        i += n;
        o.push_back(cp);
    }
    return o;
}

std::string encodeUtf8(std::u32string_view s) {
    std::string o;
    for (char32_t cp : s) {
        if (cp < 0x80) o.push_back((char)cp);
        else if (cp < 0x800) { o.push_back((char)(0xC0 | (cp >> 6))); o.push_back((char)(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) { o.push_back((char)(0xE0 | (cp >> 12))); o.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); o.push_back((char)(0x80 | (cp & 0x3F))); }
        else { o.push_back((char)(0xF0 | (cp >> 18))); o.push_back((char)(0x80 | ((cp >> 12) & 0x3F))); o.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); o.push_back((char)(0x80 | (cp & 0x3F))); }
    }
    return o;
}

// Latin-1 (U+00C0..U+00FF) sem acentos
static const char kLatin1[] =
    "AAAAAAACEEEEIIII" "DNOOOOO*OUUUUYTs" "aaaaaaaceeeeiiii" "dnooooo/ouuuuyty";

std::u32string foldAnswer(std::string_view utf8) {
    std::u32string in = decodeUtf8(utf8), out;
    bool pendingSpace = false;
    for (char32_t cp : in) {
        if (cp >= 0x300 && cp <= 0x36F) continue;  // marcas combinantes
        if (cp >= 0xC0 && cp <= 0xFF) cp = (char32_t)kLatin1[cp - 0xC0];
        bool space = cp <= 0x20 || cp == 0xA0 || (cp >= 0x2000 && cp <= 0x200B);
        if (space) { pendingSpace = !out.empty(); continue; }
        if (cp >= 'A' && cp <= 'Z') cp += 32;
        if (pendingSpace) { out.push_back(U' '); pendingSpace = false; }
        out.push_back(cp);
    }
    return out;
}

int editDistance(std::u32string_view a, std::u32string_view b) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = (int)j;
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = (int)i;
        for (size_t j = 1; j <= b.size(); ++j) {
            int sub = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, sub});
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

bool answerMatches(std::string_view typed, const std::vector<std::string>& accepted) {
    std::u32string t = foldAnswer(typed);
    if (t.empty()) return false;
    for (const auto& acc : accepted) {
        std::u32string a = foldAnswer(acc);
        // até 3 letras: exato; até 8: 1 erro; mais que isso: 2 erros
        int limit = a.size() <= 3 ? 0 : (a.size() <= 8 ? 1 : 2);
        if (editDistance(t, a) <= limit) return true;
    }
    return false;
}

}  // namespace sq
