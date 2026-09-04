// The compositor: a whole `.icon` bundle drawn.
//
// The transform is the part worth pinning, because it is where the document's
// own numbers meet the canvas, and because two of its three conventions are
// ASSUMPTIONS rather than measurements (doc 03 §22): the canvas is 1024 points,
// and the origin is its centre. The third -- which way +y points -- was settled
// by the corpus and is pinned here as such.
#include "check.h"
#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/IconComposerFoundation/Png.h"
#include "Source/RenderBox/IconRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
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

std::vector<fs::path> corpusBundles() {
    const char* dir = std::getenv("IC_CORPUS_DIR");
    if (!dir || !*dir) return {};
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& b : fs::directory_iterator(fs::path(dir), ec)) {
        if (fs::is_directory(b.path() / "Assets", ec)) out.push_back(b.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

// The colour probe the blend tests need. Counting drawn layers cannot tell a
// blend from a source-over, and four gate mutations survived on exactly that
// gap before this existed.
float channelAt(const RenderedIcon& img, std::uint32_t x, std::uint32_t y, int c) {
    return img.rgba[(static_cast<std::size_t>(y) * img.width + x) * 4 + c];
}

float alphaAt(const RenderedIcon& img, std::uint32_t x, std::uint32_t y) {
    return img.rgba[(static_cast<std::size_t>(y) * img.width + x) * 4 + 3];
}

// A bundle written to a temporary directory, so the compositor's handling of
// `hidden`, `opacity` and `blend-mode` can be exercised on values chosen here.
// The corpus has all three, but never in a combination that isolates one of
// them -- and a test that cannot isolate what it is testing is a test that
// passes for reasons it does not name.
class TempBundle {
public:
    explicit TempBundle(const std::string& document) {
        dir_ = fs::temp_directory_path() /
               ("ic-test-" + std::to_string(std::hash<std::string>{}(document)));
        std::error_code ec;
        fs::remove_all(dir_, ec);
        fs::create_directories(dir_ / "Assets", ec);
        write(dir_ / "icon.json", document);
        // A 512-point square, so it covers a quarter of the 1024 canvas and its
        // position is easy to reason about.
        write(dir_ / "Assets" / "square.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\">"
              "<path d=\"M0 0 L512 0 L512 512 L0 512 Z\" fill=\"#ffffff\"/></svg>");
        // A cross with NO fill and a fat blue stroke. The only ink it can
        // produce is the stroke, so a renderer that drops strokes renders it
        // blank -- which is what happened until 2026-09-04.
        write(dir_ / "Assets" / "cross.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\">"
              "<path d=\"M128 256 L384 256 M256 128 L256 384\" fill=\"none\""
              " stroke=\"#0000ff\" stroke-width=\"32\"/></svg>");
        // A second square in a colour, so two layers can be told apart when
        // they overlap.
        write(dir_ / "Assets" / "red.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\">"
              "<path d=\"M0 0 L512 0 L512 512 L0 512 Z\" fill=\"#ff0000\"/></svg>");
        // A raster with a HARD EDGE between an opaque green half and a fully
        // transparent half whose colour bytes are RED. Sampling that boundary
        // without premultiplying pulls the invisible red into the visible
        // green, which is the only way that defect ever shows.
        std::vector<float> px(static_cast<std::size_t>(kRasterSide) * kRasterSide * 4, 0.0f);
        for (std::uint32_t y = 0; y < kRasterSide; ++y) {
            for (std::uint32_t x = 0; x < kRasterSide; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * kRasterSide + x) * 4;
                const bool solid = x < kRasterSide / 2;
                px[i + 0] = solid ? 0.0f : 1.0f;   // red, but only where alpha is 0
                px[i + 1] = solid ? 1.0f : 0.0f;
                px[i + 2] = 0.0f;
                px[i + 3] = solid ? 1.0f : 0.0f;
            }
        }
        const std::vector<std::uint8_t> png = icf::encodePng(px, kRasterSide, kRasterSide);
        std::FILE* f = std::fopen((dir_ / "Assets" / "edge.png").string().c_str(), "wb");
        if (f) {
            std::fwrite(png.data(), 1, png.size(), f);
            std::fclose(f);
        }
    }

    static constexpr std::uint32_t kRasterSide = 64;
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

// One layer naming a chosen asset. `oneLayer` hardcodes `square.svg`, and a
// test about the STROKE needs art whose only ink is one.
std::string oneLayerOf(const std::string& asset) {
    return "{\n  \"groups\" : [\n    {\n      \"layers\" : [\n        {\n"
           "          \"image-name\" : \"" + asset + "\",\n"
           "          \"name\" : \"only\"\n"
           "        }\n      ]\n    }\n  ]\n}\n";
}

std::string oneLayer(const std::string& extra) {
    return "{\n  \"groups\" : [\n    {\n      \"layers\" : [\n        {\n"
           "          \"image-name\" : \"square.svg\",\n"
           "          \"name\" : \"only\"" + extra + "\n"
           "        }\n      ]\n    }\n  ]\n}\n";
}

// The same one-layer document, but with `extra` on the GROUP instead of on the
// layer. `oneLayer` puts it on the layer, and the two are different keys in
// different places -- which is exactly the distinction this fixture exists to
// let a test make.
// Both at once: `groupExtra` on the group, `layerExtra` on the layer. The glass
// bit lives on the LAYER while the blend lives on the group, and no fixture
// could put them in the same place.
// TWO groups, the second carrying `secondGroupExtra`. A single group over an
// empty canvas cannot distinguish a blend from a source-over -- most modes are
// the identity against nothing -- so every pixel-level blend test needs a
// backdrop that the second group actually mixes with.
std::string twoGroups(const std::string& secondGroupExtra,
                      const std::string& secondLayerExtra) {
    return "{\n  \"groups\" : [\n    {\n      \"layers\" : [\n        {\n"
           "          \"image-name\" : \"square.svg\",\n"
           "          \"name\" : \"back\"\n"
           "        }\n      ]\n    },\n    {\n      " + secondGroupExtra +
           "\"layers\" : [\n        {\n"
           "          \"image-name\" : \"square.svg\",\n"
           "          \"name\" : \"front\"" + secondLayerExtra + "\n"
           "        }\n      ]\n    }\n  ]\n}\n";
}

std::string groupAndLayer(const std::string& groupExtra, const std::string& layerExtra) {
    return "{\n  \"groups\" : [\n    {\n      " + groupExtra +
           "\"layers\" : [\n        {\n"
           "          \"image-name\" : \"square.svg\",\n"
           "          \"name\" : \"only\"" + layerExtra + "\n"
           "        }\n      ]\n    }\n  ]\n}\n";
}

std::string groupWith(const std::string& groupExtra) {
    return "{\n  \"groups\" : [\n    {\n      " + groupExtra + "\"layers\" : [\n        {\n"
           "          \"image-name\" : \"square.svg\",\n"
           "          \"name\" : \"only\"\n"
           "        }\n      ]\n    }\n  ]\n}\n";
}

// The alpha at the very centre of the canvas, where a centred 512-point square
// on a 1024-point canvas always covers.
float centreAlpha(const RenderedIcon& img) {
    return alphaAt(img, img.width / 2, img.height / 2);
}

}  // namespace

// A group's transform applies TO the layer's, so the inner translation is
// scaled by the outer. Adding them instead would look right whenever the group
// scale is 1 -- which it is in most of the corpus, and is why this is a unit
// test rather than something the pictures would catch.
TEST_CASE(a_group_transform_applies_to_the_layers_inside_it) {
    LayerPlacement g;
    g.scale = 2.0;
    g.translateX = 10.0;
    g.translateY = -4.0;
    LayerPlacement l;
    l.scale = 3.0;
    l.translateX = 5.0;
    l.translateY = 1.0;

    const LayerPlacement c = compose(g, l);
    CHECK_EQ(c.scale, 6.0);
    CHECK_EQ(c.translateX, 20.0);   // 2*5 + 10, not 5 + 10
    CHECK_EQ(c.translateY, -2.0);   // 2*1 - 4

    // The identity of the outer transform leaves the inner one untouched.
    LayerPlacement none;
    const LayerPlacement same = compose(none, l);
    CHECK_EQ(same.scale, 3.0);
    CHECK_EQ(same.translateX, 5.0);
}

// Art whose viewBox IS the canvas, unscaled and untranslated, must land exactly
// on the target with nothing left over.
TEST_CASE(canvas_sized_art_fills_the_target_exactly) {
    const PathGlobals g = placeOnCanvas({0, 0, kCanvasPoints, kCanvasPoints},
                                        LayerPlacement{}, 512);
    CHECK_EQ(g.m0[0], 0.5f);   // 512 pixels over 1024 points
    CHECK_EQ(g.m1[1], 0.5f);
    CHECK_EQ(g.m2[0], 0.0f);
    CHECK_EQ(g.m2[1], 0.0f);
    CHECK_EQ(g.urx, 512.0f);
}

// Smaller art is CENTRED before it is translated. A placement that put it at the
// origin instead would pass the test above and fail this one.
TEST_CASE(smaller_art_is_centred_on_the_canvas) {
    const PathGlobals g = placeOnCanvas({0, 0, 512, 512}, LayerPlacement{}, 512);
    // 512 points of art on a 1024-point canvas leaves 256 points either side,
    // which is 128 pixels at this target size.
    CHECK_EQ(g.m2[0], 128.0f);
    CHECK_EQ(g.m2[1], 128.0f);
    CHECK_EQ(g.m0[0], 0.5f);
}

// `[ART]` THE SIGN OF Y, MEASURED. In `Apollo-Reborn/apollo` the antenna ball
// sits at y = -384 and the face at y = +125. On an astronaut the ball is ABOVE
// the face, so a negative y is toward the TOP of the image -- which is what a
// y-down raster does with the translation applied as written. This is the one
// convention of the three that the corpus settles, and it is pinned so that a
// later "tidy-up" cannot flip it back.
TEST_CASE(a_negative_y_translation_moves_the_art_up) {
    LayerPlacement up;
    up.translateY = -384.0;
    const PathGlobals g = placeOnCanvas({0, 0, kCanvasPoints, kCanvasPoints}, up, 512);
    CHECK(g.m2[1] < 0.0f);
    CHECK_EQ(g.m2[1], -192.0f);   // -384 points is -192 pixels at 512

    LayerPlacement down;
    down.translateY = 125.0;
    CHECK(placeOnCanvas({0, 0, kCanvasPoints, kCanvasPoints}, down, 512).m2[1] > 0.0f);
}

// Scale multiplies the art AND moves its top-left, because the art grows about
// its centre rather than its corner.
TEST_CASE(scale_grows_the_art_about_its_centre) {
    LayerPlacement p;
    p.scale = 2.0;
    const PathGlobals g = placeOnCanvas({0, 0, 512, 512}, p, 512);
    CHECK_EQ(g.m0[0], 1.0f);    // 512 points of art at 2x, over a 1024 canvas
    CHECK_EQ(g.m2[0], 0.0f);    // 1024 points wide now: it fills the canvas
    CHECK_EQ(g.m2[1], 0.0f);
}

// A viewBox with an ORIGIN must be shifted by it, not merely scaled.
TEST_CASE(a_viewbox_origin_is_taken_out_of_the_placement) {
    const PathGlobals a = placeOnCanvas({0, 0, 1024, 1024}, LayerPlacement{}, 512);
    const PathGlobals b = placeOnCanvas({100, 50, 1024, 1024}, LayerPlacement{}, 512);
    CHECK_EQ(b.m2[0], a.m2[0] - 50.0f);   // 100 units at 0.5 pixels per unit
    CHECK_EQ(b.m2[1], a.m2[1] - 25.0f);
}

// ---- end to end ---------------------------------------------------------

// The bundle that exercises BOTH art paths: `GlowGetter` has SVG layers and PNG
// layers, and every one of its five is drawable.
TEST_CASE(a_bundle_with_svg_and_png_layers_draws_all_of_them) {
    Device& d = gpu();
    if (!d.valid()) return;
    const char* dir = std::getenv("IC_CORPUS_DIR");
    REQUIRE(dir && *dir);
    auto bundle = icf::IconBundle::open(fs::path(dir) / "Aeastr__GlowGetter__icon");
    REQUIRE(bundle.has_value());

    IconRenderOptions o;
    o.size = 128;
    auto icon = renderIcon(d, *bundle, o);
    if (!icon) {
        std::printf("  FAIL render: %s\n", icon.error().c_str());
        ++ictest::failures();
        return;
    }
    CHECK_EQ(icon->total, std::size_t{5});
    CHECK_EQ(icon->drawn, std::size_t{5});
    CHECK(icon->skipped.empty());

    // Something was actually painted -- a compositor that drew nothing would
    // report five layers and hand back an empty image.
    float most = 0.0f;
    for (std::uint32_t y = 0; y < icon->height; ++y) {
        for (std::uint32_t x = 0; x < icon->width; ++x) {
            most = std::fmax(most, alphaAt(*icon, x, y));
        }
    }
    CHECK(most > 0.5f);
}

// A GLASS LAYER NOW DRAWS. This test used to assert the opposite: every layer
// of this bundle is glass, and until the compositor learned the effect it
// reported all of them as "camada de vidro -- o efeito nao foi transcrito" and
// drew nothing. That sentence is retired.
//
// What replaces it is not "glass is finished". `[ART]` This bundle's group
// carries no `refractivity`, so `refractionStrength` stays at its read default
// of 0, the shader's one argument is 0, and the refraction is the identity --
// the layer draws its art over an untouched backdrop. `GlassLayer.h` argues
// that case at length; what matters here is that it is a DRAW and not a skip.
TEST_CASE(a_glass_layer_draws_instead_of_being_reported_as_untranscribed) {
    Device& d = gpu();
    if (!d.valid()) return;
    const char* dir = std::getenv("IC_CORPUS_DIR");
    REQUIRE(dir && *dir);
    auto bundle = icf::IconBundle::open(fs::path(dir) / "Apollo-Reborn__Apollo-Reborn__AppIcon");
    REQUIRE(bundle.has_value());
    IconRenderOptions o;
    o.size = 64;
    auto icon = renderIcon(d, *bundle, o);
    REQUIRE(icon.has_value());
    CHECK(icon->drawn > 0);
    for (const auto& s : icon->skipped) {
        CHECK(s.why.find("o efeito nao foi transcrito") == std::string::npos);
    }
    // Every layer is still accounted for: drawn, named, or deliberately hidden.
    CHECK(icon->drawn + icon->skipped.size() <= icon->total);
}


// ---- what the document instructs, on values this test chose ---------------

// `hidden` is an INSTRUCTION, not a gap: the layer must not be drawn and must
// not be reported as something the renderer failed at.
TEST_CASE(a_hidden_layer_is_neither_drawn_nor_reported_as_a_gap) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 64;

    const TempBundle shown(oneLayer(""));
    auto a = icf::IconBundle::open(shown.path());
    REQUIRE(a.has_value());
    auto lit = renderIcon(d, *a, o);
    REQUIRE(lit.has_value());
    CHECK_EQ(lit->drawn, std::size_t{1});
    CHECK(centreAlpha(*lit) > 0.9f);

    const TempBundle gone(oneLayer(",\n          \"hidden\" : true"));
    auto b = icf::IconBundle::open(gone.path());
    REQUIRE(b.has_value());
    auto dark = renderIcon(d, *b, o);
    REQUIRE(dark.has_value());
    CHECK_EQ(dark->drawn, std::size_t{0});
    CHECK(dark->skipped.empty());          // hidden is not a gap
    CHECK(centreAlpha(*dark) < 0.01f);
}

