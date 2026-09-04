// The stroke, both halves, against closed form.
//
// The coverage tests use a HORIZONTAL segment on purpose: for that case the
// distance to the stroke has an exact expression, so the assertions are against
// arithmetic written here and not against the transcription's own constants.
#include "check.h"

#include <cmath>

#include "Source/RenderBox/StrokeGeometry.h"

using rb::LineCap;
using rb::LineJoin;
using rb::StrokeParams;
using rb::StrokePoint;

namespace {

bool near(double a, double b, double tol = 1e-12) { return std::fabs(a - b) <= tol; }

StrokeParams sharp() {
    StrokeParams p;
    p.width = 4.0;
    p.miterLimit = 4.0;
    p.cap = LineCap::Butt;
    p.join = LineJoin::Miter;
    p.recipScale = 1.0;
    return p;
}

}  // namespace

// `(1 + cos) * ml^2 < 2` is the SVG rule `ml < 1/sin(theta/2)`, and this checks
// it against that closed form rather than against itself. The `cos > 0.99`
// early-out is a separate arm and is pinned separately -- it fires at about
// 8.1 degrees, well inside where a miter would be harmless.
TEST_CASE(the_miter_limit_matches_the_closed_form_of_the_svg_rule) {
    for (int deg = 2; deg <= 178; deg += 2) {
        const double turn = deg * 3.14159265358979323846 / 180.0;
        const StrokePoint in{1.0, 0.0};
        const StrokePoint out{std::cos(turn), std::sin(turn)};
        const double cosv = std::cos(turn);

        for (double ml : {1.0, 1.5, 2.0, 4.0, 10.0}) {
            const LineJoin got = rb::applyMiterLimit(in, out, ml);
            if (cosv > 0.99) {
                CHECK(got == LineJoin::Round);
                continue;
            }
            // The interior angle between the two segments is pi - turn, and the
            // miter ratio is 1/sin(interior/2) = 1/cos(turn/2).
            const double ratio = 1.0 / std::cos(turn / 2.0);
            const LineJoin expected = (ml < ratio) ? LineJoin::Bevel : LineJoin::Miter;
            CHECK(got == expected);
        }
    }
}

// A hairline gets NO join. `[BIN]` The floor is `bezier_flatness() * recipScale`
// with the flatness 0.25, so at one pixel per point a radius of 0.25 is the
// last one that is refused.
TEST_CASE(a_stroke_below_the_flatness_floor_gets_no_join) {
    StrokeParams p = sharp();
    p.recipScale = 1.0;
    const StrokePoint in{1.0, 0.0}, out{0.0, 1.0};  // a right angle, a real corner
    CHECK_EQ(rb::joinForCorner(in, out, 0.25, p), static_cast<int>(LineJoin::Round));
    CHECK_EQ(rb::joinForCorner(in, out, 0.2500001, p), static_cast<int>(LineJoin::Miter));
}

// An OPEN subpath of k points becomes k+2, with a mirrored ghost outside each
// end. The ghost's POSITION matters: it is the reflection of the neighbour
// through the end, not an extrapolation by some distance.
TEST_CASE(an_open_subpath_is_bracketed_by_mirrored_ghosts) {
    const std::vector<StrokePoint> pts = {{0, 0}, {10, 0}, {10, 10}};
    const auto s = rb::strokePointStream(pts, false, sharp());
    REQUIRE(s.size() == 5);

    CHECK_EQ(s[0].join, rb::kJoinGhostEnd);
    CHECK(near(s[0].x, -10.0) && near(s[0].y, 0.0));   // 2*(0,0) - (10,0)
    CHECK_EQ(s[4].join, rb::kJoinGhostEnd);
    CHECK(near(s[4].x, 10.0) && near(s[4].y, 20.0));   // 2*(10,10) - (10,0)

    // The two real ends are `round`, which emits no join primitive.
    CHECK_EQ(s[1].join, static_cast<int>(LineJoin::Round));
    CHECK_EQ(s[3].join, static_cast<int>(LineJoin::Round));
    // And the interior corner is a real right angle: ratio = 1/cos(45) = 1.414,
    // under a limit of 4, so it stays a miter.
    CHECK_EQ(s[2].join, static_cast<int>(LineJoin::Miter));

    // `[BIN]` The radius is HALF the width, and it is the only place the width
    // reaches the GPU -- `StrokeInfo` does not carry it.
    for (const auto& p : s) CHECK(near(p.radius, 2.0));
}

