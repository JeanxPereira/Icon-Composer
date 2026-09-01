#include "check.h"
#include "Source/CoreSVG/Path.h"

using namespace icf::svg;

namespace {

std::string describe(const Path& p) {
    std::string out;
    for (const auto& s : p.segments) {
        switch (s.kind) {
            case SegmentKind::Move: out += "M"; break;
            case SegmentKind::Line: out += "L"; break;
            case SegmentKind::Cubic: out += "C"; break;
            case SegmentKind::Close: out += "Z"; break;
        }
        const int n = (s.kind == SegmentKind::Cubic) ? 3 : (s.kind == SegmentKind::Close ? 0 : 1);
        for (int i = 0; i < n; ++i) {
            out += " " + std::to_string(static_cast<long long>(s.p[i].x * 1000 + (s.p[i].x < 0 ? -0.5 : 0.5))) +
                   "," + std::to_string(static_cast<long long>(s.p[i].y * 1000 + (s.p[i].y < 0 ? -0.5 : 0.5)));
        }
        out += ";";
    }
    return out;
}

}  // namespace

// Everything is normalised to absolute Move/Line/Cubic/Close. A consumer of a
// path should not have to know that `h` existed, and the rasteriser certainly
// should not carry sixteen cases.
TEST_CASE(path_reads_absolute_move_and_line) {
    auto p = parsePath("M 10 20 L 30 40");
    REQUIRE(p.has_value());
    CHECK_EQ(describe(*p), std::string("M 10000,20000;L 30000,40000;"));
}

TEST_CASE(path_repeats_the_last_command_for_bare_coordinate_pairs) {
    auto p = parsePath("M0 0 10 10 20 20");
    REQUIRE(p.has_value());
    // A repeated moveto is a LINE, per SVG. Getting this wrong turns an outline
    // into a scatter of disconnected dots.
    CHECK_EQ(describe(*p), std::string("M 0,0;L 10000,10000;L 20000,20000;"));
}

TEST_CASE(path_resolves_relative_commands_against_the_current_point) {
    auto p = parsePath("M10 10 l5 0 l0 5");
    REQUIRE(p.has_value());
    CHECK_EQ(describe(*p), std::string("M 10000,10000;L 15000,10000;L 15000,15000;"));
}

// 492 H and 357 V in the corpus. Each keeps the other coordinate of the current
// point, which is the whole reason they exist.
TEST_CASE(path_expands_horizontal_and_vertical_lines) {
    auto p = parsePath("M10 20 H30 V40 h-5 v-5");
    REQUIRE(p.has_value());
    CHECK_EQ(describe(*p),
             std::string("M 10000,20000;L 30000,20000;L 30000,40000;L 25000,40000;L 25000,35000;"));
}

TEST_CASE(path_reads_a_cubic) {
    auto p = parsePath("M0 0 C1 2 3 4 5 6");
    REQUIRE(p.has_value());
    CHECK_EQ(describe(*p), std::string("M 0,0;C 1000,2000 3000,4000 5000,6000;"));
}

// `S` reflects the previous cubic's second control point about the current
// point. After anything that is not a cubic, the reflection is the current
// point itself -- a reader that always reflects draws a different curve.
TEST_CASE(path_reflects_the_control_point_of_a_smooth_cubic) {
    auto p = parsePath("M0 0 C1 1 2 2 3 3 S4 4 5 5");
    REQUIRE(p.has_value());
    CHECK_EQ(describe(*p),
             std::string("M 0,0;C 1000,1000 2000,2000 3000,3000;C 4000,4000 4000,4000 5000,5000;"));
}

TEST_CASE(path_treats_a_smooth_cubic_after_a_line_as_starting_at_the_current_point) {
    auto p = parsePath("M0 0 L1 1 S2 2 3 3");
    REQUIRE(p.has_value());
    CHECK_EQ(describe(*p),
             std::string("M 0,0;L 1000,1000;C 1000,1000 2000,2000 3000,3000;"));
}

// After a close, the current point is the subpath's start -- not where the pen
// happened to be.
TEST_CASE(path_returns_to_the_subpath_start_after_a_close) {
    auto p = parsePath("M10 10 L20 20 Z l1 1");
    REQUIRE(p.has_value());
    CHECK_EQ(describe(*p), std::string("M 10000,10000;L 20000,20000;Z;L 11000,11000;"));
}

// Measured, not guessed: a first count of the corpus saw `e` 46 times and nearly
// filed it as a command. It is the exponent of a number.
TEST_CASE(path_reads_numbers_in_scientific_notation) {
    auto p = parsePath("M1e2 2E1 L-1.5e-1 .5");
    REQUIRE(p.has_value());
    CHECK_EQ(describe(*p), std::string("M 100000,20000;L -150,500;"));
}

TEST_CASE(path_accepts_commas_and_signs_as_separators) {
    auto p = parsePath("M0,0L1-1L-2,2");
    REQUIRE(p.has_value());
    CHECK_EQ(describe(*p), std::string("M 0,0;L 1000,-1000;L -2000,2000;"));
}

// Arcs occur TWICE in 149 files (doc 04 §2). Rather than approximate them, the
// reader refuses and says which command it refused -- the coverage report is
// what decides whether they are worth implementing.
TEST_CASE(path_refuses_a_command_it_does_not_implement_and_names_it) {
    auto p = parsePath("M0 0 A1 1 0 0 1 2 2");
    CHECK(!p.has_value());
    CHECK_EQ(p.error().unsupportedCommand, 'A');
}

TEST_CASE(path_refuses_malformed_data) {
    CHECK(!parsePath("L 10 10").has_value());       // must start with a moveto
    CHECK(!parsePath("M 10").has_value());          // a pair, not a number
    CHECK(!parsePath("M 10 10 L 20 nonsense").has_value());
}
