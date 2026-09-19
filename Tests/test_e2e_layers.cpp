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