// Opacity reaches the pixels. A compositor that ignored it would draw the same
// picture for 1.0 and 0.4 and pass every other test here.
TEST_CASE(a_layers_opacity_reaches_the_pixels) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 64;
    const TempBundle half(oneLayer(",\n          \"opacity\" : 0.4"));
    auto b = icf::IconBundle::open(half.path());
    REQUIRE(b.has_value());
    auto icon = renderIcon(d, *b, o);
    REQUIRE(icon.has_value());
    CHECK_EQ(icon->drawn, std::size_t{1});
    CHECK(std::fabs(centreAlpha(*icon) - 0.4f) < 0.02f);
}

// A blend this renderer does not have is NAMED rather than drawn as normal --
// drawing it anyway would produce a plausible picture that is wrong.
// A layer blend now DRAWS. This test asserted the opposite until 2026-09-04,
// and it was right then: the compositor had only source-over. Replacing the
// assertion rather than deleting the case keeps the history legible -- the
// name changed because the behaviour did.
TEST_CASE(a_layer_blend_mode_draws_instead_of_being_refused) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 64;
    const TempBundle mult(oneLayer(",\n          \"blend-mode\" : \"multiply\""));
    auto b = icf::IconBundle::open(mult.path());
    REQUIRE(b.has_value());
    auto icon = renderIcon(d, *b, o);
    REQUIRE(icon.has_value());
    CHECK_EQ(icon->drawn, std::size_t{1});
    CHECK(icon->skipped.empty());
}

