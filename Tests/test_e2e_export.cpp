// E2E de `File > Export Icon as Image…` (T2): o item deixou de ser cinza, e o
// que está atrás dele é um modal com tamanho, contextos e destino.
//
// O QUE CADA CASO COBRA
// -----------------------------------------------------------------------------
//   (a) O item do menu ABRE o modal, clicado de verdade. Um caso que
//       escrevesse `s->exportSheet.open = true` provaria o modal e passaria
//       com o item ainda desabilitado -- que é exatamente o estado de onde
//       esta task partiu.
//
//   (b) O botão Export PEDE ao app, e o pedido carrega o plano CONSIGO: o
//       tamanho que está marcado e só os contextos que estão marcados.
//       `State::act()` roda depois do quadro, então um pedido que dissesse só
//       "exporte" leria as caixas no instante errado.
//
//   (c) A LISTA DE CONTEXTOS é a do NOSSO modelo e é só o que o documento
//       consegue distinguir. Não há `ClearLight`/`ClearDark`: o laudo
//       2026-09-19 §2 mediu que `Clear` não é uma aparência, e os 15 campos de
//       `ICRRenderingParameters.ClearMode` não foram lidos -- as duas saídas
//       seriam idênticas às Tinted, e oferecer uma opção que entrega a imagem
//       da vizinha é mentir na tela.
//
//   (d) E O QUE MAIS IMPORTA: os bytes. O PNG que a UI grava tem de ser, byte
//       a byte, o que o `icrender` produz para o mesmo documento, tamanho e
//       contexto. Se os dois caminhos divergirem é defeito, não tolerância.
#include "check.h"
#include "Source/IconComposerFoundation/Json.h"
#include "Source/IconComposerFoundation/Png.h"
#include "Source/IconComposerKit/Export.h"
#include "Source/IconComposerKit/Headless.h"
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/Session.h"
#include "Source/RenderBox/Device.h"
#include "Source/RenderBox/IconRenderer.h"
#include "Source/RenderBox/SvgRenderer.h"
#include "Source/cli/RenderBundle.h"
#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
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

// Um documento que declara UMA plataforma e não especializa nada: o caso em
// que os cinco idiomas dariam a mesma imagem.
const char* kPlain = R"({
  "supported-platforms" : { "squares" : "shared" },
  "fill" : "automatic",
  "groups" : [
    { "name" : "Alpha", "layers" : [ { "name" : "a1", "image-name" : "art.svg" } ] }
  ]
})";

// O mesmo, com uma especialização de aparência `light` e uma de idioma
// `watchOS`: agora o documento distingue as duas coisas, e a lista cresce.
const char* kSpecialized = R"({
  "supported-platforms" : { "squares" : "shared" },
  "fill" : "automatic",
  "groups" : [
    { "name" : "Alpha",
      "layers" : [
        { "name" : "a1",
          "image-name" : "art.svg",
          "fill-specializations" : [
            { "value" : "none" },
            { "appearance" : "light", "value" : { "solid" : "gray:1.00000,1.00000" } } ],
          "image-name-specializations" : [
            { "value" : "art.svg" },
            { "idiom" : "watchOS", "value" : "art.svg" } ] } ] }
  ]
})";

fs::path makeBundle(const std::string& name, const char* document = kPlain) {
    const fs::path dir = fs::temp_directory_path() / ("ic-e2e-export-" + name + ".icon");
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "Assets", ec);
    writeFile(dir / "icon.json", document);
    // COM UMA CURVA, e isso importa: o portao de bytes (caso (d)) afirma que a
    // UI e o `icrender` concordam inclusive em `subdivisions`, e `subdivisions`
    // so move um pixel onde ha cubica. Com quatro retas a arte desenhava igual
    // com 8 e com 16 -- medido -- e o portao passava por cima dessa metade.
    writeFile(dir / "Assets" / "art.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1024 1024\">"
              "<path d=\"M128 512 C128 200 896 200 896 512 C896 824 128 824 128 512 Z\" "
              "fill=\"#3366cc\"/></svg>");
    return dir;
}

