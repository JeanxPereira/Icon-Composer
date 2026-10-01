// The gate for `SystemFill` -- the two canned ramps, the opacity rewrite, and
// the gradient placement that closes `automatic-gradient`.
//
// WHAT THIS FILE IS GUARDING AGAINST, in the order the mistakes are likely:
//
//   1. A TRANSCRIPTION THAT LOST A DIGIT. Four greys, all near a neighbour, all
//      still grey when wrong. So the greys are asserted twice: bit-exact
//      against the literal, and again against their exact 255th -- `[INF]` all
//      four are exact 255ths (255, 245, 31, 15) by arithmetic on the constants
//      as read, and a truncated literal stops being one.
//   2. THE OPACITY REWRITE READ AS A MULTIPLY. `[BIN]` The target REPLACES the
//      stop's alpha with the fill's opacity. Every stop this binary can produce
//      has alpha 1.0, where replace and multiply agree, so the target itself
//      can never show the difference. `system_fill_opacity_replaces_not_multiplies`
//      writes the wrong number next to the right one, the way
//      `test_rb_mip.cpp` does for the premultiply.
//   3. A PLACEMENT THAT IS TRANSPOSED OR UNSCALED. Every placement test uses a
//      rect that is neither square nor at the origin, so x/y swapped, scale
//      dropped, or origin dropped each fail on their own.
//   4. THE NIL SUBSTITUTION NOT ACTUALLY BEING EXERCISED. The default is
//      reached through an empty `std::optional`, never by calling
//      `defaultGradientPlacement()` -- calling it directly would test the
//      constant and not the substitution, which is the part the draw path does.
#include "check.h"
#include "Source/RenderBox/RenderingParameters.h"
#include "Source/RenderBox/SystemFill.h"

#include <cmath>
#include <optional>
#include <vector>

namespace {

// The shape both canned ramps share, asserted once so each ramp's own test is
// about its two greys. `[BIN]` `IconRendering 0x3E680`: count 2 from the header
// constant at `0x938B0`, locations from `{1.0, 0.0}` at `0x971F0`, alpha 1.0,
// and `r == g == b` because the builder stores the same `d` register three
// times.
void checkRampShape(const std::vector<rb::RampStop>& ramp) {
    REQUIRE(ramp.size() == 2u);
    CHECK(ramp[0].location == 0.0);
    CHECK(ramp[1].location == 1.0);
    for (const rb::RampStop& s : ramp) {
        CHECK(s.rgba[1] == s.rgba[0]);
        CHECK(s.rgba[2] == s.rgba[0]);
        CHECK(s.rgba[3] == 1.0);
    }
}

}  // namespace

// THE LIGHT RAMP, EXACT.
//
// `[BIN]` `0x5E8E8`: `d0 = 1.0`, `d1 = 0.9607843137254902`, into
// `ICRRenderingParameters+0x80`.
TEST_CASE(system_fill_light_ramp_is_exact) {
    const std::vector<rb::RampStop> ramp = rb::systemLightGradient();
    REQUIRE(ramp.size() == 2u);
    checkRampShape(ramp);

    // Bit-exact against the doubles as read from the two call-site immediates.
    CHECK(ramp[0].rgba[0] == 1.0);
    CHECK(ramp[1].rgba[0] == 0.9607843137254902);

    // And again as exact 255ths. `[INF]` 255 -> 245. This is the assertion a
    // dropped final digit fails: `0.960784313725490` still rounds to 245 as a
    // byte, but it is no longer the double that `245.0 / 255.0` produces.
    CHECK(ramp[0].rgba[0] == 255.0 / 255.0);
    CHECK(ramp[1].rgba[0] == 245.0 / 255.0);
    CHECK_EQ(std::lround(ramp[0].rgba[0] * 255.0), 255L);
    CHECK_EQ(std::lround(ramp[1].rgba[0] * 255.0), 245L);
}

