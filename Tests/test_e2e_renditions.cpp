// E2E da barra de renditions (T4): a forma medida é desenhada, e clicar nela
// move o canvas.
//
// O QUE ESTE ARQUIVO AFIRMA, E POR QUE CADA UMA
// -----------------------------------------------------------------------------
// A barra é a coisa mais reconhecível do alvo, e é também a mais fácil de
// desenhar ERRADO de um jeito que passa despercebido: a ordem dos três grupos
// não é a ordem do enum (Clear vem antes de Tinted), watchOS não tem seis
// renditions, e metade dos itens não pode ser desenhada por este motor. Cada um
// desses quatro fatos é `[BIN]` em
// `Docs/Laudos/2026-09-19-renditions-e-mirroring.md` §2, e cada um tem um caso
// aqui.
//
// E o clique é um CLIQUE, pelo mesmo motivo de `test_e2e_layers.cpp`: entre o
// pixel e a troca de contexto existem o retângulo do botão, o `BeginDisabled`
// em volta dele e a ordem em que os dois são submetidos. Um caso que
// escrevesse `s.view.context.appearance` na mão provaria a Session e passaria
// com o botão desligado.
#include "check.h"
#include "Source/IconComposerFoundation/IconDocument.h"
#include "Source/IconComposerKit/Headless.h"
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/Renditions.h"
#include "Source/IconComposerKit/Session.h"
#include "Source/IconComposerKit/ViewModel.h"
#include "imgui.h"

#include <cstdio>
#include <filesystem>
#include <new>
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

const char* kDoc = R"({
  "fill" : "automatic",
  "supported-platforms" : { "squares" : "shared" },
  "groups" : [
    { "name" : "Alpha", "layers" : [ { "name" : "a1", "image-name" : "art.svg" } ] }
  ]
})";

fs::path makeBundle(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("ic-e2e-renditions-" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "Assets", ec);
    writeFile(dir / "icon.json", kDoc);
    writeFile(dir / "Assets" / "art.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1024 1024\">"
              "<path d=\"M0 0 L1024 0 L1024 1024 L0 1024 Z\" fill=\"#3366cc\"/></svg>");
    return dir;
}

ick::RenditionStats barFrame(ick::HeadlessImGui& gui, ick::Session& s,
                             ick::RenditionThumbnails* thumbs = nullptr) {
    gui.newFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(1280, 220));
    ick::RenditionStats st = ick::drawRenditions(s, thumbs);
    gui.render();
    return st;
}

// Um clique de verdade, na fila de eventos do contexto headless. Quatro
// quadros pelo mesmo motivo de `test_e2e_layers.cpp`: o ponteiro chega, o botão
// desce, o botão sobe (e é aí que um `Button` dispara), e mais um para a barra
// desenhar o estado de DEPOIS.
ick::RenditionStats clickAt(ick::HeadlessImGui& gui, ick::Session& s, ImVec2 at) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(at.x, at.y);
    barFrame(gui, s);
    io.AddMouseButtonEvent(0, true);
    barFrame(gui, s);
    io.AddMouseButtonEvent(0, false);
    barFrame(gui, s);
    io.AddMousePosEvent(-1.0f, -1.0f);
    return barFrame(gui, s);
}

// Os rótulos desenhados, em ordem, como uma linha só.
std::string labels(const ick::RenditionStats& st) {
    std::string out;
    for (const ick::RenditionInfo& i : st.drawn) {
        if (!out.empty()) out += " | ";
        out += i.label;
    }
    return out;
}

std::string enabledLabels(const ick::RenditionStats& st) {
    std::string out;
    for (const ick::RenditionInfo& i : st.drawn) {
        if (!i.enabled) continue;
        if (!out.empty()) out += " | ";
        out += i.label;
    }
    return out;
}

std::string selectedLabel(const ick::RenditionStats& st) {
    std::string out;
    for (const ick::RenditionInfo& i : st.drawn) {
        if (!i.selected) continue;
        if (!out.empty()) out += "+";   // mais de uma marcada é defeito, e aparece
        out += i.label;
    }
    return out;
}

const ick::RenditionInfo* item(const ick::RenditionStats& st, const char* label) {
    for (const ick::RenditionInfo& i : st.drawn) {
        if (i.label == label) return &i;
    }
    return nullptr;
}

