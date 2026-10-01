// The fill, wired into the renderer: `automatic`, `automatic-gradient`, and the
// orientation the background throws away.
//
// WHAT IS PINNED HERE, AND WHY EACH ONE
// -------------------------------------
// The same question the resolver's own gate asked -- what would a plausible
// implementation get wrong? -- but one layer up, where the answer becomes a
// pixel:
//
//   1. `tinted` MAKES THE BACKGROUND TRANSPARENT. `[BIN]` The third arm of the
//      background switch is `IconColor.clear`, not a grey and not a ramp. A test
//      that covered only light and dark would pass with that arm deleted, and
//      the picture would gain a chiclet ramp over every tinted rendition.
//   2. THE LAYER'S `automatic` IS A NO-OP 152 TIMES OUT OF 156. So a test that
//      exercises only the common case proves almost nothing: it would pass
//      against an implementation that answered nil unconditionally. `[ART]` The
//      4 that inherit are all in `CodeEditApp__CodeEdit__CodeEdit*Icon`, layer
//      `CodeLines` under `dark`, and they are covered by name.
//   3. THE BACKGROUND'S `orientation` IS DISCARDED. `[BIN]` One of the four
//      gradient sites reads it and it is the LAYER's. `[ART]` 61 background
//      resolutions over the corpus name one anyway. Our renderer being more
//      correct than the target is a different pixel, so the discarding is
//      pinned twice: on a synthetic diagonal, where honouring it would be
//      obvious, and on a real corpus document whose orientation stops at 0.7 of
//      the height, where honouring it would leave the bottom 30% flat.
//   4. `automatic-gradient` DRAWS AT ALL. It was refused until today because
//      the axis had never been read.
//
// AND WHAT IS DELIBERATELY NOT PINNED: which END of the shape gets the first
// stop. `[OBS]` The y-handedness of the RB display list was never established,
// so a test that fixed the direction would be pinning a guess. Every assertion
// below about a ramp's ends is written as a SET of two values, or as a
// difference between two rows, never as "the top is 255".
#include "check.h"
#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/IconComposerFoundation/IconDocument.h"
#include "Source/IconComposerFoundation/Json.h"
#include "Source/IconComposerFoundation/Values.h"
#include "Source/RenderBox/FillResolve.h"
#include "Source/RenderBox/IconRenderer.h"
#include "Source/RenderBox/SystemFill.h"

// So pelo id do processo, que e o que torna o diretorio temporario unico por
// processo (ver TempBundle). `<process.h>` e nao `<windows.h>`: este ultimo
// define `near` como macro e apaga o `near()` declarado logo abaixo.
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace rb;

namespace {

namespace fs = std::filesystem;

Device& gpu() {
    static Device* d = [] {
        auto made = Device::create();
        if (!made) {
            std::printf("  FAIL no Vulkan device: %s\n", made.error().c_str());
            ++ictest::failures();
            return static_cast<Device*>(nullptr);
        }
        return new Device(std::move(*made));
    }();
    static Device dead;
    return d ? *d : dead;
}

std::string readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}

// Every corpus document, art or no art. The background needs none.
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
    std::sort(out.begin(), out.end());
    return out;
}

fs::path corpus(const char* name) {
    const char* dir = std::getenv("IC_CORPUS_DIR");
    return fs::path(dir ? dir : "") / name;
}

const icf::Appearance kAppearances[] = {icf::Appearance::Base, icf::Appearance::Light,
                                        icf::Appearance::Dark, icf::Appearance::Tinted};

const char* appearanceName(icf::Appearance a) {
    switch (a) {
        case icf::Appearance::Base: return "base";
        case icf::Appearance::Light: return "light";
        case icf::Appearance::Dark: return "dark";
        case icf::Appearance::Tinted: return "tinted";
    }
    return "?";
}

