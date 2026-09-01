#include "check.h"
#include "Source/CoreSVG/Path.h"
#include "Source/RenderBox/PathBuffer.h"

#include <cmath>

using namespace rb;
using icf::svg::parsePath;

namespace {

Result<PathBuffer> build(const char* d, int subdivisions = 4) {
    auto p = parsePath(d);
    if (!p) return std::unexpected(std::string("the path did not parse"));
    BuildOptions o;
    o.subdivisions = subdivisions;
    return buildPathBuffer(*p, o);
}

}  // namespace

// Convention 1: entry 0 is a header, not a segment.
TEST_CASE(the_first_entry_is_a_header_and_counts_the_segments) {
    auto b = build("M0 0 L10 0 L10 10");
    REQUIRE(b.has_value());
    // one break (the move) plus two lines
    CHECK_EQ(b->segmentCount(), static_cast<std::size_t>(3));
    CHECK_EQ(b->entries.front().count, 3);
}

// Convention 1, the part that is a bit trick: the total vertex count is carried
// as an INT through the float slot, because the shader loads it as an i32.
TEST_CASE(the_header_carries_the_vertex_total_through_the_float_slot) {
    auto b = build("M0 0 L10 0 L10 10", 4);
    REQUIRE(b.has_value());
    CHECK_EQ(b->vertexCount(), 8);  // two drawn segments, four vertices each
    // and it is NOT the float 8.0 -- reading it as a float must give nonsense
    CHECK(b->entries.front().recip_n != 8.0f);
}

// Convention 2: each segment's `count` is a prefix sum, which is the whole reason
// the shader can binary search it.
TEST_CASE(each_segment_count_is_a_running_total_and_never_decreases) {
    auto b = build("M0 0 L1 0 L2 0 L3 0", 4);
    REQUIRE(b.has_value());
    std::int32_t previous = -1;
    for (std::size_t i = 1; i < b->entries.size(); ++i) {
        CHECK(b->entries[i].count >= previous);
        previous = b->entries[i].count;
    }
    CHECK_EQ(b->entries.back().count, b->vertexCount());
}

// Convention 3: p0 is the previous entry's p3, so the header's p3 has to be the
// point the path starts at -- otherwise segment one begins nowhere.
TEST_CASE(the_header_p3_is_where_the_path_starts) {
    auto b = build("M7 9 L10 9");
    REQUIRE(b.has_value());
    CHECK_EQ(b->entries.front().p3[0], 7.0f);
    CHECK_EQ(b->entries.front().p3[1], 9.0f);
}

TEST_CASE(a_segment_p3_is_the_next_segments_start) {
    auto b = build("M0 0 L4 0 L4 8");
    REQUIRE(b.has_value());
    // entries: [header, break@(0,0), line->(4,0), line->(4,8)]
    REQUIRE(b->entries.size() == 4);
    CHECK_EQ(b->entries[2].p3[0], 4.0f);
    CHECK_EQ(b->entries[2].p3[1], 0.0f);
    CHECK_EQ(b->entries[3].p3[0], 4.0f);
    CHECK_EQ(b->entries[3].p3[1], 8.0f);
}

// Convention 4: a move is a non-finite p1.x, and the predicate that writes it is
// the same one that reads it.
TEST_CASE(a_move_is_written_as_a_subpath_break) {
    auto b = build("M0 0 L1 1 M5 5 L6 6");
    REQUIRE(b.has_value());
    int breaks = 0;
    for (std::size_t i = 1; i < b->entries.size(); ++i) {
        if (isSubpathBreak(b->entries[i])) ++breaks;
    }
    CHECK_EQ(breaks, 2);
    CHECK(isSubpathBreak(b->entries[1]));
    CHECK(!isSubpathBreak(b->entries[2]));
}

TEST_CASE(the_header_itself_must_not_read_as_a_break) {
    auto b = build("M0 0 L1 1");
    REQUIRE(b.has_value());
    CHECK(!isSubpathBreak(b->entries.front()));
}