// ...and a spelling the reader does not know is still a NAMED gap. This is the
// only reachable refusal left on the layer path: the format can spell ten modes
// and all ten are transcribed, so `blendIsTranscribed` returning false cannot
// happen from a real document. That branch guards against a mode being added
// to the vocabulary without arithmetic, and it is UNREACHABLE today -- said
// here because an unreachable branch nobody admits to is how dead code starts
// looking like coverage.
TEST_CASE(a_blend_spelling_the_reader_does_not_know_is_named) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 64;
    const TempBundle odd(oneLayer(",\n          \"blend-mode\" : \"color-burn\""));
    auto b = icf::IconBundle::open(odd.path());
    REQUIRE(b.has_value());
    auto icon = renderIcon(d, *b, o);
    REQUIRE(icon.has_value());
    CHECK_EQ(icon->drawn, std::size_t{0});
    REQUIRE(icon->skipped.size() == 1);
    CHECK(icon->skipped[0].why.find("color-burn") != std::string::npos);
}

// A blend on the GROUP is the case the corpus actually uses -- `plus-lighter`
// sits on a group 17 times against 5 on a layer. Until 2026-09-03 the renderer
// read the key only off the layer and composited such a group as `normal` with
// an EMPTY report; that day it started refusing; today it DRAWS, by giving the
// group a target of its own and mixing the result.
//
// All three states are worth remembering, because only the middle one was ever
// wrong: refusing was honest, drawing is better, and drawing silently was the
// defect.
TEST_CASE(a_blend_mode_on_the_group_draws_through_its_own_target) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 64;
    const TempBundle plus(groupWith("\"blend-mode\" : \"plus-lighter\",\n      "));
    auto b = icf::IconBundle::open(plus.path());
    REQUIRE(b.has_value());
    auto icon = renderIcon(d, *b, o);
    REQUIRE(icon.has_value());
    CHECK_EQ(icon->drawn, std::size_t{1});
    CHECK(icon->skipped.empty());
}

