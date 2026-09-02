#pragma once
// TWO DIFFERENT THINGS LIVE HERE, AND THE LINE BETWEEN THEM IS THE POINT.
//
// PART ONE -- `distanceGradient_v1`, a TRANSCRIPTION.
//   `[BIN]` `References/2.0-125/out/metallib-renderbox/default_mod95.ll`, the
//   stitchable function `distanceGradient_v1`. Every statement about it below
//   was read from that IR, line by line, and the line numbers of the values it
//   came from are quoted where the arithmetic is not obvious. It is Apple's
//   code, so it is sealed.
//
// PART TWO -- the field GENERATOR, which is THIS PROJECT'S OWN ALGORITHM.
//   Apple's producer builds its field from CASDF elements (spec §7.3); we have
//   an SVG path that has been flattened into contours. Nothing about how we
//   turn one into the other was read from any binary, and none of it carries a
//   seal. It is a design decision, stated below with what it costs and what it
//   gets wrong.
//
// A `[BIN]` seal on part two would be a lie about provenance, which is the one
// failure this repository cannot absorb: every other reader downstream trusts
// the seal to mean "somebody read this out of a binary".
#include <cstdint>
#include <vector>

namespace rb {

// ===========================================================================
// PART ONE -- `distanceGradient_v1`, transcribed from mod95
// ===========================================================================

// `[BIN]` The five `float2` of an `RB::Layer`, in the order the IR's own loop
// stores them (`%11`, five iterations of a two-float copy out of `params + 2`).
// Spec §5.1 names the same five: an affine 2x3 UV transform and a clamp rect.
struct FieldLayer {
    float m0[2]{1, 0};   // the column p.x multiplies
    float m1[2]{0, 1};   // the column p.y multiplies
    float m2[2]{0, 0};   // the translation
    float m3[2]{0, 0};   // clamp low
    float m4[2]{1, 1};   // clamp high
};

// `[BIN]` mod95's 48-byte `params` block, whole. Twelve floats: `params[0]` is
// broadcast as the gradient's SCALE (`%118`), `params[1]` is broadcast as the
// BIAS added to it (`%120`) and is ALSO what the zero-distance branch writes
// into `.yz` (`%52`), and `params[2..11]` are the layer above.
//
// That one float serves as both bias and fallback is a reading, not a guess:
// `%31` is loaded once and feeds `%52` in the early-out block and `%120` in the
// normal one.
struct DistanceGradientParams {
    float scale = 1.0f;
    float fallback = 0.0f;
    FieldLayer layer{};
};

// The half narrowing the target does implicitly, because its texture is
// `half4` and its return type is `half4`. It matters: the central difference
// `%102`/`%105` is an `fsub half`, so the subtraction ROUNDS TO HALF BEFORE it
// is widened to float and normalized. A transcription that differenced in
// float would drift from the target on exactly the small differences a
// distance field is made of. `packHalf2x16` performs the same narrowing on the
// GLSL side, which is what makes the differential meaningful.
float fieldNarrowToHalf(float v);

// The texture mod95 samples at `t0`, and the ONE thing about the sample this
// transcription does not claim to know.
//
// `[OBS]` The sampler is `@__air_sampler_state = i64 -9188470239253755319`,
// i.e. `0x807BFF0000080A49`. That i64 is a packed Metal sampler descriptor and
// its bit layout is not published; the filter and address modes were NOT
// decoded, so the FILTER IS UNKNOWN. Rather than invent a bilinear and gate
// against our own invention, both sides of the differential fetch the NEAREST
// texel with clamp-to-edge and narrow it to half. That keeps the differential
// pointed at what WAS read -- the tap positions, the half-precision central
// difference, the guarded normalize, the zero-distance branch -- instead of at
// a filter nobody measured.
struct FieldTexture {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> rgba;   // four floats per texel, row major