// ─── um quadro: o canvas (que desenha a barra de menu) e o modal ────────────
//
// O modal é desenhado FORA da janela do canvas, no escopo de raiz, porque é
// ali que o app o desenha (Window.cpp, `CanvasPanel::Draw`) -- um popup nasce
// no stack de IDs de quem o abre, e um teste que o abrisse noutro lugar
// estaria exercendo um modal que não é o que roda.
struct Frame {
    ick::CanvasStats canvas;
    ick::ExportSheetStats sheet;
};

Frame frame(ick::HeadlessImGui& gui, ick::Session& s, ick::MenuActions& actions) {
    const ick::RenderView view;   // nenhum render aconteceu; o canvas não precisa de um
    gui.newFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(1200, 900));
    Frame f;
    f.canvas = ick::drawCanvas(s, view, actions, "");
    f.sheet = ick::drawExportSheet(s, actions);
    gui.render();
    return f;
}

// UM CLIQUE SÃO QUATRO QUADROS, pelas mesmas razões escritas em
// test_e2e_layers.cpp: o ponteiro chega e o item vira o hovered, o botão
// desce, o botão sobe -- e um quadro a mais, que é o que a pessoa vê.
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

const ick::MenuItemInfo* find(const std::vector<ick::MenuItemInfo>& v, const std::string& label) {
    for (const auto& i : v) {
        if (i.label == label) return &i;
    }
    return nullptr;
}

// A lista de contextos como uma linha só: "base/square dark/square".
std::string shape(const std::vector<icf::Context>& v) {
    std::string out;
    for (const icf::Context& c : v) {
        if (!out.empty()) out += " ";
        out += icf::appearanceToString(c.appearance);
        out += "/";
        out += icf::idiomToString(c.idiom);
    }
    return out;
}

// O dispositivo, uma vez por processo -- o mesmo padrão de
// test_icon_render.cpp.
rb::Device& gpu() {
    static rb::Device* d = [] {
        auto made = rb::Device::create();
        if (!made) {
            std::printf("  FAIL no Vulkan device: %s\n", made.error().c_str());
            ++ictest::failures();
            return static_cast<rb::Device*>(nullptr);
        }
        return new rb::Device(std::move(*made));
    }();
    static rb::Device dead;
    return d ? *d : dead;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// (a) O ITEM DO MENU ABRE O MODAL
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(e2e_export_menu_item_is_live_and_opens_the_sheet) {
    const fs::path dir = makeBundle("abre");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 1000.0f);
    ick::MenuActions actions;
    const auto draw = [&] { return frame(gui, *s, actions); };

    // NADA DESENHA O MODAL ANTES DE ALGUÉM O PEDIR.
    const Frame closed = draw();
    CHECK(!closed.sheet.open);

    const ick::MenuItemInfo* fileTitle = find(closed.canvas.menu.titles, "File");
    REQUIRE(fileTitle != nullptr);
    const Frame opened = clickAt(draw, fileTitle->at);

    const ick::MenuItemInfo* item = find(opened.canvas.menu.drawn, "Export Icon as Image…");
    REQUIRE(item != nullptr);
    // O ESTADO DE ONDE ESTA TASK PARTIU era este item cinza com "Round 5: not
    // built yet". Se ele voltar a ser cinza, esta linha é a que diz.
    CHECK(item->enabled);
    REQUIRE(item->at.x > 0.0f);

    const Frame sheet = clickAt(draw, item->at);
    CHECK(sheet.sheet.open);
    CHECK(s->exportSheet.open);
    // E ele já vem com alguma coisa marcada: um modal que abrisse com tudo
    // desmarcado abriria com o botão Export cinza.
    CHECK(sheet.sheet.options > 0);
    CHECK_EQ(sheet.sheet.chosen, sheet.sheet.options);
    CHECK_EQ(gui.errors(), std::uint64_t{0});
}

