// E2E do inspetor: toda seção que o painel desenha é exercida como a pessoa a
// exercita, e o documento é conferido depois.
//
// POR QUE ESTE ARQUIVO EXISTE
// -----------------------------------------------------------------------------
// As seções do inspetor são treze, cada uma num arquivo de família, e cada uma
// escreve por `Section::write`. Os testes que havia cobriam a MECÂNICA embaixo
// delas -- `setProperty` nos quatro casos, `resolve` nos vinte contextos, o
// round-trip de `toJson` -- e nenhum cobria a seção. Uma seção que desenha um
// controle e não liga o `write` passa em tudo isso: a mecânica está certa,
// ninguém a chama. Foi exatamente essa a forma do defeito que 18/09 achou em
// `RenderView::refined` (escrito, nunca lido) um andar acima.
//
// O QUE ESTE ARQUIVO AFIRMA, POR SEÇÃO
// -----------------------------------------------------------------------------
//   1. a seção APARECE no inventário do nó a que pertence;
//   2. escrever sob Base cria a chave simples, e `resolve` devolve o que foi
//      escrito, byte a byte pelo lexema;
//   3. desfazer devolve o documento aos bytes que ele tinha -- não "a um estado
//      equivalente", aos bytes;
//   4. escrever sob (Dark, Base) cria entrada na lista de especialização, e a
//      partir daí os dois escopos dão respostas DIFERENTES -- que é a única
//      prova de que a entrada é mesmo do escopo e não uma segunda escrita na
//      chave simples;
//   5. remover o override apaga a lista e a chave simples volta.
//
// O PORTÃO DE COBERTURA
// -----------------------------------------------------------------------------
// O inventário vem do painel (`InspectorStats::drawn`, gravado em
// `Section::begin`), não de uma lista escrita aqui. Uma seção nova aparece no
// inventário sem ninguém a acrescentar, e o caso REPROVA até ela ganhar uma
// sonda na tabela abaixo. É isto que impede este arquivo de virar uma lista que
// envelhece: ele não pode ficar desatualizado em silêncio.
#include "check.h"
#include "Source/IconComposerFoundation/Edit.h"
#include "Source/IconComposerFoundation/Json.h"
#include "Source/IconComposerFoundation/Values.h"
#include "Source/IconComposerKit/Headless.h"
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/Session.h"
#include "imgui.h"

#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void writeFile(const fs::path& p, const std::string& text) {
    std::FILE* f = std::fopen(p.string().c_str(), "wb");
    if (!f) return;
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}

// UM DOCUMENTO MÍNIMO, e mínimo é o ponto: nenhuma das propriedades que as
// sondas escrevem existe aqui, então toda escrita ACRESCENTA um membro que o
// documento não tinha. É o caminho que o próprio painel avisa em voz alta
// ("editing writes the plain key"), e é o que torna o passo 3 -- desfazer volta
// aos bytes -- uma afirmação com conteúdo.
const char* kMinimal = R"({
  "fill" : "automatic",
  "groups" : [
    {
      "name" : "Group",
      "layers" : [
        {
          "name" : "Art",
          "image-name" : "art.svg"
        }
      ]
    }
  ]
})";

fs::path makeBundle(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("ic-e2e-" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "Assets", ec);
    writeFile(dir / "icon.json", kMinimal);
    writeFile(dir / "Assets" / "art.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1024 1024\">"
              "<path d=\"M0 0 L1024 0 L1024 1024 L0 1024 Z\" fill=\"#3366cc\"/></svg>");
    writeFile(dir / "Assets" / "other.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1024 1024\">"
              "<path d=\"M0 0 L512 0 L512 512 L0 512 Z\" fill=\"#cc3366\"/></svg>");
    return dir;
}

// A SONDA DE UMA PROPRIEDADE: dois valores da gramática dela, distintos entre
// si. Dois, e não um, porque o passo 4 precisa que Base e Dark discordem; com
// um valor só, "o override pegou" e "a chave simples foi escrita duas vezes"
// seriam indistinguíveis.
//
// Onde existe um `toJson` em Values.h, a sonda passa por ele: assim a forma é a
// que o leitor da própria casa aceita, e não uma que este arquivo inventou.
struct Probe {
    icf::json::Value base;
    icf::json::Value dark;
};