std::string groupShape(const ick::RenditionStats& st) {
    std::string out;
    for (std::size_t n : st.groupSizes) out += std::to_string(n);
    return out;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// `[BIN]` §2.7 -- `Rendition.displayGrouped` (`0x4202C`) monta [0,1] [4,5] [2,3].
// CLEAR VEM ANTES DE TINTED, que não é a ordem do enum, e é a ordem da tela.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(e2e_renditions_three_groups_in_the_measured_order) {
    const fs::path dir = makeBundle("ordem");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 900.0f);

    const ick::RenditionStats st = barFrame(gui, *s);
    CHECK_EQ(st.groups, std::size_t{3});
    CHECK_EQ(groupShape(st), std::string("222"));
    CHECK_EQ(labels(st),
             std::string("Default | Dark | Clear Light | Clear Dark | Tinted Light | Tinted Dark"));
    CHECK_EQ(st.drawn.size(), std::size_t{6});
    CHECK_EQ(gui.errors(), std::uint64_t{0});

    // Todo item desenhado tem onde ser clicado -- os dois alvos, o título e a
    // miniatura. Um `{0,0}` aqui seria um botão que existe na contagem e não
    // na tela.
    for (const ick::RenditionInfo& i : st.drawn) {
        CHECK(i.at.x > 0.0f && i.at.y > 0.0f);
        CHECK(i.imageAt.x > 0.0f && i.imageAt.y > 0.0f);
    }
    // E a barra DIZ o que está acontecendo, sempre. Vazia é a regressão de
    // 15/09 -- um painel que renderiza e não fala.
    CHECK(!st.note.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// O PAR CLEAR É CINZA, E O MOTIVO ESTÁ NA TELA.
//
// `[BIN]` §2.5: `Clear` é o terceiro valor de `RenderingMode.Contents`, não uma
// aparência; §6: os 15 campos de `ICRRenderingParameters.ClearMode` não foram
// lidos, e o laudo diz que isso "não autoriza implementar o modo Clear".
//
// E `Tinted Dark` é cinza pela SEGUNDA medição: o eixo claro/escuro que separa
// as duas tingidas é `ICRIconStyle.appearance`, que é do RENDER (§2.5) e não
// existe neste motor -- as duas leem a mesma fatia `tinted` (§2.6). Oferecer a
// segunda seria oferecer a imagem da primeira.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(e2e_renditions_all_six_are_drawable_and_the_mono_four_differ_in_the_render) {
    // DESDE 30/09 AS SEIS SAO DESENHAVEIS. Este caso afirmava o contrario --
    // o Clear cinza porque o `ClearMode` nao tinha sido lido, e o Tinted Dark
    // cinza por ler a mesma fatia do Tinted Light --, e as duas coisas
    // deixaram de ser verdade com a frente do Tauri: o Clear inteiro e o vidro
    // simulado estao na RenderBox (`rb::prepareMono`/`finishMono`), e as quatro
    // do Mono se separam no render pelo `MonoLook`.
    const fs::path dir = makeBundle("clear");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 900.0f);

    const ick::RenditionStats st = barFrame(gui, *s);
    CHECK_EQ(st.drawn.size(), std::size_t{6});
    CHECK_EQ(enabledLabels(st),
             std::string("Default | Dark | Clear Light | Clear Dark | Tinted Light | Tinted Dark"));
    CHECK_EQ(st.disabled, std::size_t{0});
    for (rb::Rendition r : ick::kAllRenditions)
        CHECK_EQ(ick::renditionUnsupportedReason(r), std::string());

    // As quatro do Mono leem a MESMA fatia e tem looks DIFERENTES: e isso que
    // impede duas miniaturas (ou dois renders do canvas) de responderem uma
    // pela outra.
    const icf::Idiom idiom = s->view.context.idiom;
    const ick::RenderLook cl = ick::lookOf(*s, rb::Rendition::LightClear, idiom, false, 128);
    const ick::RenderLook cd = ick::lookOf(*s, rb::Rendition::DarkClear, idiom, false, 128);
    const ick::RenderLook tl = ick::lookOf(*s, rb::Rendition::LightTint, idiom, false, 128);
    const ick::RenderLook td = ick::lookOf(*s, rb::Rendition::DarkTint, idiom, false, 128);
    CHECK(cl.context == cd.context);
    CHECK(cl.context == td.context);
    CHECK(cl.mono.has_value() && cd.mono.has_value() && tl.mono.has_value() && td.mono.has_value());
    CHECK(!(cl == cd));
    CHECK(!(cl == tl));
    CHECK(!(tl == td));
    // Default e Dark nao tem Mono.
    CHECK(!ick::lookOf(*s, rb::Rendition::LightColor, idiom, false, 128).mono.has_value());
    CHECK(!ick::lookOf(*s, rb::Rendition::DarkColor, idiom, false, 128).mono.has_value());
    CHECK_EQ(gui.errors(), std::uint64_t{0});
}