// ─────────────────────────────────────────────────────────────────────────────
// (b) O BOTÃO Export PEDE AO APP, COM O QUE ESTÁ MARCADO
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(e2e_export_button_asks_the_app_with_the_ticked_size_and_contexts) {
    const fs::path dir = makeBundle("pedido");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 1000.0f);
    ick::MenuActions actions;
    const auto draw = [&] { return frame(gui, *s, actions); };

    s->exportSheet.open = true;
    Frame f = draw();
    f = draw();   // o popup se posiciona no primeiro quadro; este já é estável
    REQUIRE(f.sheet.open);
    const std::vector<icf::Context> offered = ick::exportContexts(s->root());
    REQUIRE(offered.size() >= 2);
    CHECK_EQ(f.sheet.options, offered.size());

    // O TAMANHO, clicado: 1024, que não é o padrão.
    CHECK_EQ(f.sheet.size, std::uint32_t{512});
    const ick::MenuItemInfo* big = find(f.sheet.controls, "1024 px");
    REQUIRE(big != nullptr);
    REQUIRE(big->at.x > 0.0f);
    f = clickAt(draw, big->at);
    CHECK_EQ(f.sheet.size, std::uint32_t{1024});

    // UM CONTEXTO DESMARCADO, clicado. Sobram `offered.size() - 1`, e é essa
    // lista que tem de chegar ao app -- não a lista inteira.
    const std::string drop = ick::exportContextLabel(offered.front());
    const ick::MenuItemInfo* box = find(f.sheet.controls, drop);
    REQUIRE(box != nullptr);
    REQUIRE(box->at.x > 0.0f);
    f = clickAt(draw, box->at);
    CHECK_EQ(f.sheet.chosen, offered.size() - 1);
    // E o nome do arquivo que ele escreveria some da lista junto.
    CHECK_EQ(f.sheet.names.size(), offered.size() - 1);

    // DESENHAR NÃO PEDE NADA.
    CHECK(!actions.exportImage);

    REQUIRE(f.sheet.exportAt.x > 0.0f);
    f = clickAt(draw, f.sheet.exportAt);
    CHECK(actions.exportImage);
    CHECK_EQ(actions.exportPlan.size, std::uint32_t{1024});
    std::vector<icf::Context> want(offered.begin() + 1, offered.end());
    CHECK_EQ(shape(actions.exportPlan.contexts), shape(want));
    // O modal fica OCUPADO: a fila é do app e um segundo lote por cima do
    // primeiro escreveria dois PNGs no mesmo nome.
    CHECK(s->exportSheet.busy);
    const ick::MenuItemInfo* button = find(f.sheet.controls, "Export");
    REQUIRE(button != nullptr);
    CHECK(!button->enabled);
    CHECK_EQ(gui.errors(), std::uint64_t{0});

    // OS NOMES SÃO OS DO PLANO, um por contexto, sem repetir.
    std::vector<std::string> names;
    for (const icf::Context& c : actions.exportPlan.contexts) {
        names.push_back(ick::exportFileName(ick::exportStem(*s), c, 1024));
    }
    std::vector<std::string> unique = names;
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
    CHECK_EQ(unique.size(), names.size());
    // O `.icon` caiu do nome, e o resto do nome do bundle ficou.
    CHECK_EQ(ick::exportStem(*s), std::string("ic-e2e-export-pedido"));
    CHECK_EQ(names.front().compare(0, 21, "ic-e2e-export-pedido-"), 0);
    CHECK(names.front().find(".icon") == std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// SEM NENHUM CONTEXTO MARCADO, Export É CINZA -- e clicar nele não pede nada.
// Um botão que aceita o clique e não faz nada é a mentira que 18/09 mediu.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(e2e_export_is_greyed_with_nothing_ticked) {
    const fs::path dir = makeBundle("vazio");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 1000.0f);
    ick::MenuActions actions;
    const auto draw = [&] { return frame(gui, *s, actions); };

    s->exportSheet.open = true;
    draw();
    Frame f = draw();
    REQUIRE(f.sheet.open);
    REQUIRE(f.sheet.options > 0);

    // Desmarca tudo, uma caixa de cada vez, clicando.
    const std::vector<icf::Context> offered = ick::exportContexts(s->root());
    for (const icf::Context& c : offered) {
        const ick::MenuItemInfo* box = find(f.sheet.controls, ick::exportContextLabel(c));
        REQUIRE(box != nullptr);
        f = clickAt(draw, box->at);
    }
    CHECK_EQ(f.sheet.chosen, std::size_t{0});
    CHECK(f.sheet.names.empty());

    const ick::MenuItemInfo* button = find(f.sheet.controls, "Export");
    REQUIRE(button != nullptr);
    CHECK(!button->enabled);

    clickAt(draw, f.sheet.exportAt);
    CHECK(!actions.exportImage);
    CHECK(!s->exportSheet.busy);
    CHECK_EQ(gui.errors(), std::uint64_t{0});

    // E Close fecha: o modal não é uma armadilha.
    const Frame after = clickAt(draw, f.sheet.closeAt);
    CHECK(!after.sheet.open);
    CHECK(!s->exportSheet.open);
}

