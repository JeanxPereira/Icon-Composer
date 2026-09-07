// The connector: an SVG document drawn into pixels.
//
// The stages below each pass a gate of their own; this is the first test that
// asks whether they compose. It matters because a stage gate cannot catch a
// composition error -- P5's two defects (the NDC y, and the quad's corner order)
// both survived their unit tests and died only when a pixel met a closed form.
//
// THE ORACLE IS STILL GEOMETRY'S. A rectangle's per-pixel coverage has an exact
// closed form, so a render that disagrees with it is wrong however plausible it
// looks. What is new here is that the comparison now runs through the WHOLE
// chain -- viewBox fit, path buffer, coverage, fill rule, composite -- instead
// of one stage of it.
#include "check.h"
#include "Source/CoreSVG/Document.h"
#include "Source/IconComposerFoundation/Png.h"
#include "Source/RenderBox/SvgRenderer.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace rb;

namespace {

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

std::string svgWith(const char* viewBox, const std::string& body) {
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" + std::string(viewBox) +
           "\">" + body + "</svg>";
}

float alphaAt(const RenderedImage& img, std::uint32_t x, std::uint32_t y) {
    return img.rgba[(static_cast<std::size_t>(y) * img.width + x) * 4 + 3];
}

float channelAt(const RenderedImage& img, std::uint32_t x, std::uint32_t y, int k) {
    return img.rgba[(static_cast<std::size_t>(y) * img.width + x) * 4 + k];
}

// The overlap of one pixel with an axis-aligned rectangle, in closed form.
float exactRectCoverage(std::uint32_t px, std::uint32_t py, float x0, float y0, float x1,
                        float y1) {
    const float l = std::fmax(static_cast<float>(px), x0);
    const float r = std::fmin(static_cast<float>(px) + 1.0f, x1);
    const float b = std::fmax(static_cast<float>(py), y0);
    const float t = std::fmin(static_cast<float>(py) + 1.0f, y1);
    const float w = r > l ? r - l : 0.0f;
    const float h = t > b ? t - b : 0.0f;
    return w * h;
}

}  // namespace

// The mapping the whole render hangs on, checked without a GPU: a viewBox is fit
// uniformly and CENTRED, and neither axis is flipped -- SVG's y and the raster's
// y both point down.
TEST_CASE(the_viewbox_is_fit_uniformly_and_centred) {
    // A square box into a square target: scale 4, no letterbox.
    const PathGlobals a = fitViewBox({0, 0, 16, 16}, 64, 64);
    CHECK_EQ(a.m0[0], 4.0f);
    CHECK_EQ(a.m1[1], 4.0f);
    CHECK_EQ(a.m2[0], 0.0f);
    CHECK_EQ(a.m2[1], 0.0f);
    CHECK_EQ(a.m0[1], 0.0f);
    CHECK_EQ(a.m1[0], 0.0f);

    // A WIDE box into a square target: the scale comes from the wide axis and
    // the short one is centred. This is the case a non-uniform fit would pass
    // and a mis-centred one would fail.
    const PathGlobals b = fitViewBox({0, 0, 32, 16}, 64, 64);
    CHECK_EQ(b.m0[0], 2.0f);
    CHECK_EQ(b.m1[1], 2.0f);
    CHECK_EQ(b.m2[0], 0.0f);
    CHECK_EQ(b.m2[1], 16.0f);

    // A TALL box into a square target: now the letterbox is HORIZONTAL.
    //
    // This case exists because the mutation sweep found its absence. Every other
    // case here has the x axis filling the target, so `(width - s*w) * 0.5` is
    // zero in all of them -- and a fit that dropped the horizontal centring
    // entirely passed the whole suite. The vertical centring was checked and the
    // horizontal one never was, which is the same systematic blind spot that
    // once left the coverage clamp unexercised: cases chosen for variety that
    // happen to agree on the one term under test.
    const PathGlobals t = fitViewBox({0, 0, 16, 32}, 64, 64);
    CHECK_EQ(t.m0[0], 2.0f);
    CHECK_EQ(t.m2[0], 16.0f);
    CHECK_EQ(t.m2[1], 0.0f);

    // A box with a non-zero ORIGIN must be translated by it, not just scaled.
    const PathGlobals c = fitViewBox({8, 4, 16, 16}, 64, 64);
    CHECK_EQ(c.m2[0], -32.0f);
    CHECK_EQ(c.m2[1], -16.0f);

    // And every render needs the whole row reachable from an edge.
    CHECK_EQ(a.urx, 64.0f);
    CHECK_EQ(a.twoOverSize[0], 2.0f / 64.0f);
}

// End to end, against the closed form. The rectangle is placed on halves so that
// the edge pixels are partially covered -- a test whose shape lands on integer
// boundaries would pass with no anti-aliasing at all.
TEST_CASE(a_filled_rectangle_agrees_with_its_exact_coverage) {
    Device& d = gpu();
    if (!d.valid()) return;
    constexpr std::uint32_t kSize = 32;
    // viewBox 0 0 32 32 into 32x32 is scale 1, so user units ARE pixels and the
    // closed form can be written in the same numbers as the path.
    const std::string svg = svgWith(
        "0 0 32 32", "<path d=\"M4.5 6.25 L20.75 6.25 L20.75 24.5 L4.5 24.5 Z\" fill=\"#ffffff\"/>");
    auto doc = icf::svg::SvgDocument::parse(svg);
    REQUIRE(doc.has_value());

    RenderOptions o;
    o.width = kSize;
    o.height = kSize;
    o.subdivisions = 1;
    auto img = renderSvg(d, *doc, o);
    if (!img) {
        std::printf("  FAIL render: %s\n", img.error().c_str());
        ++ictest::failures();
        return;
    }
    CHECK_EQ(img->drawn, std::size_t{1});
    CHECK(img->skipped.empty());
    REQUIRE(img->rgba.size() == static_cast<std::size_t>(kSize) * kSize * 4);

    int bad = 0;
    float worst = 0.0f;
    for (std::uint32_t y = 0; y < kSize; ++y) {
        for (std::uint32_t x = 0; x < kSize; ++x) {
            const float want = exactRectCoverage(x, y, 4.5f, 6.25f, 20.75f, 24.5f);
            const float got = alphaAt(*img, x, y);
            const float gap = std::fabs(got - want);
            worst = std::fmax(worst, gap);
            // `half` is what the coverage target stores, so the allowance is one
            // step of that grid over [0.5, 1).
            if (gap > 0.0009765625f) {
                if (bad < 3) {
                    std::printf("  FAIL px(%u,%u) alpha %.6f, exact %.6f\n", x, y, got, want);
                }
                ++bad;
            }
        }
    }
    if (bad) std::printf("  worst gap %.6f over %d pixel(s)\n", worst, bad);
    CHECK_EQ(bad, 0);
}

