// The `<filter>` chain, and the one property that separates it from a clip.
//
// A clip intersects per pixel and a mask multiplies per pixel, so folding either
// into each shape of a group gives the same answer as applying it to the
// composed group. A FILTER DOES NOT, and the test that matters here is the one
// that would still pass if this renderer got that wrong: a blur of two touching
// rectangles is not the two blurs composited.
//
// The transcription's provenance is in `SvgFilter.h`. What the corpus exercises
// is exactly one chain -- Delta's `00_stripes.svg`, `feFlood` -> `feBlend` ->
// `feGaussianBlur` -- so the cases below drive the parts that chain does not
// reach as well, and each says which of the two it is.
#include "check.h"
#include "Source/CoreSVG/Document.h"
#include "Source/RenderBox/SvgFilter.h"
#include "Source/RenderBox/SvgRenderer.h"

#include <cmath>
#include <string>
#include <vector>

using namespace rb;

namespace {

constexpr std::uint32_t kSide = 64;

icf::svg::SvgDocument::Filter chainOf(const std::string& svg) {
    auto doc = icf::svg::SvgDocument::parse(svg);
    if (!doc || doc->filters.empty()) return {};
    return doc->filters.begin()->second;
}

PathGlobals identityPlacement() {
    icf::svg::ViewBox box{0, 0, kSide, kSide};
    return fitViewBox(box, kSide, kSide);
}

// A straight-RGBA canvas with one opaque white rectangle on it.
std::vector<float> whiteRect(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1,
                             std::uint32_t y1) {
    std::vector<float> px(static_cast<std::size_t>(kSide) * kSide * 4, 0.0f);
    for (std::uint32_t y = y0; y < y1; ++y) {
        for (std::uint32_t x = x0; x < x1; ++x) {
            float* p = &px[(static_cast<std::size_t>(y) * kSide + x) * 4];
            p[0] = p[1] = p[2] = 1.0f;
            p[3] = 1.0f;
        }
    }
    return px;
}

// An opaque GREEN rectangle on a field that is fully transparent and whose
// colour bytes are RED.
//
// THE INVISIBLE RED IS THE WHOLE POINT. A canvas of white-on-zero cannot test
// premultiplication at all: its transparent pixels already carry RGB 0, so
// multiplying them by their zero alpha changes nothing and a renderer that
// skipped the multiply would look identical. The bleed only shows when the
// invisible pixels carry a colour that differs from the visible ones -- which is
// the same fixture, and the same reason, as the raster sampler's.
std::vector<float> greenOnInvisibleRed(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1,
                                       std::uint32_t y1) {
    std::vector<float> px(static_cast<std::size_t>(kSide) * kSide * 4, 0.0f);
    for (std::size_t t = 0; t < static_cast<std::size_t>(kSide) * kSide; ++t) {
        px[t * 4 + 0] = 1.0f;  // red, and invisible
        px[t * 4 + 3] = 0.0f;
    }
    for (std::uint32_t y = y0; y < y1; ++y) {
        for (std::uint32_t x = x0; x < x1; ++x) {
            float* p = &px[(static_cast<std::size_t>(y) * kSide + x) * 4];
            p[0] = 0.0f;
            p[1] = 1.0f;
            p[2] = 0.0f;
            p[3] = 1.0f;
        }
    }
    return px;
}

float alphaAt(const std::vector<float>& rgba, std::uint32_t x, std::uint32_t y) {
    return rgba[(static_cast<std::size_t>(y) * kSide + x) * 4 + 3];
}

float channelAt(const std::vector<float>& rgba, std::uint32_t x, std::uint32_t y, int c) {
    return rgba[(static_cast<std::size_t>(y) * kSide + x) * 4 + c];
}

double totalAlpha(const std::vector<float>& rgba) {
    double sum = 0.0;
    for (std::size_t t = 0; t < static_cast<std::size_t>(kSide) * kSide; ++t) {
        sum += rgba[t * 4 + 3];
    }
    return sum;
}

}  // namespace

// ---- the blur ------------------------------------------------------------