// A bundle on disk, so a background and a layer fill can be exercised on values
// chosen here. The corpus has every fill kind but never in a shape that
// isolates one -- and the two shapes that matter most for `automatic` (a light
// slot that names a colour, and a background orientation that is not vertical)
// it does not have at all.
class TempBundle {
public:
    explicit TempBundle(const std::string& document) {
        // O PID entra no nome. O caminho era funcao apenas do documento, e
        // `remove_all` abre o construtor -- duas suites rodando ao mesmo tempo
        // nesta maquina escolheriam o MESMO diretorio e uma apagaria o fixture
        // da outra no meio do caso dela. Com o pid os dois processos nao se
        // encontram, e o `remove_all` continua limpando o que uma execucao
        // anterior abandonou.
        dir_ = fs::temp_directory_path() /
               ("ic-fill-" + std::to_string(processId()) + "-" +
                std::to_string(std::hash<std::string>{}(document)));
        std::error_code ec;
        fs::remove_all(dir_, ec);
        fs::create_directories(dir_ / "Assets", ec);
        write(dir_ / "icon.json", document);
        // Art that fills the whole canvas, in a colour nothing else here uses:
        // a fill that fails to override is then visible as red rather than as
        // "some colour".
        write(dir_ / "Assets" / "red.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1024 1024\">"
              "<path d=\"M0 0 L1024 0 L1024 1024 L0 1024 Z\" fill=\"#ff0000\"/></svg>");
    }
    ~TempBundle() {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    const fs::path& path() const { return dir_; }

private:
    static unsigned long processId() {
#ifdef _WIN32
        return static_cast<unsigned long>(::_getpid());
#else
        return static_cast<unsigned long>(::getpid());
#endif
    }
    static void write(const fs::path& p, const std::string& text) {
        std::FILE* f = std::fopen(p.string().c_str(), "wb");
        if (!f) return;
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }
    fs::path dir_;
};

struct Rgba {
    float r = 0, g = 0, b = 0, a = 0;
};

Rgba at(const RenderedIcon& img, std::uint32_t x, std::uint32_t y) {
    const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 4;
    return {img.rgba[i], img.rgba[i + 1], img.rgba[i + 2], img.rgba[i + 3]};
}

bool near(float a, float b, float tol = 0.01f) { return std::fabs(a - b) < tol; }

bool hasNote(const RenderedIcon& img, const char* text) {
    for (const std::string& n : img.notes) {
        if (n == text) return true;
    }
    return false;
}

// A document with no groups, so nothing at all can be responsible for a pixel
// except the background.
std::string backgroundOnly(const std::string& fillJson) {
    return "{\n  \"fill\" : " + fillJson + ",\n  \"groups\" : []\n}\n";
}

// `"glass" : false` is SPELLED here, and it has to be. A missing `glass` is
// `true` -- `[BIN]` `Layer.init()` at `0x95C10` in
// `IconComposerFoundation.arm64` stores 1 into `_isGlass.defaultValue`. These
// fixtures are about the FILL, and a layer that participates in glass gets a
// highlight and a translucency mask over the ramp, which is exactly the flatness
// these cases measure. So the fixture says what it means.
std::string oneLayer(const std::string& fillJson) {
    return "{\n  \"groups\" : [\n    {\n      \"layers\" : [\n        {\n"
           "          \"glass\" : false,\n"
           "          \"image-name\" : \"red.svg\",\n"
           "          \"name\" : \"only\",\n"
           "          " + fillJson + "\n"
           "        }\n      ]\n    }\n  ]\n}\n";
}

std::optional<RenderedIcon> render(Device& d, const fs::path& bundle, icf::Appearance a,
                                   std::uint32_t size) {
    auto b = icf::IconBundle::open(bundle);
    if (!b) return std::nullopt;
    IconRenderOptions o;
    o.size = size;
    o.context.appearance = a;
    auto icon = renderIcon(d, *b, o);
    if (!icon) {
        std::printf("  FAIL render %s: %s\n", bundle.filename().string().c_str(),
                    icon.error().c_str());
        ++ictest::failures();
        return std::nullopt;
    }
    return *icon;
}

// `[BIN]` The two canned ramps' greys, from `SystemFill.h`.
constexpr float kLight0 = 1.0f;
constexpr float kLight1 = 0.9607843137254902f;
constexpr float kDark0 = 0.12156862745098039f;
constexpr float kDark1 = 0.058823529411764705f;

// Are the two ends of a ramp the two greys, IN EITHER ORDER? The order is the
// one thing `SystemFill.h` refuses to claim -- see the header note.
bool endsAre(float top, float bottom, float x, float y) {
    return (near(top, x) && near(bottom, y)) || (near(top, y) && near(bottom, x));
}

// HOW FAR IN THE RAMP IS SAMPLED, and why it is no longer row zero.
//
// The pastille draws its OWN highlights now (`ChicletHighlights.h`), and the
// widest of the six is `keyDiffuse`/`fillDiffuse` at `distance == 40` canvas
// points -- `40 * n / 1024` pixels, so 2.5 px at `n == 64` and 1.25 px at
// `n == 32`. The outermost rows are lit, deliberately, and asking them what the
// ramp's end value is now asks the wrong pixel.
//
// Four rows in clears the widest band at both sizes and costs almost nothing in
// ramp value: the light pair spans 0.039 over the whole edge and the dark pair
// 0.063, so 4/64 of the way in moves the sample by at most 0.004 -- well inside
// `near`'s 0.01. What is being pinned is WHICH RAMP, and that is unchanged.
constexpr std::uint32_t kRampInset = 4;

}  // namespace

// ---------------------------------------------------------------------------
// The background: the three arms of the appearance switch, in pixels
// ---------------------------------------------------------------------------

// THE ARM NOBODY PREDICTED, and the one a two-armed implementation gets wrong
// in a way that shows: under `tinted` an `automatic` background is
// `IconColor.clear`. Not a grey. Not the light ramp. Nothing at all.
//
// This is the test the brief singled out: one that covers only light and dark
// passes with the clear arm deleted.
TEST_CASE(a_tinted_automatic_background_is_transparent_and_not_a_grey) {
    Device& d = gpu();
    if (!d.valid()) return;
    const TempBundle b(backgroundOnly("\"automatic\""));

    auto tinted = render(d, b.path(), icf::Appearance::Tinted, 32);
    REQUIRE(tinted.has_value());
    CHECK(tinted->backgroundPainted);         // resolved: it is an answer, not a gap
    CHECK(tinted->backgroundGap.empty());
    for (std::uint32_t y = 0; y < tinted->height; ++y) {
        for (std::uint32_t x = 0; x < tinted->width; ++x) {
            CHECK(at(*tinted, x, y).a == 0.0f);
        }
    }

    // And the same document under the other three arms is NOT transparent, so
    // the check above is about `tinted` and not about the document.
    for (icf::Appearance a : {icf::Appearance::Base, icf::Appearance::Light,
                              icf::Appearance::Dark}) {
        auto opaque = render(d, b.path(), a, 32);
        REQUIRE(opaque.has_value());
        CHECK(near(at(*opaque, 16, 16).a, 1.0f));
    }
}

