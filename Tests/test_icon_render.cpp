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
        // The same cross with a HALF-TRANSPARENT stroke. `[ART]` The corpus
        // carries `stroke-opacity: 0.2` in twelve places, so this is the real
        // shape and not a contrived one -- and without it, a renderer that
        // ignores the stroke opacity looks correct.
        write(dir_ / "Assets" / "faint.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\">"
              "<path d=\"M128 256 L384 256\" fill=\"none\""
              " stroke=\"#0000ff\" stroke-width=\"32\""
              " stroke-opacity=\"0.5\"/></svg>");
        // THE SAME SQUARE, CARRYING A FILTER IT WILL NOT GET. `[ART]` This is
        // the shape of `PDF-Archiver`'s four assets, verbatim down to the
        // `feDropShadow` arguments -- one primitive, referenced by `filter=`.
        // The reader draws the square and walks past the filter; the point of
        // the fixture is that it has to SAY so.
        write(dir_ / "Assets" / "shadowed.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\">"
              "<defs><filter id=\"s\">"
              "<feDropShadow dy=\"10\" stdDeviation=\"10\" flood-opacity=\"0.3\"/>"
              "</filter></defs>"
              "<path d=\"M0 0 L512 0 L512 512 L0 512 Z\" fill=\"#ffffff\""
              " filter=\"url(#s)\"/></svg>");
        // A gap that is STILL a gap. `shadowed.svg` stopped being one on
        // 2026-09-09: the target drops `feDropShadow` at construction and the
        // element draws NOTHING, so reproducing that is not a gap -- and the
        // report plumbing this fixture used to guard would have been left
        // uncovered by the change. `<pattern>` is read by nothing here, which
        // is what a gap actually means.
        write(dir_ / "Assets" / "patterned.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\">"
              "<defs><pattern id=\"p\" width=\"8\" height=\"8\">"
              "<rect width=\"4\" height=\"4\" fill=\"#000\"/></pattern></defs>"
              "<path d=\"M0 0 L512 0 L512 512 L0 512 Z\" fill=\"url(#p)\"/></svg>");
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
std::string twoGroups(const std::string& frontGroupExtra,
                      const std::string& frontLayerExtra) {
    // The group carrying the extras is FIRST, which is the FRONT: the array
    // runs front to back. A single group over an empty canvas cannot
    // distinguish a blend from a source-over -- most modes are the identity
    // against nothing -- so the plain group behind it is what makes the
    // blend observable.
    return "{\n  \"groups\" : [\n    {\n      " + frontGroupExtra +
           "\"layers\" : [\n        {\n"
           "          \"image-name\" : \"square.svg\",\n"
           "          \"name\" : \"front\"" + frontLayerExtra + "\n"
           "        }\n      ]\n    },\n    {\n      \"layers\" : [\n        {\n"
           "          \"image-name\" : \"square.svg\",\n"
           "          \"name\" : \"back\"\n"
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

// WHAT THE SVG READER WALKED PAST HAS TO REACH THE ICON'S REPORT.
//
// `SvgDocument::unsupported()` has always named the elements the reader does
// not draw, and `icrender` has always printed them for a LOOSE `.svg`. The
// BUNDLE path threw the set away, so an icon whose art carries a drop shadow,
// a mask or a clip path rendered without it and reported `N of N layer(s)
// drawn` -- a clean report over a wrong picture.
//
// `[ART]` Five of the eight corpus documents outside the drawable slice did
// exactly that: PDF-Archiver and PiStats (filter), CommE2E (mask), quick-push
// (clipPath) and Delta (pattern). The ruler caught them only because it reads
// the SVG itself, which is the definition of a gap the renderer cannot see.
//
// The layer still DRAWS -- dropping it would trade a wrong picture for a
// missing one -- so this is a gap and not a skip, and the two are asserted
// apart.
TEST_CASE(an_svg_feature_the_reader_ignores_reaches_the_icons_report) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 64;
    const TempBundle b(oneLayerOf("patterned.svg"));
    auto bundle = icf::IconBundle::open(b.path());
    REQUIRE(bundle.has_value());
    auto icon = renderIcon(d, *bundle, o);
    REQUIRE(icon.has_value());

    // Drawn, and not skipped.
    CHECK_EQ(icon->drawn, std::size_t{1});
    CHECK(icon->skipped.empty());

    // And the gap is NAMED, with an address on it: a gap nobody can go and fix
    // is not a report.
    std::string all;
    for (const auto& g : icon->shapeGaps) all += g + "\n";
    CHECK(all.find("pattern") != std::string::npos);
    CHECK(all.find("only / patterned.svg") != std::string::npos);
}

// AND A FILTER THE TARGET DROPS IS NOT A GAP -- IT IS THE PICTURE.
//
// `shadowed.svg` names `feDropShadow`, which `SVGFilter::filterPrimitive`
// (CoreSVG.arm64 0x2A230) refuses to construct. The hole nulls its way through
// `SVGFilter::draw` to a cleanup path that draws nothing, and its one caller has
// no fallback. So the target draws NO SQUARE AT ALL for this asset.
//
// This test exists because the honest failure here is the opposite of the usual
// one: the tempting behaviour is to draw the square unfiltered and report the
// filter as missing, which would be a picture the target never makes AND a
// complaint about art that was never lost.
TEST_CASE(a_filter_the_target_never_builds_draws_nothing_and_is_not_a_gap) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 64;
    const TempBundle b(oneLayerOf("shadowed.svg"));
    auto bundle = icf::IconBundle::open(b.path());
    REQUIRE(bundle.has_value());
    auto icon = renderIcon(d, *bundle, o);
    REQUIRE(icon.has_value());

    // THE LAYER IS STILL "DRAWN", and that word is about the layer and not
    // about ink: `RenderedIcon::drawn` counts layers the compositor processed,
    // and this one was processed -- its art simply has no shape left in it. Nor
    // is it SKIPPED, which would mean the renderer could not do it.
    CHECK_EQ(icon->drawn, std::size_t{1});
    CHECK(icon->skipped.empty());

    std::string all;
    for (const auto& g : icon->shapeGaps) all += g + "\n";
    CHECK(all.find("filter") == std::string::npos);

    // AND THE CANVAS IS ACTUALLY EMPTY WHERE THE SQUARE WAS. Counting shapes
    // is not the same as looking: the square covers the whole viewBox, so if it
    // had been drawn unfiltered every pixel here would be opaque white.
    const std::size_t centre = (static_cast<std::size_t>(o.size / 2) * o.size + o.size / 2) * 4;
    REQUIRE(icon->rgba.size() > centre + 3);
    CHECK(icon->rgba[centre + 3] < 0.5f);
}

