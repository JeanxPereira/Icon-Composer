// `automatic-gradient`: the rule, transcribed from the binary.
//
// This closes doc 01's question 4, open since the format was surveyed. What is
// pinned here is chosen by one criterion: what would a plausible INVENTION have
// got wrong? Six parameters named for brightness bands invite a rule where the
// parameters describe the bands. They do not -- the boundaries are constants in
// the code -- and a ramp built the inviting way would have looked right.
#include "check.h"
#include "Source/RenderBox/AutomaticGradient.h"

#include <cmath>
#include <vector>

using namespace rb;

namespace {

icf::Color rgb(double r, double g, double b, double a = 1.0) {
    icf::Color c;
    c.space = icf::ColorSpace::SRGB;
    c.count = 4;
    c.components[0] = r;
    c.components[1] = g;
    c.components[2] = b;
    c.components[3] = a;
    return c;
}

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

}  // namespace

// `[BIN]` Rec.709, and the three coefficients sum to exactly one -- which is
// what makes a grey stay put rather than drifting.
TEST_CASE(the_luminance_is_rec709_and_sums_to_one) {
    CHECK(near(luminance709(1, 1, 1), 1.0));
    CHECK(near(luminance709(1, 0, 0), 0.2126));
    CHECK(near(luminance709(0, 1, 0), 0.7152));
    CHECK(near(luminance709(0, 0, 1), 0.0722));
    CHECK(near(luminance709(0, 0, 0), 0.0));
    // Green is the heaviest by a long way, which a naive (r+g+b)/3 would miss.
    CHECK(luminance709(0, 1, 0) > 3.0 * luminance709(1, 0, 0));
}

// THE THING AN INVENTION WOULD HAVE GOT WRONG. The band boundaries are 0.25,
// 0.5 and 0.75, hard-coded, and the comparison is `<=`. A rule that derived the
// boundaries from `basePosition` -- the obvious guess, since it is the only
// position-shaped parameter -- would put them somewhere else entirely.
TEST_CASE(the_band_boundaries_are_fixed_and_the_comparison_is_inclusive) {
    AutomaticGradientParameters p;
    // Four greys, one per band, chosen so their luminance lands either side of
    // each boundary. A grey's luminance is its own value, because the
    // coefficients sum to one.
    struct Case {
        double grey;
        double expected;   // the lightening that band uses
    };
    const Case cases[] = {
        {0.10, p.dimLightening},        {0.25, p.dimLightening},        // <= is inclusive
        {0.26, p.midDimLightening},     {0.50, p.midDimLightening},
        {0.51, p.midBrightLightening},  {0.75, p.midBrightLightening},
        {0.76, p.brightLightening},     {1.00, p.brightLightening},
    };
    for (const Case& c : cases) {
        const std::vector<RampStop> stops = automaticGradient(rgb(c.grey, c.grey, c.grey), p);
        REQUIRE(stops.size() == 2u);
        // For a grey the saturation boost does nothing (every channel already
        // equals the luminance), so the shifted colour isolates the lightening.
        const bool positive = c.expected > 0.0;
        const RampStop& shifted = positive ? stops[0] : stops[1];
        const double toward = positive ? (1.0 - c.grey) : c.grey;
        const double want = std::min(1.0, std::max(0.0, c.grey + c.expected * toward));
        if (!near(shifted.rgba[0], want)) {
            std::printf("  FAIL grey=%.2f: got %.9f, want %.9f\n", c.grey, shifted.rgba[0],
                        want);
            ++ictest::failures();
        }
    }
    CHECK(true);
}

