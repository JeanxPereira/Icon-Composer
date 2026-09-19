// E2E da árvore de camadas: cada operação que o painel oferece é feita, e
// depois PERGUNTA-SE À ÁRVORE o que ela mostra.
//
// A DIFERENÇA PARA test_kit_layers.cpp
// -----------------------------------------------------------------------------
// Aquele arquivo prova a aritmética das operações: quantos passos um arrasto
// custa, que dois arrastos são dois undos, que `visibleRows` pula um grupo
// fechado. Nada nele desenha a árvore. Um painel que faz a operação certa e
// desenha a árvore anterior passa nele inteiro -- e é um painel quebrado para
// quem está olhando.
//
// Aqui o ciclo é sempre o mesmo: operar, desenhar um frame, ler
// `LayersStats::drawn` (gravado em `drawRow`) e comparar com o que o documento
// passou a dizer. O que se afirma é a coisa que o usuário vê.
#include "check.h"
#include "Source/IconComposerFoundation/Edit.h"
#include "Source/IconComposerFoundation/Json.h"
#include "Source/IconComposerKit/Headless.h"
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/Session.h"
#include "imgui.h"

#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

void writeFile(const fs::path& p, const std::string& text) {
    std::FILE* f = std::fopen(p.string().c_str(), "wb");
    if (!f) return;
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}

// Dois grupos, três camadas: o menor documento em que mover, apagar e alternar
// têm respostas distinguíveis umas das outras.
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
    },
    {
      "name" : "Beta",
      "layers" : [
        {
          "name" : "b1",
          "image-name" : "art.svg"
        }
      ]
    }
  ]
})";

fs::path makeBundle(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("ic-e2e-layers-" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "Assets", ec);
    writeFile(dir / "icon.json", kTree);
    writeFile(dir / "Assets" / "art.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1024 1024\">"
              "<path d=\"M0 0 L1024 0 L1024 1024 L0 1024 Z\" fill=\"#3366cc\"/></svg>");
    return dir;
}

ick::LayersStats layersFrame(ick::HeadlessImGui& gui, ick::Session& s) {
    gui.newFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(320, 900));
    ick::LayersStats st = ick::drawLayers(s);
    gui.render();
    return st;
}

// A árvore como uma linha só, para uma comparação ler como o painel lê.
// "Alpha[a1 a2] Beta[b1]" -- grupos com os filhos entre colchetes.
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

std::string shapeOf(const icf::json::Value& root) {
    std::string out;
    const icf::json::Value* groups = root.find("groups");
    if (!groups || groups->kind() != icf::json::Value::Kind::Array) return out;
    for (const icf::json::Value& g : groups->elements()) {
        if (!out.empty()) out += " ";
        const icf::json::Value* gn = g.find("name");
        out += (gn && gn->kind() == icf::json::Value::Kind::String) ? gn->rawString() : "?";
        out += "[";
        const icf::json::Value* layers = g.find("layers");
        bool first = true;
        if (layers && layers->kind() == icf::json::Value::Kind::Array) {
            for (const icf::json::Value& l : layers->elements()) {
                if (!first) out += " ";
                first = false;
                const icf::json::Value* ln = l.find("name");
                out += (ln && ln->kind() == icf::json::Value::Kind::String) ? ln->rawString() : "?";
            }
        }
        out += "]";
    }
    return out;
}

