#pragma once
// The buffer the target's path vertex shader reads, in the target's own layout.
//
// PROVENANCE
// ----------
// `[BIN]` Every field, offset and size below was read from `air.struct_type_info`
// in `RenderBox.framework/…/default.metallib`, module 58 (`shader_path.metal`),
// and the CONVENTIONS were read from the body of `path_edges_vertex` itself.
// Nothing here is a guess about how a path buffer "should" look. See doc 03 §7.
//
//     RB::Shader::Path::CubicSegment   32 bytes, align 4
//        0  int            count
//        4  float          recip_n
//        8  packed_float2  p1
//       16  packed_float2  p2
//       24  packed_float2  p3
//
// THE FOUR CONVENTIONS, and each was measured rather than assumed
// ---------------------------------------------------------------
//  1. ENTRY 0 IS A HEADER, not a segment. The shader reads `segments[0].count`
//     as the number of segments to search and `segments[0].recip_n` -- loaded as
//     an `i32`, through the float slot -- as the total vertex count.
//
//  2. EACH SEGMENT'S `count` IS A PREFIX SUM, not its own vertex count. That is
//     what lets the shader BINARY SEARCH for the segment a vertex belongs to:
//     the IR halves an interval and compares `count` against the vertex index.
//
//  3. `p0` IS THE PREVIOUS ENTRY'S `p3`. The IR indexes `segments[i - 1].p3`
//     (`getelementptr … i64 -1, i32 4`), so the points are chained and the
//     header's own `p3` is where the path starts.
//
//  4. A SUBPATH BREAK IS A NON-FINITE `p1.x`. The shader bitcasts `p1.x` to an
//     integer, masks 0x7F800000, and branches when the exponent is all ones --
//     an Inf or a NaN. That is how several subpaths live in one flat buffer.
#include <cstdint>
#include <vector>

#include "Source/CoreSVG/Path.h"
#include "Source/RenderBox/Device.h"

namespace rb {

struct CubicSegment {
    std::int32_t count = 0;
    float recip_n = 0.0f;
    float p1[2]{};
    float p2[2]{};
    float p3[2]{};
};
static_assert(sizeof(CubicSegment) == 32, "the target's layout is 32 bytes");
static_assert(alignof(CubicSegment) == 4, "the target's alignment is 4");

// Whether an entry is the start of a subpath rather than a curve to draw --
// convention 4. Written and read through the same predicate so the two can never
// drift apart.
bool isSubpathBreak(const CubicSegment& s);
void markSubpathBreak(CubicSegment& s);

// The total vertex count the header carries, undoing the int-through-float slot
// of convention 1.
std::int32_t headerVertexCount(const CubicSegment& header);

struct PathBuffer {
    // entries[0] is the header; entries[1..] are the segments.
    std::vector<CubicSegment> entries;

    std::size_t segmentCount() const { return entries.empty() ? 0 : entries.size() - 1; }
    std::int32_t vertexCount() const {
        return entries.empty() ? 0 : headerVertexCount(entries.front());
    }
};

struct BuildOptions {
    // How many vertices one curved segment contributes.
    //
    // `[INF]` -- and marked so deliberately. The target carries this number in the
    // buffer, which means its RULE lives on the CPU side and was not in the
    // shader to read. A fixed subdivision is a placeholder that produces the
    // right SHAPE of buffer; the rule that picks it per curve is an open question
    // recorded in doc 03 §7, not something to invent here.
    int subdivisions = 16;
};

// The buffer for one normalised path, or the reason there is none.
Result<PathBuffer> buildPathBuffer(const icf::svg::Path& path, BuildOptions options = BuildOptions{});

}  // namespace rb