TEST_CASE(a_blur_softens_the_edge_and_conserves_the_ink) {
    const auto filter = chainOf(
        R"SVG(<svg viewBox="0 0 64 64"><defs>
          <filter id="b"><feGaussianBlur stdDeviation="3"/></filter></defs></svg>)SVG");
    REQUIRE(filter.primitives.size() == 1);

    const std::vector<float> src = whiteRect(16, 16, 48, 48);
    const auto got = applySvgFilter(filter, src, kSide, kSide, identityPlacement());
    REQUIRE(got.ok);

    // Inside stays opaque, well away from the edge.
    CHECK(alphaAt(got.rgba, 32, 32) > 0.99f);
    // The edge is no longer a step: a pixel just outside the old boundary now
    // carries ink, and one just inside has given some away.
    CHECK(alphaAt(got.rgba, 14, 32) > 0.02f);
    CHECK(alphaAt(got.rgba, 17, 32) < 0.99f);
    // Right on the boundary a symmetric kernel splits the difference.
    CHECK(std::abs(alphaAt(got.rgba, 16, 32) - 0.5f) < 0.08f);

    // AND THE INK IS CONSERVED. A normalised kernel moves alpha around and does
    // not create or destroy it -- a kernel whose weights did not sum to one
    // would still look like a blur and would darken or brighten the whole
    // picture, which is the defect this catches and the eye does not.
    const double before = totalAlpha(src), after = totalAlpha(got.rgba);
    CHECK(std::abs(after - before) / before < 0.01);
}

TEST_CASE(the_blur_averages_premultiplied_and_does_not_pull_in_invisible_colour) {
    // THE DEFECT THIS CATCHES IS INVISIBLE IN THE ALPHA CHANNEL, and the first
    // version of this test could not see it either.
    //
    // That version blurred WHITE on a field of zeros. Its transparent pixels
    // already carried RGB 0, so premultiplying them by their zero alpha changed
    // nothing -- a renderer that skipped the multiply produced identical output
    // and the test stayed green. The mutation sweep of 2026-09-09 said so, and
    // it was right: `pre[r] = src[r] * a` mutated to `pre[r] = src[r]` SURVIVED.
    //
    // What makes the bleed visible is invisible colour that DIFFERS from the
    // visible colour. Green on a transparent RED field: premultiplied, the red
    // contributes nothing and the edge stays pure green; averaged straight, the
    // red walks into every edge pixel and the eye reads a dirty rim.
    const auto filter = chainOf(
        R"SVG(<svg viewBox="0 0 64 64"><defs>
          <filter id="b"><feGaussianBlur stdDeviation="3"/></filter></defs></svg>)SVG");
    const std::vector<float> src = greenOnInvisibleRed(16, 16, 48, 48);
    const auto got = applySvgFilter(filter, src, kSide, kSide, identityPlacement());
    REQUIRE(got.ok);

    // Right on the old boundary the alpha is about half -- and NO RED has
    // arrived. Straight averaging would put roughly 1.0 in this channel.
    CHECK(std::abs(alphaAt(got.rgba, 16, 32) - 0.5f) < 0.08f);
    CHECK(channelAt(got.rgba, 16, 32, 0) < 0.01f);
    CHECK(channelAt(got.rgba, 16, 32, 1) > 0.99f);

    // And out in the tail, where the field dominates the kernel, straight
    // averaging goes reddest of all.
    if (alphaAt(got.rgba, 13, 32) > 0.001f) {
        CHECK(channelAt(got.rgba, 13, 32, 0) < 0.01f);
        CHECK(channelAt(got.rgba, 13, 32, 1) > 0.99f);
    }

    // The interior is untouched by any of this.
    CHECK(channelAt(got.rgba, 32, 32, 0) < 0.01f);
    CHECK(channelAt(got.rgba, 32, 32, 1) > 0.99f);
}

TEST_CASE(a_blur_of_zero_is_the_identity) {
    const auto filter = chainOf(
        R"SVG(<svg viewBox="0 0 64 64"><defs>
          <filter id="b"><feGaussianBlur stdDeviation="0"/></filter></defs></svg>)SVG");
    const std::vector<float> src = whiteRect(16, 16, 48, 48);
    const auto got = applySvgFilter(filter, src, kSide, kSide, identityPlacement());
    REQUIRE(got.ok);
    CHECK_EQ(alphaAt(got.rgba, 32, 32), 1.0f);
    CHECK_EQ(alphaAt(got.rgba, 2, 2), 0.0f);
    // No blur ran, so nothing about a kernel is claimed.
    CHECK(got.notes.empty());
}