// A break draws nothing, so it must not advance the prefix sum -- if it did, the
// binary search would hand some vertex to an entry that has no curve.
TEST_CASE(a_break_contributes_no_vertices) {
    auto b = build("M0 0 L1 1 M5 5 L6 6", 4);
    REQUIRE(b.has_value());
    CHECK_EQ(b->vertexCount(), 8);  // two lines, not two lines plus two moves
    CHECK_EQ(b->entries[3].count, b->entries[2].count);  // the second move
}

// A line is a cubic whose controls sit on the chord at a third and two thirds.
// Exact, not an approximation -- and it keeps one entry shape in the buffer.
TEST_CASE(a_line_becomes_a_cubic_with_collinear_controls) {
    auto b = build("M0 0 L30 0");
    REQUIRE(b.has_value());
    const CubicSegment& s = b->entries[2];
    CHECK_EQ(s.p1[0], 10.0f);
    CHECK_EQ(s.p2[0], 20.0f);
    CHECK_EQ(s.p3[0], 30.0f);
    CHECK_EQ(s.p1[1], 0.0f);
}

TEST_CASE(a_cubic_keeps_its_own_control_points) {
    auto b = build("M0 0 C1 2 3 4 5 6");
    REQUIRE(b.has_value());
    const CubicSegment& s = b->entries[2];
    CHECK_EQ(s.p1[0], 1.0f);
    CHECK_EQ(s.p1[1], 2.0f);
    CHECK_EQ(s.p2[0], 3.0f);
    CHECK_EQ(s.p2[1], 4.0f);
    CHECK_EQ(s.p3[0], 5.0f);
    CHECK_EQ(s.p3[1], 6.0f);
}

TEST_CASE(close_returns_to_the_subpath_start) {
    auto b = build("M0 0 L10 0 L10 10 Z");
    REQUIRE(b.has_value());
    CHECK_EQ(b->entries.back().p3[0], 0.0f);
    CHECK_EQ(b->entries.back().p3[1], 0.0f);
}

// A close on the point the subpath started from is a zero-length curve. Emitting
// it would spend subdivision vertices drawing nothing, and every one of them
// would land on the same pixel.
TEST_CASE(a_close_that_would_not_move_emits_nothing) {
    auto closed = build("M0 0 L10 0 L0 0 Z");
    auto open = build("M0 0 L10 0 L0 0");
    REQUIRE(closed.has_value());
    REQUIRE(open.has_value());
    CHECK_EQ(closed->segmentCount(), open->segmentCount());
}

TEST_CASE(the_layout_matches_the_targets_offsets) {
    // Measured from air.struct_type_info, doc 03 section 7. If a compiler ever
    // pads this differently the buffer stops being the target's buffer, and the
    // failure would otherwise show up as a wrong picture rather than a wrong size.
    CHECK_EQ(sizeof(CubicSegment), static_cast<std::size_t>(32));
    CHECK_EQ(offsetof(CubicSegment, count), static_cast<std::size_t>(0));
    CHECK_EQ(offsetof(CubicSegment, recip_n), static_cast<std::size_t>(4));
    CHECK_EQ(offsetof(CubicSegment, p1), static_cast<std::size_t>(8));
    CHECK_EQ(offsetof(CubicSegment, p2), static_cast<std::size_t>(16));
    CHECK_EQ(offsetof(CubicSegment, p3), static_cast<std::size_t>(24));
}

TEST_CASE(an_empty_path_is_refused_by_name) {
    icf::svg::Path empty;
    auto b = buildPathBuffer(empty);
    REQUIRE(!b.has_value());
    CHECK(b.error().find("empty") != std::string::npos);
}

TEST_CASE(a_path_that_only_moves_is_refused) {
    auto b = build("M0 0 M5 5");
    REQUIRE(!b.has_value());
    CHECK(b.error().find("drawable") != std::string::npos);
}

TEST_CASE(zero_subdivisions_is_refused_rather_than_silently_raised) {
    auto p = parsePath("M0 0 L1 1");
    REQUIRE(p.has_value());
    BuildOptions o;
    o.subdivisions = 0;
    auto b = buildPathBuffer(*p, o);
    REQUIRE(!b.has_value());
    CHECK(b.error().find("subdivisions") != std::string::npos);
}