const ick::RowInfo* row(const ick::LayersStats& st, const char* title) {
    for (const auto& r : st.drawn) {
        if (r.title == title) return &r;
    }
    return nullptr;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// O QUE A ÁRVORE MOSTRA É O QUE O DOCUMENTO DIZ -- depois de cada operação.
//
// O invariante é comparado contra o documento, não contra um literal escrito
// aqui: um literal provaria que a operação faz o que este arquivo espera, e o
// que precisa ser provado é que o PAINEL concorda com o DOCUMENTO.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(e2e_layers_tree_shows_what_the_document_says_after_every_operation) {
    const fs::path dir = makeBundle("shape");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 1000.0f);

    auto agree = [&](const char* step) {
        const ick::LayersStats st = layersFrame(gui, *s);
        const std::string painted = shape(st);
        const std::string declared = shapeOf(s->root());
        if (painted != declared) {
            std::printf("  divergencia apos %s: painel \"%s\" != documento \"%s\"\n", step,
                        painted.c_str(), declared.c_str());
        }
        CHECK_EQ(painted, declared);
        CHECK_EQ(gui.errors(), std::uint64_t{0});
        return st;
    };

    const ick::LayersStats first = agree("abrir");
    CHECK_EQ(shape(first), std::string("Alpha[a1 a2] Beta[b1]"));
    CHECK_EQ(first.groups, std::size_t{2});
    CHECK_EQ(first.layers, std::size_t{3});

    // Mover uma camada dentro do grupo.
    REQUIRE(s->moveNode(icf::NodePath{std::size_t{0}, std::size_t{0}}, +1));
    agree("mover a1 para baixo");
    CHECK_EQ(shape(layersFrame(gui, *s)), std::string("Alpha[a2 a1] Beta[b1]"));

    // Mover um grupo.
    REQUIRE(s->moveNode(icf::NodePath{std::size_t{0}, std::nullopt}, +1));
    agree("mover Alpha para baixo");
    CHECK_EQ(shape(layersFrame(gui, *s)), std::string("Beta[b1] Alpha[a2 a1]"));

    // Renomear.
    REQUIRE(s->rename(icf::NodePath{std::size_t{0}, std::nullopt}, "Gama"));
    agree("renomear Beta");

    // Acrescentar um grupo e uma camada.
    const auto g = s->addGroup("Novo");
    REQUIRE(g.has_value());
    agree("adicionar grupo");
    const auto l = s->addLayer(*g, "n1", "art.svg");
    REQUIRE(l.has_value());
    agree("adicionar camada");

    // Apagar.
    REQUIRE(s->removeNode(icf::NodePath{*g, *l}));
    agree("apagar a camada nova");
    REQUIRE(s->removeNode(icf::NodePath{*g, std::nullopt}));
    agree("apagar o grupo novo");

    // E TODO O CAMINHO DE VOLTA. Desfazer uma operação por vez, conferindo a
    // árvore a cada passo: um undo que restaura o documento e deixa o painel
    // mostrando o estado anterior é o defeito que este caso existe para pegar.
    int undos = 0;
    while (s->canUndo()) {
        REQUIRE(s->undo());
        ++undos;
        agree("desfazer");
    }
    CHECK(undos >= 7);
    CHECK_EQ(shape(layersFrame(gui, *s)), std::string("Alpha[a1 a2] Beta[b1]"));
}

// ─────────────────────────────────────────────────────────────────────────────
// OS DOIS INTERRUPTORES DA LINHA. Cada um escreve uma chave, e a linha volta a
// desenhar o estado novo -- que é a metade que nenhum teste cobria.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(e2e_layers_row_toggles_write_and_the_row_redraws) {
    const fs::path dir = makeBundle("toggles");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 1000.0f);

    const icf::NodePath a1{std::size_t{0}, std::size_t{0}};
    const icf::NodePath alpha{std::size_t{0}, std::nullopt};

    {
        const ick::LayersStats st = layersFrame(gui, *s);
        const ick::RowInfo* r = row(st, "a1");
        REQUIRE(r != nullptr);
        CHECK(r->visible);    // sem a chave `hidden`, a camada aparece
        CHECK(!r->glass);
        CHECK(r->layer);
        const ick::RowInfo* gr = row(st, "Alpha");
        REQUIRE(gr != nullptr);
        CHECK(!gr->layer);
        CHECK(!gr->glass);    // um grupo nao tem vidro, e a linha nunca o afirma
    }

    // Esconder: a mesma escrita que o interruptor faz (`hidden` = true sob Base).
    s->setProperty(a1, "hidden", icf::Context{}, icf::json::Value::boolean(true));
    {
        const ick::LayersStats st = layersFrame(gui, *s);
        const ick::RowInfo* r = row(st, "a1");
        REQUIRE(r != nullptr);
        CHECK(!r->visible);
    }

    // E o `false` explicito e visivel -- nao e o mesmo que a chave ausente nos
    // bytes, e TEM de ser o mesmo na tela.
    s->setProperty(a1, "hidden", icf::Context{}, icf::json::Value::boolean(false));
    {
        const ick::LayersStats st = layersFrame(gui, *s);
        REQUIRE(row(st, "a1") != nullptr);
        CHECK(row(st, "a1")->visible);
    }

    // Vidro.
    s->setProperty(a1, "glass", icf::Context{}, icf::json::Value::boolean(true));
    {
        const ick::LayersStats st = layersFrame(gui, *s);
        REQUIRE(row(st, "a1") != nullptr);
        CHECK(row(st, "a1")->glass);
        // E so nela: a vizinha nao herda o interruptor da irma.
        REQUIRE(row(st, "a2") != nullptr);
        CHECK(!row(st, "a2")->glass);
    }

    // Esconder o GRUPO esconde a linha do grupo, e as camadas dentro dele
    // continuam desenhando o estado DELAS -- a arvore nao propaga o
    // interruptor, e um painel que propagasse mentiria sobre o documento.
    s->setProperty(alpha, "hidden", icf::Context{}, icf::json::Value::boolean(true));
    {
        const ick::LayersStats st = layersFrame(gui, *s);
        REQUIRE(row(st, "Alpha") != nullptr);
        CHECK(!row(st, "Alpha")->visible);
        REQUIRE(row(st, "a1") != nullptr);
        CHECK(row(st, "a1")->visible);
    }

    // Desfazer tudo devolve a arvore ao inicio, interruptor por interruptor.
    while (s->canUndo()) REQUIRE(s->undo());
    const ick::LayersStats st = layersFrame(gui, *s);
    REQUIRE(row(st, "a1") != nullptr);
    CHECK(row(st, "a1")->visible);
    CHECK(!row(st, "a1")->glass);
    REQUIRE(row(st, "Alpha") != nullptr);
    CHECK(row(st, "Alpha")->visible);
}

