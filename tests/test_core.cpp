#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "core/Config.h"
#include "core/DeathDetector.h"
#include "core/Leitner.h"
#include "core/Progress.h"
#include "core/Question.h"
#include "core/Session.h"
#include "core/Text.h"

#include <cstdlib>
#include <fstream>

using namespace sq;
namespace fs = std::filesystem;

TEST_CASE("fold ignora maiúsculas, acentos e espaços") {
    CHECK(encodeUtf8(foldAnswer("  Índice   CLUSTERIZADO ")) == "indice clusterizado");
    CHECK(encodeUtf8(foldAnswer("Ação")) == "acao");
}

TEST_CASE("lacuna tolera pequenos erros de digitação") {
    std::vector<std::string> acc = {"índice", "index"};
    CHECK(answerMatches("indice", acc));
    CHECK(answerMatches("INDICE ", acc));
    CHECK(answerMatches("indece", acc));       // 1 erro, resposta curta
    CHECK_FALSE(answerMatches("indeces", acc));
    CHECK_FALSE(answerMatches("", acc));
    std::vector<std::string> longa = {"redo log files"};
    CHECK(answerMatches("redo log fyles", longa));
    CHECK(answerMatches("redo lg fyles", longa));      // 2 erros, resposta longa
    CHECK_FALSE(answerMatches("undo log fyles", longa));
    std::vector<std::string> curta = {"sql"};
    CHECK_FALSE(answerMatches("sqr", curta));   // até 3 letras: exato
}

TEST_CASE("leitner sobe e desce caixas") {
    std::vector<int> days = {0, 1, 3, 7, 14, 30};
    CardState c;
    leitnerApply(c, true, 1000, days);
    CHECK(c.box == 1);
    CHECK(c.due == 1000 + 86400);
    leitnerApply(c, true, 2000, days);
    CHECK(c.box == 2);
    for (int i = 0; i < 10; ++i) leitnerApply(c, true, 3000, days);
    CHECK(c.box == 5);
    leitnerApply(c, false, 4000, days);
    CHECK(c.box == 1);
    CHECK(c.due == 4000);
    CHECK(c.wrong == 1);
}

static fs::path tmpDir() {
    fs::path d = fs::temp_directory_path() / "sidequest_test";
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

static void writeFile(const fs::path& p, const std::string& s) {
    std::ofstream(p, std::ios::binary) << s;
}

static const char* kBank = R"({
 "conteudo": "Teste", "questoes": [
  {"id":"A","tipo":"vf","dificuldade":"facil","enunciado":"e1","alternativas":[{"texto":"Verdadeiro","correta":true},{"texto":"Falso","correta":false}],"explicacao":"x1"},
  {"id":"B","tipo":"escolha_unica","dificuldade":"facil","enunciado":"e2","alternativas":[{"texto":"a","correta":false},{"texto":"b","correta":true}],"explicacao":"x2"},
  {"id":"C","tipo":"multipla_correta","dificuldade":"media","enunciado":"e3","alternativas":[{"texto":"a","correta":true},{"texto":"b","correta":true},{"texto":"c","correta":false}],"explicacao":"x3"},
  {"id":"D","tipo":"lacuna","dificuldade":"dificil","enunciado":"e4","respostas_aceitas":["termo"],"explicacao":"x4"},
  {"id":"RUIM","tipo":"vf","dificuldade":"facil","enunciado":"sem alternativas"}
 ]})";

TEST_CASE("banco carrega e ignora questões inválidas") {
    fs::path d = tmpDir();
    writeFile(d / "01_teste.json", kBank);
    writeFile(d / "_formato.json", "{}");
    QuestionBank b;
    CHECK(b.load(d));
    CHECK(b.all().size() == 4);
    CHECK(b.warnings.size() == 1);
    CHECK(b.find("01_teste|C")->type == QType::Multi);
    CHECK(b.titleOf("01_teste") == "Teste");
}

TEST_CASE("estado: ida e volta em disco") {
    fs::path d = tmpDir();
    ProgressStore s;
    std::string err;
    CHECK(s.load(d / "estado.json", err));
    s.get("a|1").box = 3;
    s.get("a|1").due = 12345;
    s.interrupted = {"a|2"};
    s.studyOutside.insert("a");
    CHECK(s.save());
    ProgressStore t;
    CHECK(t.load(d / "estado.json", err));
    CHECK(t.find("a|1")->box == 3);
    CHECK(t.find("a|1")->due == 12345);
    CHECK(t.interrupted.size() == 1);
    CHECK(t.studyOutside.count("a") == 1);
}