// AND THE GROUP'S GAP HAS TO NAME ITS MODE TOO, which the test above does NOT
// cover and the mutation sweep of 2026-09-04 proved it does not.
//
// That test drives `color-burn` on a LAYER, so it exercises the layer path's
// message and leaves the group path's message unasserted. Dropping
// `*groupBlend` from the group's reason kept the whole suite green: the gap was
// still reported, still counted, and no longer said WHICH mode caused it -- a
// report that names nothing is how a gap becomes unfixable.
TEST_CASE(a_group_blend_gap_names_the_mode) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 64;
    const TempBundle odd(groupWith("\"blend-mode\" : \"color-burn\",\n      "));
    auto b = icf::IconBundle::open(odd.path());
    REQUIRE(b.has_value());
    auto icon = renderIcon(d, *b, o);
    REQUIRE(icon.has_value());
    CHECK_EQ(icon->drawn, std::size_t{0});
    REQUIRE(icon->skipped.size() == 1);
    CHECK(icon->skipped[0].why.find("mescla de grupo") != std::string::npos);
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

// ...AND OVER GLASS TOO, which was refused until 2026-09-04 on a premise that
// turned out to be false.
//
// The refusal said: glass refracts its backdrop, so a group with its own target
// would refract an empty one. `[BIN]` It does not (doc 03 §34.3) --
// `GlassDisplacementStyle::draw` puts a `GenericFilter<GlassDisplacementEffect>`
// over the ITEM it is applied to, and never reaches `make_backdrop_item`. The
// glass consumes what it wears, so a target of its own starves nothing.
//
// `[ART]` Eight corpus documents put a non-normal blend and a glass layer on
// the same group, `insidegui/AssetCatalogTinkerer` and `RuntimeViewer` among
// them.
//
// The oracle here is the same EXACT arithmetic as
// `a_blended_group_changes_the_pixels_and_not_only_the_count`, and for the same
// reason: "brighter than normal" also passes for a group premultiplied twice.
// What this one adds is the `glass` key on the layer, which used to stop the
// group from being drawn at all.
TEST_CASE(a_blended_group_containing_glass_draws_and_still_blends) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 64;

    const TempBundle plain(twoGroups(
        "", ",\n          \"opacity\" : 0.5,\n          \"glass\" : true"));
    auto pb = icf::IconBundle::open(plain.path());
    REQUIRE(pb.has_value());
    auto normal = renderIcon(d, *pb, o);
    REQUIRE(normal.has_value());
    CHECK_EQ(normal->drawn, std::size_t{2});

    const TempBundle mixed(twoGroups(
        "\"blend-mode\" : \"plus-lighter\",\n      ",
        ",\n          \"opacity\" : 0.5,\n          \"glass\" : true"));
    auto mb = icf::IconBundle::open(mixed.path());
    REQUIRE(mb.has_value());
    auto plus = renderIcon(d, *mb, o);
    REQUIRE(plus.has_value());

    // It DRAWS now, and it does not draw silently: nothing may be skipped.
    CHECK_EQ(plus->drawn, std::size_t{2});
    CHECK(plus->skipped.empty());

    const float nr = channelAt(*normal, 32, 32, 0) * alphaAt(*normal, 32, 32);
    const float pr = channelAt(*plus, 32, 32, 0) * alphaAt(*plus, 32, 32);
    CHECK(std::fabs(nr - 1.0f) < 0.02f);
    CHECK(std::fabs(pr - 1.5f) < 0.02f);
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
    //
    // The white is listed FIRST because the array runs FRONT TO BACK. It was
    // the other way until 2026-09-04; the arithmetic below did not change.
    const std::string doc =
        "{\n  \"groups\" : [\n    {\n      \"layers\" : [\n"
        "        { \"image-name\" : \"square.svg\", \"name\" : \"over\", \"opacity\" : 0.5 },\n"
        "        { \"image-name\" : \"red.svg\", \"name\" : \"under\" }\n"
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

// ---- the GROUP's own `hidden` and `opacity` -------------------------------
//
// Neither was read until 2026-10-01: the loop resolved `hidden` and `opacity`
// off the LAYER only, so a hidden group drew and a half-transparent group drew
// opaque, both with a clean report. `[ART]` 17 corpus groups are hidden and 26
// carry an opacity other than 1.

// `[BIN]` A hidden group never becomes an `Icon.Layer`: the converter
// (`IconComposerKit` `0x10C494`) answers nil for it at `0x10C608`-`0x10C6D8`.
// Nothing of it is drawn and -- like a hidden layer -- nothing is reported.
TEST_CASE(a_hidden_group_is_neither_drawn_nor_reported_as_a_gap) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 64;

    const TempBundle gone(groupWith("\"hidden\" : true,\n      "));
    auto b = icf::IconBundle::open(gone.path());
    REQUIRE(b.has_value());
    auto dark = renderIcon(d, *b, o);
    REQUIRE(dark.has_value());
    CHECK_EQ(dark->drawn, std::size_t{0});
    CHECK(dark->skipped.empty());          // hidden is not a gap
    CHECK(dark->notes.empty());
    CHECK_EQ(dark->total, std::size_t{1}); // the document still has the layer
    CHECK(centreAlpha(*dark) < 0.01f);

    // And `false` is the value that means "draw": without this the case above
    // would pass for a reader that hid every group carrying the key.
    const TempBundle shown(groupWith("\"hidden\" : false,\n      "));
    auto s = icf::IconBundle::open(shown.path());
    REQUIRE(s.has_value());
    auto lit = renderIcon(d, *s, o);
    REQUIRE(lit.has_value());
    CHECK_EQ(lit->drawn, std::size_t{1});
    CHECK(centreAlpha(*lit) > 0.9f);
}