// `base` and `light` share an arm because the compare at `0x10AEF4` is
// UNSIGNED, and `dark` takes the other. The greys are the read ones, and their
// ORDER is deliberately not asserted.
TEST_CASE(an_automatic_background_paints_the_chiclet_ramp_of_its_appearance) {
    Device& d = gpu();
    if (!d.valid()) return;
    const TempBundle b(backgroundOnly("\"automatic\""));
    const std::uint32_t n = 64;

    for (icf::Appearance a : {icf::Appearance::Base, icf::Appearance::Light}) {
        auto icon = render(d, b.path(), a, n);
        REQUIRE(icon.has_value());
        CHECK(endsAre(at(*icon, n / 2, kRampInset).r,
                      at(*icon, n / 2, n - 1 - kRampInset).r, kLight0, kLight1));
    }

    auto dark = render(d, b.path(), icf::Appearance::Dark, n);
    REQUIRE(dark.has_value());
    // 31 against 15 -- the pair where the undetermined direction is visible,
    // which is why it is named in the notes rather than silently chosen.
    CHECK(endsAre(at(*dark, n / 2, kRampInset).r, at(*dark, n / 2, n - 1 - kRampInset).r,
                  kDark0, kDark1));
    CHECK(hasNote(*dark, kGradientAxisDirectionNote));
    CHECK(hasNote(*dark, kBackgroundShapeNote));

    // A ramp, not a flat: the two ends differ by the whole of 31 -> 15.
    CHECK(std::fabs(at(*dark, n / 2, kRampInset).r -
                    at(*dark, n / 2, n - 1 - kRampInset).r) > 0.05f);
}

// `[BIN]` `none` on the background is the SAME BLOCK as `automatic`, byte for
// byte. Pinned as an equality of whole images, because "both look like a ramp"
// would also be true of two different ramps.
TEST_CASE(a_none_background_paints_exactly_what_an_automatic_one_does) {
    Device& d = gpu();
    if (!d.valid()) return;
    for (icf::Appearance a : kAppearances) {
        const TempBundle none(backgroundOnly("\"none\""));
        const TempBundle automatic(backgroundOnly("\"automatic\""));
        auto x = render(d, none.path(), a, 16);
        auto y = render(d, automatic.path(), a, 16);
        REQUIRE(x.has_value());
        REQUIRE(y.has_value());
        CHECK(x->rgba == y->rgba);
        CHECK(x->backgroundPainted);
    }
}

// `system-light` names a RAMP, not a condition: it is the light ramp under
// `dark` too. An implementation that paired the case with the appearance would
// pass every test above and fail this one.
TEST_CASE(a_system_light_background_stays_the_light_ramp_under_dark) {
    Device& d = gpu();
    if (!d.valid()) return;
    const TempBundle b(backgroundOnly("\"system-light\""));
    const std::uint32_t n = 32;
    auto dark = render(d, b.path(), icf::Appearance::Dark, n);
    REQUIRE(dark.has_value());
    CHECK(endsAre(at(*dark, n / 2, kRampInset).r, at(*dark, n / 2, n - 1 - kRampInset).r,
                  kLight0, kLight1));
}

// ---------------------------------------------------------------------------
// TASK 7: the orientation the background discards
// ---------------------------------------------------------------------------

// THE DEFECT IN OUR FAVOUR, REMOVED. `[BIN]` The background converter reads
// `primaryColor` and `secondaryColor` and never touches `orientation`, so a
// background `linear-gradient` draws on the default vertical axis whatever the
// document says.
//
// The synthetic case is the crisp one: a DIAGONAL orientation, which the corpus
// does not contain. Honoured, the ramp would vary along x; discarded, every row
// is one colour. So this fails the moment somebody "improves" the orientation
// back in, and it cannot be satisfied by an accident of the corpus.
TEST_CASE(a_background_linear_gradients_orientation_is_discarded_for_the_vertical_axis) {
    Device& d = gpu();
    if (!d.valid()) return;
    const std::uint32_t n = 64;
    const std::string ramp =
        "{ \"linear-gradient\" : [ \"srgb:0.00000,0.00000,0.00000,1.00000\","
        " \"srgb:1.00000,1.00000,1.00000,1.00000\" ],"
        " \"orientation\" : { \"start\" : { \"x\" : 0, \"y\" : 0 },"
        " \"stop\" : { \"x\" : 1, \"y\" : 0 } } }";
    const TempBundle b(backgroundOnly(ramp));
    auto icon = render(d, b.path(), icf::Appearance::Light, n);
    REQUIRE(icon.has_value());
    CHECK(icon->backgroundPainted);

    // The document asks for a HORIZONTAL ramp. Every row must nevertheless be
    // one flat colour, and the variation must be down the image.
    for (std::uint32_t y = 0; y < n; ++y) {
        CHECK(near(at(*icon, 0, y).r, at(*icon, n - 1, y).r, 0.001f));
    }
    CHECK(std::fabs(at(*icon, n / 2, 0).r - at(*icon, n / 2, n - 1).r) > 0.9f);

    // And the renderer says out loud that it threw the orientation away.
    CHECK(hasNote(*icon, kDiscardedBackgroundOrientationNote));

    // The same ramp with NO orientation must give the identical image: the nil
    // is passed as a literal, not derived from the key being absent.
    const std::string bare =
        "{ \"linear-gradient\" : [ \"srgb:0.00000,0.00000,0.00000,1.00000\","
        " \"srgb:1.00000,1.00000,1.00000,1.00000\" ] }";
    const TempBundle c(backgroundOnly(bare));
    auto plain = render(d, c.path(), icf::Appearance::Light, n);
    REQUIRE(plain.has_value());
    CHECK(icon->rgba == plain->rgba);
    CHECK(!hasNote(*plain, kDiscardedBackgroundOrientationNote));
}