// `[BIN]` `drawFeGaussianBlur` (0x23D20) clamps each component against the
// double at `0x4059000000000000` -- 100 -- BEFORE using it. The corpus never
// reaches the ceiling; without this the clamp would be a line no input tests.
TEST_CASE(std_deviation_is_clamped_at_a_hundred_like_the_target) {
    const auto big = chainOf(
        R"SVG(<svg viewBox="0 0 64 64"><defs>
          <filter id="b"><feGaussianBlur stdDeviation="100000"/></filter></defs></svg>)SVG");
    const auto hundred = chainOf(
        R"SVG(<svg viewBox="0 0 64 64"><defs>
          <filter id="b"><feGaussianBlur stdDeviation="100"/></filter></defs></svg>)SVG");
    const std::vector<float> src = whiteRect(16, 16, 48, 48);
    const auto a = applySvgFilter(big, src, kSide, kSide, identityPlacement());
    const auto b = applySvgFilter(hundred, src, kSide, kSide, identityPlacement());
    REQUIRE(a.ok);
    REQUIRE(b.ok);
    for (std::uint32_t p = 0; p < kSide; p += 8) {
        CHECK(std::abs(alphaAt(a.rgba, p, 32) - alphaAt(b.rgba, p, 32)) < 1e-6f);
    }
}

// `[BIN]` At 0x23D88 the target compares the two clamped components, logs
// `"Different radii for gaussian blur not supported"` when they differ, and
// CARRIES ON with the X one. It does not refuse -- so neither does this -- but
// dropping the author's second number without a word would be the silent kind
// of gap this project does not keep.
TEST_CASE(an_anisotropic_std_deviation_uses_the_first_value_and_says_so) {
    const auto aniso = chainOf(
        R"SVG(<svg viewBox="0 0 64 64"><defs>
          <filter id="b"><feGaussianBlur stdDeviation="3 12"/></filter></defs></svg>)SVG");
    const auto iso = chainOf(
        R"SVG(<svg viewBox="0 0 64 64"><defs>
          <filter id="b"><feGaussianBlur stdDeviation="3"/></filter></defs></svg>)SVG");
    const std::vector<float> src = whiteRect(16, 16, 48, 48);
    const auto a = applySvgFilter(aniso, src, kSide, kSide, identityPlacement());
    const auto i = applySvgFilter(iso, src, kSide, kSide, identityPlacement());
    REQUIRE(a.ok);
    REQUIRE(i.ok);
    for (std::uint32_t p = 0; p < kSide; p += 8) {
        CHECK(std::abs(alphaAt(a.rgba, 32, p) - alphaAt(i.rgba, 32, p)) < 1e-6f);
    }
    bool said = false;
    for (const auto& n : a.notes) said = said || n.find("anisotropico") != std::string::npos;
    CHECK(said);
}

// ---- the corpus's one chain ----------------------------------------------

TEST_CASE(a_transparent_flood_under_a_normal_blend_is_the_identity) {
    // The first two thirds of Delta's `00_stripes.svg`: a flood at zero opacity,
    // then `SourceGraphic` blended over it. It has to come back byte for byte,
    // because that is what the author's exporter meant by it -- and a `feFlood`
    // that ignored `flood-opacity` would paint the canvas black instead.
    const auto filter = chainOf(
        R"SVG(<svg viewBox="0 0 64 64"><defs><filter id="f">
          <feFlood flood-opacity="0" result="BackgroundImageFix"/>
          <feBlend mode="normal" in="SourceGraphic" in2="BackgroundImageFix" result="shape"/>
        </filter></defs></svg>)SVG");
    REQUIRE(filter.primitives.size() == 2);
    const std::vector<float> src = whiteRect(16, 16, 48, 48);
    const auto got = applySvgFilter(filter, src, kSide, kSide, identityPlacement());
    REQUIRE(got.ok);
    for (std::size_t t = 0; t < static_cast<std::size_t>(kSide) * kSide; ++t) {
        CHECK(std::abs(got.rgba[t * 4 + 3] - src[t * 4 + 3]) < 1e-6f);
    }
    CHECK(std::abs(got.rgba[(static_cast<std::size_t>(32) * kSide + 32) * 4] - 1.0f) < 1e-6f);
}