TEST_CASE("sessão: erro volta ~3 cartões depois, interrompida vai à frente, compra só fácil") {
    fs::path d = tmpDir();
    writeFile(d / "01_teste.json", kBank);
    QuestionBank b;
    b.load(d);
    ProgressStore st;
    std::string err;
    st.load(d / "estado.json", err);
    Config cfg = Config::defaults(d);
    Session s(b, st, cfg);
    s.begin(1000);

    // compra: só V/F e escolha única fáceis
    const Question* q = s.nextQuestion(Slot::Buy, 1000);
    REQUIRE(q);
    CHECK((q->id == "A" || q->id == "B"));
    s.answered(*q, false, false, 1000);  // errou sem tempo de ver a explicação
    CHECK(s.hasPendingPill());
    auto pill = s.nextPill();
    REQUIRE(pill);
    CHECK(pill->text == q->explanation);

    // a errada só volta depois de 3 cartões
    std::string wrongKey = q->key();
    const Question* q2 = s.nextQuestion(Slot::Death, 1000);
    REQUIRE(q2);
    CHECK(q2->key() != wrongKey);
    const Question* q3 = s.nextQuestion(Slot::Death, 1000);
    REQUIRE(q3);
    CHECK(q3->key() != wrongKey);
    const Question* q4 = s.nextQuestion(Slot::Death, 1000);
    REQUIRE(q4);
    CHECK(q4->key() == wrongKey);

    // interrompida volta primeiro
    s.interrupted(*q2);
    const Question* again = s.nextQuestion(Slot::Death, 1000);
    REQUIRE(again);
    CHECK(again->key() == q2->key());
    CHECK(st.interrupted.size() == 1);
    s.answered(*again, true, true, 1000);
    CHECK(st.interrupted.empty());
}

TEST_CASE("sessão: erros repetidos no conteúdo marcam 'estudar fora'") {
    fs::path d = tmpDir();
    writeFile(d / "01_teste.json", kBank);
    QuestionBank b;
    b.load(d);
    ProgressStore st;
    std::string err;
    st.load(d / "estado.json", err);
    Config cfg = Config::defaults(d);
    Session s(b, st, cfg);
    s.begin(1000);
    for (const auto& q : b.all()) {
        if (q.id == "D") break;
        s.answered(q, false, true, 1000);
    }
    CHECK(s.flagged("01_teste"));
    CHECK(st.studyOutside.count("01_teste") == 1);
    CHECK(s.nextQuestion(Slot::Death, 1000) == nullptr);  // sem mais perguntas desse conteúdo
}

static void feed(DeathDetector& d, Features f, int n) {
    for (int i = 0; i < n; ++i) d.observe(f);
}

TEST_CASE("detector: morte exige 5 leituras seguidas sem o HUD; a volta basta com 2") {
    DeathDetector d;
    Features alive{1.0f}, dead{0.0f};
    d.setState(LifeState::Alive);

    for (int i = 0; i < 4; ++i) CHECK_FALSE(d.observe(dead).has_value());   // 4 não bastam
    CHECK_FALSE(d.observe(alive).has_value());                               // o HUD voltou: zera a contagem
    for (int i = 0; i < 4; ++i) CHECK_FALSE(d.observe(dead).has_value());
    auto ev = d.observe(dead);                                               // 5ª seguida
    REQUIRE(ev.has_value());
    CHECK(*ev == LifeState::Dead);
    CHECK_FALSE(d.observe(dead).has_value());                                // já confirmado
    CHECK_FALSE(d.observe(alive).has_value());                               // uma só ainda não
    ev = d.observe(alive);
    REQUIRE(ev.has_value());
    CHECK(*ev == LifeState::Alive);
}