// The centring is not decoration: a wide viewBox in a square target must land in
// the middle band and leave the rest empty. A render that ignored the translate
// would still fill pixels, just the wrong ones.
TEST_CASE(a_wide_viewbox_is_letterboxed_not_stretched) {
    Device& d = gpu();
    if (!d.valid()) return;
    const std::string svg =
        svgWith("0 0 32 16", "<path d=\"M0 0 L32 0 L32 16 L0 16 Z\" fill=\"#ffffff\"/>");
    auto doc = icf::svg::SvgDocument::parse(svg);
    REQUIRE(doc.has_value());
    RenderOptions o;
    o.width = 32;
    o.height = 32;
    o.subdivisions = 1;
    auto img = renderSvg(d, *doc, o);
    REQUIRE(img.has_value());

    CHECK(alphaAt(*img, 16, 16) > 0.99f);   // inside the band
    CHECK(alphaAt(*img, 16, 2) < 0.01f);    // above it
    CHECK(alphaAt(*img, 16, 29) < 0.01f);   // below it
    CHECK(alphaAt(*img, 16, 9) > 0.99f);    // the band starts at y = 8
    CHECK(alphaAt(*img, 16, 6) < 0.01f);
}

// The colour has to survive the round trip un-multiplied, and the alpha of the
// paint has to reach the output. A renderer that returned premultiplied values
// would pass an alpha check and fail this one.
TEST_CASE(the_fill_colour_and_its_alpha_reach_the_pixels) {
    Device& d = gpu();
    if (!d.valid()) return;
    const std::string svg = svgWith(
        "0 0 16 16",
        "<path d=\"M0 0 L16 0 L16 16 L0 16 Z\" fill=\"#3366cc\" fill-opacity=\"0.5\"/>");
    auto doc = icf::svg::SvgDocument::parse(svg);
    REQUIRE(doc.has_value());
    RenderOptions o;
    o.width = 16;
    o.height = 16;
    o.subdivisions = 1;
    auto img = renderSvg(d, *doc, o);
    REQUIRE(img.has_value());

    CHECK(std::fabs(alphaAt(*img, 8, 8) - 0.5f) < 0.01f);
    CHECK(std::fabs(channelAt(*img, 8, 8, 0) - 0x33 / 255.0f) < 0.01f);
    CHECK(std::fabs(channelAt(*img, 8, 8, 1) - 0x66 / 255.0f) < 0.01f);
    CHECK(std::fabs(channelAt(*img, 8, 8, 2) - 0xcc / 255.0f) < 0.01f);
}

// The even-odd rule is a BIT, and the two rules must disagree where it counts:
// a ring drawn as one path with two subpaths is hollow under even-odd and solid
// under non-zero when the windings agree.
TEST_CASE(the_fill_rule_reaches_the_render) {
    Device& d = gpu();
    if (!d.valid()) return;
    // Both squares wound the SAME way, so non-zero gives 2 inside the hole and
    // even-odd gives 0. A rule that was ignored would draw the same twice.
    const char* ring =
        "M0 0 L32 0 L32 32 L0 32 Z M8 8 L24 8 L24 24 L8 24 Z";
    for (const char* rule : {"nonzero", "evenodd"}) {
        const std::string svg =
            svgWith("0 0 32 32", std::string("<path d=\"") + ring + "\" fill=\"#ffffff\" fill-rule=\"" +
                                     rule + "\"/>");
        auto doc = icf::svg::SvgDocument::parse(svg);
        REQUIRE(doc.has_value());
        RenderOptions o;
        o.width = 32;
        o.height = 32;
        o.subdivisions = 1;
        auto img = renderSvg(d, *doc, o);
        REQUIRE(img.has_value());
        const float hole = alphaAt(*img, 16, 16);
        const float band = alphaAt(*img, 3, 16);
        CHECK(band > 0.99f);
        if (std::string(rule) == "evenodd") {
            CHECK(hole < 0.01f);
        } else {
            CHECK(hole > 0.99f);
        }
    }
}

// A shape this cannot draw is NAMED, not dropped. Silence is what turns a gap
// into a wrong picture nobody questions.
TEST_CASE(a_shape_that_cannot_be_drawn_is_named) {
    Device& d = gpu();
    if (!d.valid()) return;
    // A reference that resolves is DRAWN now -- that is what the gradient work
    // bought. What is still named is one that does not resolve, and `fill="none"`
    // is still not named at all, because a deliberate non-painting is not a gap
    // and reporting it would drown the real ones.
    const std::string svg = svgWith(
        "0 0 16 16",
        "<defs><linearGradient id=\"g\" x1=\"0\" y1=\"0\" x2=\"16\" y2=\"0\" "
        "gradientUnits=\"userSpaceOnUse\">"
        "<stop offset=\"0\" stop-color=\"#000000\"/>"
        "<stop offset=\"1\" stop-color=\"#ffffff\"/></linearGradient></defs>"
        "<path d=\"M0 0 L16 0 L16 16 L0 16 Z\" fill=\"url(#g)\"/>"
        "<path d=\"M0 0 L8 0 L8 8 L0 8 Z\" fill=\"url(#nao-existe)\"/>"
        "<path d=\"M0 0 L4 0 L4 4 L0 4 Z\" fill=\"none\"/>");
    auto doc = icf::svg::SvgDocument::parse(svg);
    REQUIRE(doc.has_value());
    RenderOptions o;
    o.width = 16;
    o.height = 16;
    o.subdivisions = 1;
    auto img = renderSvg(d, *doc, o);
    REQUIRE(img.has_value());

    CHECK_EQ(img->drawn, std::size_t{1});
    REQUIRE(img->skipped.size() == 1);
    CHECK(img->skipped[0].why.find("nao-existe") != std::string::npos);

    // And the gradient it DID draw runs dark to light across the row, which is
    // the whole point: a ramp that came back as one flat colour would still
    // count as drawn.
    const float left = channelAt(*img, 1, 8, 0);
    const float right = channelAt(*img, 14, 8, 0);
    CHECK(left < 0.15f);
    CHECK(right > 0.85f);
    CHECK(right - left > 0.7f);
}


