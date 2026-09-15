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

void rampAtPositions(const std::vector<RampPoint>& stops, float t, float (&out)[4]) {
    for (int i = 0; i < 4; ++i) out[i] = 0.0f;
    if (stops.empty()) return;
    // One stop is a constant colour, and it is the only case that cannot fall
    // through: there is no segment to interpolate along.
    if (stops.size() == 1) {
        for (int i = 0; i < 4; ++i) out[i] = stops.front().rgba[i];
        return;
    }

    // NO EARLY RETURN FOR t OUTSIDE THE RAMP, and that is deliberate.
    //
    // A first version guarded both ends explicitly, and the mutation sweep
    // showed the guards were REDUNDANT: with `t` past the last stop the scan
    // below lands on the final segment, computes a parameter above one, and the
    // saturate brings it back to exactly the last stop's colour. The two
    // branches were equivalent, so no test could tell them apart -- and a guard
    // no test can distinguish is not a guard, it is noise that makes the sweep
    // report a defect nobody can fix.
    //
    // `[BIN]` The pad is the target's own: `sample_stops_binary` saturates the
    // segment parameter before the mix, and that IS what holds the ends flat.
    //
    // The search the target does with a branchless binary partition. Linear
    // here: the corpus's longest ramp has five stops, and a binary search over
    // five is a way to get the boundaries wrong for no measurable gain.
    std::size_t i = 0;
    while (i + 2 < stops.size() && stops[i + 1].location <= t) ++i;
    const RampPoint& lo = stops[i];
    const RampPoint& hi = stops[i + 1];
    const float span = hi.location - lo.location;
    // `[BIN]` The parameter is SATURATED before the mix, which is what holds a
    // degenerate segment (two stops at the same position) to a hard edge rather
    // than letting a division by zero through.
    float f = span > 0.0f ? (t - lo.location) / span : 1.0f;
    f = saturate(f);
    for (int k = 0; k < 4; ++k) out[k] = lo.rgba[k] + (hi.rgba[k] - lo.rgba[k]) * f;
}

CubicColor smoothColorCoefficients(const float (&p0)[4], const float (&p1)[4],
                                   const float (&p2)[4], const float (&p3)[4]) {
    CubicColor out;
    for (int k = 0; k < 4; ++k) {
        const float d0 = p1[k] - p0[k];
        const float d1 = p2[k] - p1[k];
        const float d2 = p3[k] - p2[k];

        // `[BIN]` The sign test is `fcmlt ... #0.0` on each delta and an `eor`
        // of the two masks, so a delta of exactly zero counts as NOT negative.
        // Which side zero falls on never shows: the clamp below multiplies that
        // same zero by three and forces the tangent to it anyway.
        float m1 = (d0 + d1) * 0.5f;
        if ((d0 < 0.0f) != (d1 < 0.0f)) m1 = 0.0f;
        if (std::fabs(m1) > std::fabs(3.0f * d0)) m1 = 3.0f * d0;
        if (std::fabs(m1) > std::fabs(3.0f * d1)) m1 = 3.0f * d1;

        float m2 = (d1 + d2) * 0.5f;
        if ((d1 < 0.0f) != (d2 < 0.0f)) m2 = 0.0f;
        if (std::fabs(m2) > std::fabs(3.0f * d1)) m2 = 3.0f * d1;
        if (std::fabs(m2) > std::fabs(3.0f * d2)) m2 = 3.0f * d2;

        // The two inner Bezier control points, and the power basis they give.
        const float b0 = p1[k] + m1 * (1.0f / 3.0f);
        const float b1 = p2[k] - m2 * (1.0f / 3.0f);
        out.c[0][k] = p1[k];
        out.c[1][k] = m1;
        out.c[2][k] = 3.0f * (p1[k] - 2.0f * b0 + b1);
        out.c[3][k] = d1 + 3.0f * (b0 - b1);
    }
    return out;
}

void rampSmoothAtPositions(const std::vector<RampPoint>& stops, float t, float (&out)[4]) {
    for (int i = 0; i < 4; ++i) out[i] = 0.0f;
    if (stops.empty()) return;
    if (stops.size() == 1) {
        for (int i = 0; i < 4; ++i) out[i] = stops.front().rgba[i];
        return;
    }

    // The uniform sampler has no locations to read, so it can only be used where
    // the stops ARE the positions it assumes. Anything else goes back to the
    // piecewise-linear reading, which is the honest answer for a kind this
    // project has not decoded.
    const std::size_t n = stops.size();
    const float step = 1.0f / static_cast<float>(n - 1);
    for (std::size_t i = 0; i < n; ++i) {
        const float want = static_cast<float>(i) * step;
        if (std::fabs(stops[i].location - want) > 1.0e-4f) {
            rampAtPositions(stops, t, out);
            return;
        }
    }

    // `[BIN]` `t` arrives saturated and multiplied by the scale the CPU wrote
    // (`0x9ABE0`: `stops - 1`); the integer part indexes the segment and the
    // fraction drives the cubic.
    const float scaled = saturate(t) * static_cast<float>(n - 1);
    std::size_t seg = static_cast<std::size_t>(scaled);
    if (seg + 1 >= n) seg = n - 2;
    const float f = scaled - static_cast<float>(seg);

    const float* p1 = stops[seg].rgba;
    const float* p2 = stops[seg + 1].rgba;
    const float* p0 = seg == 0 ? p1 : stops[seg - 1].rgba;
    const float* p3 = seg + 2 < n ? stops[seg + 2].rgba : p2;

    const CubicColor cc = smoothColorCoefficients(
        reinterpret_cast<const float(&)[4]>(*p0), reinterpret_cast<const float(&)[4]>(*p1),
        reinterpret_cast<const float(&)[4]>(*p2), reinterpret_cast<const float(&)[4]>(*p3));
    for (int k = 0; k < 4; ++k) {
        out[k] = cc.c[0][k] + f * (cc.c[1][k] + f * (cc.c[2][k] + f * cc.c[3][k]));
    }
}

}  // namespace rb