// `[BIN]` The group's `opacity` is `FinalizedIcon.Layer.opacity` (`+0x38`,
// copied at `0x19934`-`0x19940`) and it is the `alpha:` of the group's content
// draw (`0x4B4EC`); the layer's is `Icon.Element.opacity`, applied when the
// element goes INTO the group's image (`0x1AD9C`). Two fields, two draws, so
// the two multiply: 0.4 alone, and 0.4 x 0.5 = 0.2 with both.
TEST_CASE(a_groups_opacity_reaches_the_pixels_and_multiplies_the_layers) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 64;

    const TempBundle group(groupWith("\"opacity\" : 0.4,\n      "));
    auto gb = icf::IconBundle::open(group.path());
    REQUIRE(gb.has_value());
    auto alone = renderIcon(d, *gb, o);
    REQUIRE(alone.has_value());
    CHECK_EQ(alone->drawn, std::size_t{1});
    CHECK(std::fabs(centreAlpha(*alone) - 0.4f) < 0.02f);
    // White at 0.4 is still WHITE: the group's alpha must not be multiplied
    // into the colour twice.
    CHECK(std::fabs(channelAt(*alone, 32, 32, 0) - 1.0f) < 0.02f);

    const std::string doc =
        "{\n  \"groups\" : [\n    {\n      \"opacity\" : 0.4,\n      \"layers\" : [\n"
        "        { \"image-name\" : \"square.svg\", \"name\" : \"only\", \"opacity\" : 0.5 }\n"
        "      ]\n    }\n  ]\n}\n";
    const TempBundle both(doc);
    auto bb = icf::IconBundle::open(both.path());
    REQUIRE(bb.has_value());
    auto product = renderIcon(d, *bb, o);
    REQUIRE(product.has_value());
    CHECK(std::fabs(centreAlpha(*product) - 0.2f) < 0.02f);
}

// `[BIN]` THE HIGHLIGHTS FOLLOW THE GROUP'S OPACITY AND NOT THE LAYER'S. The
// pass at `0x491C0` multiplies each highlight's alpha by `[descriptor+0x38]`
// (`0x495F0`), which is `FinalizedIcon.Layer.opacity` -- the GROUP's. Until
// 2026-10-01 this renderer fed it the layer's.
//
// The oracle is a RATIO, so no highlight value is pinned: the alpha is linear in
// that factor, so a group at 0.02 draws fifty times less highlight than a group
// at 1, and a LAYER at 0.02 inside a group at 1 draws all of it. Each render is
// compared with its own twin that has `specular` off, so what is measured is
// the highlight and nothing else.
TEST_CASE(the_specular_follows_the_groups_opacity_and_not_the_layers) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 128;

    auto highlight = [&](const std::string& groupOpacity, const std::string& layerOpacity) {
        float worst = -1.0f;
        std::vector<float> lit, flat;
        for (const char* specular : {"true", "false"}) {
            const TempBundle tb(twoGroups(
                std::string("\"specular\" : ") + specular + ",\n      \"opacity\" : " +
                    groupOpacity + ",\n      ",
                ",\n          \"opacity\" : " + layerOpacity));
            auto bundle = icf::IconBundle::open(tb.path());
            if (!bundle) return worst;
            auto icon = renderIcon(d, *bundle, o);
            if (!icon) return worst;
            (std::string(specular) == "true" ? lit : flat) = icon->rgba;
        }
        if (lit.size() != flat.size()) return worst;
        worst = 0.0f;
        for (std::size_t i = 0; i < lit.size(); ++i) {
            worst = std::fmax(worst, std::fabs(lit[i] - flat[i]));
        }
        return worst;
    };

    const float byLayer = highlight("1", "0.02");
    const float byGroup = highlight("0.02", "1");
    std::printf("  highlight: layer at 0.02 -> %.5f, group at 0.02 -> %.5f\n", byLayer, byGroup);
    // The highlight is really there...
    CHECK(byLayer > 0.01f);
    // ...and it is the GROUP's opacity that scales it: 0.02 of it, with a
    // decade of headroom either side.
    CHECK(byGroup >= 0.0f);
    CHECK(byGroup < 0.1f * byLayer);
}

// `[BIN]` AND SO DOES THE SHADOW. Its alpha is `shadowOpacity x
// Opacity[3 - sizeClass] x [descriptor+0x38]` (`0x4A06C`/`0x4A070`), and the
// third factor is the same `FinalizedIcon.Layer.opacity` -- the GROUP's.
//
// A ratio again. The shadow is `neutral`, so it is black under `multiply` and
// what it takes off an opaque red backdrop is exactly its own alpha: halving the
// group's opacity halves the darkening. The probe sits BELOW the square, where
// only the shadow reaches -- the square spans rows 64..192 at this size and the
// shadow is its rim pushed 8 px down and blurred.
TEST_CASE(the_shadow_follows_the_groups_opacity) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 256;

    auto darkening = [&](const std::string& groupOpacity) {
        const std::string doc =
            "{\n  \"groups\" : [\n"
            "    { \"opacity\" : " + groupOpacity + ",\n"
            "      \"shadow\" : { \"kind\" : \"neutral\", \"opacity\" : 1.0 },\n"
            "      \"specular\" : false,\n"
            "      \"layers\" : [ { \"image-name\" : \"square.svg\", \"name\" : \"front\" } ] },\n"
            "    { \"layers\" : [ { \"image-name\" : \"red.svg\", \"name\" : \"back\",\n"
            "        \"glass\" : false,\n"
            "        \"position\" : { \"scale\" : 2, \"translation-in-points\" : [0, 0] } } ] }\n"
            "  ]\n}\n";
        const TempBundle tb(doc);
        auto bundle = icf::IconBundle::open(tb.path());
        if (!bundle) return -1.0f;
        auto icon = renderIcon(d, *bundle, o);
        if (!icon || icon->glassShadowed != 1) return -1.0f;
        return 1.0f - channelAt(*icon, 128, 200, 0);
    };

    const float full = darkening("1");
    const float half = darkening("0.5");
    std::printf("  shadow below the square: group at 1 takes %.5f, at 0.5 takes %.5f\n", full,
                half);
    CHECK(full > 0.02f);   // the shadow is really there
    CHECK(half > 0.0f);
    CHECK(std::fabs(half / full - 0.5f) < 0.02f);
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
        // `"glass" : false` SPELLED OUT, and it is load-bearing. Since
        // 2026-09-15 an ABSENT `glass` means TRUE (`Layer.init()` writes 1 at
        // `0x95CE0`), and the raster path gained its own distance field the
        // same day -- so silence here would hand this layer the whole glass
        // chain. What this case measures is premultiplication, not glass.
        "        { \"image-name\" : \"edge.png\", \"name\" : \"edge\", \"glass\" : false,\n"
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

