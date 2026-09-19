// E2E dos três buracos que o laudo de 18/09 mediu em §4.2, §4.3 e §4.4, cada
// um exercido pela superfície que a pessoa toca:
//
//   (a) §4.3 -- importar asset só aceitava caminho DIGITADO. Agora há um botão
//       "Browse…" que PEDE um diálogo ao app pelo canal que já existia
//       (`MenuActions`). O caso clica no botão de verdade e confere que o
//       pedido saiu, e que ele carrega o NÓ e o ESCOPO consigo -- que é a
//       parte que não se vê e é onde estaria o defeito: `State::act()` roda
//       depois do frame, e "a camada selecionada" pode já ser outra.
//
//   (b) §4.2 -- o motivo de uma falha de escrita ia para `stderr`, que uma
//       janela não tem, e `State::trouble` só era desenhado no canvas VAZIO.
//       O caso produz uma falha de escrita DE VERDADE (`Session::saveAs` numa
//       pasta que não pode existir) e afirma que o texto chega às duas
//       superfícies: elidido na barra do canvas, INTEIRO no Diagnostics -- e
//       com um documento aberto, que é o único momento em que salvar pode
//       falhar e era exatamente quando a frase sumia.
//
//   (c) §7 da spec de 13/09 -- `Edit > Duplicate` não existia. O caso abre o
//       menu com um clique, clica no item, e pergunta À ÁRVORE o que ela
//       passou a mostrar.
//
// POR QUE O CLIQUE E NÃO A CHAMADA
// -----------------------------------------------------------------------------
// A mesma razão do bloco final de test_e2e_layers.cpp: entre o pixel e a
// operação existem o retângulo do item, o popup que o contém e a ordem em que
// os dois são submetidos. Um caso que chamasse `s.duplicateNode()` provaria a
// operação e passaria com o item de menu desligado. Os painéis gravam onde
// desenharam cada coisa (`MenuStats::drawn`, `MenuStats::titles`,
// `InspectorStats::importBrowseAt`) e o caso injeta o evento de mouse do ImGui
// naquele ponto -- é a fila do contexto headless, nenhum cursor do sistema se
// move.
#include "check.h"
#include "Source/IconComposerFoundation/Edit.h"
#include "Source/IconComposerFoundation/Json.h"
#include "Source/IconComposerKit/Headless.h"
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/Session.h"
#include "imgui.h"

#include <cstdio>
#include <filesystem>
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

// Um grupo com duas camadas: o menor documento em que "a duplicata entrou logo
// DEPOIS do original" é distinguível de "entrou no fim".
const char* kTree = R"({
  "fill" : "automatic",
  "groups" : [
    {
      "name" : "Alpha",
      "layers" : [
        {
          "name" : "a1",
          "image-name" : "art.svg"
        },
        {
          "name" : "a2",
          "image-name" : "art.svg"
        }
      ]
    }
  ]
})";

fs::path makeBundle(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("ic-e2e-t3-" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "Assets", ec);
    writeFile(dir / "icon.json", kTree);
    writeFile(dir / "Assets" / "art.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1024 1024\">"
              "<path d=\"M0 0 L1024 0 L1024 1024 L0 1024 Z\" fill=\"#3366cc\"/></svg>");
    return dir;
}

// ─── um quadro de cada painel ───────────────────────────────────────────────
// Cada um abre UMA janela e só ela, porque o clique precisa que a janela sob o
// ponteiro seja a que se quer clicar: com uma janela desenhada, ela é a
// hovered, e não há ambiguidade a explicar.

ick::InspectorStats inspectorFrame(ick::HeadlessImGui& gui, ick::Session& s,
                                   ick::MenuActions& actions) {
    gui.newFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(420, 1400));
    ick::InspectorStats st = ick::drawInspector(s, actions);
    gui.render();
    return st;
}

ick::CanvasStats canvasFrame(ick::HeadlessImGui& gui, ick::Session& s, ick::MenuActions& actions,
                             const std::string& trouble) {
    const ick::RenderView view;   // nenhum render aconteceu; o canvas não precisa de um
    gui.newFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(900, 700));
    ick::CanvasStats st = ick::drawCanvas(s, view, actions, trouble);
    gui.render();
    return st;
}

ick::DiagnosticsStats diagnosticsFrame(ick::HeadlessImGui& gui, ick::Session& s,
                                       const std::string& trouble) {
    const ick::RenderView view;
    gui.newFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(900, 400));
    ick::DiagnosticsStats st = ick::drawDiagnostics(s, view, trouble);
    gui.render();
    return st;
}

