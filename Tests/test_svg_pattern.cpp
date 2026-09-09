// A `<pattern>` fill, and the proof that the mapping is the right one.
//
// "10 of 10 shapes drawn" is not evidence here: a pattern whose geometry is
// wrong draws exactly as many shapes as one whose geometry is right, and the
// picture still looks like a texture. So these drive a raster whose every texel
// is KNOWN -- four flat quadrants, each a different colour -- and ask which
// colour landed where.
//
// The image goes in the way the corpus's does: encoded to PNG, base64'd into a
// `data:` URI, and read back out through the same path Delta's 2,4 MB blob
// takes. Building the fixture with `encodePng` rather than a checked-in file
// keeps the test readable and exercises the decoder the renderer actually calls.
#include "check.h"
#include "Source/CoreSVG/Document.h"
#include "Source/IconComposerFoundation/Png.h"
#include "Source/RenderBox/SvgPattern.h"
#include "Source/RenderBox/SvgRenderer.h"

#include <cmath>
#include <string>
#include <vector>

using namespace rb;

namespace {

std::string base64(const std::vector<std::uint8_t>& raw) {
    static const char* k =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < raw.size(); i += 3) {
        const std::uint32_t v = (raw[i] << 16) | (raw[i + 1] << 8) | raw[i + 2];
        out += k[(v >> 18) & 63];
        out += k[(v >> 12) & 63];
        out += k[(v >> 6) & 63];
        out += k[v & 63];
    }
    if (i + 1 == raw.size()) {
        const std::uint32_t v = raw[i] << 16;
        out += k[(v >> 18) & 63];
        out += k[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == raw.size()) {
        const std::uint32_t v = (raw[i] << 16) | (raw[i + 1] << 8);
        out += k[(v >> 18) & 63];
        out += k[(v >> 12) & 63];
        out += k[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

// FOUR QUADRANTS, each flat and opaque, on a `side` x `side` image:
//
//     red   | green
//     ------+------
//     blue  | white
//
// Flat quadrants and not a gradient on purpose: a sample that lands one texel
// off inside a quadrant reads the same colour, so what these tests measure is
// PLACEMENT and not interpolation. The one test that cares about interpolation
// says so and samples a boundary.
std::string quadrantPngDataUri(std::uint32_t side) {
    std::vector<float> px(static_cast<std::size_t>(side) * side * 4, 0.0f);
    for (std::uint32_t y = 0; y < side; ++y) {
        for (std::uint32_t x = 0; x < side; ++x) {
            float* p = &px[(static_cast<std::size_t>(y) * side + x) * 4];
            const bool right = x >= side / 2, bottom = y >= side / 2;
            p[0] = (!right && !bottom) || (right && bottom) ? 1.0f : 0.0f;
            p[1] = (right && !bottom) || (right && bottom) ? 1.0f : 0.0f;
            p[2] = (!right && bottom) || (right && bottom) ? 1.0f : 0.0f;
            p[3] = 1.0f;
        }
    }
    return "data:image/png;base64," + base64(icf::encodePng(px, side, side));
}

// Delta's idiom exactly: `patternContentUnits="objectBoundingBox"`, a tile
// larger than nothing, and a `<use>` scaling the image down to the tile.
std::string patternSvg(std::uint32_t side, double tile, double scale) {
    return "<svg viewBox=\"0 0 64 64\">"
           "<rect x=\"0\" y=\"0\" width=\"64\" height=\"64\" fill=\"url(#p)\"/>"
           "<defs>"
           "<pattern id=\"p\" patternContentUnits=\"objectBoundingBox\" width=\"" +
           std::to_string(tile) + "\" height=\"" + std::to_string(tile) +
           "\"><use xlink:href=\"#img\" transform=\"scale(" + std::to_string(scale) +
           ")\"/></pattern>"
           "<image id=\"img\" width=\"" + std::to_string(side) + "\" height=\"" +
           std::to_string(side) + "\" preserveAspectRatio=\"none\" xlink:href=\"" +
           quadrantPngDataUri(side) + "\"/>"
           "</defs></svg>";
}

Device& gpu() {
    static Device* d = [] {
        auto made = Device::create();
        return made ? new Device(std::move(*made)) : nullptr;
    }();
    static Device dead;
    return d ? *d : dead;
}

const float* at(const RenderedImage& r, std::uint32_t x, std::uint32_t y) {
    return &r.rgba[(static_cast<std::size_t>(y) * r.width + x) * 4];
}

}  // namespace

// ---- the reader ----------------------------------------------------------

TEST_CASE(a_figma_pattern_reads_into_a_tile_a_use_and_an_image) {
    auto doc = icf::svg::SvgDocument::parse(patternSvg(8, 1.0, 0.125));
    REQUIRE(doc.has_value());
    REQUIRE(doc->patterns.size() == 1);
    const auto& p = doc->patterns.begin()->second;
    CHECK_EQ(p.width, 1.0);
    CHECK_EQ(p.height, 1.0);
    // `patternContentUnits` was NAMED as objectBoundingBox; `patternUnits` was
    // not named at all and takes SVG's default, which is also objectBoundingBox.
    // The two defaults differ, which is why they are separate fields.
    CHECK(!p.contentUserSpace);
    CHECK(!p.unitsUserSpace);
    CHECK_EQ(p.imageId, std::string("img"));
    CHECK(std::abs(p.contentTransform.a - 0.125) < 1e-9);

    REQUIRE(doc->images.size() == 1);
    const auto& img = doc->images.begin()->second;
    CHECK_EQ(img.mediaType, std::string("image/png"));
    CHECK_EQ(img.width, 8.0);
    // The PNG signature survived base64 in both directions.
    REQUIRE(img.bytes.size() > 8);
    CHECK_EQ(int(img.bytes[0]), 0x89);
    CHECK_EQ(int(img.bytes[1]), 'P');
    CHECK_EQ(int(img.bytes[2]), 'N');
    CHECK_EQ(int(img.bytes[3]), 'G');

    // And it is NOT reported as a gap, because it is read.
    CHECK_EQ(doc->unsupported().count("defs:pattern"), std::size_t(0));
    CHECK_EQ(doc->unsupported().count("defs:image"), std::size_t(0));
}

TEST_CASE(base64_refuses_a_corrupt_payload_instead_of_shifting_it) {
    // A decoder that skipped the stray character would hand the PNG reader a
    // stream off by six bits and let it report the mess as ITS problem.
    auto doc = icf::svg::SvgDocument::parse(
        "<svg viewBox=\"0 0 10 10\"><defs>"
        "<image id=\"i\" width=\"2\" height=\"2\" xlink:href=\"data:image/png;base64,iVB*Rw==\"/>"
        "</defs></svg>");
    REQUIRE(doc.has_value());
    CHECK_EQ(doc->images.count("i"), std::size_t(0));
    CHECK_EQ(doc->unsupported().count("image com base64 corrompido"), std::size_t(1));
}

TEST_CASE(a_pattern_this_reader_will_not_invent_is_refused_by_name) {
    for (const char* attr : {"patternTransform=\"rotate(10)\"",
                             "patternUnits=\"userSpaceOnUse\""}) {
        const std::string svg =
            std::string("<svg viewBox=\"0 0 10 10\"><defs><pattern id=\"p\" ") + attr +
            " width=\"1\" height=\"1\"><use xlink:href=\"#i\"/></pattern></defs></svg>";
        auto doc = icf::svg::SvgDocument::parse(svg);
        REQUIRE(doc.has_value());
        // Not collected, and named -- the corpus exercises neither, and both are
        // different geometry rather than a different default.
        CHECK_EQ(doc->patterns.count("p"), std::size_t(0));
        CHECK(!doc->unsupported().empty());
    }
}

// ---- the mapping, which is what "drawn" does not prove -------------------

TEST_CASE(the_four_quadrants_land_where_the_pattern_puts_them) {
    Device& d = gpu();
    if (!d.valid()) return;

    // A tile of exactly ONE bounding box, and a `<use>` scaling the 8px image to
    // that one box: the image covers the shape exactly once. So the canvas is
    // the image, and each quadrant has to arrive in its own corner.
    auto doc = icf::svg::SvgDocument::parse(patternSvg(8, 1.0, 1.0 / 8.0));
    REQUIRE(doc.has_value());
    RenderOptions o;
    o.width = 64;
    o.height = 64;
    auto got = renderSvg(d, *doc, o);
    REQUIRE(got.has_value());
    CHECK(got->skipped.empty());

    struct Probe { std::uint32_t x, y; float r, g, b; const char* corner; };
    const Probe probes[] = {
        {16, 16, 1, 0, 0, "top-left is red"},
        {48, 16, 0, 1, 0, "top-right is green"},
        {16, 48, 0, 0, 1, "bottom-left is blue"},
        {48, 48, 1, 1, 1, "bottom-right is white"},
    };
    for (const Probe& p : probes) {
        const float* px = at(*got, p.x, p.y);
        CHECK(px[3] > 0.99f);
        CHECK(std::abs(px[0] - p.r) < 0.02f);
        CHECK(std::abs(px[1] - p.g) < 0.02f);
        CHECK(std::abs(px[2] - p.b) < 0.02f);
    }
}

TEST_CASE(a_tile_smaller_than_the_shape_repeats) {
    Device& d = gpu();
    if (!d.valid()) return;

    // `[ART]` THE CORPUS NEVER REACHES THIS. All 11 of its patterns have a tile
    // from 2,93 to 82,34 bounding boxes across -- bigger than the shape every
    // time -- so one tile always covers it and the repeat never fires. It is
    // built because refusing a second tile would be a limit with no reason, and
    // this is its only witness.
    //
    // A quarter-box tile means four tiles across, so the image's top-left
    // quadrant appears four times along the top edge.
    auto doc = icf::svg::SvgDocument::parse(patternSvg(8, 0.25, 1.0 / 32.0));
    REQUIRE(doc.has_value());
    RenderOptions o;
    o.width = 64;
    o.height = 64;
    auto got = renderSvg(d, *doc, o);
    REQUIRE(got.has_value());

    // Tile 0 and tile 2 along the top: both are the image's own top-left
    // quadrant, so both must be red -- and the pixel between them, in the same
    // tile's right half, must be green.
    for (std::uint32_t x : {4u, 20u, 36u, 52u}) {
        const float* px = at(*got, x, 4);
        CHECK(std::abs(px[0] - 1.0f) < 0.02f);
        CHECK(std::abs(px[1]) < 0.02f);
    }
    for (std::uint32_t x : {12u, 28u, 44u, 60u}) {
        const float* px = at(*got, x, 4);
        CHECK(std::abs(px[0]) < 0.02f);
        CHECK(std::abs(px[1] - 1.0f) < 0.02f);
    }
}

TEST_CASE(the_use_transform_is_undone_and_not_applied_twice) {
    Device& d = gpu();
    if (!d.valid()) return;

    // The `<use>`'s transform carries IMAGE coordinates into CONTENT
    // coordinates, so sampling has to run it BACKWARDS. Applying it forwards
    // instead still produces a picture -- it is just the wrong scale, and with a
    // flat-quadrant image the wrong scale is visible as the wrong corner.
    //
    // Here the image is scaled to HALF the tile, so the image occupies the
    // tile's top-left quarter and the rest of the tile is empty. The canvas is
    // one tile, so the four quadrants are squeezed into the canvas's top-left
    // quarter and the bottom-right of the canvas carries no ink at all.
    auto doc = icf::svg::SvgDocument::parse(patternSvg(8, 1.0, 1.0 / 16.0));
    REQUIRE(doc.has_value());
    RenderOptions o;
    o.width = 64;
    o.height = 64;
    auto got = renderSvg(d, *doc, o);
    REQUIRE(got.has_value());

    // The four quadrants, now in the canvas's top-left quarter.
    CHECK(std::abs(at(*got, 8, 8)[0] - 1.0f) < 0.02f);     // red
    CHECK(std::abs(at(*got, 24, 8)[1] - 1.0f) < 0.02f);    // green
    CHECK(std::abs(at(*got, 8, 24)[2] - 1.0f) < 0.02f);    // blue
    // And past the image, inside the same tile, nothing was painted.
    CHECK(at(*got, 48, 48)[3] < 0.01f);
}

TEST_CASE(a_pattern_reference_is_not_reported_as_a_missing_gradient) {
    Device& d = gpu();
    if (!d.valid()) return;

    // The message this replaces sent whoever read the report looking for a
    // `<linearGradient>` that never existed. `[ART]` Ten shapes over two of
    // Delta's files said it.
    auto doc = icf::svg::SvgDocument::parse(
        "<svg viewBox=\"0 0 10 10\">"
        "<rect x=\"0\" y=\"0\" width=\"10\" height=\"10\" fill=\"url(#p)\"/>"
        "<defs><pattern id=\"p\" width=\"1\" height=\"1\">"
        "<use xlink:href=\"#nosuchimage\"/></pattern></defs></svg>");
    REQUIRE(doc.has_value());
    RenderOptions o;
    o.width = 16;
    o.height = 16;
    auto got = renderSvg(d, *doc, o);
    REQUIRE(got.has_value());
    REQUIRE(got->skipped.size() == 1);
    CHECK(got->skipped[0].why.find("gradiente") == std::string::npos);
    CHECK(got->skipped[0].why.find("nosuchimage") != std::string::npos);
}

// ---- the two the mutation sweep caught me on -----------------------------

TEST_CASE(the_content_units_default_is_user_space_and_not_the_other_one) {
    Device& d = gpu();
    if (!d.valid()) return;

    // EVERY TEST ABOVE NAMES `patternContentUnits`, so none of them ever
    // exercised its DEFAULT -- and the sweep said so by surviving a mutation
    // that flipped it. The corpus names it too, in all 11, so the corpus cannot
    // decide this either.
    //
    // SVG's two defaults differ, and that is the whole point: `patternUnits`
    // falls to `objectBoundingBox` while `patternContentUnits` falls to
    // `userSpaceOnUse`. Reading them as one pair is the mistake.
    //
    // Here the box is 64 wide and the tile is one box. With the default, a
    // content coordinate IS a user offset, so `scale(8)` carries the 8px image
    // across all 64 units and the quadrants land in the corners. Read as
    // `objectBoundingBox` instead, the offset would first divide by 64 and the
    // whole canvas would sample texel zero -- flat red.
    const std::string svg =
        "<svg viewBox=\"0 0 64 64\">"
        "<rect x=\"0\" y=\"0\" width=\"64\" height=\"64\" fill=\"url(#p)\"/>"
        "<defs><pattern id=\"p\" width=\"1\" height=\"1\">"
        "<use xlink:href=\"#img\" transform=\"scale(8)\"/></pattern>"
        "<image id=\"img\" width=\"8\" height=\"8\" xlink:href=\"" +
        quadrantPngDataUri(8) + "\"/></defs></svg>";
    auto doc = icf::svg::SvgDocument::parse(svg);
    REQUIRE(doc.has_value());
    REQUIRE(doc->patterns.size() == 1);
    CHECK(doc->patterns.begin()->second.contentUserSpace);

    RenderOptions o;
    o.width = 64;
    o.height = 64;
    auto got = renderSvg(d, *doc, o);
    REQUIRE(got.has_value());
    CHECK(std::abs(at(*got, 16, 16)[0] - 1.0f) < 0.02f);   // red
    CHECK(std::abs(at(*got, 48, 16)[1] - 1.0f) < 0.02f);   // green
    CHECK(std::abs(at(*got, 16, 48)[2] - 1.0f) < 0.02f);   // blue
    // The one that separates the two readings: under the wrong one this is red.
    CHECK(std::abs(at(*got, 48, 16)[0]) < 0.02f);
}

TEST_CASE(the_sampler_weights_premultiplied_and_does_not_pull_in_invisible_colour) {
    // THE SECOND THING THE SWEEP CAUGHT, and it is the same defect the blur's
    // test had this morning: a fixture that is opaque everywhere cannot tell
    // premultiplied weighting from straight, because multiplying by an alpha of
    // one changes nothing. Every quadrant above is opaque, so the mutation that
    // dropped the multiply SURVIVED.
    //
    // What makes it visible is invisible colour that DIFFERS from the visible
    // colour. This drives the sampler directly rather than through a render:
    // two texels, one opaque GREEN and one transparent RED, sampled exactly
    // between them.
    icf::DecodedPng img;
    img.width = 2;
    img.height = 1;
    img.rgba = {
        0.0f, 1.0f, 0.0f, 1.0f,   // opaque green
        1.0f, 0.0f, 0.0f, 0.0f,   // red, and invisible
    };

    ResolvedPattern p;
    p.ok = true;
    p.image = &img;
    p.tileX = 0;
    p.tileY = 0;
    p.tileW = 2;
    p.tileH = 1;
    // User space IS image pixel space here, so the arithmetic under test is the
    // weighting and nothing else.
    p.m[0] = 1; p.m[1] = 0; p.m[2] = 0;
    p.m[3] = 0; p.m[4] = 1; p.m[5] = 0;

    float out[4] = {0, 0, 0, 0};
    // Dead centre between the two texels: half of each.
    REQUIRE(samplePattern(p, 1.0, 0.5, out));

    // Half the alpha, and the colour is STILL PURE GREEN. Straight weighting
    // would report about (0.5, 0.5, 0) -- a dirty olive that no texel holds.
    CHECK(std::abs(out[3] - 0.5f) < 0.02f);
    CHECK(out[0] < 0.02f);
    CHECK(out[1] > 0.98f);

    // And the opaque texel's own centre is untouched by any of this.
    float solid[4] = {0, 0, 0, 0};
    REQUIRE(samplePattern(p, 0.5, 0.5, solid));
    CHECK(std::abs(solid[3] - 1.0f) < 0.02f);
    CHECK(solid[0] < 0.02f);
    CHECK(solid[1] > 0.98f);
}
