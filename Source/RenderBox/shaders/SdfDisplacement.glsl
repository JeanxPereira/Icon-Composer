// `sdf_glass_displacement`, transcribed -- THE GENERATOR OF THE DISPLACEMENT MAP.
//
// This is the file the renderer includes AND the file the differential runs
// (`sdf_displacement_probe.comp`), so what draws and what is verified cannot
// drift apart. The CPU twin is `Source/RenderBox/SdfDisplacement.h/.cpp`.
//
// PROVENANCE, AND IT IS WEAKER HERE THAN ANYWHERE ELSE IN THIS TOWER.
// `[BIN]` `UberShader<T,U>::sdf_glass_displacement`, QuartzCore
// `default.metallib`, macOS 27.0 build `26A5416b` -- the same build as the
// target. `[OBS]` But this project did not read that metallib: the reading is
// AquaKit's (`Source/QuartzCore/shaders/GlassCommon.glsl`, function
// `SdfGlassDisplacement`; write-up in `docs/shaders-glass-quartzcore.md` §3 and
// §11), and there is NO RenderBox-side copy to cross-check it against, because
// RenderBox's shader library has no displacement generator in it at all. The
// long version of that caveat is at the top of `SdfDisplacement.h` and it should
// be read before this file is trusted.
//
//     field  = (d, gx, gy, coverage)
//     params = (height, curvature, offset, mask-offset)
//     rot    = (cos, sin)
//
// `[BIN]` The target takes `w` from the rasteriser: `w = max(fwidth(t), 1e-4)`.
// Here `fwidthT` is an ARGUMENT, because the differential runs in compute and a
// compute dispatch has no fragment quad -- the same shape `Displacement.glsl`
// uses for `dpdx`/`dpdy`. A fragment caller writes
//
//     float t = rbSdfDispMaskDistance(field, params);
//     vec4  r = rbSdfGlassDisplacement(field, params, rot, fwidth(t));
//
// and gets the target's arithmetic exactly. The `1e-4` floor stays inside the
// function, because the floor is the target's and not the caller's.
#ifndef RB_SDFDISPLACEMENT_GLSL
#define RB_SDFDISPLACEMENT_GLSL

// `[BIN]` `0.2929`, EXACT -- NOT `1 - sqrt(2)/2 = 0.29289321881...`. AquaKit
// seals it as a hand-written, hand-rounded constant in Apple's source rather
// than something derived at compile time; the two values part at the fifth digit
// and "correcting" this one to the closed form is a plausible wrong pixel.
const float kRbSdfDispFlatDrop = 0.2929;

// `[BIN]` The band's hard cutoff, a LENGTH in the field's own distance units --
// so it is only right while the field's ruler is one pixel per point. `[OBS]`
// Our ruler is not established against the target; a scaled path would need this
// multiplied, the way AquaKit's `SdfKeyFillHighlight` multiplies its own copy by
// `pointsToPixels`.
const float kRbSdfDispBandCutoff = -5.0;

// `[BIN]` Apple's canonical antialiasing floor, the same literal that
// `sdf_glass_highlight` and `sdf_key_fill_highlight` carry.
const float kRbSdfDispMinFilterWidth = 1e-4;

// `[BIN]` `s = -(field.x + params.z)` -- oriented depth, positive INWARD.
float rbSdfDispDepth(vec4 field, vec4 params) { return -(field.x + params.z); }

// `[BIN]` `t = s - params.w`. What the mask ramps on, what the `-5.0` cutoff
// compares against, and the value a fragment caller takes `fwidth()` of.
float rbSdfDispMaskDistance(vec4 field, vec4 params) {
    return rbSdfDispDepth(field, params) - params.w;
}

// `[BIN]` `w = max(fwidth(t), 1e-4)`.
float rbSdfDispFilterWidth(float fwidthT) { return max(fwidthT, kRbSdfDispMinFilterWidth); }

// `[BIN]` `alpha = saturate(t/w + 0.5) * field.w`, then `if (t < -5.0) alpha = 0`.
//
// The cutoff zeroes ONLY the alpha -- `disp.xy` runs on past it untouched. That
// is transcribed rather than tidied: a version that also zeroed the displacement
// agrees everywhere the mask is consumed and disagrees everywhere it is not.
// Split out so the probe can read the ramp on its own without a second copy of
// the expression living in the probe.
float rbSdfDispMaskRamp(float t, float w) { return clamp(t / w + 0.5, 0.0, 1.0); }