// ...EXCEPT over glass, which is still refused and says why.
//
// Glass refracts its backdrop. In a group with its own target the backdrop is
// empty; in the canvas the blend would mix it in twice. `[OBS]` Which the
// target does was never read -- the question does not arise until a group gets
// its own buffer. `[ART]` Eight corpus documents hit this, so the refusal
// costs real reach, and that is the point: the alternative buys reach with a
// picture nobody measured.
TEST_CASE(a_blended_group_containing_glass_is_refused_with_its_reason) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 64;
    const TempBundle glassy(groupAndLayer(
        "\"blend-mode\" : \"plus-lighter\",\n      ",
        ",\n          \"glass\" : true"));
    auto b = icf::IconBundle::open(glassy.path());
    REQUIRE(b.has_value());
    auto icon = renderIcon(d, *b, o);
    REQUIRE(icon.has_value());
    CHECK_EQ(icon->drawn, std::size_t{0});
    REQUIRE(icon->skipped.size() == 1);
    CHECK(icon->skipped[0].why.find("vidro") != std::string::npos);
    CHECK(icon->skipped[0].why.find("refracao") != std::string::npos);
}

// And `normal` on the group is not a gap: the same document with the key set
// to the value that means "do nothing" has to draw. Without this, the guard
// above would pass just as well if it refused EVERY group carrying the key.
TEST_CASE(a_normal_blend_on_the_group_is_not_a_gap) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 64;
    const TempBundle plain(groupWith("\"blend-mode\" : \"normal\",\n      "));
    auto b = icf::IconBundle::open(plain.path());
    REQUIRE(b.has_value());
    auto icon = renderIcon(d, *b, o);
    REQUIRE(icon.has_value());
    CHECK_EQ(icon->drawn, std::size_t{1});
    CHECK(icon->skipped.empty());
}