// The stroke carries its OWN opacity, and `stroke-opacity` is folded into the
// paint alpha by the reader. A gate mutation that dropped it survived the
// test above, because that cross paints at alpha 1 -- where multiplying by
// the alpha and not multiplying give the same pixel.
TEST_CASE(a_translucent_stroke_reaches_the_canvas_at_its_own_alpha) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 64;
    const TempBundle b(oneLayerOf("faint.svg"));
    auto bundle = icf::IconBundle::open(b.path());
    REQUIRE(bundle.has_value());
    auto icon = renderIcon(d, *bundle, o);
    REQUIRE(icon.has_value());
    CHECK_EQ(icon->drawn, std::size_t{1});
    // Half, not one. The arm runs 24..40 px at y = 32.
    CHECK(std::fabs(alphaAt(*icon, 32, 32) - 0.5f) < 0.03f);
    CHECK(std::fabs(alphaAt(*icon, 28, 32) - 0.5f) < 0.03f);
    // Still blue where it paints, so this is the stroke and not a stray fill.
    CHECK(channelAt(*icon, 32, 32, 2) > 0.9f);
}

// THE COMPOSITION ORDER, pinned at both levels.
//
// The array runs FRONT to BACK: `groups[0]` is the frontmost group and
// `layers[0]` the frontmost layer of its group. This renderer walked it the
// other way until 2026-09-04, and the symptom was not subtle -- a document
// whose last group is a full-canvas background came out as that background
// and nothing else, with every layer still reported as drawn.
//
// `[ART]` The evidence is in `IconRenderer.cpp`: 19 corpus documents put a
// group named for the background LAST against 1 that puts it first, and the
// `Apollo-Reborn` icon only grows its magenta eyes when BOTH levels are
// reversed -- checked against the icon its authors ship.
//
// `square.svg` is WHITE and `red.svg` is RED, both covering the same square,
// so the GREEN channel says which one is on top: 1 for white, 0 for red.
// Both orders are rendered, so the test cannot pass by accident -- if the
// order made no difference the two would agree.
TEST_CASE(the_array_runs_front_to_back_at_both_levels) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 64;

    const std::string gwDoc =
        "{\n  \"groups\" : [\n"
        "    { \"layers\" : [ { \"image-name\" : \"square.svg\", \"name\" : \"a\" } ] },\n"
        "    { \"layers\" : [ { \"image-name\" : \"red.svg\", \"name\" : \"b\" } ] }\n"
        "  ]\n}\n";
    const TempBundle gWhiteB(gwDoc);
    auto gWhiteb = icf::IconBundle::open(gWhiteB.path());
    REQUIRE(gWhiteb.has_value());
    auto gWhite = renderIcon(d, *gWhiteb, o);
    REQUIRE(gWhite.has_value());
    CHECK_EQ(gWhite->drawn, std::size_t{2});
    const std::string grDoc =
        "{\n  \"groups\" : [\n"
        "    { \"layers\" : [ { \"image-name\" : \"red.svg\", \"name\" : \"a\" } ] },\n"
        "    { \"layers\" : [ { \"image-name\" : \"square.svg\", \"name\" : \"b\" } ] }\n"
        "  ]\n}\n";
    const TempBundle gRedB(grDoc);
    auto gRedb = icf::IconBundle::open(gRedB.path());
    REQUIRE(gRedb.has_value());
    auto gRed = renderIcon(d, *gRedb, o);
    REQUIRE(gRed.has_value());
    CHECK_EQ(gRed->drawn, std::size_t{2});
    // Groups: whichever is listed FIRST wins the pixel.
    CHECK(channelAt(*gWhite, 32, 32, 1) > 0.9f);
    CHECK(channelAt(*gRed, 32, 32, 1) < 0.1f);

    const std::string lwDoc =
        "{\n  \"groups\" : [\n    {\n      \"layers\" : [\n"
        "        { \"image-name\" : \"square.svg\", \"name\" : \"a\" },\n"
        "        { \"image-name\" : \"red.svg\", \"name\" : \"b\" }\n"
        "      ]\n    }\n  ]\n}\n";
    const TempBundle lWhiteB(lwDoc);
    auto lWhiteb = icf::IconBundle::open(lWhiteB.path());
    REQUIRE(lWhiteb.has_value());
    auto lWhite = renderIcon(d, *lWhiteb, o);
    REQUIRE(lWhite.has_value());
    CHECK_EQ(lWhite->drawn, std::size_t{2});
    const std::string lrDoc =
        "{\n  \"groups\" : [\n    {\n      \"layers\" : [\n"
        "        { \"image-name\" : \"red.svg\", \"name\" : \"a\" },\n"
        "        { \"image-name\" : \"square.svg\", \"name\" : \"b\" }\n"
        "      ]\n    }\n  ]\n}\n";
    const TempBundle lRedB(lrDoc);
    auto lRedb = icf::IconBundle::open(lRedB.path());
    REQUIRE(lRedb.has_value());
    auto lRed = renderIcon(d, *lRedb, o);
    REQUIRE(lRed.has_value());
    CHECK_EQ(lRed->drawn, std::size_t{2});
    // Layers inside one group: the same rule, one level down.
    CHECK(channelAt(*lWhite, 32, 32, 1) > 0.9f);
    CHECK(channelAt(*lRed, 32, 32, 1) < 0.1f);
}

