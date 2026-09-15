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
    };
    std::vector<Ask> asks;
    std::optional<ick::RenderResult> ready;

    void request(ick::RenderRequest r) override {
        asks.push_back({r.version, r.context, r.size});
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
        res.width = size;
        res.height = size;
        res.rgba8.assign(static_cast<std::size_t>(size) * size * 4, 255);
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
