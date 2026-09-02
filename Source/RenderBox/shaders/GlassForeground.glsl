// `glassForeground_v1`, transcribed -- the pass that refracts a layer's OWN
// contents rather than what is behind it.
//
// `[BIN]` `References/2.0-125/out/metallib-renderbox/default_mod99.ll`, 454
// lines, read end to end. The provenance of every uniform byte, every constant,
// every divergence from AquaKit's independent reading of QuartzCore, and the
// three toolchain hazards this file steers around all live in
// `Source/RenderBox/GlassForeground.h`, which this file is the twin of. ONE
// explanation, in one place; this side carries only what is about being GLSL.
//
// This is the file the renderer includes AND the file the differential gates, so
// what draws and what is verified cannot drift apart.
//
// `[BIN]` The shape, in the order mod99 runs it:
//
//     f     = t1.sample(uvField(p))              // ONE tap: the distance field
//     if (f.w < 0.001) return vec4(0)            // %88, the early-out
//     d     = f.x * distanceScale + distanceBias
//     edge  = 0.5 - d / max(fwidth(d), 1e-4)
//     g     = f.yz * gradientScale + gradientBias
//     dR    = rotate(g, refractionDir) * band(d, refraction...)
//     dA    = swap(rotate(g, aberrationDir)) * band(d, aberration...)
//     base  = dR + p
//     for seven taps s: c = t0.sample(uvSource(base + dA * s))
//                       acc += (c.rgb / max(c.a, 0.001)) * weight(s)
//                       alphaSum += c.a
//     alpha = saturate(edge) * f.w * (alphaSum / 7)
//     out   = (1 - edgeFade(d)) * vec4(acc * rgbScale * alpha, alpha)
//
// `src` -- the `half4` the stitchable ABI hands every one of these functions --
// IS NEVER READ. `%1` appears in the signature and nowhere in the body.
//
// WHY `precise` IS ON EVERY INTERMEDIATE, AND WHY `fma()` APPEARS EXACTLY ONCE.
// The oracle and this file are compared bit for bit, so a driver contracting
// `a * b + c` or reassociating a sum is a failed gate rather than a footnote.
// `[BIN]` mod99 spells six of its multiply-adds `air.fma` -- a TRUE fused
// multiply-add -- and every last one of the six is a `RB::Layer` UV transform;
// the other eight are `llvm.fmuladd`, which merely permits fusion. So the UV
// transform is written with `fma()` and matched by a double-width fused
// multiply-add in the oracle, and everything else is written unfused on both
// sides. `[ART]` The reason the rule cannot simply be "use fma() wherever the IR
// says air.fma" is in `GlassOracle.h`: glslc does not decorate
// `OpExtInst ... Fma` with `NoContraction` even under `precise`, and this driver
// decomposes it -- so `fma()` is a request the GPU may decline while the
// oracle's is not. It is kept only where `Displacement.glsl` already measured
// the pairing bit-exact on this adapter, for this same expression.
#ifndef RB_GLASSFOREGROUND_GLSL
#define RB_GLASSFOREGROUND_GLSL

#include "Glass.glsl"

// ---- the samplers are INPUTS, not hidden state ---------------------------
//
// `[OBS]` The target samples both textures through `@__air_sampler_state`, one
// packed descriptor word `0x807BFF0000080A49`, and that word was NOT decoded:
// filter mode, address mode and mip selection are unread. What IS read is that
// each layer clamps its UV into its own rect before every sample, so the address
// mode is dead inside the rect the layer declares.
//
// The includer supplies them. The probe backs them with a buffer, the renderer
// will back them with real textures, and the math below does not move either
// way. `Displacement.glsl` made the same arrangement for the same reason.
vec4 rbFgSampleSource(vec2 uv);   // `t0` -- the layer's own contents, seven taps
vec4 rbFgSampleField(vec2 uv);    // `t1` -- the distance field, one tap

// `[BIN]` mod99 `%90`: `air.fast_fmax(air.fwidth.f32(d), 0x3F1A36E2E0000000)`.
// float(1e-4), bit pattern 0x38D1B717 -- and NOT `kRbGlassEpsilon`, which is the
// half 0.001 that mod98 uses for the same job. Both epsilons live in this
// module, four instructions apart. See the header.
const float kRbFgFwidthFloor = 9.99999974737875163555145e-05;

// `[BIN]` `struct RB::Layer { texture2d tex; float2 m[5]; }`, stored by the two
// identical unrolled loops at `%12` and `%32`.
struct RBFgLayer {
    vec2 m0;
    vec2 m1;
    vec2 m2;
    vec2 m3;   // clamp low
    vec2 m4;   // clamp high
};