// ---- the gradient, resolved and painted ---------------------------------

// `userSpaceOnUse` -- 159 of the corpus's 161 -- leaves the axis in the
// document's own coordinates. The ramp has to run along that axis and nowhere
// else, so this checks BOTH ends and the middle: a map that got the direction
// right and the scale wrong would pass a two-point check.
TEST_CASE(a_user_space_gradient_runs_along_its_own_axis) {
    Device& d = gpu();
    if (!d.valid()) return;
    const std::string svg = svgWith(
        "0 0 32 32",
        "<defs><linearGradient id=\"g\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"32\" "
        "gradientUnits=\"userSpaceOnUse\">"
        "<stop offset=\"0\" stop-color=\"#000000\"/>"
        "<stop offset=\"1\" stop-color=\"#ffffff\"/></linearGradient></defs>"
        "<path d=\"M0 0 L32 0 L32 32 L0 32 Z\" fill=\"url(#g)\"/>");
    auto doc = icf::svg::SvgDocument::parse(svg);
    REQUIRE(doc.has_value());
    RenderOptions o;
    o.width = o.height = 32;
    o.subdivisions = 1;
    auto img = renderSvg(d, *doc, o);
    REQUIRE(img.has_value());
    CHECK_EQ(img->drawn, std::size_t{1});

    // The axis runs DOWN, so the ramp must vary with y and not with x.
    CHECK(channelAt(*img, 16, 1, 0) < 0.1f);
    CHECK(std::fabs(channelAt(*img, 16, 16, 0) - 0.5f) < 0.05f);
    CHECK(channelAt(*img, 16, 30, 0) > 0.9f);
    CHECK(std::fabs(channelAt(*img, 2, 16, 0) - channelAt(*img, 29, 16, 0)) < 0.02f);
}

// `objectBoundingBox` is SVG's DEFAULT and occurs in 2 of the corpus's 161. Its
// coordinates are fractions of the shape's box, so the same gradient on a shape
// in a different place has to follow the shape.
TEST_CASE(an_object_bounding_box_gradient_follows_its_shape) {
    Device& d = gpu();
    if (!d.valid()) return;
    // No `gradientUnits` at all: the default applies. The axis 0..1 spans the
    // shape, and the shape is the RIGHT HALF of the canvas.
    const std::string svg = svgWith(
        "0 0 32 32",
        "<defs><linearGradient id=\"g\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"0\">"
        "<stop offset=\"0\" stop-color=\"#000000\"/>"
        "<stop offset=\"1\" stop-color=\"#ffffff\"/></linearGradient></defs>"
        "<path d=\"M16 0 L32 0 L32 32 L16 32 Z\" fill=\"url(#g)\"/>");
    auto doc = icf::svg::SvgDocument::parse(svg);
    REQUIRE(doc.has_value());
    RenderOptions o;
    o.width = o.height = 32;
    o.subdivisions = 1;
    auto img = renderSvg(d, *doc, o);
    REQUIRE(img.has_value());
    CHECK_EQ(img->drawn, std::size_t{1});

    // The ramp spans the SHAPE (x from 16 to 32), not the canvas. A renderer
    // that ignored the box would have the shape start at grey, not at black.
    CHECK(channelAt(*img, 17, 16, 0) < 0.15f);
    CHECK(channelAt(*img, 30, 16, 0) > 0.85f);
    CHECK(alphaAt(*img, 2, 16) < 0.01f);          // nothing outside the shape
}

// A radial gradient runs from its centre outward, which is a different SHAPE of
// answer from a linear one and not just a different direction.
TEST_CASE(a_radial_gradient_runs_from_its_centre) {
    Device& d = gpu();
    if (!d.valid()) return;
    const std::string svg = svgWith(
        "0 0 32 32",
        "<defs><radialGradient id=\"g\" cx=\"16\" cy=\"16\" r=\"16\" "
        "gradientUnits=\"userSpaceOnUse\">"
        "<stop offset=\"0\" stop-color=\"#000000\"/>"
        "<stop offset=\"1\" stop-color=\"#ffffff\"/></radialGradient></defs>"
        "<path d=\"M0 0 L32 0 L32 32 L0 32 Z\" fill=\"url(#g)\"/>");
    auto doc = icf::svg::SvgDocument::parse(svg);
    REQUIRE(doc.has_value());
    RenderOptions o;
    o.width = o.height = 32;
    o.subdivisions = 1;
    auto img = renderSvg(d, *doc, o);
    REQUIRE(img.has_value());
    CHECK_EQ(img->drawn, std::size_t{1});

    const float centre = channelAt(*img, 16, 16, 0);
    // Four points the same distance out must agree, which a linear ramp could
    // not do.
    //
    // The pixels are 4 and 27, not 4 and 28. A pixel's centre is at `x + 0.5`,
    // so pixels `p` and `31 - p` are the pair symmetric about the gradient's
    // centre at 16.0; 4 and 28 are 11.5 and 12.5 out, which is 6% of the ramp
    // apart. The first version of this test used them and failed the renderer
    // for the test's own asymmetry.
    const float up = channelAt(*img, 16, 4, 0);
    const float down = channelAt(*img, 16, 27, 0);
    const float left = channelAt(*img, 4, 16, 0);
    const float right = channelAt(*img, 27, 16, 0);
    CHECK(centre < 0.1f);
    CHECK(up > 0.6f);
    CHECK(std::fabs(up - down) < 0.03f);
    CHECK(std::fabs(up - left) < 0.03f);
    CHECK(std::fabs(up - right) < 0.03f);
}

