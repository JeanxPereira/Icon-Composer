// THE SUITE'S ONLY ASSERTION ABOUT TIME.
//
// WHY IT EXISTS
// -------------
// On 2026-09-15 six fronts landed in one afternoon. Every one of them shipped a
// pixel proof and a green suite, and together they took a 1024 px glass render
// from about one second to eighty-seven. Nobody noticed, and nothing could have:
// the 649 cases that ran that day assert correctness and nothing else. There is
// no failure to report when a render is merely nine hundred times too slow --
// the arithmetic is right, the picture is right, and the user's editor shows an
// empty canvas because the work does not finish inside a human's patience.
//
// That is the hole this file closes. It is not a benchmark and it is not a
// performance gate. It is a CATASTROPHE ceiling: a number so generous that
// ordinary machine noise cannot reach it, and so far below a 90x regression that
// nothing of that shape can pass. See `Docs/Laudos/2026-09-15-orcamento-de-tempo.md`.
//
// WHY THE CEILING IS DECLARED PER BUILD CONFIGURATION
// ---------------------------------------------------
// The suite is normally run from the `mingw` preset, whose `CMAKE_BUILD_TYPE` is
// `Debug`. The ratio is measured, not assumed -- see the laudo, section 4, for
// the two runs side by side.
//
// A single number would therefore either be a lie in Debug (unreachable, so it
// never bites) or a lie in Release (reachable, so it flakes). The ceilings below
// are written in RELEASE seconds -- the configuration a user's render actually
// runs in -- and multiplied by `kBuildFactor` when `NDEBUG` is absent. `NDEBUG`
// is the right discriminator and not a guess: CMake's `Release` adds `-DNDEBUG`
// and its `Debug` does not, so the constant follows the build that produced the
// binary rather than an environment variable somebody has to remember to set.
//
// WHY THE SLACK IS SO LARGE
// -------------------------
// The machine is shared. Another worktree builds while this runs, the GPU has an
// editor on it, the page cache is cold on the first touch of an asset. Measured
// dispersion (laudo section 3): three interleaved repeats of the same 1024 px
// glass render came in at 10.51 / 10.51 / 10.85 s -- 1.6 per cent apart -- but
// the FIRST run of that same render, taken minutes earlier while a build was
// finishing, was 19.26 s. That is 1.8x, from nothing but a busy machine.
//
// So the slack has to absorb a factor of two and still catch ninety. `kSlack`
// is 8: a render four times slower than a loaded machine's worst observed
// showing still passes, and the regression that started this -- 1 s to 87 s --
// misses by more than an order of magnitude. A ceiling nobody trusts is a
// ceiling everybody learns to re-run until it is green, which is worse than no
// ceiling at all, because it also costs the time.
//
// HOW TO PROVE IT STILL BITES (the negative control)
// --------------------------------------------------
// A ceiling that has never gone red is a ceiling that measures nothing.
//
//     IC_TIME_BUDGET_CONTROL=1 ic_tests time_budget_
//
// renders every gated case at FOUR TIMES the side -- 2048 px instead of 512,
// sixteen times the pixels -- against the same 512 px ceiling. That is a real
// render down the real pipeline, not a fake number handed to the comparison, and
// sixteen is twice the slack, so all three cases go red and no other case in the
// suite moves. The run is in the laudo, section 5.
//
// The knob is the render size and not the shadow's sigma for a boring reason:
// the sigma lives in `Source/RenderBox/BlurKernel.*` and `GlassShadow.*`, which
// a sibling front was rewriting the same afternoon. What the control has to
// prove is that a render which genuinely takes far too long turns this suite
// red, and a canvas sixteen times larger proves exactly that.
#include "check.h"

#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/RenderBox/IconRenderer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace rb;