// ─────────────────────────────────────────────────────────────────────────────
// A SELEÇÃO SOBREVIVE À ESTRUTURA. Mover o nó selecionado tem de deixar a
// seleção NO MESMO NÓ, não no mesmo índice -- índice é onde o nó estava.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(e2e_layers_selection_follows_the_node_not_the_index) {
    const fs::path dir = makeBundle("selecao");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 1000.0f);

    s->selection = icf::NodePath{std::size_t{0}, std::size_t{0}};   // a1
    {
        const ick::LayersStats st = layersFrame(gui, *s);
        REQUIRE(row(st, "a1") != nullptr);
        CHECK(row(st, "a1")->selected);
    }

    // O painel move e reaponta a selecao; aqui o mesmo par de chamadas.
    REQUIRE(s->moveNode(*s->selection, +1));
    s->selection = icf::NodePath{std::size_t{0}, std::size_t{1}};
    {
        const ick::LayersStats st = layersFrame(gui, *s);
        REQUIRE(row(st, "a1") != nullptr);
        CHECK(row(st, "a1")->selected);
        REQUIRE(row(st, "a2") != nullptr);
        CHECK(!row(st, "a2")->selected);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// CLIQUE DE VERDADE, SEM TOCAR NO SISTEMA
//
// Tudo acima chama a Session do jeito que o painel a chama. Isso prova a
// escrita e NÃO prova o controle: entre o pixel e a escrita existe o retângulo
// do item, a ordem em que ele é submetido e o `AllowOverlap` da linha inteira
// -- e já houve um defeito exatamente aí, um `Selectable` por cima de um
// `TreeNodeEx` que fazia o clique na parte vazia da linha não fazer nada.
//
// Aqui o clique é um clique: o painel grava onde desenhou cada item
// (`RowInfo::visibleAt` e companhia) e o caso injeta os eventos de mouse do
// ImGui naquele ponto. É a fila de eventos do contexto headless -- nenhum
// cursor do sistema se move, nenhuma janela rouba foco, e isto roda dentro da
// suíte como qualquer outro caso.
//
// Um clique são três quadros, e são três porque o ImGui é assim: no primeiro o
// ponteiro chega e o item vira o "hovered"; no segundo o botão desce; no
// terceiro ele sobe, e é no terceiro que um `Checkbox` dispara.
namespace {

ick::LayersStats clickAt(ick::HeadlessImGui& gui, ick::Session& s, ImVec2 at) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(at.x, at.y);
    layersFrame(gui, s);
    io.AddMouseButtonEvent(0, true);
    layersFrame(gui, s);
    io.AddMouseButtonEvent(0, false);
    layersFrame(gui, s);
    // E UM QUADRO A MAIS, que e o que a pessoa ve. No modo imediato a SELECAO
    // so aparece no quadro seguinte: `drawRow` le `s.selection` no topo e o
    // `Selectable` so a move depois que a linha ja foi desenhada. O quadro do
    // soltar mostra o estado de antes do clique; este mostra o de depois.
    // O ponteiro sai de cima junto, para o proximo clique nao comecar pousado
    // sobre o item anterior.
    io.AddMousePosEvent(-1.0f, -1.0f);
    return layersFrame(gui, s);
}

bool hiddenOf(const ick::Session& s, icf::NodePath p) {
    const icf::json::Value* node = icf::nodeAt(s.root(), p);
    if (!node) return false;
    const icf::json::Value* v = icf::resolve(*node, "hidden", icf::Context{});
    return v && v->kind() == icf::json::Value::Kind::Bool && v->boolean();
}

bool glassOf(const ick::Session& s, icf::NodePath p) {
    const icf::json::Value* node = icf::nodeAt(s.root(), p);
    if (!node) return false;
    const icf::json::Value* v = icf::resolve(*node, "glass", icf::Context{});
    return v && v->kind() == icf::json::Value::Kind::Bool && v->boolean();
}

}  // namespace