float rbSdfDispAlpha(float t, float w, float coverage) {
    float a = rbSdfDispMaskRamp(t, w) * coverage;
    return t < kRbSdfDispBandCutoff ? 0.0 : a;
}

// `[BIN]` `x = saturate(s / params.x)` -- 0 at the edge, 1 at the end of the
// band. `[OBS]` The target has NO guard on `height == 0`: the divide is
// unconditional. Not adding one is the transcription.
float rbSdfDispBandCoordinate(float s, float height) { return clamp(s / height, 0.0, 1.0); }

// `[BIN]` `flat = 1.0 - 0.2929 * (x < 1.0 ? 1.0 : 0.0)`. Inside the band that is
// `0.7071` -- the normal of a flat 45-degree bevel, constant across the band;
// outside it the profile is 1, which makes the displacement exactly zero.
//
// `precise` because `1 - a*b` is a multiply-add the driver may contract, and the
// CPU twin cannot (this build has no FMA instruction to contract into).
float rbSdfDispFlatProfile(float x) {
    precise float f = 1.0 - kRbSdfDispFlatDrop * (x < 1.0 ? 1.0 : 0.0);
    return f;
}

// `[BIN]` `circ = sqrt(1.0 - (1.0 - x) * (1.0 - x))`, a quarter circle. Kept in
// the target's algebra: `sqrt(x * (2 - x))` is the same function and a different
// rounding, and it is the form AquaKit's OTHER band profile (`BandAmount`, the
// compositor filter's, a different program) genuinely uses.
float rbSdfDispCircularProfile(float x) {
    float u = 1.0 - x;
    precise float inner = 1.0 - u * u;
    return sqrt(inner);
}

// `[BIN]` `mag = mix(flat, circ, curvature)`, where `mix` is `air.mix`, i.e.
// `x + (y - x) * a`. GLSL's own `mix` is free to expand as `x*(1-a) + y*a`,
// which rounds differently, so it is spelled out on both sides.
float rbSdfDispMagnitude(float flatP, float circ, float curvature) {
    precise float m = flatP + (circ - flatP) * curvature;
    return m;
}

// `[BIN]` `dir = (dot(g, (rot.x, -rot.y)), dot(g, (rot.y, rot.x)))` -- the
// rotation multiplies the GRADIENT.
//
// AquaKit has a scar precisely here
// (`docs/re/2026-08-28-aberracao-a-matriz-nao-e-o-displacement.md`): an earlier
// version of its aberration fed the DISPLACEMENT into this matrix, so a row came
// out as `(alpha, 1.0)` -- a coverage and a literal -- and the effect stopped
// varying with the geometry. The laudo settles that the matrix is a CPU-supplied
// transform from field space into UV, texel-scaled, and that its operand is
// `field.yz`.
//
// Written as explicit multiply-adds, not `dot()`: `OpDot` may fuse internally and
// `precise` is what forbids it, so the oracle's two roundings are matched here.
vec2 rbSdfDispDirection(vec2 g, vec2 rot) {
    precise float dx = g.x * rot.x + g.y * (-rot.y);
    precise float dy = g.x * rot.y + g.y * rot.x;
    return vec2(dx, dy);
}

// `[BIN]` `saturate((params.x - s) / w + 0.5)` -- the ramp that switches the
// displacement off past the end of the band. It divides by the SAME `w` the mask
// used, `fwidth(t)` and not `fwidth(s)`; those agree only because `t` and `s`
// differ by a constant.
float rbSdfDispFade(float s, float height, float w) {
    return clamp((height - s) / w + 0.5, 0.0, 1.0);
}

// The whole function, in the order the target runs it. The return's `.w` is the
// LITERAL 1.0 the target returns, not a coverage.
vec4 rbSdfGlassDisplacement(vec4 field, vec4 params, vec2 rot, float fwidthT) {
    float s = rbSdfDispDepth(field, params);
    float t = s - params.w;

    float w = rbSdfDispFilterWidth(fwidthT);
    float a = rbSdfDispAlpha(t, w, field.w);

    float x = rbSdfDispBandCoordinate(s, params.x);
    float flatP = rbSdfDispFlatProfile(x);
    float circ = rbSdfDispCircularProfile(x);
    float mag = rbSdfDispMagnitude(flatP, circ, params.y);

    vec2 dir = rbSdfDispDirection(field.yz, rot);

    vec2 disp = (1.0 - mag) * dir;
    disp *= rbSdfDispFade(s, params.x, w);

    return vec4(disp.x, disp.y, a, 1.0);
}

#endif