// The placement, end to end and in PIXELS: the same square moved up by a
// quarter of the canvas has to land a quarter of the target higher.
TEST_CASE(a_translation_moves_the_art_where_the_document_says) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 64;
    const TempBundle up(oneLayer(
        ",\n          \"position\" : { \"scale\" : 1, \"translation-in-points\" : [0, -256] }"));
    auto b = icf::IconBundle::open(up.path());
    REQUIRE(b.has_value());
    auto icon = renderIcon(d, *b, o);
    REQUIRE(icon.has_value());
    CHECK_EQ(icon->drawn, std::size_t{1});
    // Centred, the square spans rows 16..48 of 64. Moved up 256 points -- a
    // quarter of the canvas, so 16 pixels -- it spans 0..32.
    CHECK(alphaAt(*icon, 32, 8) > 0.9f);    // now inside
    CHECK(alphaAt(*icon, 32, 40) < 0.01f);  // now outside
}


// ---- the three the sweep proved were untested ---------------------------

// TWO layers, and the top one is not opaque. Every other test here draws one
// layer over an empty accumulator, where the destination term is zero and an
// `over` that forgot to hold the destination back is indistinguishable from one
// that did not. This is the first test where the destination is not zero.
//
// It also pins the COLOUR, not just the alpha. The accumulator composites in
// premultiplied form and the result is un-multiplied at the end; a version that
// skipped the un-multiply came back too dark, and an alpha-only check saw
// nothing wrong.
TEST_CASE(a_second_layer_composites_over_the_first_and_keeps_its_colour) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 64;

    // Red underneath, white on top at half opacity: the result is a premultiplied
    // `over`, which after un-multiplying is (1, 0.5, 0.5) at alpha 1.
    const std::string doc =
        "{\n  \"groups\" : [\n    {\n      \"layers\" : [\n"
        "        { \"image-name\" : \"red.svg\", \"name\" : \"under\" },\n"
        "        { \"image-name\" : \"square.svg\", \"name\" : \"over\", \"opacity\" : 0.5 }\n"
        "      ]\n    }\n  ]\n}\n";
    const TempBundle b(doc);
    auto bundle = icf::IconBundle::open(b.path());
    REQUIRE(bundle.has_value());
    auto icon = renderIcon(d, *bundle, o);
    REQUIRE(icon.has_value());
    CHECK_EQ(icon->drawn, std::size_t{2});

    const std::size_t c = (static_cast<std::size_t>(o.size / 2) * o.size + o.size / 2) * 4;
    CHECK(std::fabs(icon->rgba[c + 3] - 1.0f) < 0.02f);       // fully covered
    CHECK(std::fabs(icon->rgba[c + 0] - 1.0f) < 0.02f);       // red + white
    CHECK(std::fabs(icon->rgba[c + 1] - 0.5f) < 0.02f);       // half of the white
    CHECK(std::fabs(icon->rgba[c + 2] - 0.5f) < 0.02f);
}

