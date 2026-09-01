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
    const std::string svg = svgWith(
        "0 0 16 16",
        "<defs><linearGradient id=\"g\"><stop offset=\"0\" stop-color=\"#000\"/>"
        "<stop offset=\"1\" stop-color=\"#fff\"/></linearGradient></defs>"
        "<path d=\"M0 0 L16 0 L16 16 L0 16 Z\" fill=\"url(#g)\"/>"
        "<path d=\"M0 0 L8 0 L8 8 L0 8 Z\" fill=\"none\"/>");
    auto doc = icf::svg::SvgDocument::parse(svg);
    REQUIRE(doc.has_value());
    RenderOptions o;
    o.width = 16;
    o.height = 16;
    auto img = renderSvg(d, *doc, o);
    REQUIRE(img.has_value());

    CHECK_EQ(img->drawn, std::size_t{0});
    // The gradient is a gap and is reported; `fill="none"` is a deliberate
    // non-painting and must NOT be, or the real gaps drown in noise.
    REQUIRE(img->skipped.size() == 1);
    CHECK(img->skipped[0].why.find("gradiente") != std::string::npos ||
          img->skipped[0].why.find("referencia") != std::string::npos);
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