TEST_CASE(e2e_layers_a_real_click_on_the_visibility_box_hides_the_layer) {
    const fs::path dir = makeBundle("clique-visivel");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 1000.0f);

    const icf::NodePath a1{std::size_t{0}, std::size_t{0}};

    // Um quadro para o painel dizer onde as coisas ficaram.
    const ick::LayersStats laid = layersFrame(gui, *s);
    const ick::RowInfo* r = row(laid, "a1");
    REQUIRE(r != nullptr);
    REQUIRE(r->visibleAt.x > 0.0f);
    CHECK(!hiddenOf(*s, a1));

    // O CLIQUE. Nada mais muda entre as duas leituras.
    const ick::LayersStats after = clickAt(gui, *s, r->visibleAt);
    CHECK(hiddenOf(*s, a1));            // o documento
    const ick::RowInfo* r2 = row(after, "a1");
    REQUIRE(r2 != nullptr);
    CHECK(!r2->visible);                // e a linha, que voltou a desenhar
    CHECK_EQ(gui.errors(), std::uint64_t{0});

    // UM CLIQUE, UM UNDO. Um interruptor que empilhasse dois comandos pediria
    // dois Ctrl+Z para desfazer um clique.
    REQUIRE(s->canUndo());
    REQUIRE(s->undo());
    CHECK(!hiddenOf(*s, a1));

    // E de volta: clicar outra vez esconde de novo, entao o controle nao e de
    // mao unica.
    const ick::LayersStats again = clickAt(gui, *s, r->visibleAt);
    CHECK(hiddenOf(*s, a1));
    REQUIRE(row(again, "a1") != nullptr);
    CHECK(!row(again, "a1")->visible);
}

TEST_CASE(e2e_layers_a_real_click_on_the_glass_box_only_touches_that_layer) {
    const fs::path dir = makeBundle("clique-vidro");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 1000.0f);

    const icf::NodePath a1{std::size_t{0}, std::size_t{0}};
    const icf::NodePath a2{std::size_t{0}, std::size_t{1}};

    const ick::LayersStats laid = layersFrame(gui, *s);
    const ick::RowInfo* r = row(laid, "a1");
    REQUIRE(r != nullptr);
    REQUIRE(r->glassAt.x > 0.0f);

    const ick::LayersStats after = clickAt(gui, *s, r->glassAt);
    CHECK(glassOf(*s, a1));
    CHECK(!glassOf(*s, a2));        // a vizinha nao foi junto
    CHECK(!hiddenOf(*s, a1));       // nem o interruptor ao lado
    REQUIRE(row(after, "a1") != nullptr);
    CHECK(row(after, "a1")->glass);

    // O GRUPO NAO TEM ESSE INTERRUPTOR, e a linha dele diz isso com um ponto
    // que nao existe -- um teste que clicasse ali estaria clicando no vazio.
    const ick::RowInfo* g = row(after, "Alpha");
    REQUIRE(g != nullptr);
    CHECK_EQ(g->glassAt.x, 0.0f);
}