// `[BIN]` mod99's 72-byte `RB::Shader::Glass::ForegroundUniforms` -- the name is
// the TBAA node `!35`'s own mangling, and every field's byte offset is pinned by
// the ARM64 packer at `0x0E7D48`. The header carries the store-by-store reading.
struct RBFgUniforms {
    float distanceScale;        // +0
    float distanceBias;         // +4
    float gradientScale;        // +8
    float gradientBias;         // +12
    float refractionAmount;     // +16   inputRefractionAmount
    float invRefractionHeight;  // +20   1/inputRefractionHeight, zero guarded
    float refractionOffset;     // +24   inputRefractionOffset
    float aberrationAmount;     // +28   inputAberrationAmount
    float invAberrationHeight;  // +32   1/inputAberrationHeight, zero guarded
    float aberrationOffset;     // +36   inputAberrationOffset
    vec2 aberrationDir;         // +40   (cos, sin) of inputAberrationAngle
    float edgeStart;            // +48   inputEdgeStart
    float edgeEnd;              // +52   inputEdgeEnd
    float edgeOpacityStart;     // +56   inputEdgeOpacityStart
    float edgeOpacityEnd;       // +60   inputEdgeOpacityEnd
    vec2 refractionDir;         // +64   (cos, sin) of inputRefractionAngle
};

// `[BIN]` uv(v) = clamp(fma(v.xx, m0, fma(v.yy, m1, m2)), m3, m4).
//
// The `y` term is the INNER fma (`%71`), the `x` term the outer (`%72`). This is
// the one place `fma()` is used, and the file header says why it is used here
// and nowhere else.
vec2 rbFgLayerUV(RBFgLayer L, vec2 v) {
    precise vec2 inner = fma(vec2(v.y), L.m1, L.m2);
    precise vec2 t = fma(vec2(v.x), L.m0, inner);
    return clamp(t, L.m3, L.m4);   // `air.fast_clamp`
}

// `[BIN]` `%81`, an `llvm.fmuladd`. The field's `.x` decoded into a distance.
float rbFgDistance(RBFgUniforms u, float fieldX) {
    precise float d = fieldX * u.distanceScale + u.distanceBias;
    return d;
}

// `[BIN]` `%94`..`%100`. Lanes 1 and 2 of the field sample -- `.yz`, not `.xy`
// -- with ONE scale and ONE bias splatted across both.
vec2 rbFgGradient(RBFgUniforms u, vec2 fieldYZ) {
    precise vec2 g = fieldYZ * vec2(u.gradientScale) + vec2(u.gradientBias);
    return g;
}

// `[BIN]` `%86`..`%92`. Returned UNSATURATED: mod99 saturates this 170
// instructions later, at `%258`, after all seven taps. Keeping the two apart is
// what lets the gate see the value the target actually carries between them.
//
// `fwidth(d)` is a parameter rather than a `fwidth()` call because a compute
// dispatch has no fragment quad -- the same reason `distanceGradient_v1` and
// `displacementMap_v1` take their derivatives as arguments.
//
// `[BIN]` mod99 does NOT use the edge polynomial. `rbGlassEdgeCoverage`, the
// quartic minimax fit mod98 reaches for at three sites, appears nowhere in this
// module: the foreground's coverage is analytic, off `fwidth`.
float rbFgEdgeRaw(float d, float fwidthD) {
    precise float w = max(fwidthD, kRbFgFwidthFloor);
    precise float e = 0.5 - d / w;
    return e;
}

// `[INF]` `air.dot.v2f32` and GLSL's `dot()` are both unspecified about their
// reduction; with two terms there is one sum, but the multiplies are still
// contractible, so both sides spell it out.
float rbFgDot2(vec2 a, vec2 b) {
    precise float r = a.x * b.x + a.y * b.y;
    return r;
}

// `[BIN]` `%107`..`%135`. `(dot(g, (cos, -sin)), dot(g, (sin, cos)))`, scaled by
// the band profile over the refraction's own three uniforms.
vec2 rbFgRefractionDisplacement(RBFgUniforms u, float d, vec2 g) {
    vec2 rowX = vec2(u.refractionDir.x, -u.refractionDir.y);
    vec2 rowY = vec2(u.refractionDir.y, u.refractionDir.x);
    vec2 v = vec2(rbFgDot2(g, rowX), rbFgDot2(g, rowY));
    float amount = rbGlassBandAmount(d, u.refractionAmount, u.invRefractionHeight,
                                     u.refractionOffset);
    precise vec2 r = v * vec2(amount);
    return r;
}

// `[BIN]` `%142`..`%170`. The SAME rotation with its two components SWAPPED:
// `%143` is inserted at lane 1 and `%147` at lane 0. `[ART]` AquaKit read the
// identical swap out of QuartzCore from a different binary, so it is the
// target's and not a transposition in the reading. The header names both.
vec2 rbFgAberrationDisplacement(RBFgUniforms u, float d, vec2 g) {
    vec2 rowX = vec2(u.aberrationDir.x, -u.aberrationDir.y);
    vec2 rowY = vec2(u.aberrationDir.y, u.aberrationDir.x);
    vec2 v = vec2(rbFgDot2(g, rowY), rbFgDot2(g, rowX));
    float amount = rbGlassBandAmount(d, u.aberrationAmount, u.invAberrationHeight,
                                     u.aberrationOffset);
    precise vec2 r = v * vec2(amount);
    return r;
}

// `[BIN]` `%171 = fadd(%135, %0)`. The refraction moves the point once; the
// dispersion swings about the point it moved to.
vec2 rbFgBase(vec2 p, vec2 dispR) {
    precise vec2 b = dispR + p;
    return b;
}