ick::LayersStats layersFrame(ick::HeadlessImGui& gui, ick::Session& s) {
    gui.newFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(320, 900));
    ick::LayersStats st = ick::drawLayers(s);
    gui.render();
    return st;
}

// UM CLIQUE SÃO QUATRO QUADROS, e são quatro pela mesma razão que estão
// escritas em test_e2e_layers.cpp: o ponteiro chega e o item vira o hovered, o
// botão desce, o botão sobe -- e aí um quadro a mais, que é o que a pessoa vê,
// porque no modo imediato o efeito do clique só aparece no quadro seguinte. O
// ponteiro sai de cima no fim para o próximo clique não começar pousado.
//
// `draw` é o quadro de um painel qualquer, e o valor de retorno é o do último
// quadro -- o de depois.
template <class Draw>
auto clickAt(Draw&& draw, ImVec2 at) -> decltype(draw()) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(at.x, at.y);
    draw();
    io.AddMouseButtonEvent(0, true);
    draw();
    io.AddMouseButtonEvent(0, false);
    draw();
    io.AddMousePosEvent(-1.0f, -1.0f);
    return draw();
}

const ick::MenuItemInfo* find(const std::vector<ick::MenuItemInfo>& v, const char* label) {
    for (const auto& i : v) {
        if (i.label == label) return &i;
    }
    return nullptr;
}

// A árvore como uma linha só: "Alpha[a1 a2]".
std::string shape(const ick::LayersStats& st) {
    std::string out;
    bool open = false;
    for (const ick::RowInfo& r : st.drawn) {
        if (!r.layer) {
            if (open) out += "]";
            if (!out.empty()) out += " ";
            out += r.title + "[";
            open = true;
        } else {
            if (out.size() && out.back() != '[') out += " ";
            out += r.title;
        }
    }
    if (open) out += "]";
    return out;
}

std::string nameAt(const ick::Session& s, icf::NodePath p) {
    const icf::json::Value* node = icf::nodeAt(s.root(), p);
    if (!node) return "<ausente>";
    const icf::json::Value* n = node->find("name");
    return (n && n->kind() == icf::json::Value::Kind::String) ? n->rawString() : "<sem nome>";
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// (a) O BOTÃO "Browse…" PEDE O DIÁLOGO, E O PEDIDO CARREGA A CAMADA CONSIGO
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(e2e_asset_browse_button_asks_the_app_and_names_the_layer_that_asked) {
    const fs::path dir = makeBundle("browse");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 1600.0f);

    const icf::NodePath a2{std::size_t{0}, std::size_t{1}};
    s->selection = a2;
    // Um escopo que NÃO é Base, de propósito: o pedido tem duas coordenadas e
    // um caso feito só sob Base não distinguiria "o escopo viajou" de "o
    // escopo veio zerado".
    s->scope = icf::Context{icf::Appearance::Dark, icf::Idiom::Base};

    ick::MenuActions actions;
    const ick::InspectorStats laid = inspectorFrame(gui, *s, actions);
    CHECK_EQ(gui.errors(), std::uint64_t{0});
    // O botão existe e o painel disse onde.
    REQUIRE(laid.importBrowseAt.x > 0.0f);
    CHECK(!actions.importAsset);   // desenhar não pede nada

    const auto draw = [&] { return inspectorFrame(gui, *s, actions); };
    clickAt(draw, laid.importBrowseAt);

    CHECK(actions.importAsset);
    REQUIRE(actions.importInto.group.has_value());
    CHECK(actions.importInto == a2);
    CHECK(actions.importScope.appearance == icf::Appearance::Dark);
    CHECK(actions.importScope.idiom == icf::Idiom::Base);
    CHECK_EQ(gui.errors(), std::uint64_t{0});

    // E O PEDIDO NÃO SEGUE A SELEÇÃO. `State::act()` roda depois do frame; se
    // o pedido fosse "importe para o que estiver selecionado", mover a seleção
    // entre o clique e o diálogo escreveria `image-name` na camada errada, sem
    // um pixel na tela explicando.
    s->selection = icf::NodePath{std::size_t{0}, std::size_t{0}};
    inspectorFrame(gui, *s, actions);
    CHECK(actions.importInto == a2);

    // O NÓ QUE NÃO TEM ARTE NÃO TEM O BOTÃO. A raiz do documento não desenha a
    // seção Image Asset, e `importBrowseAt` volta a `{0,0}` -- um caso que
    // clicasse ali estaria clicando no vazio.
    ick::MenuActions other;
    s->selection = icf::NodePath{};
    const ick::InspectorStats root = inspectorFrame(gui, *s, other);
    CHECK_EQ(root.importBrowseAt.x, 0.0f);
    CHECK(!other.importAsset);
}