// Three stops, unevenly placed. Two stops cannot tell a ramp that honours stop
// POSITIONS from one that spreads them evenly, and the corpus's commonest
// gradient has three.
TEST_CASE(the_stops_are_placed_where_the_document_puts_them) {
    Device& d = gpu();
    if (!d.valid()) return;
    const std::string svg = svgWith(
        "0 0 32 32",
        "<defs><linearGradient id=\"g\" x1=\"0\" y1=\"0\" x2=\"32\" y2=\"0\" "
        "gradientUnits=\"userSpaceOnUse\">"
        "<stop offset=\"0\" stop-color=\"#000000\"/>"
        "<stop offset=\"0.25\" stop-color=\"#ffffff\"/>"
        "<stop offset=\"1\" stop-color=\"#000000\"/></linearGradient></defs>"
        "<path d=\"M0 0 L32 0 L32 32 L0 32 Z\" fill=\"url(#g)\"/>");
    auto doc = icf::svg::SvgDocument::parse(svg);
    REQUIRE(doc.has_value());
    RenderOptions o;
    o.width = o.height = 32;
    o.subdivisions = 1;
    auto img = renderSvg(d, *doc, o);
    REQUIRE(img.has_value());

    // White sits a QUARTER along, not half: at x = 8 it is bright, and at
    // x = 16 -- where an evenly-spread ramp would put it -- it is already well
    // down the second segment.
    CHECK(channelAt(*img, 8, 16, 0) > 0.9f);
    const float mid = channelAt(*img, 16, 16, 0);
    CHECK(mid < 0.8f);
    CHECK(mid > 0.4f);
    CHECK(channelAt(*img, 30, 16, 0) < 0.1f);
}

// Past either end the ramp HOLDS -- `spreadMethod` occurs zero times in the
// corpus's 161 gradients, so `pad` is the behaviour and the axis being shorter
// than the shape must not repeat or reflect it.
TEST_CASE(the_ramp_holds_past_both_ends_of_its_axis) {
    Device& d = gpu();
    if (!d.valid()) return;
    // The axis covers only the middle third of the shape.
    const std::string svg = svgWith(
        "0 0 30 30",
        "<defs><linearGradient id=\"g\" x1=\"10\" y1=\"0\" x2=\"20\" y2=\"0\" "
        "gradientUnits=\"userSpaceOnUse\">"
        "<stop offset=\"0\" stop-color=\"#000000\"/>"
        "<stop offset=\"1\" stop-color=\"#ffffff\"/></linearGradient></defs>"
        "<path d=\"M0 0 L30 0 L30 30 L0 30 Z\" fill=\"url(#g)\"/>");
    auto doc = icf::svg::SvgDocument::parse(svg);
    REQUIRE(doc.has_value());
    RenderOptions o;
    o.width = o.height = 30;
    o.subdivisions = 1;
    auto img = renderSvg(d, *doc, o);
    REQUIRE(img.has_value());

    // Everything left of the axis is the first stop, everything right of it the
    // last -- flat, not wrapped.
    CHECK(channelAt(*img, 1, 15, 0) < 0.02f);
    CHECK(std::fabs(channelAt(*img, 1, 15, 0) - channelAt(*img, 8, 15, 0)) < 0.02f);
    CHECK(channelAt(*img, 28, 15, 0) > 0.98f);
    CHECK(std::fabs(channelAt(*img, 22, 15, 0) - channelAt(*img, 28, 15, 0)) < 0.02f);
}


// A fill VALUE the reader does not know is named too, and it is a different gap
// from a reference that does not resolve. `[ART]` The corpus's colour vocabulary
// has exactly two names -- `white` and `black` (doc 04) -- so a third name is a
// value this reader refuses rather than a colour it guesses at.
TEST_CASE(a_fill_value_the_reader_does_not_know_is_named) {
    Device& d = gpu();
    if (!d.valid()) return;
    const std::string svg = svgWith(
        "0 0 16 16",
        "<path d=\"M0 0 L16 0 L16 16 L0 16 Z\" fill=\"chartreuse\"/>"
        "<path d=\"M0 0 L8 0 L8 8 L0 8 Z\" fill=\"#ffffff\"/>");
    auto doc = icf::svg::SvgDocument::parse(svg);
    REQUIRE(doc.has_value());
    RenderOptions o;
    o.width = o.height = 16;
    o.subdivisions = 1;
    auto img = renderSvg(d, *doc, o);
    REQUIRE(img.has_value());

    // The value is named ONE LAYER UP, by the document, and the shape keeps the
    // inherited paint and still draws. That is not the renderer being lax: the
    // gap is reported where the value was read, which is where a reader can say
    // WHICH value it was -- `paint:fill=chartreuse`, not "some shape".
    CHECK_EQ(img->drawn, std::size_t{2});
    CHECK(img->skipped.empty());
    bool named = false;
    for (const auto& e : doc->unsupported()) {
        named |= e.find("chartreuse") != std::string::npos;
    }
    CHECK(named);
}


// A shape whose PATH BUFFER cannot be built is named too, and it is a third
// kind of gap: not an unresolvable reference and not an unreadable value, but
// geometry the buffer refuses. `buildPathBuffer` declines a path that only
// moves -- there is no edge to draw -- and that refusal has to reach the
// caller rather than vanishing into a shape that silently draws nothing.
TEST_CASE(a_shape_whose_path_buffer_is_refused_is_named) {
    Device& d = gpu();
    if (!d.valid()) return;
    const std::string svg = svgWith(
        "0 0 16 16",
        "<path d=\"M5 5\" fill=\"#ffffff\"/>"
        "<path d=\"M0 0 L16 0 L16 16 L0 16 Z\" fill=\"#ffffff\"/>");
    auto doc = icf::svg::SvgDocument::parse(svg);
    REQUIRE(doc.has_value());
    RenderOptions o;
    o.width = o.height = 16;
    o.subdivisions = 1;
    auto img = renderSvg(d, *doc, o);
    REQUIRE(img.has_value());

    CHECK_EQ(img->drawn, std::size_t{1});
    REQUIRE(img->skipped.size() == 1);
    // The reason comes from the buffer itself, so it says WHICH refusal.
    CHECK(!img->skipped[0].why.empty());
    CHECK(img->skipped[0].why.find("move") != std::string::npos ||
          img->skipped[0].why.find("edge") != std::string::npos ||
          img->skipped[0].why.find("path") != std::string::npos);
}

// ---- the PNG ------------------------------------------------------------

// The header and the trailer, checked byte by byte, because an encoder that
// writes a plausible-looking file no viewer opens has failed silently.
TEST_CASE(the_png_carries_its_signature_and_its_chunks) {
    std::vector<float> px(4 * 4 * 4, 0.25f);
    const std::vector<std::uint8_t> png = icf::encodePng(px, 4, 4);
    REQUIRE(png.size() > 8);
    const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    for (int i = 0; i < 8; ++i) CHECK_EQ(png[i], sig[i]);
    // IHDR is the first chunk, and it must say 4x4, 8 bits, RGBA.
    CHECK_EQ(png[12], 'I');
    CHECK_EQ(png[13], 'H');
    CHECK_EQ(png[19], 4);   // width, low byte
    CHECK_EQ(png[23], 4);   // height, low byte
    CHECK_EQ(png[24], 8);   // bit depth
    CHECK_EQ(png[25], 6);   // colour type RGBA
    // IEND closes it, and its CRC is a constant.
    const std::size_t n = png.size();
    CHECK_EQ(png[n - 8], 'I');
    CHECK_EQ(png[n - 7], 'E');
    CHECK_EQ(png[n - 6], 'N');
    CHECK_EQ(png[n - 5], 'D');
    CHECK_EQ(png[n - 4], 0xAE);
    CHECK_EQ(png[n - 3], 0x42);
    CHECK_EQ(png[n - 2], 0x60);
    CHECK_EQ(png[n - 1], 0x82);
}