// THE DARK RAMP, EXACT.
//
// `[BIN]` `0x5E908`: `d0 = 0.12156862745098039`, `d1 = 0.058823529411764705`,
// into `ICRRenderingParameters+0x88`.
TEST_CASE(system_fill_dark_ramp_is_exact) {
    const std::vector<rb::RampStop> ramp = rb::systemDarkGradient();
    REQUIRE(ramp.size() == 2u);
    checkRampShape(ramp);

    CHECK(ramp[0].rgba[0] == 0.12156862745098039);
    CHECK(ramp[1].rgba[0] == 0.058823529411764705);

    // `[INF]` 31 -> 15.
    CHECK(ramp[0].rgba[0] == 31.0 / 255.0);
    CHECK(ramp[1].rgba[0] == 15.0 / 255.0);
    CHECK_EQ(std::lround(ramp[0].rgba[0] * 255.0), 31L);
    CHECK_EQ(std::lround(ramp[1].rgba[0] * 255.0), 15L);

    // The two ramps are not each other, and the dark one is not the light one
    // scaled: 31/15 is not 255/245. A single builder called with the wrong pair
    // would pass every shape check above.
    CHECK(ramp[0].rgba[0] != rb::systemLightGradient()[0].rgba[0]);
}

// The `csel` at `0x3CF7C` selects on "is it dark", so both arms are exercised
// and both are checked against the ramp they must be, not merely against each
// other.
TEST_CASE(system_fill_selects_ramp_by_appearance) {
    REQUIRE(rb::systemGradient(rb::SystemFill::Light).size() == 2u);
    REQUIRE(rb::systemGradient(rb::SystemFill::Dark).size() == 2u);
    CHECK(rb::systemGradient(rb::SystemFill::Light)[0].rgba[0] == 1.0);
    CHECK(rb::systemGradient(rb::SystemFill::Dark)[0].rgba[0] == 31.0 / 255.0);
    CHECK(rb::systemGradient(rb::SystemFill::Light)[1].rgba[0] == 245.0 / 255.0);
    CHECK(rb::systemGradient(rb::SystemFill::Dark)[1].rgba[0] == 15.0 / 255.0);
}

// THE OPACITY REWRITE, HEAD ON.
//
// `[BIN]` The helper at `IconRendering 0x3CFF4` rebuilds each stop as
// `Stop(color: (r, g, b, opacity), location: location)`, reading r, g, b from
// `+0x00/+0x08/+0x10` and the location from `+0x20`, and NEVER READING `+0x18`
// -- the source alpha. The fill's opacity therefore REPLACES the stop's alpha.
//
// A source stop with alpha 0.5 and a fill opacity of 0.25 must come out at
//
//     0.25    <- replaced, what the target does
//     0.125   <- multiplied, the wrong answer, written here on purpose
//
// Both canned ramps carry alpha 1.0 at every stop, and at 1.0 the two rules
// agree exactly. So this is the one assertion in the file whose input the
// target cannot produce -- which is precisely why it exists: the wrong rule
// would render identically forever and no corpus case would ever catch it.
TEST_CASE(system_fill_opacity_replaces_not_multiplies) {
    std::vector<rb::RampStop> source(1);
    source[0].rgba[0] = 0.4;
    source[0].rgba[1] = 0.5;
    source[0].rgba[2] = 0.6;
    source[0].rgba[3] = 0.5;   // the alpha the target never loads
    source[0].location = 0.75;

    const std::vector<rb::RampStop> out = rb::rewriteStopOpacity(source, 0.25);
    REQUIRE(out.size() == 1u);
    CHECK(out[0].rgba[3] == 0.25);    // replaced
    CHECK(out[0].rgba[3] != 0.125);   // multiplied -- 0.5 * 0.25

    // The three colour components and the location survive untouched; only the
    // alpha slot is rewritten.
    CHECK(out[0].rgba[0] == 0.4);
    CHECK(out[0].rgba[1] == 0.5);
    CHECK(out[0].rgba[2] == 0.6);
    CHECK(out[0].location == 0.75);
}