// ─────────────────────────────────────────────────────────────────────────────
// UMA EXPORTAÇÃO QUE CONTINUA DEPOIS DO `Close` TEM UM PIXEL NA TELA
// (revisão 19/09, I5).
//
// `Close` não cancela -- o rótulo não promete que cancela e a fila é do app --,
// mas o único lugar em que `sheet.status` era desenhado era DENTRO do modal.
// Fechar no meio de um lote de seis a 1024 px dava dez segundos de janela
// travando a cada dois quadros com nada na tela dizendo por quê: "Exporting 3
// of 6: …" e "Wrote 5 of 6 to …" iam para um campo que ninguém desenhava, e só
// as FALHAS apareciam (pela linha vermelha). É nominalmente a regressão de
// 15/09 que `RenderView::lastRenderSeconds` existe para não repetir.
//
// O clique no Close é um CLIQUE, pelo mesmo motivo dos outros casos daqui: um
// teste que escrevesse `s->exportSheet.open = false` passaria com o botão
// desligado, que é metade do que se está afirmando.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(e2e_export_progress_stays_on_the_canvas_bar_after_close) {
    const fs::path dir = makeBundle("fechado");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 1000.0f);
    ick::MenuActions actions;
    const auto draw = [&] { return frame(gui, *s, actions); };

    s->exportSheet.open = true;
    draw();
    Frame f = draw();
    REQUIRE(f.sheet.open);

    // O LOTE EM ANDAMENTO, como o app o deixa: `busy` e a frase do item atual
    // (Window.cpp, `State::drainExport`).
    const std::string doing = "Exporting 3 of 6: ic-e2e-export-fechado-dark-square-1024.png";
    s->exportSheet.busy = true;
    s->exportSheet.status = doing;
    f = draw();
    // Com o modal aberto a frase é dele, e a barra não a repete: o modal cobre
    // a janela inteira e dois textos iguais seriam dois.
    CHECK_EQ(f.sheet.status, doing);
    CHECK_EQ(f.canvas.exportStatus, std::string());

    // CLICAR Close.
    REQUIRE(f.sheet.closeAt.x > 0.0f);
    const Frame after = clickAt(draw, f.sheet.closeAt);
    CHECK(!after.sheet.open);
    CHECK(!s->exportSheet.open);
    // A FILA CONTINUA -- fechar não cancela --, e agora ela fala na barra do
    // canvas, que é onde `rendering…`, `stretched` e a linha vermelha moram.
    CHECK(s->exportSheet.busy);
    CHECK_EQ(after.canvas.exportStatus, doing);

    // E O RESUMO TAMBÉM: é a única frase que diz quantos PNGs foram parar na
    // pasta, e ela chega depois que o modal já fechou.
    s->exportSheet.busy = false;
    s->exportSheet.status = "Wrote 5 of 6 to C:/tmp/pasta";
    const Frame done = draw();
    CHECK_EQ(done.canvas.exportStatus, std::string("Wrote 5 of 6 to C:/tmp/pasta"));

    // ABRIR O MODAL OUTRA VEZ APAGA O RECIBO. Um recibo que ficasse ao lado do
    // progresso do lote seguinte seria duas exportações na mesma linha.
    const ick::MenuItemInfo* fileTitle = find(done.canvas.menu.titles, "File");
    REQUIRE(fileTitle != nullptr);
    const Frame opened = clickAt(draw, fileTitle->at);
    const ick::MenuItemInfo* item = find(opened.canvas.menu.drawn, "Export Icon as Image…");
    REQUIRE(item != nullptr);
    const Frame reopened = clickAt(draw, item->at);
    CHECK(reopened.sheet.open);
    CHECK_EQ(s->exportSheet.status, std::string());
    CHECK_EQ(gui.errors(), std::uint64_t{0});
}