// Um vetor e nao um mapa: `json::Value` nao tem construtor vazio (Json.h -- a
// Kind e obrigatoria), entao `map[chave] = ...` nao compila, e nem deveria.
std::vector<std::pair<std::string, Probe>> probes() {
    std::vector<std::pair<std::string, Probe>> p;
    auto add = [&](const char* prop, icf::json::Value a, icf::json::Value b) {
        p.emplace_back(prop, Probe{std::move(a), std::move(b)});
    };
    add("hidden", icf::json::Value::boolean(true), icf::json::Value::boolean(false));
    add("opacity", icf::json::Value::number(0.5), icf::json::Value::number(0.25));
    add("blend-mode",
        icf::json::Value::string(std::string(icf::blendModeToString(icf::BlendMode::Multiply))),
        icf::json::Value::string(std::string(icf::blendModeToString(icf::BlendMode::Screen))));
    add("position", icf::positionToJson(icf::Position{0.8, icf::Point{12.0, -4.0}}),
        icf::positionToJson(icf::Position{1.25, icf::Point{0.0, 7.5}}));
    // NAO `automatic`: o documento minimo ja carrega `"fill" : "automatic"`, e
    // uma sonda igual ao valor que ja esta la nao moveria byte nenhum -- o que
    // o passo 2 recusa, e com razao. Achado pelo proprio caso, na primeira
    // execucao dele.
    add("fill",
        icf::fillToJson(icf::Fill{icf::FillKind::Solid,
                                  {icf::Color{icf::ColorSpace::SRGB, 4, {0.2, 0.4, 0.8, 1.0}}},
                                  std::nullopt}),
        icf::fillToJson(icf::Fill{icf::FillKind::Automatic, {}, std::nullopt}));
    add("shadow", icf::shadowToJson(icf::Shadow{icf::ShadowKind::Neutral, 0.4}),
        icf::shadowToJson(icf::Shadow{icf::ShadowKind::LayerColor, 0.9}));
    add("translucency", icf::translucencyToJson(icf::Translucency{true, 0.6}),
        icf::translucencyToJson(icf::Translucency{true, 0.2}));
    add("refractivity", icf::refractivityToJson(icf::Refractivity{true, 0.5, 0.75}),
        icf::refractivityToJson(icf::Refractivity{true, 0.1, 0.2}));
    add("specular", icf::json::Value::boolean(true), icf::json::Value::boolean(false));
    add("glass", icf::json::Value::boolean(true), icf::json::Value::boolean(false));
    add("blur-material", icf::json::Value::number(0.56), icf::json::Value::number(0.125));
    add("lighting",
        icf::json::Value::string(std::string(icf::lightingToString(icf::Lighting::Combined))),
        icf::json::Value::string(std::string(icf::lightingToString(icf::Lighting::Individual))));
    add("image-name", icf::json::Value::string("other.svg"), icf::json::Value::string("art.svg"));
    return p;
}

const Probe* findProbe(const std::vector<std::pair<std::string, Probe>>& table,
                       const std::string& prop) {
    for (const auto& kv : table) {
        if (kv.first == prop) return &kv.second;
    }
    return nullptr;
}

// Um frame do inspetor sobre `path`, no escopo `scope`.
ick::InspectorStats inspectorFrame(ick::HeadlessImGui& gui, ick::Session& s, icf::NodePath path,
                                   icf::Context scope) {
    s.selection = path;
    s.scope = scope;
    gui.newFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(420, 1400));
    // O canal do Kit para o app, descartado aqui: nenhum caso deste arquivo
    // pede dialogo, e o botao que pede tem caso proprio
    // (test_e2e_import_save_duplicate.cpp).
    ick::MenuActions actions;
    ick::InspectorStats st = ick::drawInspector(s, actions);
    gui.render();
    return st;
}

