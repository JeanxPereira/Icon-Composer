// THE IDIOM OF THE CANVAS: does the combo reach the render, and where does the
// canvas open?
//
// The user's report was "it opens with the wrong proportions", and the two
// things that could have caused it are different bugs with different fixes:
// either the combo's value never reaches the render request, or the canvas
// opens on a composition the document does not declare. The first is a broken
// wire and the second is a wrong default, so this file measures both -- and the
// first case below is the one that settles it, because it drives the real
// `RenderCoordinator` and reads back what it ASKED FOR, rather than looking at
// a picture and forming an opinion about it.
//
// This is also the first case in the suite that links `IconComposer::Kit`, so
// it is behind `if(TARGET IconComposerKit)` in Tests/CMakeLists.txt: a build
// configured with `-DIC_BUILD_UI=OFF` has no Kit to link and must stay green.
#include "check.h"
#include "Source/IconComposerKit/RenderCoordinator.h"
#include "Source/IconComposerKit/Session.h"
#include "Source/IconComposerKit/ViewModel.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// The user's document, in the shape that mattered: `squares: "shared"` and a
// `position` specialization that only a `square` reading reaches. The scale is
// his -- 0.8, the ratio the command line measured as 360/450 = 0.800 exactly.
const char* kSquaresShared = R"({
  "supported-platforms" : { "squares" : "shared" },
  "fill" : "automatic",
  "groups" : [ { "name" : "G", "layers" : [ { "name" : "art", "image-name" : "art.svg",
      "position-specializations" : [
        { "idiom" : "square", "value" : { "scale" : 0.8, "translation-in-points" : [0, -25] } } ] } ] } ] })";

fs::path makeBundle(const std::string& name, const std::string& document) {
    const fs::path dir = fs::temp_directory_path() / ("ic-kit-" + name + ".icon");
    fs::remove_all(dir);
    fs::create_directories(dir / "Assets");
    std::ofstream(dir / "icon.json", std::ios::binary) << document;
    std::ofstream(dir / "Assets" / "art.svg", std::ios::binary) << "<svg/>";
    return dir;
}

icf::json::Value parse(const std::string& text) {
    auto v = icf::json::parse(text);
    return v ? std::move(*v) : icf::json::Value::object({});
}

// A scheduler that renders nothing and remembers everything: what the
// coordinator asked for, in order. Nothing here can hide a wire that is not
// connected, because the request is the only thing recorded.
struct Recorder : ick::RenderScheduler {
    struct Ask {
        std::uint64_t version;
        icf::Context context;
        std::uint32_t size;
        ick::TileRect tile;         // o ladrilho pedido, na grade de `size`
        std::uint32_t fallbackSize; // para onde o job cai se ele nao couber
    };
    std::vector<Ask> asks;
    std::optional<ick::RenderResult> ready;
    // O pedido INTEIRO, e nao so o resumo: `RenderRequest` carrega um
    // `IconBundle`, que nao tem construtor vazio, entao um caso que precisa
    // de um pedido de verdade (ick::failedResult) nao consegue fabricar um.
    std::optional<ick::RenderRequest> last;