// ─────────────────────────────────────────────────────────────────────────────
// (b) UMA FALHA DE ESCRITA CHEGA NA TELA, COM DOCUMENTO ABERTO
//
// A falha é de verdade: `saveAs` dentro de um caminho cujo "pai" é um ARQUIVO
// comum, e portanto `create_directories` não pode criar nada ali. Nada aqui
// inventa a frase -- ela vem do `std::error_code` do sistema de arquivos, que
// é a mesma que a pessoa veria.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(e2e_a_write_failure_reaches_the_canvas_bar_and_the_diagnostics_panel) {
    const fs::path dir = makeBundle("falha");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 1000.0f);
    ick::MenuActions actions;

    // Um arquivo comum onde uma pasta precisaria estar.
    const fs::path blocker = fs::temp_directory_path() / "ic-e2e-t3-bloqueio";
    std::error_code ec;
    fs::remove_all(blocker, ec);
    writeFile(blocker, "nao sou uma pasta");
    const std::string reason = s->saveAs(blocker / "x.icon");
    REQUIRE(!reason.empty());   // a escrita falhou mesmo
    const std::string trouble = "save as: " + reason;

    // SEM QUEIXA, NENHUM DOS DOIS PAINÉIS DIZ NADA -- e esta metade importa:
    // uma linha vermelha permanente vira parte do cenário.
    const ick::CanvasStats quiet = canvasFrame(gui, *s, actions, "");
    CHECK_EQ(quiet.trouble, std::string());
    const ick::DiagnosticsStats quietDiag = diagnosticsFrame(gui, *s, "");
    CHECK_EQ(quietDiag.trouble, std::string());

    // A BARRA DO CANVAS, com o documento aberto (é a Session que ele desenha).
    const ick::CanvasStats loud = canvasFrame(gui, *s, actions, trouble);
    CHECK(!loud.trouble.empty());
    CHECK_EQ(gui.errors(), std::uint64_t{0});
    // O que a barra mostra é o começo do que aconteceu, verbatim: ou a frase
    // inteira, ou um prefixo dela marcado com reticências.
    if (loud.trouble.size() >= trouble.size()) {
        CHECK_EQ(loud.trouble, trouble);
    } else {
        REQUIRE(loud.trouble.size() > 3);
        CHECK_EQ(loud.trouble.substr(loud.trouble.size() - 3), std::string("..."));
        const std::string head = loud.trouble.substr(0, loud.trouble.size() - 3);
        CHECK_EQ(trouble.compare(0, head.size(), head), 0);
    }
    // E a operação está no pedaço que cabe: "save as:" são oito caracteres, e
    // uma linha vermelha que não diz O QUE falhou não é melhor que um bipe.
    CHECK_EQ(loud.trouble.compare(0, 8, "save as:"), 0);

    // O DIAGNOSTICS TEM A FRASE INTEIRA. Este é o "algum lugar" onde o texto
    // completo tem de estar: a barra elide, e um caminho cortado não serve
    // para achar o arquivo.
    const ick::DiagnosticsStats diag = diagnosticsFrame(gui, *s, trouble);
    CHECK_EQ(diag.trouble, trouble);
    CHECK(diag.rows > quietDiag.rows);
    CHECK_EQ(gui.errors(), std::uint64_t{0});

    // E UMA FRASE LONGA DE PROPÓSITO, para o caminho da elisão ser exercido
    // mesmo num sistema cuja mensagem de erro seja curta.
    const std::string longOne = "save as: " + std::string(400, 'x');
    const ick::CanvasStats elided = canvasFrame(gui, *s, actions, longOne);
    CHECK(elided.trouble.size() < longOne.size());
    CHECK(!elided.trouble.empty());
    CHECK_EQ(diagnosticsFrame(gui, *s, longOne).trouble, longOne);

    fs::remove_all(blocker, ec);
}