// ─────────────────────────────────────────────────────────────────────────────
// `[BIN]` §2.7 -- `Platform.validRenditions` (`0x3A248`): `Default` vale para
// toda plataforma, as outras cinco só para plataforma <= 1. **watchOS oferece
// só `Default`**, e a grade não é 3x6.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(e2e_renditions_watchos_offers_only_default) {
    const fs::path dir = makeBundle("watchos");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 900.0f);

    // Os outros quatro idiomas mostram os seis...
    for (icf::Idiom i : {icf::Idiom::Base, icf::Idiom::Square, icf::Idiom::IOS, icf::Idiom::MacOS}) {
        s->view.context.idiom = i;
        const ick::RenditionStats st = barFrame(gui, *s);
        CHECK_EQ(st.drawn.size(), std::size_t{6});
        CHECK_EQ(st.groups, std::size_t{3});
    }
    // ...e watchOS mostra um.
    s->view.context.idiom = icf::Idiom::WatchOS;
    const ick::RenditionStats st = barFrame(gui, *s);
    CHECK_EQ(st.drawn.size(), std::size_t{1});
    CHECK_EQ(st.groups, std::size_t{1});
    CHECK_EQ(groupShape(st), std::string("1"));
    CHECK_EQ(labels(st), std::string("Default"));
    // Nada de grupo vazio: dois separadores pendurados no nada seriam dois
    // grupos que a forma medida não tem nessa plataforma.
    CHECK_EQ(st.disabled, std::size_t{0});
    CHECK_EQ(gui.errors(), std::uint64_t{0});
}

// ─────────────────────────────────────────────────────────────────────────────
// O CLIQUE LEVA O CANVAS PARA AQUELE CONTEXTO -- é o `RenditionTitleButton` do
// alvo. E o clique é um clique: o ponto vem de onde o painel DESENHOU o botão.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(e2e_renditions_a_real_click_moves_the_canvas_context) {
    const fs::path dir = makeBundle("clique");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 900.0f);

    // Um documento recém-aberto está na fatia `base` (Session.cpp só ajusta o
    // idioma), e `[BIN]` `Appearance.defaultRendition` diz `base -> Default`.
    CHECK(s->view.context.appearance == icf::Appearance::Base);
    const ick::RenditionStats laid = barFrame(gui, *s);
    CHECK_EQ(selectedLabel(laid), std::string("Default"));

    // CLICAR NO TÍTULO "Dark".
    REQUIRE(item(laid, "Dark") != nullptr);
    const ick::RenditionStats afterDark = clickAt(gui, *s, item(laid, "Dark")->at);
    CHECK(s->view.context.appearance == icf::Appearance::Dark);
    CHECK_EQ(selectedLabel(afterDark), std::string("Dark"));

    // CLICAR NA MINIATURA de "Tinted Light" -- o outro alvo do mesmo item.
    REQUIRE(item(afterDark, "Tinted Light") != nullptr);
    const ick::RenditionStats afterTint =
        clickAt(gui, *s, item(afterDark, "Tinted Light")->imageAt);
    CHECK(s->view.context.appearance == icf::Appearance::Tinted);
    CHECK_EQ(selectedLabel(afterTint), std::string("Tinted Light"));

    // CLICAR NO CLEAR DARK O ESCOLHE (30/09): a fatia continua `tinted`, e a
    // rendicao Mono que o canvas mostra passa a ser ela.
    REQUIRE(item(afterTint, "Clear Dark") != nullptr);
    const ick::RenditionStats afterClear = clickAt(gui, *s, item(afterTint, "Clear Dark")->at);
    CHECK(s->view.context.appearance == icf::Appearance::Tinted);
    CHECK(s->view.mono == rb::Rendition::DarkClear);
    CHECK_EQ(selectedLabel(afterClear), std::string("Clear Dark"));

    // E de volta ao começo, para o controle não ser de mão única.
    REQUIRE(item(afterClear, "Default") != nullptr);
    clickAt(gui, *s, item(afterClear, "Default")->at);
    CHECK(s->view.context.appearance == icf::Appearance::Light);
    CHECK_EQ(gui.errors(), std::uint64_t{0});
}