    void request(ick::RenderRequest r) override {
        asks.push_back({r.version, r.context, r.size, r.tile, r.fallbackSize});
        last = std::move(r);
    }
    std::optional<ick::RenderResult> poll() override {
        std::optional<ick::RenderResult> r = std::move(ready);
        ready.reset();
        return r;
    }
    // One 2x2 opaque frame, claiming to answer `ctx` at `size`.
    void answer(std::uint64_t version, icf::Context ctx, std::uint32_t size) {
        ick::RenderResult res;
        res.version = version;
        res.context = ctx;
        // `size` is what the coordinator's key compares (RenderCoordinator.cpp);
        // `width`/`height` are what the pixels turned out to be. This helper
        // renders no tile, so they agree.
        res.size = size;
        res.gridSize = size;
        res.width = size;
        res.height = size;
        res.rgba8.assign(static_cast<std::size_t>(size) * size * 4, 255);
        res.drawn = 1;
        res.total = 1;
        ready = std::move(res);
    }
    // O LADRILHO RESPONDIDO (spec 2026-09-16). O eco (`size`, `tile`) e o que
    // foi PEDIDO; `gridSize`/`origin`/`width` sao o que os pixels SAO.
    void answerTile(std::uint64_t version, icf::Context ctx, std::uint32_t size,
                    ick::TileRect tile) {
        ick::RenderResult res;
        res.version = version;
        res.context = ctx;
        res.size = size;
        res.tile = tile;
        res.gridSize = size;
        res.originX = tile.x;
        res.originY = tile.y;
        res.width = tile.w;
        res.height = tile.h;
        res.rgba8.assign(static_cast<std::size_t>(tile.w) * tile.h * 4, 255);
        res.drawn = 1;
        res.total = 1;
        ready = std::move(res);
    }
    // A QUEDA PARA A BASE: o ladrilho nao coube (o teto de area, ou o do
    // aparelho que chega como erro), e o job rendeu o canvas inteiro em
    // `fallback` -- com o eco INTACTO, porque e ele que responde a pergunta
    // que o coordenador fez. `refined` false e o unico sinal de que os pixels
    // nao sao os pedidos.
    void answerFallback(std::uint64_t version, icf::Context ctx, std::uint32_t size,
                        ick::TileRect tile, std::uint32_t fallback, std::string note) {
        ick::RenderResult res;
        res.version = version;
        res.context = ctx;
        res.size = size;   // o eco, intacto: e a pergunta que foi feita
        res.tile = tile;
        res.gridSize = fallback;
        res.width = fallback;
        res.height = fallback;
        res.rgba8.assign(static_cast<std::size_t>(fallback) * fallback * 4, 255);
        res.refined = false;
        res.notes.push_back(std::move(note));
        res.drawn = 1;
        res.total = 1;
        ready = std::move(res);
    }
};

// No device anywhere in this file; the ids only have to be distinct and nonzero.
struct FakeSink : ick::TextureSink {
    int live = 0;
    std::uintptr_t next = 1;
    ImTextureID create(std::uint32_t, std::uint32_t, const std::uint8_t*) override {
        ++live;
        return static_cast<ImTextureID>(next++);
    }
    bool update(ImTextureID, std::uint32_t, std::uint32_t, const std::uint8_t*) override { return true; }
    void remove(ImTextureID) override { --live; }
};

std::vector<fs::path> corpusDocuments() {
    const char* dir = std::getenv("IC_CORPUS_DIR");
    if (!dir || !*dir) return {};
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(fs::path(dir), ec)) {
        if (!e.is_directory()) continue;
        auto doc = e.path() / "icon.json";
        if (fs::exists(doc)) out.push_back(doc);
    }
    return out;
}

std::string readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

}  // namespace

// THE WIRE. Move the combo, and the coordinator must ask again, and the new ask
// must carry the new idiom. Three assertions in one, and the third is the one
// that would have caught a coordinator keyed on the version alone: a result
// that answers the OLD idiom must not be accepted as the answer to the new one.
TEST_CASE(kit_idiom_combo_reaches_the_render_request) {
    const auto dir = makeBundle("wire", kSquaresShared);
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());

    Recorder sched;
    FakeSink sink;
    ick::RenderCoordinator coord(sched, sink);

    // Frame 1: one ask, and it carries the opening idiom.
    coord.tick(*s);
    REQUIRE(sched.asks.size() == 1);
    CHECK(sched.asks[0].context.idiom == icf::Idiom::Square);
    CHECK(coord.view().pending);

    // Answer it, and the canvas stops being provisional.
    sched.answer(s->version(), sched.asks[0].context, sched.asks[0].size);
    coord.tick(*s);
    CHECK_EQ(sched.asks.size(), std::size_t{1});   // nothing moved, so nothing re-asked
    CHECK(!coord.view().pending);

    // THE COMBO. `contextBar` writes exactly this field and nothing else.
    s->view.context.idiom = icf::Idiom::WatchOS;
    coord.tick(*s);
    REQUIRE(sched.asks.size() == 2);
    CHECK(sched.asks[1].context.idiom == icf::Idiom::WatchOS);
    CHECK(sched.asks[1].version == s->version());   // the view is not an edit
    CHECK(coord.view().pending);

    // A result that answers the idiom we just LEFT is not the answer to this
    // one, and must not clear `pending` -- the version is identical, so only the
    // context can tell the two apart.
    sched.answer(s->version(), icf::Context{icf::Appearance::Base, icf::Idiom::Square}, 512);
    coord.tick(*s);
    CHECK(coord.view().pending);

    // The one that does answer it, does.
    sched.answer(s->version(), icf::Context{icf::Appearance::Base, icf::Idiom::WatchOS}, 512);
    coord.tick(*s);
    CHECK(!coord.view().pending);
}

