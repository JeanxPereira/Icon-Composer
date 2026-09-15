#pragma once
// The gradient on the CPU, mirroring `shader_gradient.metal`.
//
// `[BIN]` `RB::Shader::Gradient`. Doc 03 §23 carries the measurement, and the
// shape of it is the thing worth repeating here: `Gradient::value` and
// `Gradient::fold_value` BOTH switch on the same four-bit field, and grouping
// their cases the two different ways decomposes it without a leftover:
//
//        case | geometry | spread
//     --------+----------+--------
//      0 1 2 7|  linear  | pad repeat reflect none
//      3 4 5 8|  radial  | pad repeat reflect none
//     9 10 11 12| focal   | pad repeat reflect none
//     13 14 15 | vertex   | pad repeat reflect
//            6 |  conic   | none
//
// Four geometries by four spreads, in four bits. That the two independent
// functions agree on it is what makes it a measurement rather than a reading.
#include <cstdint>
#include <vector>

namespace rb {

// `[BIN]` The fields of RenderState word 0 this family reads.
enum GradientBit : std::uint32_t {
    kGradientKindShift = 19,   // bits 19-22: geometry x spread
    kGradientKindMask = 15u,
    kRampKindShift = 23,       // bits 23-24
    kRampKindMask = 3u,
    kStopsAltPath = 1u << 25,  // an alternate sampling path; not decoded
    kStopGamma = 1u << 26,     // two-colour: powr(t, arg). stops: stride 5, not 4
};

enum class Geometry { Linear, Radial, Focal, Conic, FromVertex };
enum class Spread { Pad, Repeat, Reflect, None };

// The 4-bit case split into its two halves. Exposed because the split IS the
// finding, and a test should be able to check the whole table.
Geometry geometryOf(std::uint32_t state);
Spread spreadOf(std::uint32_t state);

// `[BIN]` The spread. pad is `saturate`, repeat is `fract`, and reflect is
// `fract(t * 0.5) * 2` folded at 1 -- the triangle wave.
float foldValue(std::uint32_t state, float t);

// `[BIN]` The geometry: a point already in the gradient's own space becomes `t`.
//
// `a` and `b` are the two floats the target passes alongside (`arg7`/`arg8` in
// the IR). Linear ignores both; radial is `sqrt(dot(p,p)) * a + b`.
//
// `covered` is set false where the focal geometry rejects the point -- the IR
// returns 0 there through a separate path, and losing that distinction would
// paint the cone's outside with the ramp's first colour.
float gradientValue(std::uint32_t state, float px, float py, float a, float b,
                    bool* covered = nullptr);

// A stop, in the target's own layout: four halves of colour, and a fifth that is
// a gamma when word0 bit 26 is set. `[BIN]` The stride comes from
// `sample_stops_uniform`, which multiplies the index by 4 or by 5.
struct Stop {
    float rgba[4]{0, 0, 0, 0};
    float gamma = 1.0f;
};

// `[BIN]` Ramp kind 0: two colours mixed, with `powr(t, gamma)` first when bit
// 26 is set.
void rampTwoColour(std::uint32_t state, float t, const float (&a)[4], const float (&b)[4],
                   float gamma, float (&out)[4]);

// `[BIN]` Ramp kind 3: stops at even spacing. `t` arrives already saturated and
// multiplied by the target's scale, so the integer part indexes the stop and the
// fraction interpolates -- which is why the caller multiplies rather than this.
void rampUniform(std::uint32_t state, float t, const std::vector<Stop>& stops,
                 float (&out)[4]);

// ---- ramp kinds 1 and 2: a table of stops at arbitrary positions ---------
//
// `[BIN]` `sample_stops_binary` does NOT store a stop's position. Each record
// carries a precomputed affine pair `(a, b)`, and the same `a*t + b` serves
// twice: as the binary search's predicate (`> 0` means "t is past this stop")
// and, saturated, as the parameter interpolating to the NEXT record's colour.
// The record is 16 bytes with bit 25 clear, and the colour of the following
// record is read at +24 -- so the buffer holds one entry past the last stop.
//
// `[INF]` That `a = 1 / (off[i+1] - off[i])` and `b = -off[i] * a` is inference
// from those two uses, not something read from the code that FILLS the buffer.
// It is the only assignment that makes both uses mean what they do.
//
// So the observable behaviour is piecewise-linear interpolation between stops
// by position, held flat past either end -- and that is what this computes,
// expressed in positions because that is what an SVG gives.
struct RampPoint {
    float location = 0.0f;
    float rgba[4]{0, 0, 0, 0};
};

// `stops` must be sorted ascending by location. Fewer than two is not an error:
// one stop is a constant colour, and none is transparent.
void rampAtPositions(const std::vector<RampPoint>& stops, float t, float (&out)[4]);

// ---------------------------------------------------------------------------
// RAMP KIND 3, THE BRANCH NOBODY HAD FOLLOWED -- AND IT IS THE ONE THE ICON USES
// ---------------------------------------------------------------------------
//
// Doc 03 §23 read bit 25 as "an alternate sampling path in the uniform one" and
// left it there. It is not an alternate sampling of the same ramp: it changes
// what the ramp IS. With bit 25 set, `sample_stops_uniform` stops reading
// colours and starts reading a `GradientCubicColor` -- FOUR `half4` per record,
// 32 bytes -- and evaluates them by Horner:
//
//     c(f) = c0 + f*(c1 + f*(c2 + f*c3))
//
// `[BIN]` The branch is `%51` of `sample_stops_uniform` in the RenderBox
// metallib (`default_mod8.ll`), and the three `fmuladd`s are the Horner.
//
// AND THE ICON TAKES IT. The chain is four readings and no inference:
//
//  1. `[BIN]` `IconRendering 0x1BC74` -- the ONE call the icon's fill path makes
//     to `-[RBFill setAxialGradientStartPoint:endPoint:stopCount:colors:
//     colorSpace:locations:flags:]` -- passes `flags = 0x400` as a literal
//     (`mov w6, #0x400` at `0x1BC70`) and `colorSpace = 3` (`mov w4, #3`). The
//     same function's SOLID arm passes the same literal 3 (`0x1B168`), so the
//     space is a constant of the caller and not a property of the document's
//     colour.
//
//  2. `[BIN]` `RB::Fill::Gradient`'s flags word lives at `+0x34` and bits 8-11
//     are an interpolation code. `0x400` makes that code **4**. The constructor
//     clears the nibble when locations are supplied (`0x9908C`) and writes it
//     back unchanged for any code other than 1 or 2 (`0x99154`-`0x991B8`), so 4
//     survives.
//
//  3. `[BIN]` `Gradient::set_fill_state 0x9B9A0`-`0x9B9D4`: code 4 fails the
//     two-colour test (`(code - 2) & ~2 == 0` catches 2 AND 4) and lands on
//     ramp kind **3** with word0 bit 25 set (`0x9B9EC`: `cmp w8, #4` ->
//     `0x200`, which is bit 25 once the halfword is stored at `RenderState+2`,
//     `0x9BAA0`).
//
//  4. `[BIN]` `Gradient::set_gradient_color 0x9A8D4`-`0x9A8EC`: code 4 makes the
//     record **16 halves** wide instead of 4 or 5. That is the 32 bytes.
//
// So the icon's two-colour background is NOT the linear mix of ramp kind 0. It
// is a cubic, and the cubic is `smoothColorCoefficients` below.
//
// `[BIN]` What does NOT happen, which is worth as much: `Gradient::color_out`
// (the same metallib) returns its input untouched unless word0 bit 16 or 18 is
// set, and BOTH come from flags bits 6-7 (`set_fill_state 0x9BA0C`-`0x9BA38`).
// `0x400` has bits 6-7 clear, so the icon gets neither the 4096-entry transfer
// table nor the 3x3 at `0x1625D8`-`0x1625FC` -- which is the Oklab LMS matrix,
// applied to the CUBE of the colour, i.e. `oklab -> linear` (the CPU side is
// `Fill::Color::convert_to_oklab 0x12D030`, gated on the same field at
// `0x9A58C`-`0x9A5B0`). The gradient CAN be interpolated perceptually. This one
// is not. Nor is it dithered: `dither` exists as a function constant of the
// image shader (`default_mod2.ll` `!32`) and of no gradient shader.
struct CubicColor {
    // `c[k]` is the coefficient of `f^k`, four channels each.
    float c[4][4]{};
};

// `[BIN]` `RB::Fill::(anonymous)::smooth_color_coefficients(half4, half4, half4,
// half4)`, `RenderBox 0x9DEB0`-`0x9DF64`, transcribed instruction for
// instruction. It is a **Fritsch-Carlson monotone cubic Hermite** on the four
// stops around one segment, converted to the power basis through its Bezier
// control points:
//
//     d0 = p1-p0   d1 = p2-p1   d2 = p3-p2
//     m1 = (d0+d1)/2 ; m2 = (d1+d2)/2                      `0x9DECC`, `0x9DED8`
//     m1 = 0 where sign(d0) != sign(d1)                     `0x9DEE0`-`0x9DEEC`
//     m1 = clamp to +-3*d0 then to +-3*d1                   `0x9DEF0`-`0x9DF08`
//     m2 = 0 where sign(d1) != sign(d2), clamped the same   `0x9DF0C`-`0x9DF28`
//     b0 = p1 + m1/3 ; b1 = p2 - m2/3                       `0x9DF2C`-`0x9DF40`
//     c0 = p1 ; c1 = m1 ; c2 = 3*(p1 - 2*b0 + b1) ; c3 = d1 + 3*(b0 - b1)
//
// The `1/3` is the literal `0x3EAAAAAB`, and the `3` is `fmov v7.4s, #3.0`.
//
// THE CONSEQUENCE, AND IT IS THE WHOLE POINT. With TWO stops and the `pad`
// spread the caller duplicates the ends (`0x9B810`-`0x9B83C`), so `d0` and `d2`
// are zero; the clamp to `+-3*d0` then drives BOTH tangents to zero, and the
// cubic collapses to exactly
//
//     c(f) = p1 + (p2 - p1) * (3f^2 - 2f^3)
//
// -- **smoothstep**. The target's two-stop gradient eases out of both endpoint
// colours. A straight mix beside it is the hard ramp.
CubicColor smoothColorCoefficients(const float (&p0)[4], const float (&p1)[4],
                                   const float (&p2)[4], const float (&p3)[4]);

// The ramp of kind 3 + bit 25, whole: `t` is saturated, scaled by `stops-1`,
// split into a segment index and a fraction, and the segment's cubic evaluated.
//
// `[BIN]` The edges the coefficients need come from the neighbours, with the
// first and last stop duplicated under `pad` (`0x9B81C`, `0x9B838`; under
// `reflect` the target reaches across instead, which no icon fill does).
//
// `[OBS]` Ramp kind 3 assumes the stops are EVENLY SPACED -- the index is
// `int(t * scale)` and no location is stored. A ramp whose locations are not
// uniform sets the constructor's bit 15 (`0x99114`) and takes a different kind
// entirely, which was not read. Every ramp this renderer builds for an icon fill
// has exactly two stops at 0 and 1, so the question does not arise here; when it
// does, this falls back to `rampAtPositions` rather than pretending.
//
// `[OBS]` The target runs the spline on PREMULTIPLIED colour (`0x9B710`
// multiplies rgb by alpha before the coefficient pass) and stores the
// coefficients as `half`. This runs on straight colour in `float`. Every icon
// fill colour measured is alpha 1.0, where the two agree exactly; below 1.0 they
// do not, and that is not a difference this has any reading to settle.
void rampSmoothAtPositions(const std::vector<RampPoint>& stops, float t, float (&out)[4]);

}  // namespace rb