// ─────────────────────────────────────────────────────────────────────────────
// A BARRA E OS COMBOS CONCORDAM, NOS DOIS SENTIDOS.
//
// Dois controles que discordam do mesmo estado é defeito. A defesa aqui é
// estrutural e o caso a exercita: o clique na barra escreve
// `s.view.context.appearance`, que é EXATAMENTE a variável que o combo de
// aparência do canvas lê e escreve (PanelCanvas.cpp `contextBar`) e que o
// `View > Appearance` do menu também move. Não existe um segundo estado que
// possa divergir -- e é por isso que a barra não guarda "qual rendition".
//
// O caso roda a barra e o CANVAS no mesmo quadro, e afirma as duas direções.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(e2e_renditions_the_bar_and_the_appearance_combo_agree) {
    const fs::path dir = makeBundle("concordancia");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 1000.0f);

    ick::RenderView view;
    ick::MenuActions actions;
    // Um quadro com as DUAS superfícies: a barra e o canvas (que é quem
    // desenha o combo). O que se lê depois é o que as duas mostraram.
    auto bothFrame = [&]() {
        gui.newFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(1280, 220));
        ick::RenditionStats bar = ick::drawRenditions(*s, nullptr);
        ImGui::SetNextWindowPos(ImVec2(0, 230));
        ImGui::SetNextWindowSize(ImVec2(1280, 700));
        ick::CanvasStats canvas = ick::drawCanvas(*s, view, actions);
        gui.render();
        CHECK_EQ(canvas.contextControls, std::size_t{4});
        return bar;
    };

    // SENTIDO 1: o combo move, a barra acompanha. As quatro entradas do combo
    // de aparência, e a rendition que `[BIN]` `Appearance.defaultRendition`
    // (`0x20D98`, tabela `00 00 01 04`) manda marcar. Desde 30/09 sem
    // substituicao: `tinted` mede `Clear Light`, e o Clear Light e desenhavel.
    struct Expect { icf::Appearance appearance; const char* rendition; };
    const Expect kTable[] = {
        {icf::Appearance::Base, "Default"},
        {icf::Appearance::Light, "Default"},
        {icf::Appearance::Dark, "Dark"},
        {icf::Appearance::Tinted, "Clear Light"},
    };
    for (const Expect& e : kTable) {
        s->view.context.appearance = e.appearance;   // o que o `Selectable` do combo faz
        const ick::RenditionStats bar = bothFrame();
        CHECK_EQ(selectedLabel(bar), std::string(e.rendition));
        // Exatamente UMA marcada: nenhuma deixaria a pessoa sem saber o que
        // está na tela grande, duas seriam duas respostas para uma pergunta.
        std::size_t marked = 0;
        for (const ick::RenditionInfo& i : bar.drawn) marked += i.selected ? 1u : 0u;
        CHECK_EQ(marked, std::size_t{1});
    }

    // SENTIDO 2: a barra move, o combo acompanha. O rótulo conferido é o
    // MESMO que `contextBar` imprime no botão do combo (`appearanceLabel` do
    // `s.view.context.appearance`).
    struct Back { const char* rendition; const char* combo; };
    const Back kBack[] = {
        {"Dark", "Dark"},
        {"Tinted Light", "Tinted"},
        {"Default", "Light"},
    };
    for (const Back& b : kBack) {
        const ick::RenditionStats laid = bothFrame();
        const ick::RenditionInfo* it = item(laid, b.rendition);
        REQUIRE(it != nullptr);
        clickAt(gui, *s, it->at);
        const ick::RenditionStats after = bothFrame();
        CHECK_EQ(std::string(ick::appearanceLabel(s->view.context.appearance)), std::string(b.combo));
        CHECK_EQ(selectedLabel(after), std::string(b.rendition));
    }
    CHECK_EQ(gui.errors(), std::uint64_t{0});
}

// ─────────────────────────────────────────────────────────────────────────────
// O QUE IMPEDE A JANELA DE CONGELAR: UM RENDER DE CADA VEZ, E O CANVAS NA
// FRENTE.
//
// Um documento pesado custa ~0,5 s a 512 px. Quatro miniaturas pedidas de uma
// vez seriam dois segundos de janela parada -- e o agendador de verdade
// (`JobScheduler`) descartaria três delas de qualquer jeito, porque o canal é
// de um pedido pendente. Estes dois casos são a disciplina, medida com um
// agendador falso: nenhum GPU, nenhum dispositivo.
// ─────────────────────────────────────────────────────────────────────────────
namespace {

struct FakeScheduler : ick::RenderScheduler {
    struct Ask {
        std::uint64_t version = 0;
        icf::Context context;
        std::uint32_t size = 0;
        // O Mono tambem e chave (30/09): o eco o devolve, senao a resposta de
        // uma das quatro nunca casa com o pedido dela.
        std::optional<rb::MonoLook> mono;
    };
    std::vector<Ask> asks;
    std::vector<ick::RenderResult> queued;
    void request(ick::RenderRequest r) override {
        asks.push_back(Ask{r.version, r.context, r.size, r.mono});
    }
    std::optional<ick::RenderResult> poll() override {
        if (queued.empty()) return std::nullopt;
        ick::RenderResult r = std::move(queued.front());
        queued.erase(queued.begin());
        return r;
    }
    // A resposta de um pedido, com a chave ecoada como o contrato manda.
    // Sem índice, o último.
    void answer(std::size_t which = static_cast<std::size_t>(-1), std::uint32_t side = 128) {
        REQUIRE(!asks.empty());
        const Ask a = asks[which == static_cast<std::size_t>(-1) ? asks.size() - 1 : which];
        ick::RenderResult r;
        r.version = a.version;
        r.context = a.context;
        r.mono = a.mono;
        r.size = a.size;
        r.gridSize = a.size;
        r.width = side;
        r.height = side;
        r.rgba8.assign(static_cast<std::size_t>(side) * side * 4u, 0xFFu);
        queued.push_back(std::move(r));
    }
};

struct FakeSink : ick::TextureSink {
    int live = 0;
    std::uint64_t next = 1000;
    ImTextureID create(std::uint32_t, std::uint32_t, const std::uint8_t*) override {
        ++live;
        return static_cast<ImTextureID>(++next);
    }
    bool update(ImTextureID, std::uint32_t, std::uint32_t, const std::uint8_t*) override {
        return true;
    }
    void remove(ImTextureID) override { --live; }
};

}  // namespace