// The same distinction through the whole resolve, on a real ramp: the canned
// alpha is 1.0, so an opacity of 0.25 must give 0.25 on BOTH stops -- and a
// resolve that forgot to rewrite at all would leave 1.0 here.
TEST_CASE(system_fill_resolve_rewrites_every_stop) {
    const rb::ResolvedSystemFill r = rb::resolveSystemFill(rb::SystemFill::Dark, 0.25);
    REQUIRE(r.stops.size() == 2u);
    CHECK(r.stops[0].rgba[3] == 0.25);
    CHECK(r.stops[1].rgba[3] == 0.25);
    // The greys and locations are the dark ramp's, unchanged by the rewrite.
    CHECK(r.stops[0].rgba[0] == 31.0 / 255.0);
    CHECK(r.stops[1].rgba[0] == 15.0 / 255.0);
    CHECK(r.stops[0].location == 0.0);
    CHECK(r.stops[1].location == 1.0);

    // `[BIN]` `0x3CFAC` zeroes the placement region and `0x3CFB8` tags it
    // `.none`. A `.system` fill carries no geometry of its own, ever.
    CHECK(!r.placement.has_value());

    // At opacity 1.0 the ramp comes back as built -- the rewrite is a no-op on
    // exactly the input the target always has.
    const rb::ResolvedSystemFill opaque = rb::resolveSystemFill(rb::SystemFill::Light, 1.0);
    REQUIRE(opaque.stops.size() == 2u);
    CHECK(opaque.stops[0].rgba[3] == 1.0);
    CHECK(opaque.stops[1].rgba[3] == 1.0);
    CHECK(opaque.stops[1].rgba[0] == 245.0 / 255.0);
}

// `[BIN]` `GradientPlacement.default`, `IconRendering 0x38CF4`: start (0,0),
// end (0,1). The constant itself, before any rect touches it.
TEST_CASE(system_fill_default_placement_is_the_vertical_unit_axis) {
    const rb::GradientPlacement p = rb::defaultGradientPlacement();
    CHECK(p.start.x == 0.0);
    CHECK(p.start.y == 0.0);
    CHECK(p.end.x == 0.0);
    CHECK(p.end.y == 1.0);
}

// UNIT -> RECT, on a rect that is neither square nor at the origin.
//
// `[BIN]` `0x1BB60`-`0x1BB74`: `point = rect.origin + unit * (width, height)`.
// The rect below has a different origin and a different extent on each axis, so
// a transposed mapping, a dropped scale and a dropped origin all produce
// different numbers from the right one. A square rect at the origin would let
// all three pass.
TEST_CASE(system_fill_unit_point_maps_onto_rect) {
    const rb::PlacementRect rect{10.0, 20.0, 200.0, 50.0};

    const rb::PlacementPoint origin = rb::placeUnitPoint({0.0, 0.0}, rect);
    CHECK(origin.x == 10.0);
    CHECK(origin.y == 20.0);

    const rb::PlacementPoint far = rb::placeUnitPoint({1.0, 1.0}, rect);
    CHECK(far.x == 210.0);   // 10 + 1 * 200
    CHECK(far.y == 70.0);    // 20 + 1 * 50

    // An asymmetric interior point: x and y take different fractions of
    // different extents, so swapping the axes gives (60, 45) instead.
    const rb::PlacementPoint inside = rb::placeUnitPoint({0.25, 0.5}, rect);
    CHECK(inside.x == 60.0);   // 10 + 0.25 * 200
    CHECK(inside.y == 45.0);   // 20 + 0.50 * 50
    CHECK(inside.x != 45.0);
    CHECK(inside.y != 60.0);

    // A rect with a negative origin, because the origin is ADDED and not
    // clamped: nothing read says the rect sits in a positive quadrant.
    const rb::PlacementRect shifted{-8.0, -3.0, 4.0, 6.0};
    const rb::PlacementPoint p = rb::placeUnitPoint({0.5, 1.0}, shifted);
    CHECK(p.x == -6.0);
    CHECK(p.y == 3.0);
}

