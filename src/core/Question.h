#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace sq {
enum class QType { VF, Single, Multi, Gap };
enum class Diff { Easy, Medium, Hard };

struct Alt { std::string text; bool correct = false; };

struct Question {
    std::string content;       // nome do arquivo (sem .json)
    std::string contentTitle;  // campo "conteudo"
    std::string id;
    QType type = QType::Single;
    Diff diff = Diff::Medium;
    std::string prompt, explanation, origin;
    std::vector<Alt> alts;
    std::vector<std::string> accepted;
    std::string key() const { return content + "|" + id; }
};

const char* typeLabel(QType t);
const char* diffLabel(Diff d);

class QuestionBank {
public:
    // Somente leitura. Questões/arquivos inválidos viram avisos, não erros fatais.
    bool load(const std::filesystem::path& dir);
    const std::vector<Question>& all() const { return qs_; }
    const Question* find(const std::string& key) const;
    std::string titleOf(const std::string& content) const;
    size_t contentCount() const { return titles_.size(); }
    std::vector<std::string> warnings;

private:
    std::vector<Question> qs_;
    std::unordered_map<std::string, size_t> index_;
    std::unordered_map<std::string, std::string> titles_;
};
}  // namespace sq