TEST_CASE(an_opaque_flood_does_reach_the_canvas) {
    // The guard on the test above: it passes for the right reason only if a
    // flood that is NOT transparent actually paints. Without this, a `feFlood`
    // that always produced nothing would look correct.
    const auto filter = chainOf(
        R"SVG(<svg viewBox="0 0 64 64"><defs><filter id="f">
          <feFlood flood-color="#ff0000"/></filter></defs></svg>)SVG");
    const std::vector<float> src = whiteRect(16, 16, 48, 48);
    const auto got = applySvgFilter(filter, src, kSide, kSide, identityPlacement());
    REQUIRE(got.ok);
    CHECK(std::abs(alphaAt(got.rgba, 2, 2) - 1.0f) < 1e-6f);
    CHECK(std::abs(got.rgba[(static_cast<std::size_t>(2) * kSide + 2) * 4] - 1.0f) < 1e-6f);
    CHECK(std::abs(got.rgba[(static_cast<std::size_t>(2) * kSide + 2) * 4 + 1]) < 1e-6f);
}

// ---- the null semantics, which are the target's --------------------------

TEST_CASE(an_input_naming_a_result_nothing_produced_nulls_the_chain_to_nothing) {
    // `[BIN]` `inputImage` (0x2520C) returns null for a name that is not in the
    // results map, `drawFeBlend` (0x24BFC) returns null when either input is,
    // and `SVGFilter::draw` (0x29A34) sends a null final result to a cleanup
    // path that draws nothing. An empty canvas IS the answer here -- not a
    // refusal, and not the source passed through.
    const auto filter = chainOf(
        R"SVG(<svg viewBox="0 0 64 64"><defs><filter id="f">
          <feBlend mode="normal" in="SourceGraphic" in2="neverProduced"/>
        </filter></defs></svg>)SVG");
    REQUIRE(filter.primitives.size() == 1);
    const std::vector<float> src = whiteRect(16, 16, 48, 48);
    const auto got = applySvgFilter(filter, src, kSide, kSide, identityPlacement());
    REQUIRE(got.ok);
    CHECK_EQ(totalAlpha(got.rgba), 0.0);
}

TEST_CASE(an_absent_in_takes_the_previous_result) {
    // `[BIN]` Same function, the other branch: with no `in` attribute the
    // primitive takes whatever the one before it produced. Delta's chain depends
    // on this -- its `feGaussianBlur` names no input and has to blur the blend's
    // output rather than the untouched source.
    const auto filter = chainOf(
        R"SVG(<svg viewBox="0 0 64 64"><defs><filter id="f">
          <feFlood flood-color="#ffffff" flood-opacity="1" result="solid"/>
          <feGaussianBlur stdDeviation="3"/></filter></defs></svg>)SVG");
    const std::vector<float> src = whiteRect(16, 16, 48, 48);
    const auto got = applySvgFilter(filter, src, kSide, kSide, identityPlacement());
    REQUIRE(got.ok);
    // The blur ran over the FLOOD, which covers everything, so the middle is
    // still solid. Had it run over `SourceGraphic` the corner would be empty.
    CHECK(alphaAt(got.rgba, 32, 32) > 0.99f);
    CHECK(alphaAt(got.rgba, 2, 2) > 0.99f);
}

// ---- what is NOT transcribed says so -------------------------------------

TEST_CASE(a_primitive_the_target_draws_and_this_does_not_is_a_named_gap) {
    // `feOffset` is in the target's six, so a chain using it is a gap of OURS
    // and not a reproduction -- the opposite of a dropped `feColorMatrix`. It
    // has to refuse rather than quietly skip the primitive and draw the rest.
    const auto filter = chainOf(
        R"SVG(<svg viewBox="0 0 64 64"><defs><filter id="f">
          <feOffset dx="4" dy="4"/></filter></defs></svg>)SVG");
    REQUIRE(filter.primitives.size() == 1);
    const std::vector<float> src = whiteRect(16, 16, 48, 48);
    const auto got = applySvgFilter(filter, src, kSide, kSide, identityPlacement());
    CHECK(!got.ok);
    CHECK(got.why.find("feOffset") != std::string::npos);
}

TEST_CASE(a_blend_mode_other_than_normal_is_a_named_gap) {
    const auto filter = chainOf(
        R"SVG(<svg viewBox="0 0 64 64"><defs><filter id="f">
          <feBlend mode="multiply" in="SourceGraphic" in2="SourceGraphic"/>
        </filter></defs></svg>)SVG");
    const std::vector<float> src = whiteRect(16, 16, 48, 48);
    const auto got = applySvgFilter(filter, src, kSide, kSide, identityPlacement());
    CHECK(!got.ok);
    CHECK(got.why.find("multiply") != std::string::npos);
}