// THE SAME CLAIM, ON A REAL DOCUMENT. `[ART]` `OpenSource03__harnss__icon`
// names a root `linear-gradient` whose orientation runs `(0.5, 0) -> (0.5,
// 0.7)`. Honoured, the ramp would finish at 70% of the height and every row
// below would be one flat colour; discarded, it keeps changing all the way
// down. The discriminator is the FLAT REGION, which is why this works without
// asserting which end holds which colour.
TEST_CASE(a_corpus_background_orientation_that_stops_early_is_ignored) {
    Device& d = gpu();
    if (!d.valid()) return;
    const fs::path b = corpus("OpenSource03__harnss__icon");
    REQUIRE(fs::is_directory(b));
    const std::uint32_t n = 256;
    auto icon = render(d, b, icf::Appearance::Light, n);
    REQUIRE(icon.has_value());
    CHECK(icon->backgroundPainted);
    CHECK(hasNote(*icon, kDiscardedBackgroundOrientationNote));

    // Two rows both past 0.7 of the height. Under the orientation the document
    // names they would be identical.
    const Rgba a = at(*icon, n / 2, static_cast<std::uint32_t>(n * 0.75));
    const Rgba c = at(*icon, n / 2, n - 1);
    std::printf("  harnss rows 0.75 / 1.0: g %.5f vs %.5f\n", a.g, c.g);
    CHECK(std::fabs(a.g - c.g) > 0.005f);
}

// ---------------------------------------------------------------------------
// TASK 6: `automatic` on a LAYER, and `automatic-gradient`
// ---------------------------------------------------------------------------

// THE COMMON CASE, AND IT IS A NO-OP. `[ART]` 152 of the layer `automatic`'s
// 156 corpus firings terminate at nil, and the element then keeps the art's own
// colours. A renderer that answered a chiclet ramp here would repaint all 152.
TEST_CASE(a_layer_automatic_with_nothing_at_the_light_slot_leaves_the_art_alone) {
    Device& d = gpu();
    if (!d.valid()) return;
    const TempBundle b(oneLayer("\"fill\" : \"automatic\""));
    for (icf::Appearance a : kAppearances) {
        auto icon = render(d, b.path(), a, 32);
        REQUIRE(icon.has_value());
        CHECK_EQ(icon->drawn, std::size_t{1});
        CHECK(icon->skipped.empty());
        const Rgba c = at(*icon, 16, 16);
        // The art's own red, untouched. A grey here is the chiclet ramp
        // leaking into a position that must never see one.
        CHECK(near(c.r, 1.0f));
        CHECK(near(c.g, 0.0f));
        CHECK(near(c.b, 0.0f));
    }
}

// THE FOUR THAT INHERIT, in the shape the corpus gives them: a default
// specialization entry that names a colour and a `dark` entry that says
// `automatic`. Under `dark` the layer must paint what the LIGHT slot answers --
// and under `light` the same `automatic`, were it reached, would be nil.
//
// This is the case a test of the common path cannot reach, and the case an
// implementation that answered nil unconditionally would pass 152 times.
TEST_CASE(a_layer_automatic_copies_what_its_light_slot_resolves) {
    Device& d = gpu();
    if (!d.valid()) return;
    const TempBundle b(oneLayer(
        "\"fill-specializations\" : [\n"
        "            { \"value\" : { \"solid\" : \"srgb:0.00000,0.00000,1.00000,1.00000\" } },\n"
        "            { \"appearance\" : \"dark\", \"value\" : \"automatic\" }\n"
        "          ]"));

    // Light: the default entry, a blue solid over the red art.
    auto lit = render(d, b.path(), icf::Appearance::Light, 32);
    REQUIRE(lit.has_value());
    CHECK_EQ(lit->drawn, std::size_t{1});
    CHECK(near(at(*lit, 16, 16).b, 1.0f));
    CHECK(near(at(*lit, 16, 16).r, 0.0f));

    // Dark: `automatic`, which is not a colour of its own -- it is a verbatim
    // copy of what the light slot answered. So the SAME blue, and emphatically
    // not the dark chiclet ramp and not the art's red.
    auto dark = render(d, b.path(), icf::Appearance::Dark, 32);
    REQUIRE(dark.has_value());
    CHECK_EQ(dark->drawn, std::size_t{1});
    CHECK(dark->skipped.empty());
    CHECK(dark->rgba == lit->rgba);

    // THE CONTROL, and without it the test above is satisfied by an
    // implementation that simply resolved every layer fill at the light slot
    // and never read `automatic` at all -- which is a real mutation, and it
    // passed the two checks above until this was added. The same document with
    // the `dark` entry naming a colour of its own must come out green.
    const TempBundle c(oneLayer(
        "\"fill-specializations\" : [\n"
        "            { \"value\" : { \"solid\" : \"srgb:0.00000,0.00000,1.00000,1.00000\" } },\n"
        "            { \"appearance\" : \"dark\","
        " \"value\" : { \"solid\" : \"srgb:0.00000,1.00000,0.00000,1.00000\" } }\n"
        "          ]"));
    auto control = render(d, c.path(), icf::Appearance::Dark, 32);
    REQUIRE(control.has_value());
    CHECK(near(at(*control, 16, 16).g, 1.0f));
    CHECK(near(at(*control, 16, 16).b, 0.0f));
}