// ─────────────────────────────────────────────────────────────────────────────
// (c) `Edit > Duplicate`, CLICADO NO MENU
//
// Dois cliques, porque um menu são dois: o primeiro no TÍTULO abre o popup --
// e só então os itens existem para serem clicados --, o segundo no item.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(e2e_edit_duplicate_clicked_in_the_menu_copies_the_layer_in_place) {
    const fs::path dir = makeBundle("duplicar");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 1000.0f);
    ick::MenuActions actions;
    const auto draw = [&] { return canvasFrame(gui, *s, actions, ""); };

    const icf::NodePath a1{std::size_t{0}, std::size_t{0}};
    s->selection = a1;
    CHECK_EQ(shape(layersFrame(gui, *s)), std::string("Alpha[a1 a2]"));

    // Abrir o menu Edit.
    const ick::CanvasStats closed = draw();
    const ick::MenuItemInfo* editTitle = find(closed.menu.titles, "Edit");
    REQUIRE(editTitle != nullptr);
    const ick::CanvasStats opened = clickAt(draw, editTitle->at);

    // Agora o item existe, está habilitado, e o painel disse onde ele ficou.
    const ick::MenuItemInfo* dup = find(opened.menu.drawn, "Duplicate");
    REQUIRE(dup != nullptr);
    CHECK(dup->enabled);
    REQUIRE(dup->at.x > 0.0f);

    clickAt(draw, dup->at);
    CHECK_EQ(gui.errors(), std::uint64_t{0});

    // A ÁRVORE. A cópia entrou LOGO DEPOIS do original, não no fim, e leva o
    // nome verbatim (`[INF]` em Edit.h: nada medido diz que a Apple sufixa).
    CHECK_EQ(shape(layersFrame(gui, *s)), std::string("Alpha[a1 a1 a2]"));
    CHECK_EQ(nameAt(*s, icf::NodePath{std::size_t{0}, std::size_t{1}}), std::string("a1"));
    // Verbatim mesmo: os bytes do nó novo são os do original.
    const icf::json::Value* orig = icf::nodeAt(s->root(), a1);
    const icf::json::Value* copy = icf::nodeAt(s->root(), icf::NodePath{std::size_t{0}, std::size_t{1}});
    REQUIRE(orig != nullptr);
    REQUIRE(copy != nullptr);
    CHECK_EQ(icf::json::write(*orig), icf::json::write(*copy));

    // A SELEÇÃO FOI PARA A DUPLICATA, que é o nó que a pessoa acabou de criar.
    REQUIRE(s->selection.has_value());
    CHECK(*s->selection == (icf::NodePath{std::size_t{0}, std::size_t{1}}));

    // UM CLIQUE, UM UNDO. Uma operação estrutural que empilhasse dois comandos
    // pediria dois Ctrl+Z para desfazer um clique -- a regra que `addGroup` e
    // `removeNode` já seguem.
    REQUIRE(s->canUndo());
    REQUIRE(s->undo());
    CHECK_EQ(shape(layersFrame(gui, *s)), std::string("Alpha[a1 a2]"));
    CHECK(!s->canUndo());
}

// ─────────────────────────────────────────────────────────────────────────────
// E SEM SELEÇÃO O ITEM É CINZA, como os vizinhos dele. Duplicar "nada" não é
// uma operação, e um item que aceita o clique e não faz nada é a mentira que
// este projeto passou 18/09 inteiro medindo.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(e2e_edit_duplicate_is_greyed_with_no_selection) {
    const fs::path dir = makeBundle("duplicar-cinza");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 1000.0f);
    ick::MenuActions actions;
    const auto draw = [&] { return canvasFrame(gui, *s, actions, ""); };

    s->selection.reset();
    const ick::CanvasStats closed = draw();
    const ick::MenuItemInfo* editTitle = find(closed.menu.titles, "Edit");
    REQUIRE(editTitle != nullptr);
    const ick::CanvasStats opened = clickAt(draw, editTitle->at);

    const ick::MenuItemInfo* dup = find(opened.menu.drawn, "Duplicate");
    REQUIRE(dup != nullptr);
    CHECK(!dup->enabled);
    // O vizinho que já tinha essa regra, para a comparação não depender de um
    // literal: Delete e Duplicate respondem à mesma pergunta.
    const ick::MenuItemInfo* del = find(opened.menu.drawn, "Delete");
    REQUIRE(del != nullptr);
    CHECK_EQ(dup->enabled, del->enabled);

    // E clicar nele não muda a árvore.
    clickAt(draw, dup->at);
    CHECK_EQ(shape(layersFrame(gui, *s)), std::string("Alpha[a1 a2]"));
    CHECK(!s->canUndo());
    CHECK_EQ(gui.errors(), std::uint64_t{0});
}