// THE DEFAULT. A document that declares `squares` opens on the square family,
// so the specialization that sets its scale to 0.8 is the one on screen from
// the first frame -- and not one the person has to go looking for a combo to
// reach.
TEST_CASE(kit_canvas_opens_on_the_idiom_the_document_declares) {
    const auto dir = makeBundle("default", kSquaresShared);
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());
    CHECK(s->view.context.idiom == icf::Idiom::Square);
    // The VIEW moved and nothing else: the inspector still edits the plain key.
    CHECK(s->scope.idiom == icf::Idiom::Base);
    CHECK(s->scope.appearance == icf::Appearance::Base);
    CHECK(s->view.context.appearance == icf::Appearance::Base);
    CHECK(!s->isDirty());

    // And the specialization it opens on is the one that resolves.
    const icf::json::Value* layer = icf::nodeAt(s->root(), icf::NodePath{std::size_t{0}, std::size_t{0}});
    REQUIRE(layer != nullptr);
    const icf::json::Value* p = icf::resolve(*layer, "position", s->view.context);
    REQUIRE(p != nullptr);
    auto pos = icf::positionFrom(*p);
    REQUIRE(pos.has_value());
    CHECK_EQ(pos->scale, 0.8);
    // Under Base -- where the canvas used to open -- there is no position at all.
    CHECK(icf::resolve(*layer, "position", icf::Context{}) == nullptr);
}

// The mapping itself, form by form. Every branch is a form the corpus writes,
// except the last two, which are what the reader does when handed a document
// that declares nothing.
TEST_CASE(kit_declared_idiom_reads_every_form_of_supported_platforms) {
    auto idiomOf = [](const char* text) {
        return ick::declaredIdiom(parse(text));
    };
    CHECK(idiomOf(R"({"supported-platforms":{"squares":"shared"}})") == icf::Idiom::Square);
    CHECK(idiomOf(R"({"supported-platforms":{"squares":"shared","circles":["watchOS"]}})") ==
          icf::Idiom::Square);
    CHECK(idiomOf(R"({"supported-platforms":{"squares":["macOS"]}})") == icf::Idiom::MacOS);
    CHECK(idiomOf(R"({"supported-platforms":{"squares":["iOS"]}})") == icf::Idiom::IOS);
    // Two members, and the only scope that covers both is the family itself.
    CHECK(idiomOf(R"({"supported-platforms":{"squares":["iOS","macOS"]}})") == icf::Idiom::Square);
    // 0 of 145 documents, but a round-only icon must not open on a squircle.
    CHECK(idiomOf(R"({"supported-platforms":{"circles":["watchOS"]}})") == icf::Idiom::WatchOS);
    // Declaring nothing is the one case where Base is the honest answer.
    CHECK(idiomOf(R"({"supported-platforms":{}})") == icf::Idiom::Base);
    CHECK(idiomOf(R"({"groups":[]})") == icf::Idiom::Base);
    CHECK(idiomOf(R"({"supported-platforms":"shared"})") == icf::Idiom::Base);
    CHECK(idiomOf(R"({"supported-platforms":{"squares":[]}})") == icf::Idiom::Base);
}

