#pragma once
// The stroke, both halves: the point stream the CPU emits, and the coverage the
// fragment computes from it.
//
// WHY BOTH HALVES ARE IN ONE FILE
// -------------------------------
// They are one contract. `doc 03 §10` transcribed the vertex stage and stopped
// at a branch on bits nobody had decoded; `§31` read the producer. Splitting
// them across two units would let one be changed without the other, and the
// failure mode of that is a plausible picture -- the geometry runs, fed a
// buffer that means something else.
//
// The sentinels are the seam. `join` is `RB::LineJoin` for a real vertex and
// NEGATIVE for a marker, and the GPU's discard rule reads two neighbours of it.
//
// WHAT IS TRANSCRIBED AND WHAT THE CORPUS ACTUALLY EXERCISES
// -----------------------------------------------------------
// All seven caps and all three joins are here, because the shader reads the
// field whole (`switch` over 7 cases) and half a transcription of an enum is
// worse than none.
//
// `[ART]` But **the corpus exercises exactly one combination**: none of the 35
// SVGs with a painted stroke names `stroke-linecap` or `stroke-linejoin`, so
// all 35 fall on the SVG defaults -- `butt` and `miter`. The other six caps and
// two joins are gated against closed form and run in no real document, which is
// the same standing as the glass refraction (spec 2026-09-02 §9.1) and is said
// here rather than left for a reader to assume.
//
// THE MITER LIMIT IS RESOLVED HERE, NOT ON THE GPU
// -------------------------------------------------
// `[BIN]` `apply_miter_limit` (`RenderBox 0x11DE6C`) runs per point on the CPU
// and writes an already-degenerated `join`. A transcription that carried the
// limit through to the shader would be implementing a different target. See
// `joinForCorner`.
#include <cstdint>
#include <vector>

namespace rb {

// `[BIN]` `RB::LineCap`, names from the table at `RenderBox 0x18F920`.
enum class LineCap : std::uint8_t {
    Round = 0,
    Square = 1,
    Butt = 2,
    OutwardsTriangle = 3,
    InwardsTriangle = 4,
    ForwardsTriangle = 5,
    BackwardsTriangle = 6,
};

// `[BIN]` `RB::LineJoin`, names from `RenderBox 0x18F958`. Same numbering as
// `CGLineJoin` -- `rb_line_join` (`0x8B6B0`) is the identity on 0/1/2.
enum class LineJoin : std::uint8_t { Miter = 0, Round = 1, Bevel = 2 };

struct StrokePoint {
    double x = 0.0;
    double y = 0.0;
};

// `[BIN]` `RB::Shader::StrokeLinePoint`. `join` is a `short` at offset 14 and
// carries `RB::LineJoin` for a real vertex, or one of the three markers.
struct StrokeLinePoint {
    double x = 0.0;
    double y = 0.0;
    double radius = 0.0;
    double alpha = 1.0;
    int join = 0;
};

// The markers, named. `[BIN]` §31.2.
inline constexpr int kJoinGhostEnd = -3;   // the mirrored point outside an open end
inline constexpr int kJoinWrap = -1;       // the wrap-around duplicate of a closed subpath
inline constexpr int kJoinObliqueButt = -2;

struct StrokeParams {
    double width = 1.0;
    double miterLimit = 4.0;
    LineCap cap = LineCap::Butt;
    LineJoin join = LineJoin::Miter;
    // `[BIN]` `recip_scale`: the size of one device pixel in the space the
    // points live in. The fragment's antialiasing is exactly one of these wide.
    double recipScale = 1.0;
    // `[BIN]` `RenderState` word 0, bit 11: `0` antialiases with a smoothstep,
    // `1` gives hard binary coverage. `[OBS]` The bit has no name in any
    // `air.static_init` that was swept.
    bool hardCoverage = false;
};

// `[BIN]` `apply_miter_limit`, `RenderBox 0x11DE6C`, disassembled whole:
//
//     c = dot(normalize(dIn), normalize(dOut))
//     if (c > 0.99f)              return Round;
//     if ((1 + c) * ml*ml < 2.0f) return Bevel;
//                                 return Miter;
//
// `[BIN]` The `0.99f` is the constant at `0x161E40`, and the third argument is
// the miter limit SQUARED -- `flatten_points` stores `ml*ml` in `Flattener+0x24`
// (`fmul s9, s3, s3` at `0x11CE9C`). So `(1+c)*ml² < 2` is `ml² < 2/(1+c)`,
// which is the SVG and CoreGraphics rule exactly.
LineJoin applyMiterLimit(StrokePoint dirIn, StrokePoint dirOut, double miterLimit);

// The per-point decision, `[BIN]` from `Flattener::lineto` (`0x11DD70`) and
// `flush_lineto` (`0x11DE2C`). A global `bevel` or `round` short-circuits; only
// `miter` consults the limit. And a radius at or below the flatness floor is
// forced to `round`, which emits no join primitive at all.
int joinForCorner(StrokePoint dirIn, StrokePoint dirOut, double radius,
                  const StrokeParams& params);

// `[BIN]` `RB::bezier_flatness()` (`0x110CA0`) returns 0.25; `Flattener[0x2c]`
// holds it divided by the scale.
inline constexpr double kBezierFlatness = 0.25;

// One flattened subpath into the point stream the GPU expects, ghosts and all.
// `[BIN]` §31.3: an OPEN subpath of `k` real points becomes `k + 2`; a CLOSED
// one becomes `k + 3`. Consecutive points at the same position are dropped, as
// `add_point` (`0xAA54C`) drops them -- the buffer is not a 1:1 transcription of
// the polyline.
std::vector<StrokeLinePoint> strokePointStream(const std::vector<StrokePoint>& pts,
                                               bool closed, const StrokeParams& params);

// `[BIN]` `draw_buffer` (`0xAA3E0`): the lines pass runs `N - 3` instances over
// a window of `P[iid..iid+3]`, drawing the segment `P[iid+1] -> P[iid+2]`.
// Returns 0 rather than a negative for a stream too short to draw.
std::size_t strokeLineInstanceCount(std::size_t pointCount);

// `[BIN]` `stroke_lines_vertex`: the instance is discarded when
// `min(join[iid+1], join[iid+2]) < 0`. **The indices are +1 and +2**, not 0 and
// 2 -- doc 03 §10.2 carried the wrong one until 2026-09-04, and with it every
// subpath would lose its first segment.
bool strokeInstanceIsDrawn(const std::vector<StrokeLinePoint>& stream, std::size_t iid);

// The fragment, transcribed: coverage at a point, for the segment
// `stream[iid+1] -> stream[iid+2]`.
//
// `[BIN]` `stroke_lines_fragment` (`default_mod71.ll`), read whole. The cap
// distance functions are the seven arms of its `switch`; the resolve is a
// smoothstep one `recipScale` wide across the boundary, or a hard compare when
// bit 11 is set.
double strokeCoverageAt(const std::vector<StrokeLinePoint>& stream, std::size_t iid,
                        double px, double py, const StrokeParams& params);

// The seven arms on their own, so a test can walk the enum instead of only the
// one value the corpus uses. `ov` is the overshoot past the nearest end, `d` the
// perpendicular distance, `r` the effective half-width, `s` the pixel, and
// `alongPositive` says which end we are past.
double strokeCapDistance(LineCap cap, double ov, double d, double r, double s,
                         bool alongPositive);

}  // namespace rb