// AND THE CHAIN THAT MUST TERMINATE. `automatic` inheriting from an
// `automatic` ends at nil, because the light-slot pass maps `automatic` to nil
// as well. The terminator is read, not a depth limit invented here -- so the
// art keeps its colours instead of the renderer looping.
TEST_CASE(a_layer_automatic_inheriting_an_automatic_terminates_at_the_arts_colours) {
    Device& d = gpu();
    if (!d.valid()) return;
    const TempBundle b(oneLayer(
        "\"fill-specializations\" : [\n"
        "            { \"value\" : \"automatic\" },\n"
        "            { \"appearance\" : \"dark\", \"value\" : \"automatic\" }\n"
        "          ]"));
    auto dark = render(d, b.path(), icf::Appearance::Dark, 32);
    REQUIRE(dark.has_value());
    CHECK_EQ(dark->drawn, std::size_t{1});
    CHECK(near(at(*dark, 16, 16).r, 1.0f));   // the art's red
    CHECK(near(at(*dark, 16, 16).g, 0.0f));
}

// THE FOUR, BY NAME. `[ART]` Over the whole corpus exactly four layer
// `automatic` firings inherit a colour, and all four are the `CodeLines` layer
// of a `CodeEdit` icon under `dark`, inheriting an `automatic-gradient` of
// white. Rendered here so the count is not only a resolver's claim.
TEST_CASE(the_four_corpus_layers_whose_automatic_inherits_do_render) {
    Device& d = gpu();
    if (!d.valid()) return;
    const char* names[] = {"CodeEditApp__CodeEdit__CodeEditAlphaIcon",
                           "CodeEditApp__CodeEdit__CodeEditBetaIcon",
                           "CodeEditApp__CodeEdit__CodeEditDevIcon",
                           "CodeEditApp__CodeEdit__CodeEditPreIcon"};
    for (const char* name : names) {
        const fs::path b = corpus(name);
        if (!fs::is_directory(b / "Assets")) continue;   // document-only in the corpus

        auto tree = icf::json::parse(readAll(b / "icon.json"));
        REQUIRE(tree.has_value());
        auto doc = icf::IconDocument::open(*tree);
        REQUIRE(doc.has_value());

        icf::Context dark;
        dark.appearance = icf::Appearance::Dark;
        icf::Context light;
        light.appearance = icf::Appearance::Light;

        // The claim, at the resolver: this document HAS a layer whose
        // `automatic` inherits. If a future corpus edit removed it, this test
        // would otherwise keep passing while testing nothing.
        int inheriting = 0;
        for (const auto& g : doc->groups()) {
            for (const auto& l : g.layers()) {
                const FillResolution r = resolveLayerFill(l, dark);
                const FillResolution atLight = resolveLayerFill(l, light);
                const icf::json::Value* node = l.resolve("fill", dark);
                auto parsed = node ? icf::fillFrom(*node) : std::nullopt;
                if (!parsed || parsed->kind != icf::FillKind::Automatic) continue;
                if (r.outcome != FillOutcome::Resolved) continue;
                ++inheriting;
                CHECK(atLight.outcome == FillOutcome::Resolved);
                CHECK(identical(r.fill, atLight.fill));
            }
        }
        CHECK_EQ(inheriting, 1);

        auto icon = render(d, b, icf::Appearance::Dark, 32);
        REQUIRE(icon.has_value());
        for (const auto& s : icon->skipped) {
            CHECK(s.why.find("fill de camada") == std::string::npos);
        }
    }
}

// `automatic-gradient` DRAWS. Until today it was named and refused, because the
// stops were derivable and the AXIS had never been read. It has been: the
// converter passes `placement: nil` and nil is `(0,0)->(0,1)`.
//
// The pin is the geometry, not the colours -- `test_auto_gradient.cpp` owns the
// derivation. Every row flat, the columns changing: that is the default
// vertical axis and nothing else.
TEST_CASE(an_automatic_gradient_layer_draws_on_the_default_vertical_axis) {
    Device& d = gpu();
    if (!d.valid()) return;
    const std::uint32_t n = 64;
    const TempBundle b(oneLayer(
        "\"fill\" : { \"automatic-gradient\" : \"srgb:0.20000,0.40000,0.80000,1.00000\" }"));
    auto icon = render(d, b.path(), icf::Appearance::Light, n);
    REQUIRE(icon.has_value());
    CHECK_EQ(icon->drawn, std::size_t{1});
    CHECK(icon->skipped.empty());     // it was a SKIP until the axis was read
    CHECK(hasNote(*icon, kGradientAxisDirectionNote));

    for (std::uint32_t y = 0; y < n; ++y) {
        CHECK(near(at(*icon, 1, y).r, at(*icon, n - 2, y).r, 0.001f));
    }
    // Two stops that differ, so the ramp is a ramp.
    CHECK(std::fabs(at(*icon, n / 2, 1).r - at(*icon, n / 2, n - 2).r) > 0.001f);
    // And it is not the art's red, which is what "not overridden" would give.
    CHECK(at(*icon, n / 2, n / 2).r < 0.9f);
}