// A stored deflate block is legal only when LEN and its complement agree, and a
// zlib header is legal only when the two bytes are divisible by 31. Both are the
// kind of thing that "looks written" and is rejected by every decoder.
TEST_CASE(the_png_payload_is_a_valid_zlib_stream) {
    std::vector<float> px(2 * 2 * 4, 1.0f);
    const std::vector<std::uint8_t> png = icf::encodePng(px, 2, 2);
    // IHDR is 25 bytes after the signature; IDAT's data starts 8 bytes into it.
    const std::size_t idat = 8 + 25;
    CHECK_EQ(png[idat + 4], 'I');
    CHECK_EQ(png[idat + 5], 'D');
    const std::size_t z = idat + 8;
    CHECK_EQ((png[z] * 256 + png[z + 1]) % 31, 0);
    CHECK_EQ(png[z] & 0x0F, 8);          // deflate
    CHECK_EQ(png[z + 2] & 0x06, 0);      // BTYPE 00: stored
    const std::uint16_t len = static_cast<std::uint16_t>(png[z + 3] | (png[z + 4] << 8));
    const std::uint16_t nlen = static_cast<std::uint16_t>(png[z + 5] | (png[z + 6] << 8));
    CHECK_EQ(static_cast<std::uint16_t>(~len), nlen);
    // Two rows of two RGBA pixels, each row with its filter byte.
    CHECK_EQ(len, 2 * (1 + 2 * 4));
}

// Out-of-range channels are CLAMPED, never wrapped: a value that left [0, 1] is
// a defect upstream, and a wrapped byte would hide it behind a plausible colour.
TEST_CASE(the_png_clamps_instead_of_wrapping) {
    std::vector<float> px{2.0f, -1.0f, 0.5f, 1.0f};
    const std::vector<std::uint8_t> png = icf::encodePng(px, 1, 1);
    const std::size_t z = 8 + 25 + 8;
    const std::size_t data = z + 7;  // past the zlib header and the block header
    CHECK_EQ(png[data], 0);          // the row's filter byte
    CHECK_EQ(png[data + 1], 255);    // 2.0 clamps up
    CHECK_EQ(png[data + 2], 0);      // -1.0 clamps down
    CHECK_EQ(png[data + 3], 128);    // 0.5 rounds to 128
    CHECK_EQ(png[data + 4], 255);
}

// `opacity` REACHES THE PIXELS, and it is a third multiplier -- not a rename of
// `fill-opacity`.
//
// `[ART]` 24 corpus elements carry `opacity < 1` across 5 documents, and three
// of those documents were already INSIDE the drawable slice: SAP, PingPlace and
// LaunchNext drew at full strength where their authors asked for 0.1, 0.2 and
// 0.5. Nothing reported it, because "drawable" was never "right".
//
// The numbers here are exact and stacked on purpose. A square painted with an
// `#ffffff` fill at `fill-opacity="0.5"` inside `opacity="0.5"` must land on
// 0.25, and the three ways to get this wrong all miss it: ignoring `opacity`
// gives 0.5, treating it AS `fill-opacity` gives 0.5, and applying it twice
// gives 0.125.
TEST_CASE(opacity_multiplies_fill_opacity_rather_than_replacing_it) {
    Device& d = gpu();
    if (!d.valid()) return;
    constexpr std::uint32_t kSize = 32;
    RenderOptions o;
    o.width = kSize;
    o.height = kSize;
    o.subdivisions = 1;

    auto alphaOf = [&](const std::string& attrs) -> float {
        const std::string svg = svgWith(
            "0 0 32 32", "<path d=\"M8 8 L24 8 L24 24 L8 24 Z\" fill=\"#ffffff\" " + attrs + "/>");
        auto doc = icf::svg::SvgDocument::parse(svg);
        if (!doc) return -1.0f;   // no valid alpha is negative, so this fails below
        auto img = renderSvg(d, *doc, o);
        if (!img) return -1.0f;
        return alphaAt(*img, 16, 16);
    };

    CHECK(std::fabs(alphaOf("") - 1.0f) < 0.001f);
    CHECK(std::fabs(alphaOf("opacity=\"0.5\"") - 0.5f) < 0.001f);
    CHECK(std::fabs(alphaOf("fill-opacity=\"0.5\"") - 0.5f) < 0.001f);
    CHECK(std::fabs(alphaOf("opacity=\"0.5\" fill-opacity=\"0.5\"") - 0.25f) < 0.001f);
}

// A GROUP'S `opacity` FOLDS INTO ITS SHAPES, and nested groups MULTIPLY.
//
// `opacity` is the one property here that does not inherit-and-replace: a group
// at 0.5 inside a group at 0.5 composites at 0.25, and a reader that treated it
// like `fill` would report 0.5. `[ART]` The corpus nests them exactly this way
// in SAP's `l4.svg`.
TEST_CASE(nested_group_opacity_multiplies_down_the_tree) {
    Device& d = gpu();
    if (!d.valid()) return;
    constexpr std::uint32_t kSize = 32;
    RenderOptions o;
    o.width = kSize;
    o.height = kSize;
    o.subdivisions = 1;

    const std::string svg = svgWith(
        "0 0 32 32",
        "<g opacity=\"0.5\"><g opacity=\"0.5\">"
        "<path d=\"M8 8 L24 8 L24 24 L8 24 Z\" fill=\"#ffffff\"/>"
        "</g></g>");
    auto doc = icf::svg::SvgDocument::parse(svg);
    REQUIRE(doc.has_value());
    REQUIRE(doc->shapes.size() == 1);
    CHECK(std::fabs(doc->shapes[0].opacity - 0.25) < 1e-9);

    auto img = renderSvg(d, *doc, o);
    REQUIRE(img.has_value());
    CHECK(std::fabs(alphaAt(*img, 16, 16) - 0.25f) < 0.001f);

    // ONE shape under the group, so folding IS compositing -- nothing to name.
    CHECK(doc->unsupported().count("opacity de grupo sobre mais de uma forma") == 0);
}