// `[BIN]` `%181`/`%222`, `llvm.fmuladd`, unfused. The second loop negates the
// displacement once and walks a positive weight; `rbGlassAberrationTap` carries
// that sign in the offset instead, and `(-x) * w` and `x * (-w)` are the same
// float.
vec2 rbFgTapPoint(vec2 base, vec2 dispA, float tapOffset) {
    precise vec2 q = dispA * vec2(tapOffset) + base;
    return q;
}

// `[BIN]` `%258`..`%272`, in the target's grouping:
//
//     edge  = saturate(edgeRaw)
//     alpha = (edge * coverage) * (alphaSum * (1/7))
//     rgb   = (acc * kRbAberrationRgbScale) * alpha
//
// NOT `rbGlassAberrationCombine`, which is otherwise these same three lines:
// mod99 folds the coverage into the alpha at `%266`, BETWEEN the `1/7` and the
// premultiply, and the shared combine premultiplies by the un-covered alpha
// because that is what mod98's call site does. The two constants are the shared
// ones; only the grouping is local.
vec4 rbFgResolve(vec3 acc, float alphaSum, float coverage, float edgeRaw) {
    precise float edge = clamp(edgeRaw, 0.0, 1.0);
    precise float sevenths = alphaSum * kRbAberrationAlphaScale;
    precise float alpha = (edge * coverage) * sevenths;
    precise vec3 scaled = acc * kRbAberrationRgbScale;
    precise vec3 rgb = scaled * vec3(alpha);
    return vec4(rgb, alpha);
}

// `[BIN]` `%281`..`%289`. The second term is a DIVISION (`%284` is
// `fdiv(fneg(edgeStart), span)`), not a multiply by the reciprocal -- AquaKit
// writes the multiply, and mod99 wins. `air.mix` is expanded by hand because
// GLSL's `mix` may expand the other way.
//
// `[OBS]` A zero span divides by zero, and mod99 has no guard around it. There
// is nothing there to transcribe; recorded rather than repaired.
float rbFgEdgeFade(RBFgUniforms u, float d) {
    precise float span = u.edgeEnd - u.edgeStart;
    precise float invSpan = 1.0 / span;
    precise float bias = -u.edgeStart / span;
    precise float raw = d * invSpan + bias;
    precise float t = clamp(raw, 0.0, 1.0);
    precise float opacity = u.edgeOpacityStart + (u.edgeOpacityEnd - u.edgeOpacityStart) * t;
    precise float fade = 1.0 - opacity;
    return fade;
}

// The whole stage.
//
// `[BIN]` The early-out at `%88` tests the FIELD SAMPLE'S ALPHA against the half
// `0xH1419` and returns zero outright. That epsilon is `kRbGlassEpsilon`; the
// one four instructions later, flooring `fwidth`, is `kRbFgFwidthFloor`. They
// are not the same number and they are not interchangeable.
vec4 rbGlassForeground(RBFgUniforms u, RBFgLayer source, RBFgLayer field, vec2 p,
                       float fwidthD) {
    vec4 f = rbFgSampleField(rbFgLayerUV(field, p));
    float d = rbFgDistance(u, f.x);
    if (f.w < kRbGlassEpsilon) return vec4(0.0);

    float edgeRaw = rbFgEdgeRaw(d, fwidthD);
    vec2 g = rbFgGradient(u, f.yz);
    vec2 dispR = rbFgRefractionDisplacement(u, d, g);
    vec2 dispA = rbFgAberrationDisplacement(u, d, g);
    vec2 base = rbFgBase(p, dispR);

    // `[BIN]` `%175`..`%256` are two loops, three iterations then four. One loop
    // of seven reproduces them because `rbGlassAberrationTap` already carries
    // which is which -- the sign of the offset and the zero weights are the
    // whole of the difference.
    vec3 acc = vec3(0.0);
    float alphaSum = 0.0;
    for (int i = 0; i < kRbAberrationTaps; ++i) {
        vec4 tap = rbGlassAberrationTap(i);
        vec4 c = rbFgSampleSource(rbFgLayerUV(source, rbFgTapPoint(base, dispA, tap.x)));
        // `[BIN]` `%190`/`%231`: the guarded divisor is the HALF epsilon, the
        // same one the early-out uses.
        precise float a = max(c.w, kRbGlassEpsilon);
        precise vec3 unpremultiplied = c.xyz / vec3(a);
        precise vec3 weighted = unpremultiplied * tap.yzw;
        acc = acc + weighted;
        // `[BIN]` `%211`/`%252` add the sample's RAW alpha -- the value before
        // the max, not the floored divisor.
        alphaSum = alphaSum + c.w;
    }

    // `[BIN]` `%173`: the coverage is the field's alpha, unfloored.
    vec4 rgba = rbFgResolve(acc, alphaSum, f.w, edgeRaw);
    // `[BIN]` `%291`/`%292`: the fade is splatted over ALL FOUR channels.
    precise vec4 result = vec4(rbFgEdgeFade(u, d)) * rgba;
    return result;
}

#endif