// An `automatic-gradient` that carries an `orientation` -- `[ART]` 8 corpus
// fills do -- must draw exactly as one that does not. All three
// `automaticGradient` sites pass nil.
TEST_CASE(an_automatic_gradients_orientation_changes_nothing_anywhere) {
    Device& d = gpu();
    if (!d.valid()) return;
    const TempBundle plain(oneLayer(
        "\"fill\" : { \"automatic-gradient\" : \"srgb:0.20000,0.40000,0.80000,1.00000\" }"));
    const TempBundle oriented(oneLayer(
        "\"fill\" : { \"automatic-gradient\" : \"srgb:0.20000,0.40000,0.80000,1.00000\","
        " \"orientation\" : { \"start\" : { \"x\" : 0, \"y\" : 0 },"
        " \"stop\" : { \"x\" : 1, \"y\" : 0 } } }"));
    auto a = render(d, plain.path(), icf::Appearance::Light, 32);
    auto b = render(d, oriented.path(), icf::Appearance::Light, 32);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->rgba == b->rgba);
}

// THE ONE SITE OF FOUR THAT DOES READ IT. The layer's `linear-gradient` builds
// its placement from the document's orientation, so being less clever on the
// background must not have made the renderer less clever here.
TEST_CASE(a_layer_linear_gradient_still_honours_its_orientation) {
    Device& d = gpu();
    if (!d.valid()) return;
    const std::uint32_t n = 64;
    const TempBundle b(oneLayer(
        "\"fill\" : { \"linear-gradient\" : [ \"srgb:0.00000,0.00000,0.00000,1.00000\","
        " \"srgb:1.00000,1.00000,1.00000,1.00000\" ],"
        " \"orientation\" : { \"start\" : { \"x\" : 0, \"y\" : 0 },"
        " \"stop\" : { \"x\" : 1, \"y\" : 0 } } }"));
    auto icon = render(d, b.path(), icf::Appearance::Light, n);
    REQUIRE(icon.has_value());
    CHECK_EQ(icon->drawn, std::size_t{1});
    // Horizontal, as asked: the columns change and the rows do not.
    CHECK(std::fabs(at(*icon, 1, n / 2).r - at(*icon, n - 2, n / 2).r) > 0.9f);
    CHECK(near(at(*icon, n / 2, 1).r, at(*icon, n / 2, n - 2).r, 0.01f));
    // A placement was read, so the direction gap does not apply to it.
    CHECK(!hasNote(*icon, kGradientAxisDirectionNote));
}

// `[ART]` 26 of the corpus's 48 layer `linear-gradient` fills name no
// orientation, and until today that was a refusal. The nil now means what the
// binary says it means, so they draw -- vertically.
TEST_CASE(a_layer_linear_gradient_without_an_orientation_draws_on_the_default_axis) {
    Device& d = gpu();
    if (!d.valid()) return;
    const std::uint32_t n = 64;
    const TempBundle b(oneLayer(
        "\"fill\" : { \"linear-gradient\" : [ \"srgb:0.00000,0.00000,0.00000,1.00000\","
        " \"srgb:1.00000,1.00000,1.00000,1.00000\" ] }"));
    auto icon = render(d, b.path(), icf::Appearance::Light, n);
    REQUIRE(icon.has_value());
    CHECK_EQ(icon->drawn, std::size_t{1});
    CHECK(icon->skipped.empty());
    CHECK(std::fabs(at(*icon, n / 2, 1).r - at(*icon, n / 2, n - 2).r) > 0.9f);
    CHECK(near(at(*icon, 1, n / 2).r, at(*icon, n - 2, n / 2).r, 0.01f));
    CHECK(hasNote(*icon, kGradientAxisDirectionNote));
}

// ---------------------------------------------------------------------------
// The gaps that are NAMED rather than filled
// ---------------------------------------------------------------------------