TEST_CASE("detector: sem HUD desde o começo (lobby/menu) não é morte") {
    DeathDetector d;                                // estado desconhecido
    for (int i = 0; i < 20; ++i) CHECK_FALSE(d.observe({0.0f}).has_value());
    // o HUD aparece (entrou na partida) e depois some (morreu): aí sim é morte
    CHECK_FALSE(d.observe({1.0f}).has_value());
    auto ev = d.observe({1.0f});
    REQUIRE(ev.has_value());
    CHECK(*ev == LifeState::Alive);
    feed(d, {0.0f}, 4);
    ev = d.observe({0.0f});
    REQUIRE(ev.has_value());
    CHECK(*ev == LifeState::Dead);
}

TEST_CASE("detector: menu aberto (elementos do menu) não conta como morte") {
    DeathDetector d;
    d.setState(LifeState::Alive);
    Features pausa{0.0f, 0.0f, 1.0f};               // HUD "sumiu" porque o menu de pausa está aberto
    Features compra{0.0f, 1.0f, 0.0f};              // menu de compra: linha do cartão do jogador
    for (int i = 0; i < 30; ++i) CHECK_FALSE(d.observe(pausa).has_value());
    for (int i = 0; i < 30; ++i) CHECK_FALSE(d.observe(compra).has_value());
    CHECK(d.state() == LifeState::Alive);
    CHECK_FALSE(DeathDetector::menuOpen({0.0f, 0.0f, 0.0f}));   // só os elementos contam
    // um menu no meio das leituras de morte zera a contagem
    feed(d, {0.0f}, 4);
    CHECK_FALSE(d.observe(pausa).has_value());
    CHECK_FALSE(d.observe({0.0f}).has_value());
    feed(d, {0.0f}, 3);
    auto ev = d.observe({0.0f});                    // 5 seguidas depois do menu
    REQUIRE(ev.has_value());
    CHECK(*ev == LifeState::Dead);
}

TEST_CASE("detector: espectando alguém (ícone) é morte mesmo com o HUD de baixo presente") {
    DeathDetector d;
    d.setState(LifeState::Alive);
    Features espec{1.0f, 0.0f, 0.0f, 1.0f};         // HUD presente (é o do jogador observado) + ícone de espectador
    for (int i = 0; i < 4; ++i) CHECK_FALSE(d.observe(espec).has_value());
    auto ev = d.observe(espec);                      // 5 leituras seguidas, como qualquer morte
    REQUIRE(ev.has_value());
    CHECK(*ev == LifeState::Dead);
    // parou de espectar e o HUD vivo está lá: volta a "vivo" (2 leituras)
    CHECK_FALSE(d.observe({1.0f}).has_value());
    ev = d.observe({1.0f});
    REQUIRE(ev.has_value());
    CHECK(*ev == LifeState::Alive);
}

TEST_CASE("detector: espectar vale como prova mesmo sem ter visto o HUD vivo antes") {
    DeathDetector d;                                 // estado desconhecido (ex.: app aberto já morto)
    Features espec{0.0f, 0.0f, 0.0f, 1.0f};
    for (int i = 0; i < 4; ++i) CHECK_FALSE(d.observe(espec).has_value());
    auto ev = d.observe(espec);
    REQUIRE(ev.has_value());
    CHECK(*ev == LifeState::Dead);
}

TEST_CASE("detector: ícone parcial (poucos pontos) não conta como espectador") {
    DeathDetector d;
    d.setState(LifeState::Alive);
    for (int i = 0; i < 20; ++i) CHECK_FALSE(d.observe({1.0f, 0.0f, 0.0f, 0.4f}).has_value());   // só 40% dos pontos
    CHECK(d.state() == LifeState::Alive);
}

TEST_CASE("detector: ponta parcial do HUD (abaixo do limiar) conta como ausente") {
    DeathDetector d;                                // limiar padrão 0,8
    d.setState(LifeState::Alive);
    for (int i = 0; i < 4; ++i) CHECK_FALSE(d.observe({0.5f}).has_value());
    auto ev = d.observe({0.5f});
    REQUIRE(ev.has_value());
    CHECK(*ev == LifeState::Dead);
}

TEST_CASE("banco real do usuário (se existir)") {
    fs::path d = "D:/Arquivos/Downloads/sbd2/questoes";
    if (!fs::exists(d)) return;
    QuestionBank b;
    CHECK(b.load(d));
    for (auto& w : b.warnings) MESSAGE(w);
    CHECK(b.all().size() > 50);
    MESSAGE(b.all().size() << " questões em " << b.contentCount() << " conteúdos");
}
