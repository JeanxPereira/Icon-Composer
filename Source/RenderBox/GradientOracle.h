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

}  // namespace rb