// THE NUMBER THAT MAKES THE DEFAULT A MEASUREMENT AND NOT A PREFERENCE.
//
// 145 of 145 documents declare `supported-platforms`, and all 145 declare
// `squares`. Under `Idiom::Base` not one of the corpus's 84 idiom-predicated
// specialization entries resolves; under the declaration, 71 of them do.
TEST_CASE(kit_corpus_declares_a_platform_in_every_document) {
    auto docs = corpusDocuments();
    REQUIRE(!docs.empty());
    CHECK_EQ(docs.size(), std::size_t{145});

    std::map<std::string, int> chosen;
    int declaring = 0;
    for (const auto& d : docs) {
        const icf::json::Value root = parse(readAll(d));
        if (const icf::json::Value* sp = root.find("supported-platforms")) {
            if (sp->kind() == icf::json::Value::Kind::Object) ++declaring;
        }
        ++chosen[std::string(icf::idiomToString(ick::declaredIdiom(root)))];
    }
    CHECK_EQ(declaring, 145);
    // Not one document opens on Base, because not one declines to say where it
    // ships. That is the whole argument for the default in one line.
    CHECK_EQ(chosen["base"], 0);
    CHECK_EQ(chosen["square"], 120);
    CHECK_EQ(chosen["macOS"], 22);
    CHECK_EQ(chosen["iOS"], 3);
    CHECK_EQ(chosen["watchOS"], 0);
}

// THE CLOCK THE COORDINATOR KEEPS, AND WHY IT IS HERE AND NOT IN A CANVAS TEST.
//
// On 2026-09-15 six fronts turned a 1024 px glass render into eighty-seven
// seconds and the editor showed an empty canvas with `pending` under it. Nothing
// failed: the arithmetic was right, no layer was missing, and the Diagnostics
// panel's only word for "forty milliseconds" and for "a minute and a half" was
// the same word. The fix is a number, and a number nobody asserts is a number
// that quietly goes back to zero.
//
// Two things are pinned, because they are two different failures:
//
//   1. WHILE IT IS IN FLIGHT, `pendingSeconds` moves. This is the one that
//      matters -- a render that never finishes produces no result to time, so a
//      clock that only ran on completion would have said nothing on the day this
//      happened.
//   2. WHEN IT LANDS, `lastRenderSeconds` stops being negative. Negative and not
//      zero, because "no render has finished yet" must not print as `0.00 s`.
//
// It lives in this file because `Recorder` and `FakeSink` do: the coordinator is
// the only thing that can be asked this question without a device.
TEST_CASE(kit_coordinator_times_the_render_it_is_waiting_for) {
    const auto dir = makeBundle("clock", kSquaresShared);
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());

    Recorder sched;
    FakeSink sink;
    ick::RenderCoordinator coord(sched, sink);

    coord.tick(*s);
    REQUIRE(sched.asks.size() == 1);
    CHECK(coord.view().pending);
    // Nothing has come back, so there is no completed render to report -- and
    // the sentinel is negative, not zero.
    CHECK(coord.view().lastRenderSeconds < 0.0);

    // A second frame with the answer still missing: the wait is still being
    // counted, and it is not standing still.
    const double first = coord.view().pendingSeconds;
    coord.tick(*s);
    const double second = coord.view().pendingSeconds;
    CHECK(second >= first);
    CHECK(second > 0.0);

    // The answer lands. The wait becomes a measurement, and the counter of the
    // render in flight goes back to zero because there is no longer one.
    sched.answer(sched.asks[0].version, sched.asks[0].context, sched.asks[0].size);
    coord.tick(*s);
    CHECK(!coord.view().pending);
    CHECK(coord.view().lastRenderSeconds >= 0.0);
    CHECK(coord.view().lastRenderSeconds >= second);
    CHECK_EQ(coord.view().pendingSeconds, 0.0);
}

