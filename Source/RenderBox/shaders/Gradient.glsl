// The gradient, transcribed from `RB::Shader::Gradient`.
//
// `[BIN]` Doc 03 §23. The four-bit field at word0 bits 19-22 is FOUR GEOMETRIES
// BY FOUR SPREADS, and that decomposition is a measurement rather than a
// reading: `Gradient::value` and `Gradient::fold_value` switch on the same field
// and group its sixteen cases two different ways, and the two groupings cross
// without a leftover.
//
// This file is the one the renderer runs AND the one the oracle is gated
// against, so what draws and what is verified cannot drift apart.
#ifndef RB_GRADIENT_GLSL
#define RB_GRADIENT_GLSL

const uint kGradientKindShift = 19u;
const uint kRampKindShift = 23u;
const uint kStopsAltPath = 33554432u;   // bit 25
const uint kStopGamma = 67108864u;      // bit 26

// 0 linear, 1 radial, 2 focal, 3 conic, 4 from-vertex
int rbGradientGeometry(uint state) {
    uint k = (state >> kGradientKindShift) & 15u;
    if (k == 0u || k == 1u || k == 2u || k == 7u) return 0;
    if (k == 3u || k == 4u || k == 5u || k == 8u) return 1;
    if (k == 9u || k == 10u || k == 11u || k == 12u) return 2;
    if (k == 6u) return 3;
    return 4;
}

// 0 pad, 1 repeat, 2 reflect, 3 none
int rbGradientSpread(uint state) {
    uint k = (state >> kGradientKindShift) & 15u;
    if (k == 0u || k == 3u || k == 9u || k == 13u) return 0;
    if (k == 1u || k == 4u || k == 10u || k == 14u) return 1;
    if (k == 2u || k == 5u || k == 11u || k == 15u) return 2;
    return 3;
}

// `[BIN]` pad is saturate, repeat is fract, and reflect is the triangle wave
// `fract(t * 0.5) * 2` folded at 1. The half constants in the IR are 0xH3800
// (0.5), 0xH4000 (2.0) and 0xH3C00 (1.0).
float rbFoldValue(uint state, float t) {
    int s = rbGradientSpread(state);
    if (s == 0) return clamp(t, 0.0, 1.0);
    if (s == 1) return fract(t);
    if (s == 2) {
        precise float doubled = fract(t * 0.5) * 2.0;
        return doubled > 1.0 ? 2.0 - doubled : doubled;
    }
    return t;
}

// `[BIN]` The geometry. A point already in the gradient's space becomes `t`.
// `covered` is 0 where this transcription does not cover the case -- NOT
// "transparent", which is a different statement and would paint something.
float rbGradientValue(uint state, vec2 p, float a, float b, out int covered) {
    covered = 1;
    int g = rbGradientGeometry(state);
    if (g == 0) return p.x;                       // linear: the transform did the work
    if (g == 1) return sqrt(dot(p, p)) * a + b;   // radial
    // `[OBS]` conic, focal and from-vertex are not transcribed -- doc 03 §23.2.
    covered = 0;
    return 0.0;
}

// `[BIN]` Ramp kind 0: two colours mixed, with `powr(t, gamma)` on the
// SATURATED parameter first when bit 26 is set, and saturated again after.
vec4 rbRampTwoColour(uint state, float t, vec4 lo, vec4 hi, float gamma) {
    float u = t;
    if ((state & kStopGamma) != 0u) {
        u = clamp(pow(clamp(t, 0.0, 1.0), gamma), 0.0, 1.0);
    }
    // `[BIN]` `air.mix` is `x + (y - x) * a`, and that factoring is transcribed
    // rather than delegated to GLSL's `mix`, whose expansion is not specified --
    // an implementation free to emit `x*(1-a) + y*a` rounds differently and the
    // differential against the oracle stops being bit-for-bit.
    //
    // This is the same lesson as the Bezier in §8: the target's ARRANGEMENT of
    // the arithmetic is part of what is being reproduced, not just its value.
    precise vec4 span = hi - lo;
    precise vec4 result = lo + span * u;
    return result;
}

#endif