TEST_CASE(e2e_renditions_thumbnails_are_rendered_one_at_a_time) {
    const fs::path dir = makeBundle("miniaturas");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 900.0f);
    FakeScheduler sched;
    FakeSink sink;
    {
        ick::RenditionThumbnails thumbs(sched, sink, 128);

        // Seis renditions desenháveis -> seis looks (as quatro do Mono dividem
        // a fatia e nao o look), e o primeiro pedido é o da SELECIONADA: quem
        // acabou de escolher vê a dela primeiro.
        const ick::RenditionStats first = barFrame(gui, *s, &thumbs);
        CHECK_EQ(sched.asks.size(), std::size_t{1});
        CHECK(sched.asks[0].context.appearance == icf::Appearance::Light);   // Default, o padrão de `base`
        CHECK_EQ(sched.asks[0].size, std::uint32_t{128});
        CHECK_EQ(thumbs.stale(), std::size_t{6});
        CHECK(!first.note.empty());
        CHECK(first.note.find("rendering") != std::string::npos);
        // E O ITEM DIZ QUE É ELE. `RenditionInfo::pending` era escrito e não
        // era lido por ninguém (revisão 19/09, M3): o cabeçalho o descreve como
        // parte do relato e nada o cobrava, então ele podia estar errado desde
        // sempre. Exatamente um item em voo, porque é um render de cada vez, e
        // é o da SELECIONADA, que é a primeira pedida.
        std::size_t inFlight = 0;
        for (const ick::RenditionInfo& i : first.drawn) inFlight += i.pending ? 1u : 0u;
        CHECK_EQ(inFlight, std::size_t{1});
        for (const ick::RenditionInfo& i : first.drawn) {
            if (i.pending) CHECK(i.selected);
        }

        // MAIS UM QUADRO NÃO É MAIS UM PEDIDO. Um painel que pedisse por quadro
        // afogaria o canvas sozinho.
        barFrame(gui, *s, &thumbs);
        barFrame(gui, *s, &thumbs);
        CHECK_EQ(sched.asks.size(), std::size_t{1});

        // Respondido, o próximo sai -- e um só.
        sched.answer();
        barFrame(gui, *s, &thumbs);
        CHECK_EQ(sched.asks.size(), std::size_t{2});
        CHECK_EQ(thumbs.stale(), std::size_t{5});
        CHECK_EQ(sink.live, 1);

        // As outras quatro, uma por resposta.
        for (std::size_t n = 3; n <= 6; ++n) {
            sched.answer();
            barFrame(gui, *s, &thumbs);
            CHECK_EQ(sched.asks.size(), n);
        }
        sched.answer();
        const ick::RenditionStats done = barFrame(gui, *s, &thumbs);
        CHECK_EQ(thumbs.stale(), std::size_t{0});
        // Nada em voo: `pending` desce, senão ele seria um campo que só sabe
        // dizer "sim".
        for (const ick::RenditionInfo& i : done.drawn) CHECK(!i.pending);
        CHECK_EQ(sink.live, 6);
        // Nada mais é pedido: as seis respondem a versão atual do documento.
        barFrame(gui, *s, &thumbs);
        barFrame(gui, *s, &thumbs);
        CHECK_EQ(sched.asks.size(), std::size_t{6});
        // E as seis estão na tela, com o relógio da última na nota.
        std::size_t textured = 0;
        for (const ick::RenditionInfo& i : done.drawn) textured += i.textured ? 1u : 0u;
        CHECK_EQ(textured, std::size_t{6});
        CHECK(done.note.find("px") != std::string::npos);

        // UMA EDIÇÃO ENVELHECE AS SEIS, e elas voltam a sair uma por vez.
        s->rename(icf::NodePath{std::size_t{0}, std::nullopt}, "Beta");
        barFrame(gui, *s, &thumbs);
        CHECK_EQ(thumbs.stale(), std::size_t{6});
        CHECK_EQ(sched.asks.size(), std::size_t{7});
    }
    // O destrutor devolve toda textura que criou: uma barra que vazasse uma
    // por documento aberto esgotaria o pool numa tarde.
    CHECK_EQ(sink.live, 0);
    CHECK_EQ(gui.errors(), std::uint64_t{0});
}