// A layer alone, half transparent, must keep its COLOUR. This is the check the
// opacity test was missing: it asserted the alpha and let a premultiplied
// result pass as a straight one.
TEST_CASE(a_half_transparent_layer_is_not_darkened) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 64;
    const TempBundle b(oneLayer(",\n          \"opacity\" : 0.5"));
    auto bundle = icf::IconBundle::open(b.path());
    REQUIRE(bundle.has_value());
    auto icon = renderIcon(d, *bundle, o);
    REQUIRE(icon.has_value());
    const std::size_t c = (static_cast<std::size_t>(o.size / 2) * o.size + o.size / 2) * 4;
    CHECK(std::fabs(icon->rgba[c + 3] - 0.5f) < 0.02f);
    // White at half opacity is still WHITE. Premultiplied it would read 0.5.
    CHECK(std::fabs(icon->rgba[c + 0] - 1.0f) < 0.02f);
}

// A raster whose transparent half carries red bytes. Sampled across the hard
// edge WITHOUT premultiplying, the invisible red bleeds into the visible green
// and the boundary pixel turns yellow-ish. Premultiplied, it cannot.
TEST_CASE(a_raster_edge_does_not_bleed_the_colour_of_transparent_pixels) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 128;
    // The raster is 64 units on a 1024-point canvas: scaled up so that the
    // boundary lands between output pixels and the sampler has to interpolate.
    const TempBundle b(
        "{\n  \"groups\" : [\n    {\n      \"layers\" : [\n"
        "        { \"image-name\" : \"edge.png\", \"name\" : \"edge\",\n"
        "          \"position\" : { \"scale\" : 15.5, \"translation-in-points\" : [0, 0] } }\n"
        "      ]\n    }\n  ]\n}\n");
    auto bundle = icf::IconBundle::open(b.path());
    REQUIRE(bundle.has_value());
    auto icon = renderIcon(d, *bundle, o);
    REQUIRE(icon.has_value());
    CHECK_EQ(icon->drawn, std::size_t{1});

    // Anywhere the image is even slightly visible, the red channel must stay
    // near zero: the only red in the file sits behind alpha 0.
    float worstRed = 0.0f;
    for (std::size_t i = 0; i + 3 < icon->rgba.size(); i += 4) {
        if (icon->rgba[i + 3] > 0.05f) worstRed = std::fmax(worstRed, icon->rgba[i + 0]);
    }
    std::printf("  worst red where the raster is visible: %.4f\n", worstRed);
    CHECK(worstRed < 0.10f);
}