// ---- the group, composited as ONE thing ------------------------------------
//
// `[BIN]` `FinalizedIcon.Layer` -- a document GROUP -- carries one image, one
// SDF and one shadow image, and the compositor (`IconRendering` `0x48B74`) has
// no element loop. Until 2026-10-01 this renderer ran every effect per LAYER and
// let a layer's blend meet the whole icon. The cases below pin what moved, each
// against the reading and none against a picture.

namespace {

// A group in FRONT of an opaque red backdrop that fills the canvas. `layers` is
// the front group's layer array, verbatim, and `groupExtra` its other keys.
std::string overRed(const std::string& groupExtra, const std::string& layers) {
    return "{\n  \"groups\" : [\n    { " + groupExtra + "\"layers\" : [\n" + layers +
           "\n      ] },\n"
           "    { \"layers\" : [ { \"image-name\" : \"red.svg\", \"name\" : \"back\",\n"
           "        \"glass\" : false,\n"
           "        \"position\" : { \"scale\" : 2, \"translation-in-points\" : [0, 0] } } ] }\n"
           "  ]\n}\n";
}

}  // namespace

// `[BIN]` AN ELEMENT'S BLEND MEETS ITS OWN GROUP, NOT THE ICON. The finaliser
// draws a group's elements into one list (`0x1A9D0`) and rasterises that alone
// (`0x13590`), applying each element's opacity and blend inside it
// (`0x1AD9C`/`0x1B448`): an element blends against the EARLIER ELEMENTS OF ITS
// GROUP over transparent.
//
// Two red layers at half opacity, the upper one `plus-lighter`, over an opaque
// red backdrop. Inside the group: `(0.5, 0, 0, 0.5)` plus `(0.5, 0, 0, 0.5)` is
// `(1, 0, 0, 1)`, an opaque red, and that over the backdrop is the backdrop's
// own red -- premultiplied red 1.0. Blended against the ICON, as it used to be,
// the upper layer adds its 0.5 to a red that is already 1.0 and the pixel
// reaches 1.5.
TEST_CASE(a_layers_blend_meets_its_own_group_and_not_the_icon) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 64;
    const TempBundle tb(overRed(
        "\"specular\" : false, ",
        "        { \"image-name\" : \"red.svg\", \"name\" : \"upper\", \"glass\" : false,\n"
        "          \"opacity\" : 0.5, \"blend-mode\" : \"plus-lighter\" },\n"
        "        { \"image-name\" : \"red.svg\", \"name\" : \"lower\", \"glass\" : false,\n"
        "          \"opacity\" : 0.5 }"));
    auto bundle = icf::IconBundle::open(tb.path());
    REQUIRE(bundle.has_value());
    auto icon = renderIcon(d, *bundle, o);
    REQUIRE(icon.has_value());
    CHECK_EQ(icon->drawn, std::size_t{3});
    CHECK(icon->skipped.empty());
    const float red = channelAt(*icon, 32, 32, 0) * alphaAt(*icon, 32, 32);
    CHECK(std::fabs(red - 1.0f) < 0.02f);
    CHECK(std::fabs(alphaAt(*icon, 32, 32) - 1.0f) < 0.02f);
}

// `[BIN]` THE TRANSLUCENCY IS A CLIP OVER THE WHOLE GROUP IMAGE. The content
// pass opens with it (`0x4ACB4`-`0x4AD88`: `beginLayer`, the mask from the SDF,
// `clipLayerWithAlpha:1 mode:0`) and draws the group's image under it, so it
// fades every element the group's field covers -- the ones that do not take
// part in the glass included. Until 2026-10-01 it multiplied only the glass
// layer's own art, and a plain layer on top of it stayed opaque.
//
// The group here is a glass square with a PLAIN red square over it. The frame
// is the glass square's, the image is the red one's, and the alpha at the
// centre is the mask's own value there -- computed below from the transcribed
// mask, not read off the render: `f = 1.0 x 0.5`, so the lower bound is
// `1 - (1 - 0) * 0.5 = 0.5` against an upper bound of 1, the ramp runs down the
// glass element's box, and the result goes through `a * a * (3 - 2a)`.
//
// `[BIN]` AND IN GENERATION 27 THE MASK IS A GRADIENT. `useSimpleMask` takes
// the other branch of `0x0000FD28` (`0x0000FE04`-`0x0001064C`): an axial
// gradient whose rect is the frame on the canvas, with no texture border. Until
// 2026-10-01 this case computed the rect grown by the SDF's one-texel border,
// because the mask drawn was generation 26's shader. The case below this one
// holds what the gradient does that the shader does not.
TEST_CASE(the_translucency_fades_the_whole_group_image_plain_layers_included) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 64;
    const std::string doc =
        "{\n  \"groups\" : [\n    { \"specular\" : false,\n"
        "      \"translucency\" : { \"enabled\" : true, \"value\" : 0.5 },\n"
        "      \"layers\" : [\n"
        "        { \"image-name\" : \"red.svg\", \"name\" : \"plain\", \"glass\" : false },\n"
        "        { \"image-name\" : \"square.svg\", \"name\" : \"glass\", \"glass\" : true }\n"
        "      ] }\n  ]\n}\n";
    const TempBundle tb(doc);
    auto bundle = icf::IconBundle::open(tb.path());
    REQUIRE(bundle.has_value());
    auto icon = renderIcon(d, *bundle, o);
    REQUIRE(icon.has_value());
    CHECK_EQ(icon->drawn, std::size_t{2});
    CHECK_EQ(icon->glassTranslucent, std::size_t{1});

    // The glass square spans rows 16..48 of 64, and the gradient's rect is that
    // box on the canvas: `v = (y - 16) / 32` at the pixel centre 32.5. The
    // seventeen stops are `S(1 - 0.5 k/16)` and the cubic between two of them
    // stays within a thousandth of `S` itself.
    const double v = (32.5 - 16.0) / 32.0;
    const double body = 1.0 + (0.5 - 1.0) * v;
    const double mask = body * body * (3.0 - 2.0 * body);
    std::printf("  centre alpha %.5f, mask %.5f\n", static_cast<double>(alphaAt(*icon, 32, 32)),
                mask);
    CHECK(std::fabs(alphaAt(*icon, 32, 32) - static_cast<float>(mask)) < 0.005f);
    // And it is the PLAIN layer that was faded: the pixel is still red.
    CHECK(channelAt(*icon, 32, 32, 0) > 0.98f);
    CHECK(channelAt(*icon, 32, 32, 1) < 0.02f);

}