    // Nearest with clamp-to-edge, narrowed to half. `field_probe.comp` defines
    // `rbFieldSample` by the same rule and must stay in step with it.
    float sampleX(const float uv[2]) const;
};

// `[BIN]` `uv(p) = clamp(fma(p.xx, m0, fma(p.yy, m1, m2)), m3, m4)`.
//
// The nesting is the IR's (`%44` is the inner fma over `p.yy`, `%45` the outer
// over `p.xx`), and it is `air.fma`, the TRUE fused multiply-add, not the
// contractible `llvm.fmuladd` that appears later at `%121`. Reassociating it
// would change the last bit, and the differential is bit-for-bit here.
void fieldUv(const FieldLayer& layer, float px, float py, float out[2]);

// `[BIN]` The whole of `distanceGradient_v1`, in the order the IR runs it.
//
// `dpdx` and `dpdy` are the target's `dfdx(p.x)` and `dfdy(p.y)`. They are
// PASSED IN rather than computed, for a reason that is not a shortcut: a
// derivative exists only in a fragment shader's quad, and both the CPU oracle
// and the compute probe that gates it live outside one. The transcription
// still owns the `abs` around them (`air.fast_fabs`, `%60`/`%63`), because
// dropping that sign would move two of the four taps to the wrong side.
//
// Returns `half4`: `.x` the centre tap's distance passed straight through,
// `.yz` the scaled gradient, `.w` coverage -- 1 normally, 0 on the
// zero-distance branch. That is what `glassBackground_v1` reads back (§5.2).
void distanceGradient(float px, float py, float dpdx, float dpdy,
                      const DistanceGradientParams& params, const FieldTexture& texture,
                      float out[4]);

// ===========================================================================
// PART TWO -- the field generator. OUR ALGORITHM, NOT A TRANSCRIPTION.
// ===========================================================================
//
// THIS IS NOT READ FROM ANYTHING. It carries no `[BIN]`, no `[ART]`, and it is
// not an inference from a binary either. It is a method this project chose,
// and the choice is argued rather than asserted.
//
// WHAT IS BORROWED, AND FROM WHERE
// --------------------------------
// Only the OUTPUT CONVENTION. `[INF]` from AquaKit's
// `Source/QuartzCore/shaders/Field.frag`, which transcribed
// `QuartzCore.CASDFGenerator`: the field is RGBA16F holding
// `(d, gx, gy, coverage)` with **d NEGATIVE INSIDE**. The sign is not a
// stylistic choice on their side -- `CASDFOutputEffect.minimum = -10000` is a
// floor on a quantity that goes negative, and a field positive inside would
// never approach it. `glassBackground_v1` agrees with that reading from the
// other end: it computes coverage as `saturate(-d/max(fwidth(d),1e-3) + 0.5)`
// (§5.3 block 1), and the minus sign there only makes coverage rise inside the
// shape if `d` is negative inside.
//
// THE METHOD, AND WHAT IT COSTS
// -----------------------------
// BRUTE FORCE: for every sample point, the exact Euclidean distance to every
// segment of every contour, minimised, then signed by a winding test.
//
//   cost:    O(W * H * E). A 512x512 field over a path flattened to 2000
//            segments is 5.2e8 point-segment tests. That is seconds, not
//            milliseconds, and it is the whole of the argument against it.
//   error:   ZERO against the geometry it is handed. The only error is the
//            caller's flattening of curves into segments, which the caller
//            controls and which the gate MEASURES (see `test_rb_field.cpp`:
//            the rectangle, whose contour is exact, comes back exact to float
//            rounding; the circle's error is the polygon's sagitta and nothing
//            more).
//
// WHY NOT SOMETHING FASTER. Jump flooding and 8SSEDT are both far cheaper and
// both approximate: they propagate a nearest-SEED over a grid, so their answer
// is quantised to the grid and JFA additionally admits propagation errors that
// are hard to bound analytically. THIS IS AN ORACLE. A field that is wrong by
// a third of a pixel poisons every glass test that consumes it, and the tests
// downstream would then be measuring our approximation instead of Apple's
// shader. Correctness first; if the cost ever bites, the fast method can be
// added BESIDE this one and gated AGAINST it, which is only possible because
// this one is exact.
//
// WHAT IT STILL GETS WRONG, named rather than hidden:
//   - The gradient is discontinuous on the shape's MEDIAL AXIS. That is a
//     property of the true distance field, not a defect of this code, and it
//     is asserted rather than smoothed over.
//   - Self-intersecting and overlapping contours are resolved by the FILL RULE
//     for the sign, but the DISTANCE is to the nearest edge whether or not
//     that edge is interior to the union. For a shape whose contours cross,
//     the interior edge shows up as a crease in the field. Apple's smooth
//     union (AquaKit's `SmoothUnion`) does not have that crease. We do not
//     model a union at all, because an SVG path is one shape and not a set.
//   - Coverage is analytic, not sampled: `clamp(0.5 - d, 0, 1)` over a field
//     whose slope is 1 per pixel. That is `Field.frag`'s
//     `clamp(-d/max(fwidth(d),1e-4) + 0.5, 0, 1)` with `fwidth(d)` at its true
//     value of 1, so the two agree wherever the field is smooth and differ
//     only across the medial axis, where `fwidth` spikes and theirs narrows the
//     band. Ours does not.

// A closed contour, as x,y pairs in the FIELD'S OWN PIXEL SPACE -- the same
// space `CoveragePass` rasterises into, so a caller that already flattened a
// path for the coverage pass hands the same points here. The closing segment
// from the last point back to the first is implied and must not be repeated.
struct FieldContour {
    std::vector<float> xy;
};

// The two rules `CoreSVG` parses and `PathResolve` implements. The generator
// needs one because the SIGN of the distance is an inside/outside question and
// nothing else about it is.
enum class FieldRule : std::uint32_t { NonZero = 0, EvenOdd = 1 };

struct FieldOptions {
    FieldRule rule = FieldRule::NonZero;
    // The width, in pixels, of the coverage ramp across the boundary. 1 is the
    // one that matches `Field.frag` on a pixel grid; it is a parameter only so
    // a test can prove the band is where it says it is.
    float aaWidth = 1.0f;
};

// What the field holds at one point.
struct FieldSample {
    float distance = 0.0f;   // NEGATIVE INSIDE
    float gx = 0.0f;         // unit, pointing the way `distance` INCREASES,
    float gy = 0.0f;         // i.e. away from the shape
    float coverage = 0.0f;
};

// The shape with its segments already gathered, so a sweep over a million
// pixels does not rebuild the list a million times.
class FieldShape {
public:
    explicit FieldShape(const std::vector<FieldContour>& contours);