// `[BIN]` `supportsChicletAlignmentForSystemFills` (`ICRRenderingParameters
// +0x360`): set, a `.system` fill's rect is origin (0,0) with the canvas's size
// (`0x1BA98`-`0x1BAF4`) instead of the shape's bounding box; clear, it is the
// box. Generation 27 sets it and generation 26 clears it (`0x7707C`).
//
// Until 2026-10-01 this case pinned the OPPOSITE for generation 27 -- "drawn on
// the bounding rect, and says so" -- because the size the aligned branch takes
// had not been read and `systemFillRect` refused to answer. It is read; the
// note that named the gap is gone with it.
//
// The fixture is a layer at half scale, so its box is the middle half of the
// canvas (rows 16..47 of 64). Aligned, the layer shows the MIDDLE half of a
// ramp that spans the canvas: neither end grey, and half the ramp's span from
// its top row to its bottom row. Unaligned, the whole ramp fits the box: both
// end greys, the generation's own.
TEST_CASE(a_layer_system_fill_spans_the_canvas_in_27_and_its_own_box_in_26) {
    Device& d = gpu();
    if (!d.valid()) return;
    const std::uint32_t n = 64;
    const TempBundle b(oneLayer(
        "\"position\" : { \"scale\" : 0.5, \"translation-in-points\" : [0, 0] },\n"
        "          \"fill\" : \"system-dark\""));
    auto bundle = icf::IconBundle::open(b.path());
    REQUIRE(bundle.has_value());
    auto draw = [&](DesignGeneration g) {
        IconRenderOptions o;
        o.size = n;
        o.context.appearance = icf::Appearance::Light;
        o.generation = g;
        return renderIcon(d, *bundle, o);
    };
    const std::uint32_t topRow = 17, bottomRow = 46, x = n / 2;

    auto g27 = draw(DesignGeneration::G27);
    REQUIRE(g27.has_value());
    CHECK_EQ(g27->drawn, std::size_t{1});
    CHECK(near(at(*g27, x, topRow).a, 1.0f));
    CHECK(at(*g27, x, 8).a == 0.0f);              // above the layer: nothing
    const float t27 = at(*g27, x, topRow).r, b27 = at(*g27, x, bottomRow).r;
    // Strictly inside the ramp at both rows...
    CHECK(std::min(t27, b27) > kDark1 + 0.008f);
    CHECK(std::max(t27, b27) < kDark0 - 0.008f);
    // ...and about half of its span between them (31 -> 15 over the canvas,
    // the layer covering rows 17..46 of 64).
    const float span27 = kDark0 - kDark1;
    CHECK(std::fabs(t27 - b27) > 0.3f * span27);
    CHECK(std::fabs(t27 - b27) < 0.7f * span27);
    CHECK(hasNote(*g27, kGradientAxisDirectionNote));

    auto g26 = draw(DesignGeneration::G26);
    REQUIRE(g26.has_value());
    CHECK_EQ(g26->drawn, std::size_t{1});
    const float t26 = at(*g26, x, topRow).r, b26 = at(*g26, x, bottomRow).r;
    // Generation 26's dark ramp, 49 -> 20, end to end across the layer's box.
    CHECK(endsAre(t26, b26, 49.0f / 255.0f, 20.0f / 255.0f));

    // The background is the canvas either way, and it never carried a gap of
    // its own: a solid one provokes no ramp and no gap sentence at all.
    const TempBundle flat(backgroundOnly(
        "{ \"solid\" : \"srgb:0.50000,0.50000,0.50000,1.00000\" }"));
    auto plain = render(d, flat.path(), icf::Appearance::Light, 16);
    REQUIRE(plain.has_value());
    CHECK(!hasNote(*plain, kGradientAxisDirectionNote));
    CHECK(hasNote(*plain, kBackgroundShapeNote));   // the chiclet's shape, always
}

// The layer's fill reaches the vector path only. `[ART]` `GlowGetter` names
// three PNG layers, every one of them carrying a solid fill, so the divergence
// is real and in the corpus rather than hypothetical.
TEST_CASE(a_fill_on_raster_art_is_named_rather_than_dropped_in_silence) {
    Device& d = gpu();
    if (!d.valid()) return;
    auto icon = render(d, corpus("Aeastr__GlowGetter__icon"), icf::Appearance::Light, 32);
    REQUIRE(icon.has_value());
    CHECK_EQ(icon->drawn, std::size_t{5});    // still drawn: a wrong colour beats a hole
    CHECK(hasNote(*icon, kRasterFillNote));
}

// ---------------------------------------------------------------------------
// THE SWEEP: every fill of every document, in all four appearances
// ---------------------------------------------------------------------------