// AND WHERE FOLDING CAN DIFFER, IT IS NAMED INSTEAD OF ASSUMED.
//
// Two shapes under one `opacity` group is the case where compositing the group
// once and compositing each shape separately give different pixels wherever the
// two overlap. `[ART]` Delta does this with 44 shapes at 0.2 and five pairs at
// 0.8; SAP never does. The reader folds either way -- dropping the shapes would
// be worse -- and says so, which is the difference between an approximation and
// a silent one.
TEST_CASE(a_group_opacity_over_two_shapes_is_named) {
    const std::string svg = svgWith(
        "0 0 32 32",
        "<g opacity=\"0.5\">"
        "<path d=\"M0 0 L16 0 L16 16 L0 16 Z\" fill=\"#ffffff\"/>"
        "<path d=\"M8 8 L24 8 L24 24 L8 24 Z\" fill=\"#ffffff\"/>"
        "</g>");
    auto doc = icf::svg::SvgDocument::parse(svg);
    REQUIRE(doc.has_value());
    REQUIRE(doc->shapes.size() == 2);
    CHECK(std::fabs(doc->shapes[0].opacity - 0.5) < 1e-9);
    CHECK(std::fabs(doc->shapes[1].opacity - 0.5) < 1e-9);
    CHECK(doc->unsupported().count("opacity de grupo sobre mais de uma forma") == 1);
}

// A value SVG tells us how to read is read, not refused.
//
// SVG 1.1 §14 clamps `opacity` to [0,1]. Refusing an out-of-range number would
// drop a shape over something the specification answers, and reading `2` as 2
// would brighten a composite that cannot exceed 1. A value that is not a number
// at all is a different thing and IS named.
TEST_CASE(opacity_out_of_range_clamps_and_a_non_number_is_named) {
    auto op = [](const char* v) -> double {
        auto doc = icf::svg::SvgDocument::parse(svgWith(
            "0 0 32 32",
            std::string("<path d=\"M0 0 L8 0 L8 8 Z\" fill=\"#fff\" opacity=\"") + v + "\"/>"));
        if (!doc || doc->shapes.size() != 1) return -1.0;
        return doc->shapes[0].opacity;
    };
    CHECK(std::fabs(op("2") - 1.0) < 1e-9);
    CHECK(std::fabs(op("-1") - 0.0) < 1e-9);
    CHECK(std::fabs(op("0.25") - 0.25) < 1e-9);

    auto doc = icf::svg::SvgDocument::parse(svgWith(
        "0 0 32 32", "<path d=\"M0 0 L8 0 L8 8 Z\" fill=\"#fff\" opacity=\"inherit\"/>"));
    REQUIRE(doc.has_value());
    CHECK(doc->unsupported().count("paint:opacity=inherit") == 1);
    CHECK(std::fabs(doc->shapes[0].opacity - 1.0) < 1e-9);
}

// A CLIP ACTUALLY CLIPS, and the two halves are asserted apart.
//
// The shape is the full 32x32 canvas; the clip is its left half. Inside the
// clip the pixel has to be the shape's, outside it has to be UNTOUCHED -- and
// the second half is the one that fails when a clip is parsed, cached, reported
// as read, and then never multiplied in. `[ART]` quick-push's `bolt.brakesignal
// 1.svg` is this shape exactly: one `<clipPath>` holding one `<rect>`.
TEST_CASE(a_clip_path_cuts_the_shape_and_leaves_the_rest_empty) {
    Device& d = gpu();
    if (!d.valid()) return;
    constexpr std::uint32_t kSize = 32;
    RenderOptions o;
    o.width = kSize;
    o.height = kSize;
    o.subdivisions = 1;

    const std::string svg = svgWith(
        "0 0 32 32",
        "<defs><clipPath id=\"c\"><rect width=\"16\" height=\"32\"/></clipPath></defs>"
        "<path d=\"M0 0 L32 0 L32 32 L0 32 Z\" fill=\"#ffffff\" clip-path=\"url(#c)\"/>");
    auto doc = icf::svg::SvgDocument::parse(svg);
    REQUIRE(doc.has_value());
    REQUIRE(doc->shapes.size() == 1);
    REQUIRE(doc->shapes[0].clipPaths.size() == 1);
    CHECK(doc->clipPaths.count("c") == 1);
    // The clip's own child is NOT a shape to draw.
    CHECK(doc->unsupported().count("clipPath") == 0);
    CHECK(doc->unsupported().count("defs:clipPath") == 0);
    CHECK(doc->unsupported().count("paint:clip-path") == 0);

    auto img = renderSvg(d, *doc, o);
    REQUIRE(img.has_value());
    CHECK(img->skipped.empty());
    CHECK(std::fabs(alphaAt(*img, 4, 16) - 1.0f) < 0.001f);    // inside the clip
    CHECK(std::fabs(alphaAt(*img, 27, 16) - 0.0f) < 0.001f);   // outside it
}

// TWO CLIPS INTERSECT rather than the innermost winning.
//
// A group clipped to the left half holding a shape clipped to the top half
// leaves one quadrant. A reader that let the inner clip replace the outer would
// leave the whole top band, which is a plausible picture and the wrong one.
TEST_CASE(nested_clip_paths_intersect) {
    Device& d = gpu();
    if (!d.valid()) return;
    constexpr std::uint32_t kSize = 32;
    RenderOptions o;
    o.width = kSize;
    o.height = kSize;
    o.subdivisions = 1;

    const std::string svg = svgWith(
        "0 0 32 32",
        "<defs>"
        "<clipPath id=\"l\"><rect width=\"16\" height=\"32\"/></clipPath>"
        "<clipPath id=\"t\"><rect width=\"32\" height=\"16\"/></clipPath>"
        "</defs>"
        "<g clip-path=\"url(#l)\">"
        "<path d=\"M0 0 L32 0 L32 32 L0 32 Z\" fill=\"#ffffff\" clip-path=\"url(#t)\"/>"
        "</g>");
    auto doc = icf::svg::SvgDocument::parse(svg);
    REQUIRE(doc.has_value());
    REQUIRE(doc->shapes.size() == 1);
    CHECK_EQ(doc->shapes[0].clipPaths.size(), std::size_t{2});

    auto img = renderSvg(d, *doc, o);
    REQUIRE(img.has_value());
    CHECK(std::fabs(alphaAt(*img, 4, 4) - 1.0f) < 0.001f);     // left AND top
    CHECK(std::fabs(alphaAt(*img, 4, 27) - 0.0f) < 0.001f);    // left, not top
    CHECK(std::fabs(alphaAt(*img, 27, 4) - 0.0f) < 0.001f);    // top, not left
    CHECK(std::fabs(alphaAt(*img, 27, 27) - 0.0f) < 0.001f);   // neither
}