TEST_CASE(e2e_renditions_the_shared_scheduler_keeps_the_canvas_in_front) {
    FakeScheduler real;
    ick::SharedScheduler mux(real);
    ick::RenderScheduler& canvas = mux.canvasLane();
    ick::RenderScheduler& thumbs = mux.thumbnailLane();

    const fs::path dir = makeBundle("mux");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    auto ask = [&](ick::RenderScheduler& lane, std::uint64_t version, std::uint32_t size) {
        icf::Context ctx;
        ctx.appearance = size == 128u ? icf::Appearance::Dark : icf::Appearance::Light;
        lane.request(ick::RenderRequest{version, s->bundle().clone(), ctx, size, ick::TileRect{}, size});
    };

    // UM PEDIDO DE VERDADE POR VEZ. Com a raia livre, quem pede primeiro sai
    // primeiro -- não há como interromper um render em voo, e o multiplexador
    // não finge que há.
    ask(canvas, 1, 512);
    CHECK_EQ(real.asks.size(), std::size_t{1});
    CHECK_EQ(real.asks[0].size, std::uint32_t{512});

    // Com esse em voo, as DUAS filas enchem -- e nada mais é submetido.
    ask(thumbs, 1, 128);
    ask(canvas, 2, 512);
    CHECK_EQ(real.asks.size(), std::size_t{1});

    // Respondido o primeiro: ele vai para o canvas e para MAIS NINGUÉM (um
    // resultado entregue à raia errada é uma miniatura que some para sempre e
    // um `pending` que nunca cai), e quem sai em seguida é o CANVAS, embora a
    // miniatura tenha entrado na fila antes dele.
    real.answer(0);
    CHECK(!thumbs.poll().has_value());
    auto got = canvas.poll();
    REQUIRE(got.has_value());
    CHECK_EQ(got->size, std::uint32_t{512});
    CHECK_EQ(got->version, std::uint64_t{1});
    CHECK_EQ(real.asks.size(), std::size_t{2});
    CHECK_EQ(real.asks[1].size, std::uint32_t{512});
    CHECK_EQ(real.asks[1].version, std::uint64_t{2});

    // E só depois dele a miniatura.
    real.answer(1);
    CHECK(!thumbs.poll().has_value());
    auto second = canvas.poll();
    REQUIRE(second.has_value());
    CHECK_EQ(second->version, std::uint64_t{2});
    CHECK_EQ(real.asks.size(), std::size_t{3});
    CHECK_EQ(real.asks[2].size, std::uint32_t{128});
    real.answer(2);
    CHECK(!canvas.poll().has_value());
    auto thumb = thumbs.poll();
    REQUIRE(thumb.has_value());
    CHECK_EQ(thumb->size, std::uint32_t{128});
    CHECK_EQ(real.asks.size(), std::size_t{3});
}