TEST_CASE(the_blur_announces_that_its_kernel_was_never_measured) {
    // `[OBS]` The routing is measured -- `stdDeviation` reaches
    // `CIGaussianBlur`'s `inputRadius` (0x23DF4) -- and what CoreImage builds
    // from that number was not read. A picture that is drawn without being
    // claimed has to say which of the two it is, every time.
    const auto filter = chainOf(
        R"SVG(<svg viewBox="0 0 64 64"><defs>
          <filter id="b"><feGaussianBlur stdDeviation="2"/></filter></defs></svg>)SVG");
    const std::vector<float> src = whiteRect(16, 16, 48, 48);
    const auto got = applySvgFilter(filter, src, kSide, kSide, identityPlacement());
    REQUIRE(got.ok);
    bool said = false;
    for (const auto& n : got.notes) said = said || n.find("inputRadius") != std::string::npos;
    CHECK(said);
}

// ---- the region ----------------------------------------------------------

TEST_CASE(the_filter_region_cuts_what_falls_outside_it) {
    // A blur spreads ink past the region; `userSpaceOnUse` says how far it may
    // reach. Without the cut the blur's tail would paint outside the box the
    // author fenced off.
    const auto filter = chainOf(
        R"SVG(<svg viewBox="0 0 64 64"><defs>
          <filter id="b" x="20" y="20" width="24" height="24" filterUnits="userSpaceOnUse">
            <feGaussianBlur stdDeviation="4"/></filter></defs></svg>)SVG");
    REQUIRE(filter.hasRegion);
    REQUIRE(filter.userSpace);
    const std::vector<float> src = whiteRect(24, 24, 40, 40);
    const auto got = applySvgFilter(filter, src, kSide, kSide, identityPlacement());
    REQUIRE(got.ok);
    CHECK(alphaAt(got.rgba, 32, 32) > 0.5f);
    // Just inside the region the tail survives; just outside it is cut.
    CHECK(alphaAt(got.rgba, 21, 32) > 0.0f);
    CHECK_EQ(alphaAt(got.rgba, 18, 32), 0.0f);
}

// ---- AND THE ONE THAT SEPARATES A FILTER FROM A CLIP ----------------------

TEST_CASE(the_filter_applies_to_the_composed_group_not_to_each_shape) {
    Device& d = [] () -> Device& {
        static Device* dev = [] {
            auto made = Device::create();
            return made ? new Device(std::move(*made)) : nullptr;
        }();
        static Device dead;
        return dev ? *dev : dead;
    }();
    if (!d.valid()) return;

    // TWO TOUCHING RECTANGLES, under one filtered group. Blurring the union
    // gives a flat interior across the seam; blurring each and compositing lets
    // each one's edge fall off at the join, and the seam shows.
    //
    // This is the whole reason a filter cannot be folded per shape the way a
    // clip is, and it is the defect a renderer would most plausibly ship: the
    // picture still looks blurred.
    auto doc = icf::svg::SvgDocument::parse(
        R"SVG(<svg viewBox="0 0 64 64">
          <g filter="url(#b)">
            <rect x="16" y="16" width="16" height="32" fill="#ffffff"/>
            <rect x="32" y="16" width="16" height="32" fill="#ffffff"/>
          </g>
          <defs><filter id="b" x="0" y="0" width="64" height="64"
                        filterUnits="userSpaceOnUse">
            <feGaussianBlur stdDeviation="2"/></filter></defs></svg>)SVG");
    REQUIRE(doc.has_value());
    REQUIRE(doc->shapes.size() == 2);
    CHECK_EQ(doc->shapes[0].filterInstance, doc->shapes[1].filterInstance);

    RenderOptions o;
    o.width = kSide;
    o.height = kSide;
    auto got = renderSvg(d, *doc, o);
    REQUIRE(got.has_value());

    // The seam sits at x = 32. Across it the interior has to stay flat: the two
    // rectangles are one shape as far as the blur is concerned.
    const float left = alphaAt(got->rgba, 28, 32);
    const float seam = alphaAt(got->rgba, 32, 32);
    const float right = alphaAt(got->rgba, 36, 32);
    CHECK(seam > 0.99f);
    CHECK(std::abs(seam - left) < 0.01f);
    CHECK(std::abs(seam - right) < 0.01f);

    // And the group DID get blurred -- otherwise the assertions above would
    // pass on an unfiltered pair just as well.
    CHECK(alphaAt(got->rgba, 14, 32) > 0.02f);
    CHECK(alphaAt(got->rgba, 32, 14) > 0.02f);

    // The `[OBS]` about the kernel reaches the render's own report.
    CHECK(!got->filterNotes.empty());
}
