#include "Source/RenderBox/SdfDisplacement.h"

#include <cmath>

namespace rb {
namespace sdfdisp {
namespace {

// `air.saturate`, written the way `GlassBackground.cpp` writes it and the way
// `SdfDisplacement.glsl` writes it, so the two sides of the differential clamp
// the same way. A NaN falls through both comparisons and comes out a NaN, which
// is the honest answer for the one input that produces one (`height == 0` at
// `s == 0`, see `bandCoordinate`); GLSL's `clamp` leaves that case undefined, so
// the gate keeps it off the GPU and pins it on the CPU alone.
float saturate(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// NO FUSED MULTIPLY-ADD ANYWHERE IN THIS FILE, and that is a transcription
// decision rather than an omission.
//
// `[ART]` Three toolchain hazards were measured in this tower on 2026-09-01 and
// two of them land here. `glslc` emits `precise` as SPIR-V's `NoContraction` on
// `OpFMul` and `OpFAdd` but NOT on `OpExtInst ... Fma` (`GlassOracle.h`), so a
// `fma()` in a shader is a request this driver declines and decomposes; and
// `std::fma` for float on this toolchain is not correctly rounded
// (`DisplacementOracle.cpp`, which routes around it with a double-width
// product). The stable arrangement is therefore the unfused one on BOTH sides:
// `a * b + c`, two roundings each, with `precise` in the GLSL to stop the driver
// contracting it back. AquaKit's transcription of this function has no `fma` in
// it either, so nothing is being given up.

}  // namespace

float depth(const Field& field, const Params& params) {
    // `s = -(field.x + params.z)`. The negation is on the SUM, not distributed
    // over it -- the same value either way here, and the same shape as the IR.
    return -(field.d + params.offset);
}

float maskDistance(const Field& field, const Params& params) {
    return depth(field, params) - params.maskOffset;
}

float filterWidth(float fwidthT) {
    // `[BIN]` `max(fwidth(t), 1e-4)`. The floor is the target's, so it lives
    // here and not at the caller that supplies the derivative.
    return fwidthT > kMinFilterWidth ? fwidthT : kMinFilterWidth;
}

float maskRamp(float t, float w) { return saturate(t / w + 0.5f); }

float alpha(float t, float w, float coverage) {
    const float a = maskRamp(t, w) * coverage;
    // `[BIN]` The cutoff is a comparison on `t`, not on the ramp, and it zeroes
    // the alpha AFTER the coverage has multiplied it. Both facts are observable:
    // a coverage of 0 and a cutoff both produce 0, but only the cutoff produces
    // it for a `t` where the ramp is 1.
    return t < kBandCutoff ? 0.0f : a;
}

float bandCoordinate(float s, float height) { return saturate(s / height); }

float flatProfile(float x) {
    // Transcribed with the multiply left in. `0.2929 * 0.0` is exactly zero and
    // folding the branch to `x < 1 ? 0.7071 : 1.0` would be the same number --
    // but `0.7071` is not `1 - 0.2929f` in float32, so the fold would introduce
    // a constant that was never read.
    return 1.0f - kFlatDrop * (x < 1.0f ? 1.0f : 0.0f);
}

float circularProfile(float x) {
    // `sqrt(1 - (1-x)^2)`, in the target's algebra. `sqrt(x * (2 - x))` is the
    // same function and a different rounding; AquaKit's OTHER band profile
    // (`BandAmount`, the compositor filter's) really is written the second way,
    // which is exactly why this one is not silently rewritten to match it.
    const float u = 1.0f - x;
    return std::sqrt(1.0f - u * u);
}

float magnitude(float flat, float circ, float curvature) {
    // `air.mix(x, y, a) = x + (y - x) * a`.
    return flat + (circ - flat) * curvature;
}

void direction(const Field& field, const Rot& rot, float (&dir)[2]) {
    // Rows `(cos, -sin)` and `(sin, cos)`, applied to the GRADIENT. The two dot
    // products are spelled out rather than handed to a `dot()`: a dot product is
    // free to fuse its multiply-add, and the GLSL side has to be able to forbid
    // that with `precise` for the differential to be bit for bit.
    dir[0] = field.gx * rot.cos + field.gy * (-rot.sin);
    dir[1] = field.gx * rot.sin + field.gy * rot.cos;
}

float fade(float s, float height, float w) { return saturate((height - s) / w + 0.5f); }

Result glassDisplacement(const Field& field, const Params& params, const Rot& rot, float fwidthT) {
    const float s = depth(field, params);
    const float t = s - params.maskOffset;

    const float w = filterWidth(fwidthT);
    const float a = alpha(t, w, field.coverage);

    const float x = bandCoordinate(s, params.height);
    const float flat = flatProfile(x);
    const float circ = circularProfile(x);
    const float mag = magnitude(flat, circ, params.curvature);

    float dir[2];
    direction(field, rot, dir);

    const float amount = 1.0f - mag;
    float dispX = amount * dir[0];
    float dispY = amount * dir[1];

    const float f = fade(s, params.height, w);
    dispX *= f;
    dispY *= f;

    // `[BIN]` The `.w` is the literal 1.0 the target returns, not a coverage.
    return Result{dispX, dispY, a, 1.0f};
}

}  // namespace sdfdisp
}  // namespace rb