// A CLIP THAT NAMES NOTHING IS A GAP, not a shape drawn whole.
//
// Drawing unclipped would put the entire shape on the canvas where the author
// asked for a piece of it: a plausible picture with a clean report, which is
// this project's worst failure mode. So the shape is skipped and the id is in
// the reason.
TEST_CASE(a_clip_path_that_resolves_to_nothing_is_named) {
    Device& d = gpu();
    if (!d.valid()) return;
    RenderOptions o;
    o.width = 32;
    o.height = 32;
    o.subdivisions = 1;

    auto doc = icf::svg::SvgDocument::parse(svgWith(
        "0 0 32 32",
        "<path d=\"M0 0 L32 0 L32 32 L0 32 Z\" fill=\"#ffffff\" clip-path=\"url(#gone)\"/>"));
    REQUIRE(doc.has_value());
    auto img = renderSvg(d, *doc, o);
    REQUIRE(img.has_value());
    CHECK_EQ(img->drawn, std::size_t{0});
    REQUIRE(img->skipped.size() == 1);
    CHECK(img->skipped[0].why.find("gone") != std::string::npos);
    CHECK(std::fabs(alphaAt(*img, 16, 16) - 0.0f) < 0.001f);
}

// `objectBoundingBox` IS A DIFFERENT GEOMETRY, and it is refused by name.
//
// It re-scales the clip to each referrer's bounding box, so one definition
// means different geometry per user. Reading it as user space would be a
// silently wrong clip on a file that says exactly what it wants.
TEST_CASE(a_clip_path_in_bounding_box_units_is_named_not_guessed) {
    auto doc = icf::svg::SvgDocument::parse(svgWith(
        "0 0 32 32",
        "<defs><clipPath id=\"c\" clipPathUnits=\"objectBoundingBox\">"
        "<rect width=\"0.5\" height=\"1\"/></clipPath></defs>"
        "<path d=\"M0 0 L32 0 L32 32 Z\" fill=\"#fff\" clip-path=\"url(#c)\"/>"));
    REQUIRE(doc.has_value());
    CHECK(doc->unsupported().count("clipPathUnits:objectBoundingBox") == 1);
    CHECK(doc->clipPaths.count("c") == 0);
}

// A MASK MASKS, and it masks by LUMINANCE rather than by geometry.
//
// This is the whole difference from a clip, and the fixture is built so that a
// reader that treated a mask as a clip would pass the first half and fail the
// second: white and black cover the SAME area, and only their luminance tells
// them apart. `[ART]` CommE2E's `cut` is exactly this -- a white rect with a
// black circle and a black rounded rect punched out of it.
TEST_CASE(a_mask_hides_by_luminance_and_not_by_area) {
    Device& d = gpu();
    if (!d.valid()) return;
    constexpr std::uint32_t kSize = 32;
    RenderOptions o;
    o.width = kSize;
    o.height = kSize;
    o.subdivisions = 1;

    const std::string svg = svgWith(
        "0 0 32 32",
        "<defs><mask id=\"m\" maskUnits=\"userSpaceOnUse\" x=\"0\" y=\"0\""
        " width=\"32\" height=\"32\">"
        "<rect width=\"32\" height=\"32\" fill=\"white\"/>"
        "<rect x=\"16\" width=\"16\" height=\"32\" fill=\"black\"/>"
        "</mask></defs>"
        "<path d=\"M0 0 L32 0 L32 32 L0 32 Z\" fill=\"#ffffff\" mask=\"url(#m)\"/>");
    auto doc = icf::svg::SvgDocument::parse(svg);
    REQUIRE(doc.has_value());
    REQUIRE(doc->shapes.size() == 1);
    REQUIRE(doc->shapes[0].masks.size() == 1);
    CHECK(doc->masks.count("m") == 1);
    // The mask's own children are not shapes to draw.
    CHECK(doc->unsupported().count("mask") == 0);
    CHECK(doc->unsupported().count("defs:mask") == 0);
    CHECK(doc->unsupported().count("paint:mask") == 0);

    auto img = renderSvg(d, *doc, o);
    REQUIRE(img.has_value());
    CHECK(img->skipped.empty());
    // Under the WHITE half the shape survives; under the BLACK half it is gone.
    // A clip would keep both, because both halves are covered.
    CHECK(std::fabs(alphaAt(*img, 4, 16) - 1.0f) < 0.001f);
    CHECK(std::fabs(alphaAt(*img, 27, 16) - 0.0f) < 0.001f);
}

// AND IT IS A SCALE, not a switch. Mid-grey masks to mid-alpha.
//
// A reader that thresholded the luminance would pass the test above and fail
// this one, and thresholding is the natural mistake to make when the only masks
// you have ever seen are black and white -- which is every mask in this corpus.
TEST_CASE(a_grey_mask_scales_the_alpha_rather_than_switching_it) {
    Device& d = gpu();
    if (!d.valid()) return;
    RenderOptions o;
    o.width = 32;
    o.height = 32;
    o.subdivisions = 1;

    const std::string svg = svgWith(
        "0 0 32 32",
        "<defs><mask id=\"m\" maskUnits=\"userSpaceOnUse\" x=\"0\" y=\"0\""
        " width=\"32\" height=\"32\">"
        "<rect width=\"32\" height=\"32\" fill=\"#808080\"/></mask></defs>"
        "<path d=\"M0 0 L32 0 L32 32 L0 32 Z\" fill=\"#ffffff\" mask=\"url(#m)\"/>");
    auto doc = icf::svg::SvgDocument::parse(svg);
    REQUIRE(doc.has_value());
    auto img = renderSvg(d, *doc, o);
    REQUIRE(img.has_value());
    // 0x80 is 128/255 = 0.502, and the luminance coefficients sum to 1, so a
    // neutral grey masks to its own value whichever colour space is used.
    CHECK(std::fabs(alphaAt(*img, 16, 16) - 0.502f) < 0.01f);
}

