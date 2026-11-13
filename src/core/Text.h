#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace sq {
std::u32string decodeUtf8(std::string_view s);
std::string encodeUtf8(std::u32string_view s);
// minúsculas, sem acentos, espaços colapsados e aparados
std::u32string foldAnswer(std::string_view utf8);
int editDistance(std::u32string_view a, std::u32string_view b);
// compara a resposta digitada com as aceitas (tolerância de digitação por tamanho)
bool answerMatches(std::string_view typed, const std::vector<std::string>& accepted);
}  // namespace sq