// No GPU: what is being swept is the arithmetic between a resolution and a
// paint, and a sweep that needed a device would not run where it is most
// wanted. `[ART]` 145 documents, 580 background resolutions and 1748 layer
// resolutions.
//
// The assertion that carries the most weight is the last one: EVERY background
// gradient in the corpus, under every appearance, must come out on exactly the
// default vertical axis -- start (0,0), end (0,h) -- however many of the 61
// orientations the documents name.
TEST_CASE(corpus_every_fill_becomes_paint_without_a_refusal) {
    auto docs = corpusDocuments();
    REQUIRE(!docs.empty());   // IC_CORPUS_DIR unset or empty

    const std::uint32_t size = 512;
    const PlacementRect canvas{0.0, 0.0, static_cast<double>(size),
                               static_cast<double>(size)};
    // A layer's art box: deliberately NOT the canvas, so a paint that silently
    // ignored the rect it was handed would still be swept over a rect that
    // differs from the background's.
    const PlacementRect art{64.0, 96.0, 320.0, 256.0};

    int documents = 0, backgrounds = 0, layers = 0;
    int backgroundRamps = 0, layerRamps = 0, layerSolids = 0, layerNil = 0;
    int backgroundOrientationsNamed = 0, layerAxesFromTheDocument = 0;
    std::vector<std::string> refusals;
    std::map<std::string, int> offAxis;

    for (const auto& p : docs) {
        const std::string name = p.parent_path().filename().string();
        auto tree = icf::json::parse(readAll(p));
        if (!tree) continue;
        auto doc = icf::IconDocument::open(*tree);
        if (!doc) continue;
        ++documents;

        for (icf::Appearance a : kAppearances) {
            icf::Context ctx;
            ctx.appearance = a;

            const FillResolution bg = resolveBackgroundFill(*tree, ctx);
            ++backgrounds;
            if (bg.outcome == FillOutcome::Refused) {
                refusals.push_back(name + " background/" + appearanceName(a) + ": " + bg.why);
            } else if (bg.outcome == FillOutcome::Resolved) {
                std::string why;
                const FillOverride paint = fillPaint(bg.fill, canvas, why);
                if (!why.empty()) {
                    refusals.push_back(name + " background/" + appearanceName(a) +
                                       " paint: " + why);
                } else if (paint.kind == FillOverride::Kind::Ramp) {
                    ++backgroundRamps;
                    // THE AXIS, EXACTLY. `(0,0) -> (0,h)` maps a pixel to
                    // `y / h`: no x term, no offset. An orientation that
                    // survived would move at least one of the three.
                    if (paint.m[0] != 0.0 || paint.m[2] != 0.0 ||
                        std::fabs(paint.m[1] - 1.0 / canvas.height) > 1e-12) {
                        offAxis[name + "/" + appearanceName(a)]++;
                    }
                }
                if (const icf::json::Value* node = icf::resolve(*tree, "fill", ctx)) {
                    auto parsed = icf::fillFrom(*node);
                    if (parsed && parsed->orientation) ++backgroundOrientationsNamed;
                }
            }

            for (const auto& g : doc->groups()) {
                for (const auto& l : g.layers()) {
                    const FillResolution lf = resolveLayerFill(l, ctx);
                    ++layers;
                    if (lf.outcome == FillOutcome::Refused) {
                        refusals.push_back(name + " layer '" + std::string(l.name()) + "'/" +
                                           appearanceName(a) + ": " + lf.why);
                        continue;
                    }
                    if (lf.outcome == FillOutcome::NoFill) {
                        ++layerNil;
                        continue;
                    }
                    std::string why;
                    const FillOverride paint = fillPaint(lf.fill, art, why);
                    if (!why.empty()) {
                        refusals.push_back(name + " layer '" + std::string(l.name()) + "'/" +
                                           appearanceName(a) + " paint: " + why);
                        continue;
                    }
                    if (paint.kind == FillOverride::Kind::Ramp) {
                        ++layerRamps;
                        if (lf.fill.placement) ++layerAxesFromTheDocument;
                    } else if (paint.kind == FillOverride::Kind::Solid) {
                        ++layerSolids;
                    }
                }
            }
        }
    }

    CHECK_EQ(documents, 145);
    CHECK_EQ(backgrounds, 580);
    CHECK_EQ(layers, 1748);
    CHECK_EQ(static_cast<int>(refusals.size()), 0);
    for (std::size_t i = 0; i < refusals.size() && i < 10; ++i) {
        std::printf("  REFUSED %s\n", refusals[i].c_str());
    }

    // §5.2 in pixels: not one background gradient escapes the default axis.
    CHECK_EQ(static_cast<int>(offAxis.size()), 0);
    for (const auto& [where, n] : offAxis) std::printf("  OFF AXIS %s x%d\n", where.c_str(), n);

    // And the documents DID ask: 61 background resolutions name an orientation
    // that every one of those axes threw away. Without this the check above
    // would also pass over a corpus that named none.
    CHECK_EQ(backgroundOrientationsNamed, 61);

    // The layer, which is the one site of four that reads the orientation, must
    // still be reading it.
    CHECK_EQ(layerAxesFromTheDocument, 45);

    std::printf("  paint sweep    %d documents, %d background + %d layer resolutions,"
                " %d refusals\n",
                documents, backgrounds, layers, static_cast<int>(refusals.size()));
    std::printf("  background     %d ramps, all on the default vertical axis"
                " (%d named an orientation)\n",
                backgroundRamps, backgroundOrientationsNamed);
    std::printf("  layer          %d ramps (%d with the document's own axis), %d solids,"
                " %d nil\n",
                layerRamps, layerAxesFromTheDocument, layerSolids, layerNil);
}

// END TO END, on the documents this work exists for. Every bundle that names an
// `automatic` anywhere, in all four appearances: no error, no layer lost, a
// background that is either painted or named, and no skip that mentions a fill.
TEST_CASE(corpus_automatic_bundles_render_in_every_appearance) {
    Device& d = gpu();
    if (!d.valid()) return;
    const char* dir = std::getenv("IC_CORPUS_DIR");
    REQUIRE(dir && *dir);

    int rendered = 0, painted = 0, gaps = 0;
    std::vector<std::string> offenders;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(fs::path(dir), ec)) {
        if (!fs::is_directory(e.path() / "Assets", ec)) continue;
        const std::string text = readAll(e.path() / "icon.json");
        if (text.find("automatic") == std::string::npos) continue;

        for (icf::Appearance a : kAppearances) {
            auto icon = render(d, e.path(), a, 32);
            if (!icon) {
                offenders.push_back(e.path().filename().string());
                continue;
            }
            ++rendered;
            // No layer goes missing, the way the compositor's own gate counts.
            if (icon->drawn + icon->skipped.size() > icon->total) {
                offenders.push_back(e.path().filename().string() + ": more outcomes than layers");
            }
            // `[ART]` Every corpus document carries a root `fill`, so a
            // background that is neither painted nor named is a silent default.
            if (icon->backgroundPainted) {
                ++painted;
            } else if (!icon->backgroundGap.empty()) {
                ++gaps;
            } else {
                offenders.push_back(e.path().filename().string() + "/" + appearanceName(a) +
                                    ": background neither painted nor named");
            }
            for (const auto& s : icon->skipped) {
                if (s.why.find("fill de camada") != std::string::npos ||
                    s.why.find("fill de raiz") != std::string::npos) {
                    offenders.push_back(e.path().filename().string() + ": " + s.why);
                }
            }
        }
    }
    std::printf("  %d renders of automatic-bearing bundles: %d backgrounds painted,"
                " %d named, %zu offender(s)\n",
                rendered, painted, gaps, offenders.size());
    for (const auto& o : offenders) std::printf("    %s\n", o.c_str());
    CHECK(rendered > 0);
    CHECK(offenders.empty());
    CHECK_EQ(gaps, 0);
}