std::string bytes(const ick::Session& s) { return icf::json::write(s.root()); }

const icf::json::Value* resolved(const ick::Session& s, icf::NodePath path, const std::string& prop,
                                 icf::Context scope) {
    const icf::json::Value* node = icf::nodeAt(s.root(), path);
    if (!node) return nullptr;
    return icf::resolve(*node, prop, scope);
}

std::string lexeme(const icf::json::Value* v) { return v ? icf::json::write(*v) : std::string("<ausente>"); }

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// A VARREDURA. Um nó de cada espécie, todas as seções que ele desenha, os cinco
// passos em cada uma.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(e2e_every_inspector_section_writes_reads_scopes_and_undoes) {
    const fs::path dir = makeBundle("inspector");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 1600.0f);
    const std::vector<std::pair<std::string, Probe>> table = probes();

    const icf::Context base{};
    const icf::Context dark{icf::Appearance::Dark, icf::Idiom::Base};

    struct Node { const char* what; icf::NodePath path; };
    const Node nodes[] = {
        {"documento", icf::NodePath{}},
        {"grupo", icf::NodePath{std::size_t{0}, std::nullopt}},
        {"camada", icf::NodePath{std::size_t{0}, std::size_t{0}}},
    };

    std::size_t exercised = 0;
    for (const Node& n : nodes) {
        const ick::InspectorStats inv = inspectorFrame(gui, *s, n.path, base);
        CHECK_EQ(gui.errors(), std::uint64_t{0});
        REQUIRE(!inv.drawn.empty());

        for (const ick::SectionInfo& sec : inv.drawn) {
            // O PORTÃO. Uma seção sem sonda reprova aqui, com o nome dela.
            const Probe* found = findProbe(table, sec.prop);
            if (!found) {
                std::printf("  secao sem sonda: %s (%s) no %s\n", sec.label.c_str(),
                            sec.prop.c_str(), n.what);
            }
            REQUIRE(found != nullptr);
            const Probe& probe = *found;

            const std::string before = bytes(*s);

            // O QUE O NO JA TINHA SOB BASE. Para duas das dezoito secoes o
            // documento minimo ja carrega a chave (`fill` no documento,
            // `image-name` na camada), e para essas "remover" nao e "voltar ao
            // que era" -- a limpeza no fim tem de REPOR o valor, nao apagar a
            // chave. A primeira execucao deste caso reprovou exatamente nas
            // duas, e a mensagem foi essa.
            std::optional<icf::json::Value> had;
            if (const icf::json::Value* n0 = icf::nodeAt(s->root(), n.path)) {
                if (icf::hasOwnEntry(*n0, sec.prop, base)) {
                    if (const icf::json::Value* v0 = icf::resolve(*n0, sec.prop, base)) had = *v0;
                }
            }

            // 2. escrever sob Base, e ler de volta.
            s->setProperty(n.path, sec.prop, base, icf::json::Value(probe.base));
            const std::string wantBase = icf::json::write(probe.base);
            CHECK_EQ(lexeme(resolved(*s, n.path, sec.prop, base)), wantBase);
            const bool moved = bytes(*s) != before;
            if (!moved) {
                std::printf("  escrita sem efeito: %s (%s) no %s\n", sec.label.c_str(),
                            sec.prop.c_str(), n.what);
            }
            CHECK(moved);   // uma escrita que nao move byte nao e escrita

            // 3. desfazer devolve os BYTES.
            REQUIRE(s->canUndo());
            REQUIRE(s->undo());
            CHECK_EQ(bytes(*s), before);

            // 2'. de volta ao valor de Base, para o passo 4 ter de onde partir.
            s->setProperty(n.path, sec.prop, base, icf::json::Value(probe.base));

            // 4. o override de escopo: os dois escopos passam a discordar.
            s->setProperty(n.path, sec.prop, dark, icf::json::Value(probe.dark));
            const icf::json::Value* node = icf::nodeAt(s->root(), n.path);
            REQUIRE(node != nullptr);
            CHECK(icf::hasOwnEntry(*node, sec.prop, dark));
            CHECK_EQ(lexeme(resolved(*s, n.path, sec.prop, dark)), icf::json::write(probe.dark));
            CHECK_EQ(lexeme(resolved(*s, n.path, sec.prop, base)), wantBase);

            // 5. remover o override: a lista some e a chave simples volta.
            s->setProperty(n.path, sec.prop, dark, std::nullopt);
            const icf::json::Value* after = icf::nodeAt(s->root(), n.path);
            REQUIRE(after != nullptr);
            CHECK(!icf::hasOwnEntry(*after, sec.prop, dark));
            CHECK_EQ(lexeme(resolved(*s, n.path, sec.prop, dark)), wantBase);
            CHECK(after->find(std::string(sec.prop) + "-specializations") == nullptr);

            // E o documento volta ao ponto de partida para a secao seguinte:
            // repondo o valor que havia, ou apagando a chave que nao havia.
            s->setProperty(n.path, sec.prop, base,
                           had ? std::optional<icf::json::Value>(*had) : std::nullopt);
            CHECK_EQ(bytes(*s), before);
            ++exercised;
        }
    }

    // Não é asserção de número mágico: é a recusa de passar com zero seções
    // varridas, que é o que aconteceria se `drawn` deixasse de ser preenchido.
    CHECK(exercised >= 10);
    std::printf("  %zu seção(ões) de inspetor varridas ponta a ponta\n", exercised);
}