// `[BIN]` The sign of the lightening flips WHICH END the derived colour sits
// at, and the target sorts the two stops by location because of it. A fixed
// order would be right for the three lightening bands and reversed for the
// darkening one.
TEST_CASE(the_sign_of_the_lightening_flips_the_ends) {
    // A dark colour: dimLightening is positive, so the LIGHTENED colour leads.
    const std::vector<RampStop> dark = automaticGradient(rgb(0.1, 0.1, 0.1));
    REQUIRE(dark.size() == 2u);
    CHECK(near(dark[0].location, 0.0));
    CHECK(near(dark[1].location, 1.0));
    CHECK(dark[0].rgba[0] > 0.1);              // lightened, and it comes first
    CHECK(near(dark[1].rgba[0], 0.1));         // the original

    // A bright colour: brightLightening is NEGATIVE, so the original leads and
    // the darkened one trails.
    const std::vector<RampStop> bright = automaticGradient(rgb(0.9, 0.9, 0.9));
    REQUIRE(bright.size() == 2u);
    CHECK(near(bright[0].location, 0.0));
    CHECK(near(bright[1].location, 1.0));
    CHECK(near(bright[0].rgba[0], 0.9));       // the original comes first now
    CHECK(bright[1].rgba[0] < 0.9);            // darkened, ×(1 - 0.05)
    CHECK(near(bright[1].rgba[0], 0.9 * 0.95));
}

// `[BIN]` The boost pushes each channel AWAY from the luminance, so a saturated
// colour gets more saturated and a grey does not move at all.
TEST_CASE(the_saturation_boost_pushes_away_from_the_luminance) {
    const std::vector<RampStop> red = automaticGradient(rgb(0.8, 0.2, 0.2));
    REQUIRE(red.size() == 2u);
    const double L = luminance709(0.8, 0.2, 0.2);
    // The derived stop is the one that is NOT the original.
    const RampStop& derived = std::fabs(red[0].rgba[0] - 0.8) > 1e-12 ? red[0] : red[1];
    // Before any lightening, the boosted red is 0.8 + 0.2*(0.8 - L) and the
    // boosted green is 0.2 + 0.2*(0.2 - L): one moves up, the other down.
    CHECK(derived.rgba[0] > 0.8);
    CHECK(derived.rgba[1] < 0.2 + 0.2);
    CHECK(L > 0.2);
    CHECK(L < 0.8);

    // A grey has every channel at the luminance, so the boost is exactly zero
    // and only the lightening moves it.
    const std::vector<RampStop> grey = automaticGradient(rgb(0.4, 0.4, 0.4));
    const AutomaticGradientParameters p;
    CHECK(near(grey[0].rgba[0], 0.4 + p.midDimLightening * (1.0 - 0.4)));
}

// `[BIN]` `basePosition` moves ONLY the original colour's stop, and at its
// measured default of 0.0 it does nothing at all -- which is why reading the
// defaults without the rule could not have told anyone what it does.
TEST_CASE(base_position_moves_only_the_second_stop) {
    AutomaticGradientParameters p;
    const std::vector<RampStop> at0 = automaticGradient(rgb(0.1, 0.1, 0.1), p);
    p.basePosition = 0.25;
    const std::vector<RampStop> at25 = automaticGradient(rgb(0.1, 0.1, 0.1), p);
    REQUIRE(at0.size() == 2u && at25.size() == 2u);
    CHECK(near(at0[0].location, at25[0].location));      // the derived end stays
    CHECK(near(at0[1].location, 1.0));
    CHECK(near(at25[1].location, 0.75));                 // 1 - basePosition
}

// The alpha passes through untouched to BOTH stops -- it is not lightened, not
// boosted, and not clamped along with the colour.
TEST_CASE(the_alpha_reaches_both_stops_untouched) {
    const std::vector<RampStop> s = automaticGradient(rgb(0.3, 0.5, 0.7, 0.42));
    REQUIRE(s.size() == 2u);
    CHECK(near(s[0].rgba[3], 0.42));
    CHECK(near(s[1].rgba[3], 0.42));
}

// The result never leaves [0,1]: the target clamps, and a colour near white in
// a positive band would otherwise run past one.
TEST_CASE(the_derived_colour_is_clamped) {
    const std::vector<RampStop> s = automaticGradient(rgb(1.0, 0.0, 0.0));
    REQUIRE(s.size() == 2u);
    for (const RampStop& stop : s) {
        for (int i = 0; i < 3; ++i) {
            CHECK(stop.rgba[i] >= 0.0);
            CHECK(stop.rgba[i] <= 1.0);
        }
    }
    // Red at full has luminance 0.2126, so it is in the dim band and gets
    // boosted away from that luminance -- which pushes red ABOVE one before the
    // clamp catches it.
    const double L = luminance709(1.0, 0.0, 0.0);
    CHECK(1.0 - 0.2 * (L - 1.0) > 1.0);
}