namespace {

namespace fs = std::filesystem;

// ---- the budget ---------------------------------------------------------

#if defined(NDEBUG)
constexpr double kBuildFactor = 1.0;
constexpr const char* kBuildName = "Release";
#else
// MEASURED, not assumed, and NOT the 5.6x this front was handed. The same three
// fixtures, the same corpus, whole-suite runs of both builds (laudo section 4):
//
//     fixture                       Release     Debug    ratio
//     no glass, 64 layers, 512       0.263s     0.602s    2.3x
//     full glass chain, 4, 512       0.229s     1.914s    8.4x
//     corpus, 10 glass layers, 512   2.218s    16.181s    7.3x
//
// The ratio is NOT one number -- it runs 2.3x to 8.4x here, and a filtered run
// of the glass case earlier put it at 11.2x. Debug taxes the inner loops (the
// distance field's exact EDT) far harder than it taxes the parse and the
// composite around them, so the fixture with the most field per second has the
// worst ratio. 12 is the worst seen, rounded up, so the Debug ceiling is never
// TIGHTER in real terms than the Release one it is derived from. It makes the
// floor case's Debug ceiling very loose indeed -- 22 s against a 0.6 s
// measurement -- and that is the right trade: the cheap case exists to say "the
// glass chain is not what broke", and a 90x regression on it still lands at 54 s,
// well over.
constexpr double kBuildFactor = 12.0;
constexpr const char* kBuildName = "Debug (NDEBUG absent)";
#endif

// See the header note. Eight, because the machine alone has been seen to move a
// render by 1.8x and a catastrophe moves it by ninety.
constexpr double kSlack = 8.0;

// The preview size the app actually renders at (`Session::view.size`), so the
// number gated here is the number the user waits for.
constexpr std::uint32_t kPreviewSize = 512;

// The negative control multiplies the SIDE by this, so the pixel count by its
// square. Four, because sixteen is twice `kSlack` and a control that only just
// clears the ceiling proves nothing anybody would trust.
constexpr std::uint32_t kControlScale = 4;

std::uint32_t renderSize() {
    const char* v = std::getenv("IC_TIME_BUDGET_CONTROL");
    const bool control = v && v[0] == '1';
    return control ? kPreviewSize * kControlScale : kPreviewSize;
}

double ceilingFor(double releaseSeconds) { return releaseSeconds * kSlack * kBuildFactor; }

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

// A render, timed the same way `icrender` times one: the clock is around
// `renderIcon` and nothing else. The bundle is opened outside it, because a cold
// page cache on an asset is not the renderer's cost and would be the single
// noisiest thing in this file.
//
// `warmUp` runs the render once and throws it away first. The device is a
// function-local static, so WITHOUT this the first budget case in the file would
// pay Vulkan's device and pipeline creation inside its own stopwatch -- and that
// case is the cheapest one, with the tightest absolute ceiling. Paying it twice
// costs a few tens of milliseconds and removes the only systematic error in the
// file; the corpus case does not warm up because by then the device is warm and
// a second 2.6 s render would be the most expensive thing in the suite.
double timeRender(const icf::IconBundle& bundle, std::uint32_t size, std::size_t& drawn,
                  bool warmUp) {
    Device& device = gpu();   // hoisted OUT of the timed region on purpose
    IconRenderOptions io;
    io.size = size;
    if (warmUp) (void)renderIcon(device, bundle, io);
    const auto t0 = std::chrono::steady_clock::now();
    auto icon = renderIcon(device, bundle, io);
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    drawn = icon ? icon->drawn : 0;
    return s;
}

// `what` is named in the failure so a red line says WHICH render blew up without
// anyone having to read this file to find out.
void expectUnder(const char* what, double seconds, double releaseBudget,
                 std::size_t drawn) {
    const double ceiling = ceilingFor(releaseBudget);
    // PRINTED WHETHER OR NOT IT PASSES. A budget nobody can see is a budget
    // nobody recalibrates: the day the renderer legitimately gets faster or
    // slower, the person editing this file needs the measurement, and going and
    // fetching it by hand is exactly the step that does not happen. It is also
    // the only place in the suite where a reader learns what a render costs.
    std::printf("  [budget] %-58s %7.3f s  of %7.3f s\n", what, seconds, ceiling);
    if (seconds <= ceiling) return;
    std::printf("  FAIL time budget: %s took %.3f s, ceiling %.3f s (%s: %.2f s "
                "measured in Release x %.0f slack x %.1f build factor); %zu layer(s) drawn\n",
                what, seconds, ceiling, kBuildName, releaseBudget, kSlack, kBuildFactor,
                drawn);
    std::printf("    This is a CATASTROPHE ceiling, not a performance one. Being over it\n"
                "    does not mean the render got a little slower -- it means it got\n"
                "    slower by a factor the machine cannot explain. See\n"
                "    Docs/Laudos/2026-09-15-orcamento-de-tempo.md.\n");
    ++ictest::failures();
}

// ---- the fixtures -------------------------------------------------------

class TempBundle {
public:
    explicit TempBundle(const std::string& name, const std::string& document) {
        dir_ = fs::temp_directory_path() / ("ic-budget-" + name);
        std::error_code ec;
        fs::remove_all(dir_, ec);
        fs::create_directories(dir_ / "Assets", ec);
        write(dir_ / "icon.json", document);
        // A ring: a filled outer circle with a circular hole, as two subpaths in
        // opposite winding. It is deliberately NOT a rectangle -- the distance
        // field, the five specular highlights and the shadow's ring mask all key
        // off a contour, and a square's field is degenerate enough that a
        // regression in any of the three could hide in it.
        write(dir_ / "Assets" / "ring.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\">"
              "<path d=\"M256 32 A224 224 0 1 1 255 32 Z"
              " M256 160 A96 96 0 1 0 257 160 Z\" fill=\"#ffffff\"/></svg>");
    }
    ~TempBundle() {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    const fs::path& path() const { return dir_; }

private:
    static void write(const fs::path& p, const std::string& text) {
        std::FILE* f = std::fopen(p.string().c_str(), "wb");
        if (!f) return;
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }
    fs::path dir_;
};

std::string layers(int count, bool glass) {
    std::string out;
    for (int i = 0; i < count; ++i) {
        if (i) out += ",\n";
        out += "        { \"image-name\" : \"ring.svg\", \"name\" : \"l" +
               std::to_string(i) + "\", \"glass\" : " + (glass ? "true" : "false") + " }";
    }
    return out;
}

// HOW MANY LAYERS EACH SYNTHETIC FIXTURE CARRIES, AND WHY IT IS NOT ONE.
//
// A single layer of either kind measures 0.010 s / 0.062 s in Release, and a
// ceiling built on ten milliseconds is not a ceiling -- it is a jitter detector.
// The OS can lose a 10 ms measurement several times over to scheduling alone,
// and `kSlack` would then be spent on noise instead of on headroom.
//
// The counts were raised until the baseline sat at about 0.22 s in Release,
// where eight times it means something, AND where the negative control's
// sixteen-fold canvas clears the ceiling by a wide margin. The first attempt
// used 1 layer and 1 layer; the second used 16 and 4 and was calibrated from
// STANDALONE runs, which is the mistake worth recording: the same case measured
// 0.062 s alone and 0.118 s inside the whole suite, on a warm device with 626
// other cases behind it. The suite is the condition the ceiling actually runs
// in, so the numbers below come from whole-suite runs and never from a filtered
// one.
constexpr int kPlainLayers = 64;
constexpr int kGlassLayers = 4;

// No glass at all: the floor of the whole pipeline -- parse, place, rasterise,
// composite, and the background. If THIS ever blows its ceiling the problem is
// not the glass chain, and the two cases together say which it is.
std::string plainDocument() {
    return "{\n  \"fill\" : { \"solid\" : \"srgb:0.10000,0.20000,0.40000,1.00000\" },\n"
           "  \"groups\" : [\n    {\n      \"layers\" : [\n" +
           layers(kPlainLayers, false) + "\n      ]\n    }\n  ]\n}\n";
}

// THE COMPLETE CHAIN, with every knob the corpus actually carries turned on:
// a neutral shadow at the corpus's modal opacity, a specular, a translucency,
// and a positive `blur-material`. `[ART]` The numbers are Apollo's, verbatim.
std::string glassDocument(int layerCount) {
    return "{\n  \"fill\" : { \"solid\" : \"srgb:0.10000,0.20000,0.40000,1.00000\" },\n"
           "  \"groups\" : [\n    {\n"
           "      \"shadow\" : { \"kind\" : \"neutral\", \"opacity\" : 0.6 },\n"
           "      \"specular\" : true,\n"
           "      \"translucency\" : { \"enabled\" : true, \"value\" : 0.2 },\n"
           "      \"blur-material\" : 0.24,\n"
           "      \"layers\" : [\n" +
           layers(layerCount, true) + "\n      ]\n    }\n  ]\n}\n";
}

// The corpus bundle with the most GLASS layers -- the one closest in shape to
// what a real user opens, and the one where every per-layer cost is multiplied.
// Chosen by measuring the corpus rather than by naming a file, so the choice
// survives the corpus growing; the name is printed when the case fails, because
// "the budget blew" is not actionable without it.
fs::path heaviestCorpusBundle(std::size_t& glassLayers) {
    const char* dir = std::getenv("IC_CORPUS_DIR");
    glassLayers = 0;
    if (!dir || !*dir) return {};
    fs::path best;
    std::error_code ec;
    std::vector<fs::path> bundles;
    for (const auto& b : fs::directory_iterator(fs::path(dir), ec)) {
        if (fs::is_directory(b.path() / "Assets", ec)) bundles.push_back(b.path());
    }
    std::sort(bundles.begin(), bundles.end());   // ties broken by name, so it is stable
    for (const auto& p : bundles) {
        auto bundle = icf::IconBundle::open(p);
        if (!bundle) continue;
        std::size_t n = 0;
        auto doc = bundle->document();
        for (const auto& g : doc.groups()) {
            for (const auto& l : g.layers()) {
                const icf::json::Value* glass = l.resolve("glass", icf::Context{});
                const bool isGlass = !glass ||
                                     glass->kind() != icf::json::Value::Kind::Bool ||
                                     glass->boolean();
                if (isGlass && l.resolve("image-name", icf::Context{})) ++n;
            }
        }
        if (n > glassLayers) {
            glassLayers = n;
            best = p;
        }
    }
    return best;
}

}  // namespace

// ---- the three gated renders --------------------------------------------

// `[INF]` 0.230 s, the worst of six whole-suite runs in Release. The
// floor case: no glass anywhere, sixteen layers, the background painted.
TEST_CASE(time_budget_no_glass_512) {
    TempBundle b("plain", plainDocument());
    auto bundle = icf::IconBundle::open(b.path());
    REQUIRE(bundle.has_value());
    std::size_t drawn = 0;
    const double s = timeRender(*bundle, renderSize(), drawn, /*warmUp=*/true);
    CHECK_EQ(drawn, static_cast<std::size_t>(kPlainLayers));
    expectUnder("no glass, 64 layers, 512 px", s, 0.230, drawn);
}

// `[INF]` 0.310 s, the worst of six whole-suite runs in Release. Four glass layers with the whole chain on
// each: distance field, refraction gate, translucency mask, five specular
// highlights, and the shadow.
TEST_CASE(time_budget_full_glass_chain_512) {
    TempBundle b("glass", glassDocument(kGlassLayers));
    auto bundle = icf::IconBundle::open(b.path());
    REQUIRE(bundle.has_value());
    std::size_t drawn = 0;
    const double s = timeRender(*bundle, renderSize(), drawn, /*warmUp=*/true);
    CHECK_EQ(drawn, static_cast<std::size_t>(kGlassLayers));
    expectUnder("full glass chain, 4 layers, 512 px", s, 0.310, drawn);
}

// `[INF]` 2.800 s, the worst of six whole-suite runs in Release, on `Apollo-Reborn__Apollo-Reborn__AppIcon`,
// which is the corpus's heaviest at ten glass layers. This is the case that
// stands closest to what the user saw: a real document, every layer paying the
// whole chain.
TEST_CASE(time_budget_corpus_heaviest_512) {
    std::size_t glassLayers = 0;
    const fs::path p = heaviestCorpusBundle(glassLayers);
    // IC_CORPUS_DIR unset or empty. A budget case that SKIPPED here would be the
    // same hole in a smaller shape, so it fails like every other corpus case in
    // this suite.
    REQUIRE(!p.empty());
    // Ten today. A corpus that no longer contains a document this heavy would
    // silently turn the ceiling into a much looser one, so the shape is pinned
    // as well as the time.
    CHECK(glassLayers >= 8);
    auto bundle = icf::IconBundle::open(p);
    REQUIRE(bundle.has_value());
    std::size_t drawn = 0;
    const double s = timeRender(*bundle, renderSize(), drawn, /*warmUp=*/false);
    const std::string what = "corpus " + p.filename().string() + ", " +
                             std::to_string(glassLayers) + " glass layer(s), 512 px";
    expectUnder(what.c_str(), s, 2.800, drawn);
}