// ─────────────────────────────────────────────────────────────────────────────
// ESCAPE FECHA O MODAL (revisão 19/09, M2).
//
// E a causa não era a que o laudo supôs. Ele atribuiu o Escape inerte ao
// `OpenPopup` por quadro de `Export.cpp` -- "qualquer fechamento que não passe
// pelo botão Close é desfeito no quadro seguinte". Medido no imgui desta
// árvore: `NavUpdateCancelRequest` (imgui.cpp:15039) fecha popup e menu e
// EXCLUI explicitamente `ImGuiWindowFlags_Modal`, e nem chegaria lá sem
// `ImGuiConfigFlags_NavEnableKeyboard`, que nem o headless nem o app ligam.
// ImGui nunca fechou este modal; não havia fechamento sendo desfeito. O
// sintoma era real, a causa era outra, e o conserto é tratar o Escape aqui.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(e2e_export_escape_closes_the_sheet) {
    const fs::path dir = makeBundle("escape");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 1000.0f);
    ick::MenuActions actions;
    const auto draw = [&] { return frame(gui, *s, actions); };

    s->exportSheet.open = true;
    draw();
    REQUIRE(draw().sheet.open);

    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiKey_Escape, true);
    draw();
    io.AddKeyEvent(ImGuiKey_Escape, false);
    const Frame after = draw();
    CHECK(!after.sheet.open);
    CHECK(!s->exportSheet.open);
    // E ELE NÃO RESSUSCITA. É esta linha que cobriria a causa que o laudo
    // supôs, se ela existisse.
    CHECK(!draw().sheet.open);
    CHECK(!draw().sheet.open);
    CHECK(!s->exportSheet.open);

    // E o Escape não pede exportação nenhuma: fechar não é exportar.
    CHECK(!actions.exportImage);
    CHECK_EQ(gui.errors(), std::uint64_t{0});
}

// ─────────────────────────────────────────────────────────────────────────────
// (c) A LISTA DE CONTEXTOS É A DO NOSSO MODELO, NARROWED PELO DOCUMENTO
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(export_offers_only_the_contexts_the_document_can_tell_apart) {
    const fs::path plainDir = makeBundle("simples", kPlain);
    auto plain = ick::Session::open(plainDir);
    REQUIRE(plain.has_value());

    // SEM ESPECIALIZAÇÃO NENHUMA: um idioma -- o declarado -- e três
    // aparências. `light` não entra porque `base` e `light` caem no MESMO
    // braço do conversor (`[BIN]` FillResolve.cpp, `0x10AEF4`), e uma caixa
    // que entregasse a imagem da vizinha seria uma mentira na tela.
    CHECK_EQ(shape(ick::exportContexts(plain->root())),
             std::string("base/square dark/square tinted/square"));

    // E NENHUMA DELAS SE CHAMA `clear`. O alvo tem seis renditions, duas
    // delas Clear -- mas `Clear` não é uma aparência (laudo 2026-09-19 §2): é
    // o terceiro valor de `RenderingMode.Contents`, e os 15 campos de
    // `ICRRenderingParameters.ClearMode` não foram lidos. Uma entrada `clear`
    // aqui sairia idêntica à Tinted.
    for (const icf::Context& c : ick::exportContexts(plain->root())) {
        CHECK(icf::appearanceToString(c.appearance) != std::string("clear"));
    }

    const fs::path specDir = makeBundle("especializado", kSpecialized);
    auto spec = ick::Session::open(specDir);
    REQUIRE(spec.has_value());

    // COM UMA ENTRADA `appearance: light` E UMA `idiom: watchOS`, a lista
    // cresce nos dois eixos -- e sem ninguém reescrever o modal, que é o que
    // "deixe a lista numa forma que aceite mais entradas" quer dizer.
    CHECK_EQ(shape(ick::exportContexts(spec->root())),
             std::string("base/square light/square dark/square tinted/square "
                         "base/watchOS light/watchOS dark/watchOS tinted/watchOS"));

    // O NOME DO ARQUIVO usa a grafia do PRÓPRIO DOCUMENTO, que é a que
    // `icf::appearanceToString`/`idiomToString` escrevem em disco.
    CHECK_EQ(ick::exportFileName("AppIcon-27", icf::Context{icf::Appearance::Dark, icf::Idiom::MacOS},
                                 1024),
             std::string("AppIcon-27-dark-macOS-1024.png"));
    CHECK_EQ(ick::exportStem(*plain), std::string("ic-e2e-export-simples"));
}