// ─────────────────────────────────────────────────────────────────────────────
// O TERCEIRO CONSUMIDOR DO DISPOSITIVO: A EXPORTAÇÃO (revisão 19/09, C1).
//
// `State::drainExport` renderiza na THREAD DO FRAME, chamando `rb::renderIcon`
// direto -- ela não pode virar uma raia porque o PNG sai dos floats e
// `RenderResult` só carrega 8 bits (Export.h). Enquanto isso o `JobScheduler`
// pode estar com `renderNow` numa thread do `JobQueue`, sobre o MESMO
// `rb::Device`, cujo `VkCommandPool` e cuja `VkQueue` são objetos de
// sincronização EXTERNA pela especificação Vulkan. Clicar Export com uma
// miniatura em voo alcançava esse uso concorrente.
//
// O QUE ESTE CASO COBRA é o mecanismo que o impede, e as duas metades dele:
//
//   1. com render em voo, `tryLease` RECUSA -- e recusa sem bloquear, que é a
//      diferença entre isto e um `std::mutex` em volta do dispositivo (o job o
//      segura por segundos, e a janela congelaria muda);
//   2. com o arrendamento de pé, NENHUMA raia submete -- senão a exclusão
//      valeria só para o render que já estava rodando, e um pedido do canvas
//      nascido no meio da exportação recriaria a corrida.
//
// `Source/app` não é linkado por `ic_tests`, então `drainExport` em si não tem
// caso; o que dá para cobrar é o portão que ela atravessa, e é ele que carrega
// a correção.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(e2e_renditions_the_export_leases_the_device_instead_of_sharing_it) {
    FakeScheduler real;
    ick::SharedScheduler mux(real);
    ick::RenderScheduler& canvas = mux.canvasLane();
    ick::RenderScheduler& thumbs = mux.thumbnailLane();

    const fs::path dir = makeBundle("arrendamento");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    auto ask = [&](ick::RenderScheduler& lane, std::uint64_t version, std::uint32_t size) {
        icf::Context ctx;
        ctx.appearance = size == 128u ? icf::Appearance::Dark : icf::Appearance::Light;
        lane.request(ick::RenderRequest{version, s->bundle().clone(), ctx, size, ick::TileRect{}, size});
    };

    // Com o dispositivo livre, a exportação o pega e o devolve.
    CHECK(!mux.leased());
    REQUIRE(mux.tryLease());
    CHECK(mux.leased());
    mux.endLease();
    CHECK(!mux.leased());

    // UM RENDER EM VOO RECUSA O ARRENDAMENTO. Este é o caso do laudo: o
    // `CanvasPanel` submeteu no começo do quadro e o `DiagnosticsPanel` chama
    // `drainExport` no fim do MESMO quadro.
    ask(canvas, 1, 512);
    CHECK_EQ(real.asks.size(), std::size_t{1});
    CHECK(!mux.tryLease());
    CHECK(!mux.leased());
    // E recusar não é engolir o pedido de ninguém: a miniatura continua na fila.
    ask(thumbs, 1, 128);
    CHECK_EQ(real.asks.size(), std::size_t{1});

    // Terminado o render, o dispositivo é arrendável -- e `tryLease` recolhe o
    // resultado antes de responder, para não recusar por causa de um resultado
    // que só faltava ser colhido.
    real.answer(0);
    REQUIRE(mux.tryLease());
    CHECK_EQ(real.asks.size(), std::size_t{1});   // recolher não submete
    auto got = canvas.poll();
    REQUIRE(got.has_value());
    CHECK_EQ(got->size, std::uint32_t{512});

    // ENQUANTO A EXPORTAÇÃO SEGURA O DISPOSITIVO, nada sai -- nem o pedido que
    // já estava esperando, nem um novo.
    ask(canvas, 2, 512);
    CHECK_EQ(real.asks.size(), std::size_t{1});
    CHECK(!canvas.poll().has_value());
    CHECK(!thumbs.poll().has_value());
    CHECK_EQ(real.asks.size(), std::size_t{1});

    // Devolvido, a fila anda outra vez, e na ordem de sempre: o canvas na frente.
    mux.endLease();
    CHECK_EQ(real.asks.size(), std::size_t{2});
    CHECK_EQ(real.asks[1].size, std::uint32_t{512});
    CHECK_EQ(real.asks[1].version, std::uint64_t{2});
    real.answer(1);
    REQUIRE(canvas.poll().has_value());
    CHECK_EQ(real.asks.size(), std::size_t{3});
    CHECK_EQ(real.asks[2].size, std::uint32_t{128});
}

// ─────────────────────────────────────────────────────────────────────────────
// O ARRENDAMENTO SOBREVIVE A UM `throw` (revisão 19/09, N3).
//
// O caso acima cobra o portão com as duas chamadas casadas na mão. O que ele
// NÃO pegava é a saída pelo meio: `State::drainExport` chamava
// `renderExportFile` entre `tryLease()` e `endLease()`, e esse render aloca o
// ladrilho inteiro a 1024 px -- `bad_alloc` é o escape que este projeto já
// nomeia como o esperado dele, no `Work` do job (`OnyxPorts.cpp`). Com o par
// cru, um `throw` ali deixava `leased_` de pé PARA SEMPRE: `pump()` nunca mais
// submetia, o canvas e as miniaturas paravam de renderizar, e a única pista
// era uma tela que não muda.
//
// `SharedScheduler::Lease` é o guarda que fecha isso, e é ele que este caso
// exercita: o `throw` atravessa o escopo, o dispositivo volta, e a fila que
// tinha ficado presa ANDA. A última metade é o que separa este caso de um que
// só olhasse o booleano -- `leased_` falso sem `pump()` seria um editor
// igualmente parado até o próximo pedido.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(e2e_renditions_a_throw_inside_the_export_gives_the_device_back) {
    FakeScheduler real;
    ick::SharedScheduler mux(real);
    ick::RenderScheduler& canvas = mux.canvasLane();

    const fs::path dir = makeBundle("arrendamento-lancou");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    auto ask = [&](std::uint64_t version) {
        icf::Context ctx;
        canvas.request(
            ick::RenderRequest{version, s->bundle().clone(), ctx, 512u, ick::TileRect{}, 512u});
    };

    // A exportação pega o dispositivo e o render lança.
    bool threw = false;
    try {
        ick::SharedScheduler::Lease lease(mux);
        REQUIRE(static_cast<bool>(lease));
        CHECK(mux.leased());
        throw std::bad_alloc{};
    } catch (const std::bad_alloc&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(!mux.leased());

    // E não é só o booleano: o dispositivo está mesmo utilizável de novo.
    ask(1);
    CHECK_EQ(real.asks.size(), std::size_t{1});
    real.answer(0);
    REQUIRE(canvas.poll().has_value());

    // O OUTRO LADO DO GUARDA: um `Lease` que NÃO conseguiu o dispositivo não o
    // devolve ao sair -- devolver o que não se pegou libertaria o render do
    // vizinho no meio dele.
    ask(2);
    CHECK_EQ(real.asks.size(), std::size_t{2});
    {
        ick::SharedScheduler::Lease refused(mux);
        CHECK(!static_cast<bool>(refused));
    }
    CHECK(!mux.leased());
    // O pedido em voo continua em voo: nada foi resubmetido por cima dele.
    CHECK_EQ(real.asks.size(), std::size_t{2});
    real.answer(1);
    REQUIRE(canvas.poll().has_value());
}

