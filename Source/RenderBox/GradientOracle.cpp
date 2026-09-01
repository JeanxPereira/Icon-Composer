#include "Source/RenderBox/GradientOracle.h"

#include <cmath>

namespace rb {
namespace {

float saturate(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

float fract(float v) { return v - std::floor(v); }

std::uint32_t kindOf(std::uint32_t state) {
    return (state >> kGradientKindShift) & kGradientKindMask;
}

}  // namespace

Geometry geometryOf(std::uint32_t state) {
    switch (kindOf(state)) {
        case 0: case 1: case 2: case 7:   return Geometry::Linear;
        case 3: case 4: case 5: case 8:   return Geometry::Radial;
        case 9: case 10: case 11: case 12: return Geometry::Focal;
        case 6:                            return Geometry::Conic;
        default:                           return Geometry::FromVertex;
    }
}

Spread spreadOf(std::uint32_t state) {
    switch (kindOf(state)) {
        case 0: case 3: case 9: case 13:  return Spread::Pad;
        case 1: case 4: case 10: case 14: return Spread::Repeat;
        case 2: case 5: case 11: case 15: return Spread::Reflect;
        default:                           return Spread::None;
    }
}

float foldValue(std::uint32_t state, float t) {
    switch (spreadOf(state)) {
        case Spread::Pad:
            return saturate(t);
        case Spread::Repeat:
            return fract(t);
        case Spread::Reflect: {
            // `[BIN]` fract(t * 0.5) * 2, folded at 1. The half constants in the
            // IR are 0xH3800 (0.5) and 0xH4000 (2.0), and the comparison is
            // against 0xH3C00 (1.0).
            const float doubled = fract(t * 0.5f) * 2.0f;
            return doubled > 1.0f ? 2.0f - doubled : doubled;
        }
        case Spread::None:
            return t;
    }
    return t;
}

float gradientValue(std::uint32_t state, float px, float py, float a, float b,
                    bool* covered) {
    if (covered) *covered = true;
    switch (geometryOf(state)) {
        case Geometry::Linear:
            // `[BIN]` Just the x coordinate: whatever put the point into the
            // gradient's space already did the work.
            return px;

        case Geometry::Radial:
            return std::sqrt(px * px + py * py) * a + b;

        case Geometry::Conic:
        case Geometry::Focal:
        case Geometry::FromVertex:
        default:
            // `[OBS]` None of the three is transcribed, and each for its own
            // reason -- all three recorded in doc 03 §23.2 as SHAPE rather than
            // as code:
            //
            //   conic  an atan minimax turned into a turn by 1/(2*pi). Its
            //          structure is read; its coefficients are not decoded,
            //          because transcribing them means hand-reading hex
            //          constants for a geometry the corpus uses ZERO times --
            //          SVG has no conic gradient and neither does the `.icon`
            //          format. That is fifty stubs wearing a function's name,
            //          the same trap §16.1 avoided with the blend modes.
            //   focal  branches on `1 - a` and `b > 1` and REJECTS points
            //          outside its cone. Also absent from both formats.
            //   vertex the value is computed in the vertex stage; this function
            //          never sees it.
            //
            // `covered` false is the honest answer: not "transparent", but "this
            // transcription does not cover the case". A caller that painted the
            // ramp's first colour here would be inventing.
            if (covered) *covered = false;
            return 0.0f;
    }
}

void rampTwoColour(std::uint32_t state, float t, const float (&a)[4], const float (&b)[4],
                   float gamma, float (&out)[4]) {
    // `[BIN]` The gamma is applied to the SATURATED parameter, and the result is
    // saturated again before the mix.
    float u = t;
    if ((state & kStopGamma) != 0u) u = saturate(std::pow(saturate(t), gamma));
    for (int i = 0; i < 4; ++i) out[i] = a[i] + (b[i] - a[i]) * u;
}

void rampUniform(std::uint32_t state, float t, const std::vector<Stop>& stops,
                 float (&out)[4]) {
    for (int i = 0; i < 4; ++i) out[i] = 0.0f;
    if (stops.empty()) return;
    // `[BIN]` The index is a CONVERSION, not a floor: the IR calls
    // `air.convert.s.i16.f.f16`, which truncates toward zero.
    int i = static_cast<int>(t);
    float f = t - std::floor(t);
    if (i < 0) {
        i = 0;
        f = 0.0f;
    }
    const int last = static_cast<int>(stops.size()) - 1;
    if (i >= last) {
        i = last;
        f = 0.0f;
    }
    // `[BIN]` Bit 26 makes the stride 5 instead of 4, and the fifth half is a
    // per-stop exponent applied to the fraction.
    if ((state & kStopGamma) != 0u) f = std::pow(f, stops[static_cast<std::size_t>(i)].gamma);
    const Stop& lo = stops[static_cast<std::size_t>(i)];
    const Stop& hi = stops[static_cast<std::size_t>(i < last ? i + 1 : last)];
    for (int k = 0; k < 4; ++k) out[k] = lo.rgba[k] + (hi.rgba[k] - lo.rgba[k]) * f;
}

}  // namespace rb