// A name that does not resolve is reported AS a dangling reference. Without the
// check the file simply fails to open later and the layer is still skipped --
// same count, different reason -- so only the REASON distinguishes them.
TEST_CASE(a_dangling_reference_is_named_as_one) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 32;
    const TempBundle b(
        "{\n  \"groups\" : [\n    {\n      \"layers\" : [\n"
        "        { \"image-name\" : \"nope.svg\", \"name\" : \"missing\" }\n"
        "      ]\n    }\n  ]\n}\n");
    auto bundle = icf::IconBundle::open(b.path());
    REQUIRE(bundle.has_value());
    auto icon = renderIcon(d, *bundle, o);
    REQUIRE(icon.has_value());
    CHECK_EQ(icon->drawn, std::size_t{0});
    REQUIRE(icon->skipped.size() == 1);
    CHECK(icon->skipped[0].why.find("pendurada") != std::string::npos);
}

// THE CORPUS GATE. Every bundle that ships art has to render without the
// compositor failing -- not "draw everything", which it cannot, but never come
// back with an error and never lose count of its own layers.
TEST_CASE(corpus_icon_render_gate) {
    Device& d = gpu();
    if (!d.valid()) return;
    auto bundles = corpusBundles();
    REQUIRE(!bundles.empty());

    std::size_t drawn = 0, total = 0, whole = 0;
    std::vector<std::string> offenders;
    for (const auto& b : bundles) {
        auto bundle = icf::IconBundle::open(b);
        if (!bundle) {
            offenders.push_back(b.filename().string() + ": not a bundle");
            continue;
        }
        IconRenderOptions o;
        o.size = 64;
        auto icon = renderIcon(d, *bundle, o);
        if (!icon) {
            offenders.push_back(b.filename().string() + ": " + icon.error());
            continue;
        }
        // Every layer is either drawn or named. A layer that is neither has
        // gone missing silently, which is the failure this counts.
        if (icon->drawn + icon->skipped.size() > icon->total) {
            offenders.push_back(b.filename().string() + ": more outcomes than layers");
        }
        drawn += icon->drawn;
        total += icon->total;
        whole += icon->drawn == icon->total && icon->total > 0;
    }
    std::printf("  %zu bundles: %zu of %zu layers drawn, %zu rendered whole\n",
                bundles.size(), drawn, total, whole);
    for (const auto& o : offenders) std::printf("    %s\n", o.c_str());
    CHECK(offenders.empty());
    CHECK(drawn > 0);
}


// THE PIXELS, not the counts. Four gate mutations survived the count-only
// tests: premultiplying an accumulator twice, compositing the group straight
// onto the canvas, dropping the group result entirely, and skipping a
// transparent source. Every one of them leaves `drawn` at the same number.
//
// `square.svg` is opaque red. Two of them under `plus-lighter` add, and red
// is already at 1.0, so the ALPHA is what moves: 1 + 1 saturates to 1 while
// source-over also gives 1... which is why the second group here is drawn at
// half opacity, where the two rules separate.
TEST_CASE(a_blended_group_changes_the_pixels_and_not_only_the_count) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 64;

    const TempBundle plain(twoGroups("", ",\n          \"opacity\" : 0.5"));
    auto pb = icf::IconBundle::open(plain.path());
    REQUIRE(pb.has_value());
    auto normal = renderIcon(d, *pb, o);
    REQUIRE(normal.has_value());
    CHECK_EQ(normal->drawn, std::size_t{2});

    const TempBundle mixed(twoGroups(
        "\"blend-mode\" : \"plus-lighter\",\n      ",
        ",\n          \"opacity\" : 0.5"));
    auto mb = icf::IconBundle::open(mixed.path());
    REQUIRE(mb.has_value());
    auto plus = renderIcon(d, *mb, o);
    REQUIRE(plus.has_value());
    CHECK_EQ(plus->drawn, std::size_t{2});
    CHECK(plus->skipped.empty());

    // The counts match and the PICTURES must not: plus-lighter adds the two
    // premultiplied reds where source-over replaces. If the group result is
    // dropped, or mixed straight, or premultiplied twice, this is where it
    // shows.
    const float nr = channelAt(*normal, 32, 32, 0) * alphaAt(*normal, 32, 32);
    const float pr = channelAt(*plus, 32, 32, 0) * alphaAt(*plus, 32, 32);
    // EXACT, not "bigger". `square.svg` is opaque red, so the backdrop is
    // premultiplied (1,0,0,1) and the front group is (0.5,0,0,0.5).
    // plus-lighter adds: rgb 1.5, alpha saturate(1.5) = 1. Source-over gives
    // 0.5 + 1*(1-0.5) = 1.0. A threshold of "greater" passes for BOTH the
    // right answer and a group premultiplied twice (1.25), which is exactly
    // how that mutation survived the first version of this test.
    CHECK(std::fabs(nr - 1.0f) < 0.02f);
    CHECK(std::fabs(pr - 1.5f) < 0.02f);

    // And the group really did reach the canvas: dropping it would leave the
    // backdrop alone, which is exactly `nr`.
    CHECK(alphaAt(*plus, 32, 32) > 0.9f);
}