// THE NIL SUBSTITUTION, reached the way the draw path reaches it.
//
// `[BIN]` `0x1BAB8`-`0x1BAE4` reads the `Optional<GradientPlacement>`
// discriminator and substitutes the default on `.none`. The input here is an
// EMPTY `std::optional` -- `defaultGradientPlacement()` is never called by this
// test, because calling it would check the constant and skip the substitution.
TEST_CASE(system_fill_nil_placement_substitutes_the_default) {
    const rb::PlacementRect rect{10.0, 20.0, 200.0, 50.0};
    const std::optional<rb::GradientPlacement> none;
    REQUIRE(!none.has_value());

    const rb::GradientAxis axis = rb::placeGradient(none, rect);
    // (0,0) and (0,1) through the rect: both x land on the LEFT edge, and y
    // spans the full height. If the default were substituted after the mapping
    // rather than before, the endpoints would come out (0,0) and (0,1).
    CHECK(axis.start.x == 10.0);
    CHECK(axis.start.y == 20.0);
    CHECK(axis.end.x == 10.0);
    CHECK(axis.end.y == 70.0);

    // The axis is vertical in rect coordinates and its length is the rect's
    // HEIGHT, not its width and not the larger side -- the mapping applies each
    // extent to its own axis with no aspect correction.
    CHECK(axis.end.x == axis.start.x);
    CHECK(axis.end.y - axis.start.y == rect.height);

    // `[OBS]` Which END of that segment carries 255 and which 245 -- and, on
    // the dark ramp, 31 against 15 -- is NOT asserted here and is not known.
    // The RB display list's y-handedness was never established, so this test
    // pins the axis's geometry and deliberately says nothing about its
    // direction on screen.
}

// A resolved `.system` fill drives the substitution end to end: the resolve
// produces the nil, and the nil produces the default axis. Nothing between them
// supplies a placement.
TEST_CASE(system_fill_resolved_system_fill_draws_on_the_default_axis) {
    const rb::ResolvedSystemFill r = rb::resolveSystemFill(rb::SystemFill::Light, 1.0);
    const rb::PlacementRect rect{-40.0, 5.0, 80.0, 320.0};
    const rb::GradientAxis axis = rb::placeGradient(r.placement, rect);
    CHECK(axis.start.x == -40.0);
    CHECK(axis.start.y == 5.0);
    CHECK(axis.end.x == -40.0);
    CHECK(axis.end.y == 325.0);
}

// A placement that IS present must be used as given, or the substitution would
// be indistinguishable from ignoring the input entirely.
TEST_CASE(system_fill_present_placement_is_not_replaced) {
    const rb::PlacementRect rect{10.0, 20.0, 200.0, 50.0};
    rb::GradientPlacement custom;
    custom.start = {1.0, 0.0};
    custom.end = {0.0, 0.0};
    const rb::GradientAxis axis = rb::placeGradient(std::optional<rb::GradientPlacement>(custom), rect);
    CHECK(axis.start.x == 210.0);
    CHECK(axis.start.y == 20.0);
    CHECK(axis.end.x == 10.0);
    CHECK(axis.end.y == 20.0);
    // Horizontal, and pointing backwards -- neither of which the default is.
    CHECK(axis.start.y == axis.end.y);
    CHECK(axis.start.x > axis.end.x);
}