// ─────────────────────────────────────────────────────────────────────────────
// WATCHOS COM `dark` OU `tinted` (revisão 19/09, I3).
//
// O caso acima só exercitava watchOS com a aparência `base`, que é justamente o
// caso em que a conta fecha sozinha. Com `dark`, `renditionForAppearance` dava
// `Dark` -- uma rendition que `[BIN]` `Platform.validRenditions` EXCLUI dessa
// plataforma --, então:
//
//   * nenhum item ficava marcado, quebrando o invariante que o caso
//     `..._the_bar_and_the_appearance_combo_agree` escreve com todas as
//     letras ("exatamente UMA marcada");
//   * e a miniatura desenhada era a do contexto `light`, enquanto o canvas
//     mostrava `dark` -- a imagem da vizinha, sem dizer.
//
// O que a barra faz agora: marca a única rendition que a plataforma tem, pede a
// miniatura da fatia que o CANVAS está mostrando, e diz na nota que essa fatia
// não tem rendition nesta plataforma. Alcançável por dois combos do canvas.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(e2e_renditions_watchos_marks_one_and_shows_the_canvas_slice_in_dark_and_tinted) {
    const fs::path dir = makeBundle("watchos-escuro");
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui(1440.0f, 900.0f);
    s->view.context.idiom = icf::Idiom::WatchOS;

    struct Case { icf::Appearance appearance; const char* slice; };
    const Case kCases[] = {
        {icf::Appearance::Base, "light"},     // a fatia de `base` É `light` (`[BIN]` 0x10AEF4)
        {icf::Appearance::Light, "light"},
        {icf::Appearance::Dark, "dark"},
        {icf::Appearance::Tinted, "tinted"},
    };
    for (const Case& c : kCases) {
        s->view.context.appearance = c.appearance;
        FakeScheduler sched;
        FakeSink sink;
        ick::RenditionThumbnails thumbs(sched, sink, 128);
        const ick::RenditionStats st = barFrame(gui, *s, &thumbs);

        // A forma medida não muda com a aparência: um item, um grupo.
        CHECK_EQ(st.drawn.size(), std::size_t{1});
        CHECK_EQ(labels(st), std::string("Default"));
        // EXATAMENTE UMA MARCADA -- era zero em `dark` e em `tinted`.
        std::size_t marked = 0;
        for (const ick::RenditionInfo& i : st.drawn) marked += i.selected ? 1u : 0u;
        CHECK_EQ(marked, std::size_t{1});
        CHECK_EQ(selectedLabel(st), std::string("Default"));

        // E A MINIATURA PEDIDA É A DO CANVAS. Um só pedido, e no contexto que
        // a tela grande está mostrando -- não o `light` do nome da rendition.
        REQUIRE(sched.asks.size() == std::size_t{1});
        CHECK_EQ(std::string(icf::appearanceToString(sched.asks[0].context.appearance)),
                 std::string(c.slice));
        CHECK(sched.asks[0].context.idiom == icf::Idiom::WatchOS);
        CHECK_EQ(thumbs.stale(), std::size_t{1});

        // A DIVERGÊNCIA ESTÁ DITA quando ela existe, e só então.
        const bool divergent = std::string(c.slice) != std::string("light");
        CHECK_EQ(st.note.find("has no rendition for") != std::string::npos, divergent);
    }
    CHECK_EQ(gui.errors(), std::uint64_t{0});
}