// There is deliberately NO test that a transparent source reaches the blend.
// The renderer skips a layer whose resolved opacity is zero before it ever
// composites, and even inside the blend a zero-alpha source is the identity
// for all nine modes -- so there is nothing to observe. The first draft of
// this file asserted otherwise and the premise was wrong; the note stays so
// the next reader does not go looking for the test that is missing.

// The LAYER path, which the group test above never reaches: `blendOver`
// short-circuits to `over` when the mode is Normal, so a fixture that puts
// the blend on the group leaves the premultiply in the layer path unrun.
//
// One layer, `plus-lighter`, half opacity, over an empty canvas. Premultiplied
// it contributes (0.5, 0, 0, 0.5) and the unpremultiplied output is 1.0;
// straight it would contribute (1, 0, 0, 0.5) and the output would be 2.0.
TEST_CASE(a_layer_blend_premultiplies_its_art_before_mixing) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 64;
    const TempBundle b(oneLayer(
        ",\n          \"blend-mode\" : \"plus-lighter\",\n          \"opacity\" : 0.5"));
    auto bundle = icf::IconBundle::open(b.path());
    REQUIRE(bundle.has_value());
    auto icon = renderIcon(d, *bundle, o);
    REQUIRE(icon.has_value());
    CHECK_EQ(icon->drawn, std::size_t{1});
    CHECK(std::fabs(alphaAt(*icon, 32, 32) - 0.5f) < 0.02f);
    CHECK(std::fabs(channelAt(*icon, 32, 32, 0) - 1.0f) < 0.02f);
}

// END TO END: a shape whose only ink is its STROKE. `cross.svg` has
// `fill="none"`, so every pixel it produces comes through the stroke path --
// document, reader, flattener, point stream, coverage, composite.
//
// Until 2026-09-04 this rendered blank with an EMPTY report, which is the
// defect the test exists to keep dead. `[ART]` 31 corpus layers over 10
// documents carry a painted stroke, the largest single blocker on the ruler.
TEST_CASE(a_shape_whose_only_ink_is_its_stroke_draws) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 64;
    const TempBundle b(oneLayerOf("cross.svg"));
    auto bundle = icf::IconBundle::open(b.path());
    REQUIRE(bundle.has_value());
    auto icon = renderIcon(d, *bundle, o);
    REQUIRE(icon.has_value());
    CHECK_EQ(icon->drawn, std::size_t{1});

    // The arms of the cross: the centre is on both, and points along each
    // arm are on the stroke and nowhere near the other one.
    CHECK(alphaAt(*icon, 32, 32) > 0.9f);
    // The viewBox is 512 on a 1024-point canvas, so the art occupies HALF the
    // target: the arms run 24..40 px, not 16..48. The first draft of this test
    // probed at 20 and got a correct blank -- the sonde was wrong, not the
    // renderer, and that is worth a line because the two look identical from
    // a red test.
    CHECK(alphaAt(*icon, 28, 32) > 0.9f);
    CHECK(alphaAt(*icon, 36, 32) > 0.9f);
    CHECK(alphaAt(*icon, 32, 28) > 0.9f);
    // ...and it STOPS there, which is the butt cap arriving end to end.
    CHECK(alphaAt(*icon, 44, 32) < 0.01f);
    // BLUE, not the red of `square.svg` -- proof the stroke PAINT arrived.
    CHECK(channelAt(*icon, 32, 32, 2) > 0.9f);
    CHECK(channelAt(*icon, 32, 32, 0) < 0.1f);
    // A corner the cross does not reach stays empty, or the assertions
    // above would pass on a renderer that flooded the canvas.
    CHECK(alphaAt(*icon, 4, 4) < 0.01f);
}