// `[BIN]` THE GRADIENT IS LAID ON AN INFINITE SHAPE (`setInfinite`,
// `0x00010524`), so in generation 27 the mask has no silhouette: it fades the
// group's image wherever there is image, inside the glass elements' field or
// not. The frame it is measured in is still the glass elements' own.
//
// A plain red square over a glass square at HALF its size. The frame is the
// small one's box, rows 24..40 of 64; the red one spans rows 16..48. Beside the
// small square the red layer carries the row's mask to the float; above the
// frame the ramp is held at its first stop, `S(1) = 1`; below it at its last,
// `S(1 - 0.5) = 0.5`. Generation 26's shader leaves all three opaque: they are
// outside the field.
TEST_CASE(the_generation_27_mask_reaches_the_group_image_outside_the_glass) {
    Device& d = gpu();
    if (!d.valid()) return;
    const std::string doc =
        "{\n  \"groups\" : [\n    { \"specular\" : false,\n"
        "      \"translucency\" : { \"enabled\" : true, \"value\" : 0.5 },\n"
        "      \"layers\" : [\n"
        "        { \"image-name\" : \"red.svg\", \"name\" : \"plain\", \"glass\" : false },\n"
        "        { \"image-name\" : \"square.svg\", \"name\" : \"glass\", \"glass\" : true,\n"
        "          \"position\" : { \"scale\" : 0.5, \"translation-in-points\" : [0, 0] } }\n"
        "      ] }\n  ]\n}\n";
    const TempBundle tb(doc);
    auto bundle = icf::IconBundle::open(tb.path());
    REQUIRE(bundle.has_value());
    IconRenderOptions o;
    o.size = 64;
    auto g27 = renderIcon(d, *bundle, o);
    REQUIRE(g27.has_value());
    CHECK_EQ(g27->drawn, std::size_t{2});
    CHECK_EQ(g27->glassTranslucent, std::size_t{1});
    // The row through the middle: over the glass square and beside it.
    CHECK(alphaAt(*g27, 32, 32) < 0.9f);
    CHECK_EQ(alphaAt(*g27, 18, 32), alphaAt(*g27, 32, 32));
    CHECK_EQ(alphaAt(*g27, 46, 32), alphaAt(*g27, 32, 32));
    // Above the frame and below it, still on the red layer.
    CHECK(std::fabs(alphaAt(*g27, 32, 18) - 1.0f) < 1e-5f);
    CHECK(std::fabs(alphaAt(*g27, 32, 46) - 0.5f) < 1e-5f);
    // No pixel is "missed": the blind-spot sentence has nothing to count.
    for (const std::string& g : g27->shapeGaps) {
        CHECK(g.find("translucidez aplicada com buraco") == std::string::npos);
    }

    o.generation = DesignGeneration::G26;
    auto g26 = renderIcon(d, *bundle, o);
    REQUIRE(g26.has_value());
    CHECK_EQ(g26->glassTranslucent, std::size_t{1});
    CHECK(alphaAt(*g26, 32, 32) < 0.9f);                       // inside the field: faded
    CHECK(std::fabs(alphaAt(*g26, 18, 32) - 1.0f) < 1e-5f);    // beside it: the shader's `cov` is 0
    CHECK(std::fabs(alphaAt(*g26, 32, 18) - 1.0f) < 1e-5f);
    CHECK(std::fabs(alphaAt(*g26, 32, 46) - 1.0f) < 1e-5f);
}

// `[BIN]` THE SHADOW IS CAST FROM THE ART BEFORE THE MASK. The finaliser builds
// `shadowImage` from a list of its own (`0x1C0DC`) through colour, translate,
// blur and the ring clip (`0x19468`-`0x195CC`), and the translucency mask is
// nowhere in that chain. Until 2026-10-01 this renderer cast it from the art
// AFTER the mask, so a translucent group cast a thinner shadow.
//
// So switching the translucency on must not move the shadow. The probe sits
// below the square, where only the shadow reaches (and where the overdraw, which
// is clipped to the content, cannot), and the two renders must agree there to
// the float -- same source, same blur, same backdrop.
TEST_CASE(the_shadow_is_cast_from_the_art_before_the_translucency_mask) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 256;

    auto render = [&](const char* translucency) -> std::optional<RenderedIcon> {
        const TempBundle tb(overRed(
            std::string("\"specular\" : false,\n"
                        "      \"shadow\" : { \"kind\" : \"neutral\", \"opacity\" : 1.0 },\n"
                        "      \"translucency\" : ") + translucency + ",\n      ",
            "        { \"image-name\" : \"square.svg\", \"name\" : \"front\" }"));
        auto bundle = icf::IconBundle::open(tb.path());
        if (!bundle) return std::nullopt;
        auto icon = renderIcon(d, *bundle, o);
        if (!icon) return std::nullopt;
        return std::move(*icon);
    };
    auto opaque = render("{ \"enabled\" : false, \"value\" : 0.5 }");
    auto faded = render("{ \"enabled\" : true, \"value\" : 0.5 }");
    REQUIRE(opaque && faded);
    CHECK_EQ(opaque->glassShadowed, std::size_t{1});
    CHECK_EQ(faded->glassShadowed, std::size_t{1});
    CHECK_EQ(opaque->glassTranslucent, std::size_t{0});
    CHECK_EQ(faded->glassTranslucent, std::size_t{1});

    // The shadow is really there...
    CHECK(1.0f - channelAt(*opaque, 128, 200, 0) > 0.02f);
    // ...and the mask did not touch it.
    for (int k = 0; k < 4; ++k) {
        CHECK_EQ(channelAt(*faded, 128, 200, k), channelAt(*opaque, 128, 200, k));
    }
    // While the art itself WAS faded, or the case would pass with the mask off.
    CHECK(alphaAt(*faded, 128, 128) > 0.9f);   // over an opaque backdrop
    CHECK(channelAt(*faded, 128, 128, 1) < channelAt(*opaque, 128, 128, 1) - 0.05f);
}

