#include "Source/RenderBox/GlassForeground.h"

#include "Source/RenderBox/GlassOracle.h"

namespace rb {
namespace {

float saturate(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// `air.fast_fmax` / `air.fmax`, with the operand order the IR has.
float fmax1(float a, float b) { return a > b ? a : b; }

// The true fused multiply-add the six `air.fma.v2f32` calls ask for, computed
// the way `DisplacementOracle` computes it and for the same measured reason:
// `[ART]` this toolchain's `std::fma(float, float, float)` is not correctly
// rounded (`test_rb_displacement.cpp` pins a case where the product is exact and
// libc still answers one ULP off). `a * b` is exact in double -- 24 + 24 bits fit
// in 53 -- and `c` is exact, so the only rounding left is the sum's.
float fma1(float a, float b, float c) {
    return static_cast<float>(static_cast<double>(a) * static_cast<double>(b) +
                              static_cast<double>(c));
}

// `[INF]` `air.dot.v2f32` does not fix which product is added to which. With two
// terms there is only one sum, so the order is not the question -- what is, is
// that GLSL's `dot()` is equally unspecified. Both sides spell the same two
// multiplies and one add out by hand instead of calling it.
float dot2(const float a[2], const float b[2]) { return a[0] * b[0] + a[1] * b[1]; }

// `air.mix(x, y, a)` = `x + (y - x) * a`. Never a library `mix`: GLSL's is free
// to expand as `x * (1 - a) + y * a`, which rounds elsewhere.
float mix1(float x, float y, float a) { return x + (y - x) * a; }

}  // namespace

ForegroundParams parseForegroundParams(const float* f) {
    ForegroundParams p;
    ForegroundUniforms& u = p.uniforms;
    u.distanceScale = f[0];
    u.distanceBias = f[1];
    u.gradientScale = f[2];
    u.gradientBias = f[3];
    u.refractionAmount = f[4];
    u.invRefractionHeight = f[5];
    u.refractionOffset = f[6];
    u.aberrationAmount = f[7];
    u.invAberrationHeight = f[8];
    u.aberrationOffset = f[9];
    u.aberrationDir[0] = f[10];
    u.aberrationDir[1] = f[11];
    u.edgeStart = f[12];
    u.edgeEnd = f[13];
    u.edgeOpacityStart = f[14];
    u.edgeOpacityEnd = f[15];
    u.refractionDir[0] = f[16];
    u.refractionDir[1] = f[17];
    // `[BIN]` `%11` is `params + 18` and `%30` is `params + 28`; five `float2`
    // each, and nothing past float 37 is read.
    for (int i = 0; i < 5; ++i) {
        p.source.m[i][0] = f[18 + i * 2 + 0];
        p.source.m[i][1] = f[18 + i * 2 + 1];
    }
    for (int i = 0; i < 5; ++i) {
        p.field.m[i][0] = f[28 + i * 2 + 0];
        p.field.m[i][1] = f[28 + i * 2 + 1];
    }
    return p;
}

void foregroundLayerUV(const ForegroundLayer& layer, float x, float y, float (&uv)[2]) {
    for (int k = 0; k < 2; ++k) {
        // `[BIN]` `%71` is the inner fma, over `p.yy`; `%72` the outer, over
        // `p.xx`. The other nesting gives the same transform and different last
        // bits, and this gate is bit for bit.
        const float inner = fma1(y, layer.m[1][k], layer.m[2][k]);
        const float t = fma1(x, layer.m[0][k], inner);
        // `[BIN]` `air.fast_clamp(%72, m3, m4)` -- low bound first.
        uv[k] = t < layer.m[3][k] ? layer.m[3][k] : (t > layer.m[4][k] ? layer.m[4][k] : t);
    }
}

float foregroundDistance(const ForegroundUniforms& u, float fieldX) {
    // `[BIN]` `%81` is `llvm.fmuladd`, which permits fusion rather than
    // requiring it. Unfused on both sides -- the header says why.
    return fieldX * u.distanceScale + u.distanceBias;
}

void foregroundGradient(const ForegroundUniforms& u, float fieldY, float fieldZ,
                        float (&g)[2]) {
    // `[BIN]` `%94` takes lanes 1 and 2 of the field sample -- `.yz`, not `.xy`
    // -- and `%96`/`%98` splat ONE scale and ONE bias across both lanes.
    g[0] = fieldY * u.gradientScale + u.gradientBias;
    g[1] = fieldZ * u.gradientScale + u.gradientBias;
}

float foregroundEdgeRaw(float d, float fwidthD) {
    // `[BIN]` `%90`: `air.fast_fmax(fwidth(d), 0x3F1A36E2E0000000)`, which is
    // float(1e-4). NOT the half epsilon four instructions above it.
    const float w = fmax1(fwidthD, kForegroundFwidthFloor);
    // `[BIN]` `%91`/`%92`: a true divide, then `0.5 - q`. The saturate is not
    // here; mod99 applies it at `%258`.
    return 0.5f - d / w;
}

void foregroundRefractionDisplacement(const ForegroundUniforms& u, float d, const float g[2],
                                      float (&out)[2]) {
    // `[BIN]` `%107` is `(cos, -sin)` and `%111` is `(sin, cos)`.
    const float c = u.refractionDir[0];
    const float s = u.refractionDir[1];
    const float rowX[2] = {c, -s};
    const float rowY[2] = {s, c};
    const float v[2] = {dot2(g, rowX), dot2(g, rowY)};
    // `[BIN]` `%124`..`%131` is `rbGlassBandAmount` exactly -- the fifth of the
    // seven sites the profile appears at across mod98 and mod99, and it is
    // reused rather than rewritten.
    const float amount = glass::bandAmount(d, u.refractionAmount, u.invRefractionHeight,
                                           u.refractionOffset);
    out[0] = v[0] * amount;
    out[1] = v[1] * amount;
}

void foregroundAberrationDisplacement(const ForegroundUniforms& u, float d, const float g[2],
                                      float (&out)[2]) {
    const float c = u.aberrationDir[0];
    const float s = u.aberrationDir[1];
    // `[BIN]` `%143` (the `(cos, -sin)` dot) is inserted at LANE 1 and `%147`
    // (the `(sin, cos)` dot) at LANE 0. The swap is the target's; the header
    // records the second, independent reading that corroborates it.
    const float rowX[2] = {c, -s};
    const float rowY[2] = {s, c};
    const float v[2] = {dot2(g, rowY), dot2(g, rowX)};
    const float amount = glass::bandAmount(d, u.aberrationAmount, u.invAberrationHeight,
                                           u.aberrationOffset);
    out[0] = v[0] * amount;
    out[1] = v[1] * amount;
}

void foregroundBase(float px, float py, const float dispR[2], float (&base)[2]) {
    // `[BIN]` `%171 = fadd(%135, %0)` -- the displacement is the left operand.
    base[0] = dispR[0] + px;
    base[1] = dispR[1] + py;
}

void foregroundTapPoint(const float base[2], const float dispA[2], float tapOffset,
                        float (&q)[2]) {
    // `[BIN]` `%181`/`%222`, `llvm.fmuladd`, unfused. The second loop negates
    // the displacement once (`%174`) and walks a positive weight; the tap's own
    // offset carries that sign instead, and `(-x) * w` and `x * (-w)` are the
    // same float.
    q[0] = dispA[0] * tapOffset + base[0];
    q[1] = dispA[1] * tapOffset + base[1];
}

void foregroundResolve(const float acc[3], float alphaSum, float coverage, float edgeRaw,
                       float (&out)[4]) {
    const float edge = saturate(edgeRaw);                          // %258
    const float sevenths = alphaSum * glass::kAberrationAlphaScale;  // %261
    const float alpha = (edge * coverage) * sevenths;              // %259, %266
    for (int i = 0; i < 3; ++i) {
        // `[BIN]` `%264` then `%270` -- two multiplies the IR keeps separate,
        // and they stay separate here.
        out[i] = (acc[i] * glass::kAberrationRgbScale[i]) * alpha;
    }
    out[3] = alpha;
}

float foregroundEdgeFade(const ForegroundUniforms& u, float d) {
    const float span = u.edgeEnd - u.edgeStart;             // %281
    const float invSpan = 1.0f / span;                      // %282
    const float bias = -u.edgeStart / span;                 // %284 -- a second fdiv
    const float t = saturate(d * invSpan + bias);           // %285, %286
    const float opacity = mix1(u.edgeOpacityStart, u.edgeOpacityEnd, t);  // %287
    return 1.0f - opacity;                                  // %289
}

void glassForeground(const ForegroundParams& params, float px, float py, float fwidthD,
                     const SampledImage& source, const SampledImage& field, float (&out)[4]) {
    const ForegroundUniforms& u = params.uniforms;

    // `[BIN]` `%73`/`%74`: one sample of `t1`, through the layer at float 28.
    float uvField[2];
    foregroundLayerUV(params.field, px, py, uvField);
    float f[4];
    sampleBilinear(field, uvField[0], uvField[1], f);

    const float d = foregroundDistance(u, f[0]);

    // `[BIN]` `%87`/`%88`: `fcmp olt half %87, 0xH1419` on the field's ALPHA,
    // and the true edge jumps straight to the return with zero. Everything below
    // is dead when it takes.
    if (f[3] < glass::kEpsilon) {
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        return;
    }

    const float edgeRaw = foregroundEdgeRaw(d, fwidthD);

    float g[2];
    foregroundGradient(u, f[1], f[2], g);

    float dispR[2];
    foregroundRefractionDisplacement(u, d, g, dispR);
    float dispA[2];
    foregroundAberrationDisplacement(u, d, g, dispA);

    float base[2];
    foregroundBase(px, py, dispR, base);

    // `[BIN]` `%175`..`%256`: two loops, three iterations then four, and this
    // one loop of seven reproduces them because `rb::glass::aberrationTap`
    // already carries which is which -- the offset's sign and the zero weights
    // are what separate them.
    float acc[3] = {0.0f, 0.0f, 0.0f};
    float alphaSum = 0.0f;
    for (int i = 0; i < glass::kAberrationTaps; ++i) {
        const glass::AberrationTap tap = glass::aberrationTap(i);
        float q[2];
        foregroundTapPoint(base, dispA, tap.offset, q);
        float uvSource[2];
        foregroundLayerUV(params.source, q[0], q[1], uvSource);
        float c[4];
        sampleBilinear(source, uvSource[0], uvSource[1], c);
        // `[BIN]` `%190`/`%231`: `air.fmax(sample.w, 0xH1419)`, the SAME half
        // epsilon as the early-out and not the `1e-4` the fwidth uses.
        const float a = fmax1(c[3], glass::kEpsilon);
        for (int k = 0; k < 3; ++k) acc[k] += (c[k] / a) * tap.weight[k];
        // `[BIN]` `%211`/`%252` add `%189`/`%230`, the sample's RAW alpha -- the
        // value before the fmax, not the floored divisor.
        alphaSum += c[3];
    }

    // `[BIN]` `%173`: the coverage that scales the result is the field's alpha
    // widened from half, unfloored.
    float rgba[4];
    foregroundResolve(acc, alphaSum, f[3], edgeRaw, rgba);

    // `[BIN]` `%291`/`%292`: the fade is splatted over ALL FOUR channels, alpha
    // included. Applying it to rgb alone would leave the result un-premultiplied
    // against its own alpha.
    const float fade = foregroundEdgeFade(u, d);
    for (int k = 0; k < 4; ++k) out[k] = fade * rgba[k];
}

}  // namespace rb