// O LADRILHO NA CHAVE, E A BASE PARA ONDE CAIR (spec 2026-09-16, "O que o Kit
// faz").
//
// O canvas escreve `view.tileSize`/`view.tile` quando o pan e o zoom param
// (Tests/test_kit_canvas.cpp mede aquele lado); daqui para baixo o ladrilho e
// so mais um pedaco da chave, e e esta a metade que decide se um resultado
// responde a pergunta que esta em pe. Dois ladrilhos VIZINHOS da mesma grade
// tem a mesma versao, o mesmo contexto e a mesma `size`: sem o ladrilho na
// chave, o primeiro a chegar apagaria o `pending` do segundo e a tela ficaria
// parada no pedaco errado.
//
// A queda para a base tambem mora aqui. Ate esta task nada no editor escrevia
// `RenderRequest::fallbackSize`, entao os dois caminhos de queda do job
// (`Source/app/OnyxPorts.cpp`) nunca tinham sido exercitados por teste nenhum.
TEST_CASE(kit_coordinator_keys_on_the_tile_and_falls_back_to_the_base) {
    const auto dir = makeBundle("tile", kSquaresShared);
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());

    Recorder sched;
    FakeSink sink;
    ick::RenderCoordinator coord(sched, sink);

    // Sem ladrilho: o pedido de sempre -- o canvas inteiro na base. E a base
    // para onde cair ja vai junto, e nunca e zero: com ela, o job nunca
    // devolve "recusado, sem pixels e sem erro", que seria uma tela parada sem
    // nada que a explicasse.
    coord.tick(*s);
    REQUIRE(sched.asks.size() == 1);
    CHECK_EQ(sched.asks[0].size, 512u);
    CHECK_EQ(sched.asks[0].tile.w, 0u);
    CHECK_EQ(sched.asks[0].fallbackSize, 512u);
    sched.answer(s->version(), sched.asks[0].context, 512);
    coord.tick(*s);
    CHECK(!coord.view().pending);
    CHECK(coord.view().refined);
    CHECK_EQ(coord.view().gridSize, 512u);

    // 400%: a grade e 2048 e o pedido e o retangulo visivel dentro dela.
    const ick::TileRect want{100, 100, 700, 600};
    s->view.tileSize = 2048;
    s->view.tile = want;
    coord.tick(*s);
    REQUIRE(sched.asks.size() == 2);
    CHECK_EQ(sched.asks[1].size, 2048u);
    CHECK(sched.asks[1].tile == want);
    CHECK_EQ(sched.asks[1].fallbackSize, 512u);   // a base, nao a grade
    CHECK(coord.view().pending);

    // O VIZINHO NAO RESPONDE. Mesma versao, mesmo contexto, mesma grade, outro
    // retangulo: nao e a resposta, e `pending` continua de pe.
    sched.answerTile(s->version(), s->view.context, 2048, ick::TileRect{340, 100, 700, 600});
    coord.tick(*s);
    CHECK(coord.view().pending);
    CHECK_EQ(coord.view().gridSize, 512u);   // a textura de antes continua

    // O que responde, responde -- e traz consigo onde os pixels ficam.
    sched.answerTile(s->version(), s->view.context, 2048, want);
    coord.tick(*s);
    CHECK(!coord.view().pending);
    CHECK_EQ(coord.view().gridSize, 2048u);
    CHECK_EQ(coord.view().width, 700u);
    CHECK_EQ(coord.view().height, 600u);
    CHECK_EQ(coord.view().originX, 100);
    CHECK_EQ(coord.view().originY, 100);
    CHECK(coord.view().refined);

    // A QUEDA. O ladrilho seguinte nao cabe -- o teto de area, ou o do
    // aparelho -- e o job devolve o canvas inteiro na base, com o eco intacto.
    // O resultado E valido, entao o motivo vai nas notas e nao em `error`
    // (Ports.h: `error` nao-vazio diz "nada mais e valido").
    const ick::TileRect big{0, 0, 8192, 8192};
    s->view.tileSize = 8192;
    s->view.tile = big;
    coord.tick(*s);
    REQUIRE(sched.asks.size() == 3);
    CHECK_EQ(sched.asks[2].size, 8192u);
    sched.answerFallback(s->version(), s->view.context, 8192, big, 512,
                         "viewport acima do teto de area: nada desenhado");
    coord.tick(*s);
    CHECK(!coord.view().pending);       // respondeu a chave, entao E a resposta
    CHECK(!coord.view().refined);       // e diz que nao e o que foi pedido
    CHECK_EQ(coord.view().gridSize, 512u);
    CHECK_EQ(coord.view().width, 512u);
    CHECK_EQ(coord.view().originX, 0);
    CHECK_EQ(coord.view().originY, 0);
    CHECK(coord.view().error.empty());
    REQUIRE(coord.view().notes.size() == 1);
    CHECK_EQ(coord.view().notes[0], std::string("viewport acima do teto de area: nada desenhado"));

    // De volta a 100%: o canvas inteiro na base, e `refined` volta a ser
    // verdade quando os pixels pedidos sao os que chegam.
    s->view.tileSize = 0;
    s->view.tile = ick::TileRect{};
    coord.tick(*s);
    REQUIRE(sched.asks.size() == 4);
    CHECK_EQ(sched.asks[3].size, 512u);
    CHECK_EQ(sched.asks[3].tile.w, 0u);
    sched.answer(s->version(), s->view.context, 512);
    coord.tick(*s);
    CHECK(!coord.view().pending);
    CHECK(coord.view().refined);
    CHECK_EQ(coord.view().gridSize, 512u);
}