// `[BIN]` THE OVERDRAW RUNS ONLY OVER A GROUP THAT BLENDS NORMALLY. `0x4ADBC`
// (and `0x45F10`) require the byte at `[descriptor+0x31]` to be zero before the
// pass opens, and that byte is `FinalizedIcon.Layer.blendMode`: the group's
// `blend-mode`, where zero is `normal`. Until 2026-10-01 the gate was carried as
// unread and the pass ran over every group.
//
// The main shadow has no such gate, so it is the control: one shadow either
// way, and the second composite only without the blend.
TEST_CASE(the_shadow_overdraw_is_gated_on_a_normal_group_blend) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 128;

    auto render = [&](const char* blend) -> std::optional<RenderedIcon> {
        const TempBundle tb(overRed(
            std::string("\"specular\" : false,\n"
                        "      \"blend-mode\" : \"") + blend + "\",\n"
                        "      \"shadow\" : { \"kind\" : \"neutral\", \"opacity\" : 1.0 },\n"
                        "      \"translucency\" : { \"enabled\" : true, \"value\" : 0.5 },\n      ",
            "        { \"image-name\" : \"square.svg\", \"name\" : \"front\" }"));
        auto bundle = icf::IconBundle::open(tb.path());
        if (!bundle) return std::nullopt;
        auto icon = renderIcon(d, *bundle, o);
        if (!icon) return std::nullopt;
        return std::move(*icon);
    };
    auto normal = render("normal");
    auto blended = render("plus-lighter");
    REQUIRE(normal && blended);
    CHECK_EQ(normal->glassShadowed, std::size_t{1});
    CHECK_EQ(blended->glassShadowed, std::size_t{1});
    CHECK_EQ(normal->glassShadowOverdrawn, std::size_t{1});
    CHECK_EQ(blended->glassShadowOverdrawn, std::size_t{0});
}

// `[BIN]` ONE OF EACH PER GROUP. Three glass layers in one group, with every
// effect the material has: the group casts ONE shadow from its flattened source
// (`0x1C0DC`), refracts ONCE (`0x4A5E4`), is clipped by ONE mask (`0x4ACB4`) and
// is lit by ONE highlight pass (`0x491C0`). Until 2026-10-01 each layer did all
// four, and the counters said three.
//
// The three squares are staggered so the group has a silhouette no single layer
// has, and the layers are still counted one by one where they belong: `drawn`.
TEST_CASE(a_groups_glass_effects_run_once_per_group_and_not_per_layer) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 128;
    auto layer = [](const char* name, int tx, int ty) {
        return std::string("        { \"image-name\" : \"square.svg\", \"name\" : \"") + name +
               "\",\n          \"position\" : { \"scale\" : 0.6, \"translation-in-points\" : [" +
               std::to_string(tx) + ", " + std::to_string(ty) + "] } }";
    };
    const TempBundle tb(overRed(
        "\"specular\" : true,\n"
        "      \"shadow\" : { \"kind\" : \"neutral\", \"opacity\" : 1.0 },\n"
        "      \"translucency\" : { \"enabled\" : true, \"value\" : 0.5 },\n"
        "      \"refractivity\" : { \"enabled\" : true, \"depth\" : 0.25, \"strength\" : -0.1 },\n"
        "      ",
        layer("a", -150, -150) + ",\n" + layer("b", 0, 0) + ",\n" + layer("c", 150, 150)));
    auto bundle = icf::IconBundle::open(tb.path());
    REQUIRE(bundle.has_value());
    auto icon = renderIcon(d, *bundle, o);
    REQUIRE(icon.has_value());
    CHECK_EQ(icon->drawn, std::size_t{4});   // the three, and the backdrop
    CHECK(icon->skipped.empty());
    CHECK_EQ(icon->glassShadowed, std::size_t{1});
    CHECK_EQ(icon->glassShadowOverdrawn, std::size_t{1});
    CHECK_EQ(icon->glassTranslucent, std::size_t{1});
    CHECK_EQ(icon->glassRefracted, std::size_t{1});
    CHECK_EQ(icon->glassSpecular, std::size_t{1});
    // And the stack that makes one field out of three says so.
    bool stacked = false;
    for (const std::string& n : icon->notes) {
        if (n == kFieldStackNote) stacked = true;
    }
    CHECK(stacked);
}

// ---- `lighting`: one field per group, built two ways -----------------------

namespace {

// Two glass squares that ABUT: 256 points a side, the left one ending and the
// right one starting on the canvas's centre line, over an opaque red backdrop.
// `lighting` is the group key verbatim (with its trailing comma), or empty.
std::string abuttingGlass(const std::string& lighting, bool specular) {
    return overRed(
        lighting + "\"specular\" : " + (specular ? "true" : "false") + ",\n      ",
        "        { \"image-name\" : \"square.svg\", \"name\" : \"right\",\n"
        "          \"position\" : { \"scale\" : 0.5, \"translation-in-points\" : [128, 0] } },\n"
        "        { \"image-name\" : \"square.svg\", \"name\" : \"left\",\n"
        "          \"position\" : { \"scale\" : 0.5, \"translation-in-points\" : [-128, 0] } }");
}

// The largest change the highlights make in a window astride the seam, halfway
// down it: each render against its own twin with `specular` off, so nothing but
// the highlight pass is measured. At 256 px the squares span x 64..128 and
// 128..192 and y 96..160, so the window is 32 px from the top and the bottom
// edges -- five times the reach of the widest highlight (24 points, 6 px).
float seamHighlight(Device& d, const std::string& lighting, std::vector<std::string>* notes) {
    IconRenderOptions o;
    o.size = 256;
    std::vector<float> lit, flat;
    for (const bool specular : {true, false}) {
        const TempBundle tb(abuttingGlass(lighting, specular));
        auto bundle = icf::IconBundle::open(tb.path());
        if (!bundle) return -1.0f;
        auto icon = renderIcon(d, *bundle, o);
        if (!icon || icon->drawn != 3) return -1.0f;
        if (specular && notes) *notes = icon->notes;
        (specular ? lit : flat) = icon->rgba;
    }
    float worst = 0.0f;
    for (std::uint32_t y = 118; y < 138; ++y) {
        for (std::uint32_t x = 122; x < 134; ++x) {
            for (int k = 0; k < 4; ++k) {
                const std::size_t i = (static_cast<std::size_t>(y) * 256 + x) * 4 + k;
                worst = std::fmax(worst, std::fabs(lit[i] - flat[i]));
            }
        }
    }
    return worst;
}

bool says(const std::vector<std::string>& notes, const char* note) {
    for (const std::string& n : notes) {
        if (n == note) return true;
    }
    return false;
}

}  // namespace