TEST_CASE(e2e_layers_a_real_click_on_the_row_selects_and_the_box_does_not) {
    const fs::path dir = makeBundle("clique-selecao");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 1000.0f);

    const icf::NodePath a2{std::size_t{0}, std::size_t{1}};
    s->selection = icf::NodePath{std::size_t{0}, std::size_t{0}};   // a1

    const ick::LayersStats laid = layersFrame(gui, *s);
    const ick::RowInfo* second = row(laid, "a2");
    REQUIRE(second != nullptr);

    // Clicar no CORPO da linha seleciona.
    const ick::LayersStats sel = clickAt(gui, *s, second->rowAt);
    REQUIRE(s->selection.has_value());
    CHECK(*s->selection == a2);
    REQUIRE(row(sel, "a2") != nullptr);
    CHECK(row(sel, "a2")->selected);

    // Clicar no INTERRUPTOR de outra linha alterna aquela linha e NAO move a
    // selecao -- e o `AllowOverlap` que faz os dois conviverem, e sem ele um
    // dos dois come o outro.
    const ick::RowInfo* first = row(sel, "a1");
    REQUIRE(first != nullptr);
    clickAt(gui, *s, first->visibleAt);
    CHECK(hiddenOf(*s, icf::NodePath{std::size_t{0}, std::size_t{0}}));
    REQUIRE(s->selection.has_value());
    CHECK(*s->selection == a2);   // continua onde estava
}

// ─────────────────────────────────────────────────────────────────────────────
// CLICAR NO ÍCONE ESCOLHE A CAMADA -- E A ÁRVORE ACENDE
//
// Os casos acima clicam na árvore. Este clica no CANVAS, que até 19/09 não
// selecionava nada, e cobra as duas pontas do caminho de volta:
//
//   1. o clique sobre a arte de uma camada escolhe AQUELA, e não a vizinha;
//   2. a linha correspondente da ÁRVORE acende no mesmo gesto.
//
// A (2) é o que não se pode supor. Os dois painéis compartilham
// `Session::selection` e portanto "deveria funcionar de graça" -- mas "de
// graça" é uma previsão, e um canvas que guardasse a escolha num estático seu,
// ou que a escrevesse depois de a árvore já ter desenhado, passaria na (1)
// inteira com a árvore apagada. Por isso o quadro deste caso desenha OS DOIS
// painéis, e o que se lê é `LayersStats::drawn`.
//
// O ALVO DO CLIQUE NÃO É CALCULADO AQUI. Ele é lido de `CanvasStats::selection`
// -- o retângulo que o painel diz ter desenhado -- com a camada selecionada
// pela árvore antes. Um alvo recalculado neste arquivo provaria que o clique
// concorda com a aritmética deste arquivo; lido do painel, o que se prova é
// que o que a pessoa VÊ é o que ela ACERTA.
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// Duas camadas pequenas em lados opostos, e NADA no meio: é o vazio do meio
// que faz "clicar no vazio limpa" ser uma afirmação sobre um pixel real.
const char* kPlaced = R"({
  "supported-platforms" : { "squares" : "shared" },
  "fill" : "automatic",
  "groups" : [
    { "name" : "Esq",
      "layers" : [
        { "name" : "esquerda", "image-name" : "art.svg",
          "position" : { "scale" : 0.25, "translation-in-points" : [ -256, 0 ] } } ] },
    { "name" : "Dir",
      "layers" : [
        { "name" : "direita", "image-name" : "art.svg",
          "position" : { "scale" : 0.25, "translation-in-points" : [ 256, 0 ] } } ] }
  ] })";

fs::path makePlacedBundle(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("ic-e2e-canvas-" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "Assets", ec);
    writeFile(dir / "icon.json", kPlaced);
    writeFile(dir / "Assets" / "art.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1024 1024\">"
              "<path d=\"M0 0 L1024 0 L1024 1024 L0 1024 Z\" fill=\"#3366cc\"/></svg>");
    return dir;
}

// Um render que já chegou. Nenhum dispositivo: `AddImage` só guarda o id numa
// draw command, e nada aqui a consome.
ick::RenderView landed() {
    ick::RenderView v;
    v.texture = static_cast<ImTextureID>(4242);
    v.width = 512;
    v.height = 512;
    v.gridSize = 512;
    v.drawn = 2;
    v.total = 2;
    return v;
}

struct BothStats {
    ick::CanvasStats canvas;
    ick::LayersStats layers;
};

// UM QUADRO COM OS DOIS PAINÉIS, lado a lado e sem se tocarem. A ordem é a da
// janela real: o canvas primeiro (é ele que carrega a barra de menu), a árvore
// depois -- e é justamente essa ordem que faz a pergunta valer, porque uma
// escrita tardia do canvas ficaria invisível na árvore do mesmo quadro.
BothStats bothFrame(ick::HeadlessImGui& gui, ick::Session& s, const ick::RenderView& view) {
    BothStats out;
    ick::MenuActions actions;
    gui.newFrame();
    ImGui::SetNextWindowPos(ImVec2(400, 0));
    ImGui::SetNextWindowSize(ImVec2(840, 700));
    out.canvas = ick::drawCanvas(s, view, actions);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(320, 900));
    out.layers = ick::drawLayers(s);
    gui.render();
    return out;
}