// TWO INSTANCES, NOT THREE. Five points give `N-3 = 2` line instances, which is
// exactly the two real segments -- and every one of them is drawn, because the
// discard test reads `iid+1` and `iid+2`.
//
// This is the test that would have caught doc 03 §10.2's off-by-one: with the
// published `min(join[iid], join[iid+2])` the first instance reads the ghost and
// the first segment of every subpath vanishes.
TEST_CASE(every_real_segment_of_an_open_subpath_is_drawn) {
    const std::vector<StrokePoint> pts = {{0, 0}, {10, 0}, {10, 10}};
    const auto s = rb::strokePointStream(pts, false, sharp());
    CHECK_EQ(rb::strokeLineInstanceCount(s.size()), std::size_t{2});
    CHECK(rb::strokeInstanceIsDrawn(s, 0));
    CHECK(rb::strokeInstanceIsDrawn(s, 1));
    // The wrong rule, spelled out, so the difference is visible in the file.
    const bool wrongRule = std::min(s[0].join, s[2].join) >= 0;
    CHECK(!wrongRule);
}

// A CLOSED subpath is k+3, and the ring closes: k instances for k edges.
TEST_CASE(a_closed_subpath_wraps_and_draws_every_edge) {
    const std::vector<StrokePoint> square = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    const auto s = rb::strokePointStream(square, true, sharp());
    REQUIRE(s.size() == 7);
    CHECK_EQ(rb::strokeLineInstanceCount(s.size()), std::size_t{4});
    for (std::size_t i = 0; i < 4; ++i) CHECK(rb::strokeInstanceIsDrawn(s, i));
    CHECK_EQ(s[0].join, rb::kJoinWrap);
    CHECK_EQ(s[6].join, rb::kJoinWrap);
    // Every corner of a square is a right angle, so all four are miters.
    for (std::size_t i = 1; i <= 5; ++i) CHECK_EQ(s[i].join, static_cast<int>(LineJoin::Miter));

    // A trailing point equal to the first is the same ring, not a fifth corner.
    const std::vector<StrokePoint> repeated = {{0, 0}, {10, 0}, {10, 10}, {0, 10}, {0, 0}};
    CHECK_EQ(rb::strokePointStream(repeated, true, sharp()).size(), std::size_t{7});
}

// COVERAGE, against the closed form. A horizontal segment with `butt` caps and
// hard coverage covers exactly the rectangle `[0,L] x [-r, r]` grown by half a
// pixel -- which is a rectangle, and a rectangle can be checked by arithmetic.
TEST_CASE(a_butt_capped_segment_covers_the_rectangle_it_should) {
    StrokeParams p = sharp();
    p.hardCoverage = true;
    p.recipScale = 1.0;
    const std::vector<StrokePoint> pts = {{0, 0}, {10, 0}};
    const auto s = rb::strokePointStream(pts, false, p);
    REQUIRE(s.size() == 4);

    // Inside, well away from every edge.
    CHECK(near(rb::strokeCoverageAt(s, 0, 5.0, 0.0, p), 1.0));
    CHECK(near(rb::strokeCoverageAt(s, 0, 5.0, 1.5, p), 1.0));
    // Outside across the width: r = 2, plus half a pixel.
    CHECK(near(rb::strokeCoverageAt(s, 0, 5.0, 3.0, p), 0.0));
    // Past the end: butt stops at the true end, plus the same half pixel.
    CHECK(near(rb::strokeCoverageAt(s, 0, 10.4, 0.0, p), 1.0));
    CHECK(near(rb::strokeCoverageAt(s, 0, 11.0, 0.0, p), 0.0));
}