// `[BIN]` `lighting` IS `performsLightingByElement`, AND IT DECIDES HOW THE
// GROUP'S ONE FIELD IS BUILT (`IconRendering` `0x1CB90`, the branch at
// `0x1CDD8`). Element by element, every glass element gets a field of its own
// and they are stacked, so each keeps its own rim. `combined`, all the
// silhouettes go into ONE list and one field is taken of it: the field of the
// UNION, in which a seam between two elements is deep inside the shape.
//
// So where two glass squares abut, `individual` puts a highlight along the seam
// and `combined` puts NONE -- not a fainter one. The seam's middle is 32 px from
// the union's nearest edge and no highlight reaches past 6, so the highlight
// pass leaves those pixels exactly as it found them.
//
// `[BIN]` And a MISSING key is `individual`: the converter passes
// `cmp w19, #0; cset w3, eq` (`IconComposerKit` `0x10CA38`-`0x10CB08`), case 0
// is `individual`, and the key's default is 0 (`IconComposerFoundation`
// `0x90978`). Until 2026-10-01 the key was read and nothing consumed it.
TEST_CASE(combined_lighting_draws_no_rim_where_two_glass_elements_meet) {
    Device& d = gpu();
    if (!d.valid()) return;

    std::vector<std::string> individualNotes, combinedNotes, absentNotes;
    const float individual =
        seamHighlight(d, "\"lighting\" : \"individual\",\n      ", &individualNotes);
    const float combined =
        seamHighlight(d, "\"lighting\" : \"combined\",\n      ", &combinedNotes);
    const float absent = seamHighlight(d, "", &absentNotes);
    std::printf("  highlight on the seam: individual %.5f, combined %.5f, no key %.5f\n",
                individual, combined, absent);

    CHECK(individual > 0.01f);         // a rim along the seam
    CHECK_EQ(combined, 0.0f);          // and none at all in the union
    CHECK_EQ(absent, individual);      // no key is `individual`, to the float

    // Each mode says which field it built, and only that one.
    CHECK(says(individualNotes, kFieldStackNote));
    CHECK(!says(individualNotes, kCombinedFieldNote));
    CHECK(says(combinedNotes, kCombinedFieldNote));
    CHECK(!says(combinedNotes, kFieldStackNote));
    CHECK(says(absentNotes, kFieldStackNote));
}

// `[BIN]` THE QUIRK OF THE COMBINED LIST, reproduced and not corrected. The loop
// that draws the silhouettes is `isOpaque = isOpaque && draw(element)`
// (`0x1D15C`-`0x1D194`), and the draw answers 0 for raster content (`0x1B6FC`).
// `&&` stops evaluating once its left side is false, so the first RASTER glass
// element is drawn and every glass element after it -- in front of it -- is not.
//
// So a vector glass layer in front of a raster one shapes nothing: the field is
// the same as if that layer did not take part in the glass at all. With no
// shadow and no translucency the field is the only thing its `glass` bit
// reaches, and the two documents must then render to the same floats.
TEST_CASE(combined_lighting_leaves_out_the_glass_elements_after_the_first_raster) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 128;

    auto render = [&](const char* frontGlass) -> std::optional<RenderedIcon> {
        const TempBundle tb(overRed(
            "\"lighting\" : \"combined\",\n      \"specular\" : true,\n      ",
            std::string("        { \"image-name\" : \"square.svg\", \"name\" : \"front\",\n"
                        "          \"glass\" : ") + frontGlass + ",\n"
                        "          \"position\" : { \"scale\" : 0.4, \"translation-in-points\" : [150, 150] } },\n"
                        "        { \"image-name\" : \"edge.png\", \"name\" : \"raster\",\n"
                        "          \"position\" : { \"scale\" : 6, \"translation-in-points\" : [-150, 0] } },\n"
                        "        { \"image-name\" : \"square.svg\", \"name\" : \"back\",\n"
                        "          \"position\" : { \"scale\" : 0.4, \"translation-in-points\" : [150, -150] } }"));
        auto bundle = icf::IconBundle::open(tb.path());
        if (!bundle) return std::nullopt;
        auto icon = renderIcon(d, *bundle, o);
        if (!icon) return std::nullopt;
        return std::move(*icon);
    };
    auto glass = render("true");
    auto plain = render("false");
    REQUIRE(glass && plain);
    CHECK_EQ(glass->drawn, std::size_t{4});
    CHECK(glass->skipped.empty());
    CHECK_EQ(glass->glassSpecular, std::size_t{1});

    // The quirk is said, and only where it bites: in `plain` nothing comes
    // after the raster that the field would have taken.
    CHECK(says(glass->notes, kCombinedRasterNote));
    CHECK(!says(plain->notes, kCombinedRasterNote));
    CHECK(says(plain->notes, kCombinedFieldNote));

    REQUIRE(glass->rgba.size() == plain->rgba.size());
    std::size_t differing = 0;
    for (std::size_t i = 0; i < glass->rgba.size(); ++i) {
        if (glass->rgba[i] != plain->rgba[i]) ++differing;
    }
    std::printf("  front glass layer left out: %zu of %zu components differ\n", differing,
                glass->rgba.size());
    CHECK_EQ(differing, std::size_t{0});
}