// O mesmo arnês de `clickAt`, sobre os dois painéis: chegar, apertar, soltar,
// e um quadro a mais -- o que a pessoa vê depois do clique.
BothStats clickCanvasAt(ick::HeadlessImGui& gui, ick::Session& s, const ick::RenderView& view,
                        ImVec2 at) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(at.x, at.y);
    bothFrame(gui, s, view);
    io.AddMouseButtonEvent(0, true);
    bothFrame(gui, s, view);
    io.AddMouseButtonEvent(0, false);
    bothFrame(gui, s, view);
    io.AddMousePosEvent(-1.0f, -1.0f);
    return bothFrame(gui, s, view);
}

ImVec2 centreOf(const ick::CanvasRect& r) {
    return ImVec2((r.x0 + r.x1) * 0.5f, (r.y0 + r.y1) * 0.5f);
}

// Onde o painel desenhou o retângulo de `p`: seleciona pela árvore, desenha,
// lê. Deixa a seleção onde estava antes de perguntar.
ick::CanvasRect rectOf(ick::HeadlessImGui& gui, ick::Session& s, const ick::RenderView& view,
                       icf::NodePath p) {
    const std::optional<icf::NodePath> was = s.selection;
    s.selection = p;
    const ick::CanvasStats st = bothFrame(gui, s, view).canvas;
    s.selection = was;
    return st.selectionDrawn ? st.selection : ick::CanvasRect{};
}

bool inside(const ick::CanvasRect& clip, ImVec2 p) {
    return p.x > clip.x0 && p.x < clip.x1 && p.y > clip.y0 && p.y < clip.y1;
}

}  // namespace

TEST_CASE(e2e_canvas_a_real_click_on_the_art_selects_that_layer_and_lights_the_tree) {
    const fs::path dir = makePlacedBundle("clique");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 1000.0f);
    const ick::RenderView view = landed();

    const icf::NodePath esquerda{std::size_t{0}, std::size_t{0}};
    const icf::NodePath direita{std::size_t{1}, std::size_t{0}};

    // Um quadro para o canvas assentar no Fit e dizer onde as coisas ficaram.
    const BothStats laid = bothFrame(gui, *s, view);
    REQUIRE(!laid.canvas.selectionDrawn);            // nada selecionado ainda
    const ick::CanvasRect clip = laid.canvas.clip;

    const ick::CanvasRect re = rectOf(gui, *s, view, esquerda);
    const ick::CanvasRect rd = rectOf(gui, *s, view, direita);
    REQUIRE(!re.empty());
    REQUIRE(!rd.empty());
    REQUIRE(re.x1 < rd.x0);                          // as duas artes não se tocam
    const ImVec2 ce = centreOf(re), cd = centreOf(rd);
    REQUIRE(inside(clip, ce));
    REQUIRE(inside(clip, cd));

    // Nada selecionado quando o clique começa.
    s->selection.reset();
    REQUIRE(bothFrame(gui, *s, view).layers.drawn.size() == 4);

    // ── O CLIQUE SOBRE A ARTE DA ESQUERDA ───────────────────────────────────
    const BothStats first = clickCanvasAt(gui, *s, view, ce);
    REQUIRE(s->selection.has_value());
    CHECK(*s->selection == esquerda);                 // o modelo
    CHECK(first.canvas.selectionDrawn);               // o canvas enquadrou
    REQUIRE(row(first.layers, "esquerda") != nullptr);
    CHECK(row(first.layers, "esquerda")->selected);   // E A ÁRVORE ACENDEU
    REQUIRE(row(first.layers, "direita") != nullptr);
    CHECK(!row(first.layers, "direita")->selected);
    CHECK_EQ(gui.errors(), std::uint64_t{0});

    // ── A OUTRA ARTE, para que o verde acima não seja "seleciona sempre a
    // primeira". A escolha muda, na árvore também.
    const BothStats second = clickCanvasAt(gui, *s, view, cd);
    REQUIRE(s->selection.has_value());
    CHECK(*s->selection == direita);
    REQUIRE(row(second.layers, "direita") != nullptr);
    CHECK(row(second.layers, "direita")->selected);
    CHECK(!row(second.layers, "esquerda")->selected);

    // ── O VAZIO LIMPA ───────────────────────────────────────────────────────
    // O meio da praça, entre as duas artes: pixel de canvas que camada nenhuma
    // cobre.
    const ImVec2 vazio((re.x1 + rd.x0) * 0.5f, ce.y);
    REQUIRE(inside(clip, vazio));
    const BothStats cleared = clickCanvasAt(gui, *s, view, vazio);
    CHECK(!s->selection.has_value());
    CHECK(!cleared.canvas.selectionDrawn);
    for (const ick::RowInfo& r : cleared.layers.drawn) CHECK(!r.selected);
    CHECK_EQ(gui.errors(), std::uint64_t{0});

    // ── E O ALVO ANDA COM O ZOOM ────────────────────────────────────────────
    // O retângulo é geometria de tela, então a 300% ele está noutro lugar --
    // e é no lugar NOVO que o clique tem de acertar. Um alvo que continuasse
    // valendo no lugar velho seria um overlay desenhado a partir de uma
    // segunda cópia da transformação.
    s->view.zoomRequest = 3.0f;
    for (int i = 0; i < 80; ++i) bothFrame(gui, *s, view);
    const ick::CanvasRect rd2 = rectOf(gui, *s, view, direita);
    REQUIRE(!rd2.empty());
    CHECK(rd2.width() > rd.width() * 2.0f);          // cresceu de verdade
    const ImVec2 cd2 = centreOf(rd2);
    REQUIRE(inside(clip, cd2));

    s->selection.reset();
    const BothStats zoomed = clickCanvasAt(gui, *s, view, cd2);
    REQUIRE(s->selection.has_value());
    CHECK(*s->selection == direita);
    REQUIRE(row(zoomed.layers, "direita") != nullptr);
    CHECK(row(zoomed.layers, "direita")->selected);

    // E o ponto VELHO, que agora não é a arte da direita, não a escolhe.
    s->selection.reset();
    clickCanvasAt(gui, *s, view, cd);
    CHECK(!(s->selection.has_value() && *s->selection == direita));
    CHECK_EQ(gui.errors(), std::uint64_t{0});
}