// UM RENDER QUE MORREU AINDA TEM QUE CHEGAR.
//
// O job roda numa thread de trabalho, e Onyx CONTEM uma excecao que escape
// dele (Jobs.cpp:180-185: "a throw is contained here and the job still
// completes normally"). Contida quer dizer engolida: o `RenderResult` que o
// job ia preencher volta como foi CONSTRUIDO -- versao 0, contexto vazio --
// e o coordenador compara a chave inteira, entao uma resposta assim nunca
// casa. Ela e descartada em todo frame, para sempre: `pending` nao cai, o
// canvas fica vazio, e o motivo nao chega em lugar nenhum da tela.
//
// O controle negativo abaixo E esse resultado mudo, e ele tem que ser
// descartado -- se o coordenador o aceitasse, o caso seguinte passaria por
// acidente e nao provaria nada. `failedResult` e a unica diferenca entre os
// dois.
TEST_CASE(kit_failed_result_reaches_the_canvas) {
    const auto dir = makeBundle("render-morto", kSquaresShared);
    auto s = ick::Session::open(dir);
    REQUIRE(s.has_value());

    Recorder sched;
    FakeSink sink;
    ick::RenderCoordinator coord(sched, sink);

    coord.tick(*s);
    REQUIRE(sched.asks.size() == 1);
    CHECK(coord.view().pending);

    // O MUDO: o erro esta la, a chave nao.
    ick::RenderResult mudo;
    mudo.error = "std::bad_alloc";
    sched.ready = mudo;
    coord.tick(*s);
    CHECK(coord.view().pending);         // descartado, como tem que ser
    CHECK(coord.view().error.empty());   // e ninguem nunca soube por que
    CHECK_EQ(sched.asks.size(), std::size_t{1});   // nada mudou, nada re-pedido

    // O MESMO ERRO, dito com a chave -- a partir do pedido de verdade, que e
    // exatamente o que o job tem na mao quando algo escapa dele.
    REQUIRE(sched.last.has_value());
    const ick::RenderRequest& r = *sched.last;
    const ick::RenderResult morto = ick::failedResult(r, "o render lancou: std::bad_alloc");

    // O eco, campo por campo: e disto que o casamento depende.
    CHECK_EQ(morto.version, r.version);
    CHECK(morto.context == r.context);
    CHECK_EQ(morto.size, r.size);
    CHECK(morto.tile == r.tile);
    CHECK(morto.rgba8.empty());   // `error` nao-vazio: nao ha pixels

    sched.ready = morto;
    coord.tick(*s);
    CHECK(!coord.view().pending);
    CHECK_EQ(coord.view().error, std::string("o render lancou: std::bad_alloc"));
    CHECK_EQ(sink.live, 0);              // um fracasso nao cria textura
    CHECK_EQ(coord.view().gridSize, 0u); // nem adota uma grade que nao veio
}