    // Signed distance alone. Negative inside.
    float distanceAt(float x, float y, FieldRule rule) const;

    // Distance, gradient and coverage together.
    //
    // The gradient is EXACT rather than differenced: brute force already knows
    // the closest point on the boundary, and `grad = sign * unit(p - closest)`
    // is the analytic derivative of the distance. It is a unit vector
    // everywhere the field is differentiable, which a central difference is not
    // -- a difference straddling the medial axis returns a vector shorter than
    // 1, and near a corner it rounds the corner off. The one place the exact
    // form has nothing to say is a point EXACTLY on the boundary, where
    // `p - closest` is the zero vector; there, and only there, this falls back
    // to a central difference with e = 0.5, which is the same e `Field.frag`
    // uses.
    FieldSample sampleAt(float x, float y, const FieldOptions& options) const;

    std::size_t segmentCount() const { return segments_.size() / 4; }

private:
    std::vector<double> segments_;   // x0, y0, x1, y1 per segment
};

// The field as a picture: four floats per pixel in AquaKit's `(d, gx, gy,
// coverage)` layout, sampled at PIXEL CENTRES (x + 0.5, y + 0.5), row major.
//
// Pixel centres and not corners, because that is where `gl_FragCoord.xy` lands
// in `Field.frag` and where `CoveragePass` measures coverage. Sampling at
// corners would put the shape half a pixel off from everything else this tower
// draws.
struct FieldImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> rgba;

    const float* at(std::uint32_t x, std::uint32_t y) const {
        return rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4;
    }
};

FieldImage generateField(const FieldShape& shape, std::uint32_t width, std::uint32_t height,
                         FieldOptions options = FieldOptions{});

FieldImage generateField(const std::vector<FieldContour>& contours, std::uint32_t width,
                         std::uint32_t height, FieldOptions options = FieldOptions{});

}  // namespace rb