// ─────────────────────────────────────────────────────────────────────────────
// ARRASTAR NÃO É CLICAR
//
// O mesmo botão esquerdo faz o pan. Se qualquer soltar contasse como escolha,
// arrastar o ícone para o lado mudaria a seleção ao largar -- e a pessoa nunca
// pediu isso. O que separa os dois é "soltou onde apertou", e este caso arrasta
// de propósito por cima de uma arte e cobra que a seleção NÃO se mexeu.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(e2e_canvas_a_drag_pans_and_does_not_change_the_selection) {
    const fs::path dir = makePlacedBundle("arrasto");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 1000.0f);
    const ick::RenderView view = landed();

    const icf::NodePath esquerda{std::size_t{0}, std::size_t{0}};
    bothFrame(gui, *s, view);
    const ick::CanvasRect rd = rectOf(gui, *s, view, icf::NodePath{std::size_t{1}, std::size_t{0}});
    REQUIRE(!rd.empty());

    // A seleção começa na ESQUERDA, e o arrasto acaba sobre a arte da direita.
    s->selection = esquerda;
    const float panBefore = s->view.panTargetX;

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 from = centreOf(rd);
    io.AddMousePosEvent(from.x - 90.0f, from.y);
    bothFrame(gui, *s, view);
    io.AddMouseButtonEvent(0, true);
    bothFrame(gui, *s, view);
    for (int i = 1; i <= 9; ++i) {
        io.AddMousePosEvent(from.x - 90.0f + static_cast<float>(i) * 10.0f, from.y);
        bothFrame(gui, *s, view);
    }
    io.AddMouseButtonEvent(0, false);
    const BothStats after = bothFrame(gui, *s, view);

    CHECK(s->view.panTargetX > panBefore + 50.0f);    // o arrasto de fato panejou
    REQUIRE(s->selection.has_value());
    CHECK(*s->selection == esquerda);                 // e não escolheu nada
    REQUIRE(row(after.layers, "esquerda") != nullptr);
    CHECK(row(after.layers, "esquerda")->selected);
    CHECK_EQ(gui.errors(), std::uint64_t{0});
}