// ─────────────────────────────────────────────────────────────────────────────
// O INVENTÁRIO POR ESPÉCIE DE NÓ. Uma seção que passa a aparecer no nó errado
// -- `glass` num grupo, `lighting` numa camada -- não é pega pelo caso acima,
// porque lá toda seção que aparece é exercida onde apareceu.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(e2e_inspector_shows_the_right_sections_for_each_node) {
    const fs::path dir = makeBundle("inventario");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 1600.0f);

    auto props = [&](icf::NodePath p) {
        const ick::InspectorStats st = inspectorFrame(gui, *s, p, icf::Context{});
        std::vector<std::string> out;
        for (const auto& sec : st.drawn) out.push_back(sec.prop);
        return out;
    };
    auto has = [](const std::vector<std::string>& v, const char* what) {
        for (const auto& x : v) if (x == what) return true;
        return false;
    };

    const std::vector<std::string> doc = props(icf::NodePath{});
    const std::vector<std::string> grp = props(icf::NodePath{std::size_t{0}, std::nullopt});
    const std::vector<std::string> lay = props(icf::NodePath{std::size_t{0}, std::size_t{0}});

    // O documento tem o preenchimento -- é o fundo do ícone -- e não tem nada
    // que pertença a uma camada.
    CHECK(has(doc, "fill"));
    CHECK(!has(doc, "glass"));
    CHECK(!has(doc, "image-name"));

    // O grupo tem os efeitos de grupo e não tem a arte.
    CHECK(has(grp, "blur-material"));
    CHECK(has(grp, "refractivity"));
    CHECK(has(grp, "lighting"));
    CHECK(!has(grp, "image-name"));
    CHECK(!has(grp, "glass"));

    // A camada tem a arte e o vidro, e não tem os efeitos que são do grupo.
    CHECK(has(lay, "image-name"));
    CHECK(has(lay, "glass"));
    CHECK(!has(lay, "lighting"));

    // `opacity` NAO e do documento: `Coverage.cpp` lista as chaves do nivel do
    // documento e ela nao esta la -- opacidade e de grupo e de camada. Esta
    // linha ja afirmou o contrario e o caso a reprovou na primeira execucao.
    CHECK(!has(doc, "opacity"));
    for (const auto* v : {&grp, &lay}) {
        CHECK(has(*v, "opacity"));
        CHECK(has(*v, "hidden"));
        CHECK(has(*v, "blend-mode"));
        CHECK(has(*v, "position"));
    }
}