// A MASK'S OWN ALPHA COUNTS TOO. White at half alpha masks to a half.
//
// The value is luminance TIMES alpha, and a reader that took only the luminance
// would report a fully opaque mask for art that is barely there.
TEST_CASE(a_masks_alpha_multiplies_its_luminance) {
    Device& d = gpu();
    if (!d.valid()) return;
    RenderOptions o;
    o.width = 32;
    o.height = 32;
    o.subdivisions = 1;

    const std::string svg = svgWith(
        "0 0 32 32",
        "<defs><mask id=\"m\" maskUnits=\"userSpaceOnUse\" x=\"0\" y=\"0\""
        " width=\"32\" height=\"32\">"
        "<rect width=\"32\" height=\"32\" fill=\"white\" fill-opacity=\"0.5\"/>"
        "</mask></defs>"
        "<path d=\"M0 0 L32 0 L32 32 L0 32 Z\" fill=\"#ffffff\" mask=\"url(#m)\"/>");
    auto doc = icf::svg::SvgDocument::parse(svg);
    REQUIRE(doc.has_value());
    auto img = renderSvg(d, *doc, o);
    REQUIRE(img.has_value());
    CHECK(std::fabs(alphaAt(*img, 16, 16) - 0.5f) < 0.01f);
}

// A mask that names nothing is a GAP, for the reason the clip's is.
TEST_CASE(a_mask_that_resolves_to_nothing_is_named) {
    Device& d = gpu();
    if (!d.valid()) return;
    RenderOptions o;
    o.width = 32;
    o.height = 32;
    o.subdivisions = 1;

    auto doc = icf::svg::SvgDocument::parse(svgWith(
        "0 0 32 32",
        "<path d=\"M0 0 L32 0 L32 32 L0 32 Z\" fill=\"#ffffff\" mask=\"url(#gone)\"/>"));
    REQUIRE(doc.has_value());
    auto img = renderSvg(d, *doc, o);
    REQUIRE(img.has_value());
    CHECK_EQ(img->drawn, std::size_t{0});
    REQUIRE(img->skipped.size() == 1);
    CHECK(img->skipped[0].why.find("gone") != std::string::npos);
}

// `maskUnits` DEFAULTS TO `objectBoundingBox`, which is a different geometry --
// so a mask that does not say `userSpaceOnUse` is refused by name rather than
// read as if it had.
TEST_CASE(a_mask_in_bounding_box_units_is_named_not_guessed) {
    auto doc = icf::svg::SvgDocument::parse(svgWith(
        "0 0 32 32",
        "<defs><mask id=\"m\"><rect width=\"1\" height=\"1\" fill=\"white\"/></mask></defs>"
        "<path d=\"M0 0 L32 0 L32 32 Z\" fill=\"#fff\" mask=\"url(#m)\"/>"));
    REQUIRE(doc.has_value());
    CHECK(doc->unsupported().count("maskUnits:objectBoundingBox") == 1);
    CHECK(doc->masks.count("m") == 0);
}

// THE MASK'S REGION FENCES ITS ART, and this test exists because the code that
// does it had none.
//
// `x`/`y`/`width`/`height` on a `<mask>` say where the mask applies at all;
// content outside is not mask, it is nothing. `[ART]` Both corpus masks draw
// their region exactly, so the corpus cannot tell a renderer that honours the
// fence from one that ignores it -- and a mask whose art is bigger than its
// region would then mask with paint the author fenced off.
//
// Outside the region the mask value is ZERO, which HIDES the shape. That is the
// direction worth stating: an implementation that treated "outside the region"
// as "unmasked" would show the shape there instead.
TEST_CASE(a_mask_region_fences_the_mask_and_hides_what_is_outside) {
    Device& d = gpu();
    if (!d.valid()) return;
    RenderOptions o;
    o.width = 32;
    o.height = 32;
    o.subdivisions = 1;

    // White over the WHOLE canvas, but the region is only the left half.
    const std::string svg = svgWith(
        "0 0 32 32",
        "<defs><mask id=\"m\" maskUnits=\"userSpaceOnUse\" x=\"0\" y=\"0\""
        " width=\"16\" height=\"32\">"
        "<rect width=\"32\" height=\"32\" fill=\"white\"/></mask></defs>"
        "<path d=\"M0 0 L32 0 L32 32 L0 32 Z\" fill=\"#ffffff\" mask=\"url(#m)\"/>");
    auto doc = icf::svg::SvgDocument::parse(svg);
    REQUIRE(doc.has_value());
    auto img = renderSvg(d, *doc, o);
    REQUIRE(img.has_value());
    CHECK(std::fabs(alphaAt(*img, 4, 16) - 1.0f) < 0.001f);    // inside the region
    CHECK(std::fabs(alphaAt(*img, 27, 16) - 0.0f) < 0.001f);   // outside it
}

// THE STROKE IS CLIPPED AND MASKED TOO, and this test exists because the gate's
// stale-anchor pre-flight made me look at the line.
//
// `clipAt` was added to BOTH composites on 2026-09-05, and every clip and mask
// test written that day drives a FILLED shape. Deleting the factor from the
// stroke's composite would therefore have changed no test at all -- an
// unguarded half of a feature that already looked finished.
TEST_CASE(a_clip_cuts_the_stroke_and_not_only_the_fill) {
    Device& d = gpu();
    if (!d.valid()) return;
    RenderOptions o;
    o.width = 32;
    o.height = 32;
    o.subdivisions = 1;

    // A horizontal line across the middle, fat enough to sample, with NO fill:
    // the only ink it can make is the stroke.
    const std::string svg = svgWith(
        "0 0 32 32",
        "<defs><clipPath id=\"c\"><rect width=\"16\" height=\"32\"/></clipPath></defs>"
        "<path d=\"M0 16 L32 16\" fill=\"none\" stroke=\"#ffffff\" stroke-width=\"8\""
        " clip-path=\"url(#c)\"/>");
    auto doc = icf::svg::SvgDocument::parse(svg);
    REQUIRE(doc.has_value());
    auto img = renderSvg(d, *doc, o);
    REQUIRE(img.has_value());
    CHECK(img->skipped.empty());
    CHECK(std::fabs(alphaAt(*img, 4, 16) - 1.0f) < 0.01f);    // stroke, inside
    CHECK(std::fabs(alphaAt(*img, 27, 16) - 0.0f) < 0.01f);   // stroke, clipped
}