// The antialiased resolve is a smoothstep exactly ONE pixel wide, centred on
// the boundary. So coverage is 0.5 on the boundary itself, and reaches 0 and 1
// half a pixel either side.
TEST_CASE(the_antialiased_edge_is_a_one_pixel_smoothstep_on_the_boundary) {
    StrokeParams p = sharp();
    p.hardCoverage = false;
    p.recipScale = 1.0;
    const std::vector<StrokePoint> pts = {{0, 0}, {10, 0}};
    const auto s = rb::strokePointStream(pts, false, p);

    CHECK(near(rb::strokeCoverageAt(s, 0, 5.0, 2.0, p), 0.5));
    CHECK(near(rb::strokeCoverageAt(s, 0, 5.0, 2.5, p), 0.0));
    CHECK(near(rb::strokeCoverageAt(s, 0, 5.0, 1.5, p), 1.0));
    // A quarter pixel in: smoothstep(0.75) = 0.75^2 * (3 - 1.5) = 0.84375
    CHECK(near(rb::strokeCoverageAt(s, 0, 5.0, 1.75, p), 0.84375));
}

// ALL SEVEN CAPS, walked. `[ART]` Six of them run in no corpus document -- the
// 35 SVGs with a painted stroke name no `stroke-linecap` at all -- so this is
// the only thing that exercises them, and it is against arithmetic written out
// here rather than against the transcription.
TEST_CASE(the_seven_cap_distance_functions_are_each_their_own_shape) {
    const double ov = 0.75, d = 1.25, r = 2.0, s = 1.0;
    CHECK(near(rb::strokeCapDistance(LineCap::Round, ov, d, r, s, true),
               std::sqrt(ov * ov + d * d)));
    CHECK(near(rb::strokeCapDistance(LineCap::Square, ov, d, r, s, true),
               std::fmax(ov, d)));
    CHECK(near(rb::strokeCapDistance(LineCap::Butt, ov, d, r, s, true),
               std::fmax(r + ov - s, d)));
    CHECK(near(rb::strokeCapDistance(LineCap::OutwardsTriangle, ov, d, r, s, true),
               d + ov));
    CHECK(near(rb::strokeCapDistance(LineCap::InwardsTriangle, ov, d, r, s, true),
               std::fmax(r + ov - d, d)));
    // The two directional ones are mirrors, and swapping the end swaps them.
    CHECK(near(rb::strokeCapDistance(LineCap::ForwardsTriangle, ov, d, r, s, true),
               rb::strokeCapDistance(LineCap::BackwardsTriangle, ov, d, r, s, false)));
    CHECK(near(rb::strokeCapDistance(LineCap::ForwardsTriangle, ov, d, r, s, false),
               rb::strokeCapDistance(LineCap::BackwardsTriangle, ov, d, r, s, true)));
    // ...and they are genuinely different from each other at the same end, or
    // the mirror check above proves nothing.
    CHECK(!near(rb::strokeCapDistance(LineCap::ForwardsTriangle, ov, d, r, s, true),
                rb::strokeCapDistance(LineCap::BackwardsTriangle, ov, d, r, s, true), 1e-9));
}

// A hairline keeps its ink by FADING, not by vanishing between samples: the
// half-width has a floor of half a pixel and the alpha is scaled by the ratio.
TEST_CASE(a_hairline_thinner_than_a_pixel_fades_instead_of_disappearing) {
    StrokeParams p = sharp();
    p.width = 0.5;          // r = 0.25, half a pixel is 0.5
    p.recipScale = 1.0;
    const std::vector<StrokePoint> pts = {{0, 0}, {10, 0}};
    const auto s = rb::strokePointStream(pts, false, p);
    // On the axis: rEff = 0.5, sd = -0.5, smoothstep((0.5+0.5)/1) = 1, and the
    // alpha scale is r/rEff = 0.5.
    CHECK(near(rb::strokeCoverageAt(s, 0, 5.0, 0.0, p), 0.5));
}