// THE RECT, BOTH WAYS.
//
// `[BIN]` `supportsChicletAlignmentForSystemFills` is `true` in generation 27
// (`+0x360`, `strh w22,[x19,#0x360]` with `w22 = 1` at `0x5EDB0`) and `false`
// in generation 26 (`0x7707C`), and the branch it guards fires only for
// `Contents == .system` (`0x1AD40`). Set, the rect is origin `(0, 0)` -- three
// zeroed registers at `0x1BAA0` -- with the size the drawing context carries at
// `+0x48`, the canvas; clear, it is the shape's own bounding rect (`0x1BC8C`).
//
// This case asserted the ABSENCE of the aligned rect until 2026-10-01, and said
// that whoever filled the branch in should bring the reading. That is the
// reading; `SystemFill.h` carries the seals.
TEST_CASE(system_fill_rect_is_the_canvas_when_aligned_and_the_shape_box_when_not) {
    const rb::PlacementRect bounds{10.0, 20.0, 200.0, 50.0};
    const rb::PlacementRect canvas{0.0, 0.0, 512.0, 512.0};

    const rb::PlacementRect own =
        rb::systemFillRect(rb::SystemFillRectSource::BoundingRect, bounds, canvas);
    CHECK(own.x == 10.0);
    CHECK(own.y == 20.0);
    CHECK(own.width == 200.0);
    CHECK(own.height == 50.0);

    const rb::PlacementRect aligned =
        rb::systemFillRect(rb::SystemFillRectSource::ChicletAligned, bounds, canvas);
    CHECK(aligned.x == 0.0);
    CHECK(aligned.y == 0.0);
    CHECK(aligned.width == 512.0);
    CHECK(aligned.height == 512.0);

    // The default vertical axis over each: the ramp of an aligned fill runs the
    // whole canvas, whatever the shape.
    const rb::GradientAxis axis = rb::placeGradient(std::nullopt, aligned);
    CHECK(axis.start.y == 0.0);
    CHECK(axis.end.y == 512.0);
    const rb::GradientAxis ownAxis = rb::placeGradient(std::nullopt, own);
    CHECK(ownAxis.start.y == 20.0);
    CHECK(ownAxis.end.y == 70.0);

    CHECK(rb::renderingParameters(rb::DesignGeneration::G27).supportsChicletAlignmentForSystemFills);
    CHECK(!rb::renderingParameters(rb::DesignGeneration::G26).supportsChicletAlignmentForSystemFills);
}

// THE TWO RAMPS OF GENERATION 26, EXACT.
//
// `[BIN]` `0x76FC0` builds both again and stores them over `+0x80` and `+0x88`
// (`0x77124`, `0x77190`): light `1.0 -> 0.925` (`0x770F8`-`0x77118`), dark
// `0.19215686274509805 -> 0.0784313725490196` (`0x77154`-`0x77184`). Same
// builder, so the same shape: two stops at 0 and 1, grey, alpha 1.
TEST_CASE(system_fill_ramps_of_generation_26_are_exact) {
    const rb::SystemGradients& g26 =
        rb::renderingParameters(rb::DesignGeneration::G26).systemGradients;
    const std::vector<rb::RampStop> light = rb::systemLightGradient(g26);
    const std::vector<rb::RampStop> dark = rb::systemDarkGradient(g26);
    REQUIRE(light.size() == 2u);
    REQUIRE(dark.size() == 2u);
    checkRampShape(light);
    checkRampShape(dark);

    CHECK(light[0].rgba[0] == 1.0);
    CHECK(light[1].rgba[0] == 0.925);
    CHECK(dark[0].rgba[0] == 0.19215686274509805);
    CHECK(dark[1].rgba[0] == 0.0784313725490196);
    // The dark pair is 49 and 20 two-hundred-and-fifty-fifths; 0.925 is not a
    // 255th at all (it would be 235.875).
    CHECK(dark[0].rgba[0] == 49.0 / 255.0);
    CHECK(dark[1].rgba[0] == 20.0 / 255.0);

    // Through the selector and the resolve, and generation 27 untouched by the
    // parameter existing.
    CHECK(rb::systemGradient(rb::SystemFill::Dark, g26)[0].rgba[0] == 49.0 / 255.0);
    CHECK(rb::systemGradient(rb::SystemFill::Light, g26)[1].rgba[0] == 0.925);
    CHECK(rb::resolveSystemFill(rb::SystemFill::Dark, 0.5, g26).stops[1].rgba[0] == 20.0 / 255.0);
    CHECK(rb::resolveSystemFill(rb::SystemFill::Dark, 0.5, g26).stops[1].rgba[3] == 0.5);
    const rb::SystemGradients& g27 =
        rb::renderingParameters(rb::DesignGeneration::G27).systemGradients;
    CHECK(rb::systemGradient(rb::SystemFill::Dark, g27)[0].rgba[0] == 31.0 / 255.0);
    CHECK(rb::systemGradient(rb::SystemFill::Light, g27)[1].rgba[0] == 245.0 / 255.0);
}
