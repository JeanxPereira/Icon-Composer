// The stroke from an SVG shape to pixels: the flattener, and the scan.
#include "check.h"

#include <cmath>

#include "Source/RenderBox/StrokeRender.h"

using icf::svg::Path;
using icf::svg::Segment;
using icf::svg::SegmentKind;

namespace {

Segment move(double x, double y) { Segment s; s.kind = SegmentKind::Move; s.p[0] = {x, y}; return s; }
Segment line(double x, double y) { Segment s; s.kind = SegmentKind::Line; s.p[0] = {x, y}; return s; }
Segment close() { Segment s; s.kind = SegmentKind::Close; return s; }
Segment cubic(double ax, double ay, double bx, double by, double cx, double cy) {
    Segment s;
    s.kind = SegmentKind::Cubic;
    s.p[0] = {ax, ay}; s.p[1] = {bx, by}; s.p[2] = {cx, cy};
    return s;
}

}  // namespace

// A `Move` ends a subpath WITHOUT closing it, and a `Close` closes it. Getting
// that wrong turns caps into joins and vice versa, and nothing about the
// picture would obviously say so.
TEST_CASE(a_move_ends_a_subpath_and_a_close_shuts_it) {
    Path p;
    p.segments = {move(0, 0), line(10, 0), close(), move(20, 0), line(30, 0)};
    const auto subs = rb::flattenForStroke(p, 16);
    REQUIRE(subs.size() == 2);
    CHECK(subs[0].closed);
    CHECK(!subs[1].closed);
    CHECK_EQ(subs[0].points.size(), std::size_t{2});
    CHECK_EQ(subs[1].points.size(), std::size_t{2});
}

// A cubic becomes `subdivisions` segments, and the knob is the SAME one the
// distance field uses -- a layer's stroke and its glass are flattened alike by
// construction, which is the only reason the two agree at a boundary.
TEST_CASE(a_cubic_is_split_into_the_subdivisions_asked_for) {
    Path p;
    p.segments = {move(0, 0), cubic(0, 10, 10, 10, 10, 0)};
    CHECK_EQ(rb::flattenForStroke(p, 4).front().points.size(), std::size_t{5});
    CHECK_EQ(rb::flattenForStroke(p, 16).front().points.size(), std::size_t{17});
    // And the last point is the curve's end, exactly -- an off-by-one in the
    // loop would stop short and leave a gap the join pass would paper over.
    const auto pts = rb::flattenForStroke(p, 4).front().points;
    CHECK(std::fabs(pts.back().x - 10.0) < 1e-12);
    CHECK(std::fabs(pts.back().y - 0.0) < 1e-12);
}

// A subpath of one point draws nothing and must not reach the stream, where it
// would make the instance count lie.
TEST_CASE(a_degenerate_subpath_is_dropped_before_it_reaches_the_stream) {
    Path p;
    p.segments = {move(5, 5), move(0, 0), line(10, 0)};
    const auto subs = rb::flattenForStroke(p, 16);
    REQUIRE(subs.size() == 1);
    CHECK_EQ(subs[0].points.size(), std::size_t{2});
}

// THE SCAN, against the rectangle it should paint. A horizontal line of width 4
// under an identity placement covers `y` in `[-2, 2]` around the axis, and the
// pixel centres at 0.5 offsets make that checkable by counting.
TEST_CASE(a_horizontal_stroke_paints_the_band_it_should) {
    icf::svg::Shape shape;
    shape.path.segments = {move(10, 32), line(54, 32)};
    shape.stroke.kind = icf::svg::PaintKind::Color;
    shape.strokeWidth = 4.0;

    rb::StrokePlacement pl;  // identity, scale 1
    rb::StrokeParams base;
    base.cap = rb::LineCap::Butt;
    base.join = rb::LineJoin::Miter;

    const auto cov = rb::rasteriseStroke(shape, pl, 64, 64, 16, base);
    REQUIRE(cov.size() == std::size_t{64 * 64});

    auto at = [&](int x, int y) { return cov[static_cast<std::size_t>(y) * 64 + x]; };
    // Dead centre of the band, well inside both ends.
    CHECK(at(32, 32) > 0.99f);
    CHECK(at(32, 31) > 0.99f);
    // Four pixels away across the width is outside by more than the ramp.
    CHECK(at(32, 36) < 0.01f);
    CHECK(at(32, 27) < 0.01f);
    // And past the ends, where `butt` stops.
    CHECK(at(5, 32) < 0.01f);
    CHECK(at(60, 32) < 0.01f);
}

// An UNSTROKED shape produces nothing at all, rather than a transparent image.
// The caller composites only what it gets, and an empty vector is how it knows
// there was nothing -- a full one of zeros costs a pass over every pixel.
TEST_CASE(an_unstroked_shape_rasterises_to_nothing) {
    icf::svg::Shape shape;
    shape.path.segments = {move(0, 0), line(10, 0)};
    shape.stroke.kind = icf::svg::PaintKind::None;
    CHECK(rb::rasteriseStroke(shape, {}, 64, 64, 16, {}).empty());

    shape.stroke.kind = icf::svg::PaintKind::Color;
    shape.strokeWidth = 0.0;
    CHECK(rb::rasteriseStroke(shape, {}, 64, 64, 16, {}).empty());
}

// The width crosses from USER SPACE into pixels through the map's scale. A
// stroke drawn under a 2x placement is twice as wide in pixels, and getting
// this wrong is invisible at scale 1 -- which is what a unit test would use by
// default and a real document never does.
TEST_CASE(the_stroke_width_is_scaled_by_the_placement) {
    icf::svg::Shape shape;
    shape.path.segments = {move(0, 16), line(32, 16)};
    shape.stroke.kind = icf::svg::PaintKind::Color;
    shape.strokeWidth = 2.0;

    rb::StrokePlacement twice;
    twice.m0[0] = 2.0;
    twice.m1[1] = 2.0;
    twice.scale = 2.0;

    const auto cov = rb::rasteriseStroke(shape, twice, 64, 64, 16, {});
    REQUIRE(cov.size() == std::size_t{64 * 64});
    auto at = [&](int x, int y) { return cov[static_cast<std::size_t>(y) * 64 + x]; };
    // The line lands at y = 32 and is 4 pixels wide, so 31 and 33 are inside
    // and 35 is out. At scale 1 it would be 2 wide and 33 would be on the edge.
    CHECK(at(32, 32) > 0.99f);
    CHECK(at(32, 33) > 0.99f);
    CHECK(at(32, 35) < 0.01f);
}

// Overlapping segments take the MAX, not the sum. Every join is an overlap, and
// summing would draw a bright seam at each corner -- which looks like a
// highlight and is nobody's stroke.
TEST_CASE(overlapping_segments_do_not_accumulate_into_a_seam) {
    icf::svg::Shape shape;
    shape.path.segments = {move(16, 32), line(32, 32), line(48, 32)};
    shape.stroke.kind = icf::svg::PaintKind::Color;
    shape.strokeWidth = 6.0;

    const auto cov = rb::rasteriseStroke(shape, {}, 64, 64, 16, {});
    auto at = [&](int x, int y) { return cov[static_cast<std::size_t>(y) * 64 + x]; };
    // The shared point and a point well inside one segment must read the same.
    CHECK(std::fabs(at(32, 32) - at(24, 32)) < 0.01f);
    CHECK(at(32, 32) <= 1.0f);
}