// ─────────────────────────────────────────────────────────────────────────────
// (d) OS BYTES SÃO OS DO `icrender`
//
// Byte a byte, não "parecido". As duas sequências estão lado a lado aqui de
// propósito: a da UI é `ick::renderExportFile`, a do `icrender` é o que
// `Source/cli/render_main.cpp` faz no caminho de bundle, transcrito. Se
// alguém puser um `IconViewport`, um `subdivisions` diferente, ou fizer os
// pixels passarem por 8 bits e voltarem, esta linha é a que cai.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(e2e_export_bytes_are_exactly_what_icrender_writes) {
    const fs::path dir = makeBundle("bytes");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    rb::Device& device = gpu();

    const icf::Context ctx{icf::Appearance::Dark, icf::Idiom::Square};
    constexpr std::uint32_t kSize = 128;   // pequeno de propósito: isto é um gate, não um retrato

    // O CAMINHO DA UI.
    const ick::ExportFile made =
        ick::renderExportFile(device, s->bundle(), ick::exportStem(*s), kSize, ctx);
    CHECK_EQ(made.error, std::string());
    REQUIRE(!made.png.empty());
    CHECK_EQ(made.name, std::string("ic-e2e-export-bytes-dark-square-128.png"));
    CHECK(made.drawn > 0);

    // O CAMINHO DO `icrender`, que é o `icrender`. `iccli::renderBundleIcon`
    // (Source/cli/RenderBundle.h) é a função que o binário chama -- as opções
    // e o render saíram do `main` para lá exatamente para que esta linha
    // deixasse de ser uma TRANSCRIÇÃO. Enquanto era uma, um campo novo em
    // `render_main.cpp` mudava o `icrender`, não mudava a UI, e este caso
    // continuava verde comparando a UI com a cópia que ele tinha do que o
    // `icrender` fazia em 19/09. `subdivisions` é o padrão do `--subdivisions`.
    // O `subdivisions` que o `icrender` passa sem `--subdivisions` é o padrão
    // de `rb::RenderOptions`, que é de onde o `main` o tira.
    auto icon = iccli::renderBundleIcon(device, s->bundle(), kSize,
                                        rb::RenderOptions{}.subdivisions, ctx);
    REQUIRE(icon.has_value());
    const std::vector<std::uint8_t> reference =
        icf::encodePng(icon->rgba, icon->width, icon->height);
    REQUIRE(!reference.empty());

    CHECK_EQ(made.png.size(), reference.size());
    CHECK(made.png == reference);

    // E O QUE O `icrender` REALMENTE CHAMA é `icf::writePng`, que codifica e
    // grava. Os bytes no disco são os mesmos que a UI entregaria ao app.
    const fs::path file = dir / "referencia.png";
    CHECK_EQ(icf::writePng(file.string(), icon->rgba, icon->width, icon->height), std::string());
    std::FILE* f = std::fopen(file.string().c_str(), "rb");
    REQUIRE(f != nullptr);
    std::vector<std::uint8_t> onDisk;
    std::uint8_t buf[4096];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) onDisk.insert(onDisk.end(), buf, buf + n);
    std::fclose(f);
    CHECK_EQ(onDisk.size(), made.png.size());
    CHECK(onDisk == made.png);

    // O TAMANHO PEDIDO É O TAMANHO GRAVADO. Esta linha não depende da
    // transcrição acima: um ladrilho que vazasse para a exportação sairia como
    // um PNG menor que o pedido, e nenhuma comparação com o `icrender` o
    // pegaria se o `icrender` ganhasse o mesmo defeito.
    const icf::DecodedPng back = icf::decodePng(made.png.data(), made.png.size());
    CHECK_EQ(back.error, std::string());
    CHECK_EQ(back.width, kSize);
    CHECK_EQ(back.height, kSize);
}
