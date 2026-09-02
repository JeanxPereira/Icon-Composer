// `displacementMap_v1`, transcribed.
//
// `[BIN]` `References/2.0-125/out/metallib-renderbox/default_mod97.ll` -- 548
// lines whose `source_filename` is the function's own name. This is the only
// glass-layer stage that is decoded end to end, because it reads a float and two
// `RB::Layer`s and never touches the 75-field uniform block the other four wait
// on.
//
// `[BIN]` The body is one expression, supersampled N times:
//
//     disp   = t1.sample(uvB(q)).xy
//     offset = disp * (2s, 2s) + (-s, -s)      // (disp*2 - 1) * s
//     colour = t0.sample(uvA(q + offset))
//     acc   += colour * t1_sample.zzzz         // .z is a weight PER TAP
//
// `[BIN]` The stitchable ABI hands this function a `half4 src` and a third
// texture and it uses neither: both carry `readnone captures(none)`, and the
// returned half4 is built out of the two texture samples alone.
//
// This is the file the renderer includes AND the file the differential runs, so
// what draws and what is verified cannot drift apart.
#ifndef RB_DISPLACEMENT_GLSL
#define RB_DISPLACEMENT_GLSL

// ---- the sampler is an INPUT, not hidden state ---------------------------
//
// `[OBS]` The target samples through `@__air_sampler_state`, one packed
// descriptor word `0x807bff0000080a49`, and that word was NOT decoded: filter
// mode, address mode and mip selection are unread. What IS read is that the
// shader clamps UV into the layer's own rect before every single sample, so the
// address mode is dead inside the rect the layer declares.
//
// So the includer supplies the two samplers. The probe backs them with a
// buffer; the renderer will back them with real textures; the math below does
// not change either way.
vec4 rbDispSampleSource(vec2 uv);
vec4 rbDispSampleMap(vec2 uv);

// `[BIN]` `struct RB::Layer { texture2d tex; float2 m[5]; }`, the five float2
// loaded by the unrolled loop at the head of the function.
struct RBDispLayer {
    vec2 m0;
    vec2 m1;
    vec2 m2;
    vec2 m3;   // clamp low
    vec2 m4;   // clamp high
};

// `[BIN]` uv(v) = clamp(fma(v.xx, m0, fma(v.yy, m1, m2)), m3, m4).
//
// The `y` term is the INNER fma. That order is transcribed rather than
// simplified: this stage is gated bit for bit against the oracle, and the two
// arrangements do not round alike.
vec2 rbDispLayerUV(RBDispLayer L, vec2 v) {
    precise vec2 inner = fma(vec2(v.y), L.m1, L.m2);
    precise vec2 t = fma(vec2(v.x), L.m0, inner);
    return clamp(t, L.m3, L.m4);   // `air.fast_clamp`
}

// `[BIN]` The IR builds the pair as `fmul(s, 2.0)` splatted into lane 0 and
// `fneg(s)` splatted into lane 1, then reads lane 0 of the first and lane 1 of
// the second -- so the multiplier is 2s on both lanes and the bias is -s on
// both.
//
// The neutral point of the whole stage lives here: a map value of exactly 0.5
// decodes to an offset of exactly zero.
vec2 rbDispDecodeOffset(float s, vec2 disp) {
    precise vec2 result = fma(disp, vec2(s * 2.0), vec2(-s));
    return result;
}

// `[BIN]` q = fma(dfdy(p), oy, fma(dfdx(p), ox, p)). `ox` and `oy` are scalars
// splatted across both lanes; the derivatives are vectors, so the jitter follows
// the transform's own axes rather than the screen's.
vec2 rbDispJitter(vec2 p, vec2 dpdx, vec2 dpdy, float ox, float oy) {
    precise vec2 a = fma(dpdx, vec2(ox), p);
    precise vec2 b = fma(dpdy, vec2(oy), a);
    return b;
}

// ---- the supersampling pattern -------------------------------------------
//
// `[BIN]` A `switch` on `RB::shaderVariant & 7`. Variant 0 is split off with an
// `icmp eq` BEFORE the switch and is the only path that never calls `air.dfdx`
// or `air.dfdy`; cases 1 and 2 are named; 3 through 7 all fall to the default,
// which is the eight-tap block.
//
// `[BIN]` The literals, read one `llvm.fmuladd.v2f32` splat at a time. Each
// pair is (coefficient of dfdx, coefficient of dfdy), in emission order.
// `[INF]` The eight are 1/16-pixel units and match the standard 8x MSAA
// pattern exactly -- the identification is inference, the sixteen numbers are
// not.
int rbDispTapCount(uint variant) {
    uint v = variant & 7u;
    if (v == 0u) return 1;
    if (v == 1u) return 2;
    if (v == 2u) return 4;
    return 8;
}

// `[BIN]` The trailing `fmul` on the accumulated half4: none on variant 0, then
// 0xH3800, 0xH3400, 0xH3000.
float rbDispTapScale(uint variant) {
    uint v = variant & 7u;
    if (v == 0u) return 1.0;
    if (v == 1u) return 0.5;
    if (v == 2u) return 0.25;
    return 0.125;
}

vec2 rbDispTapOffset(uint variant, int i) {
    uint v = variant & 7u;
    if (v == 0u) return vec2(0.0, 0.0);
    if (v == 1u) return i == 0 ? vec2(0.25, 0.25) : vec2(-0.25, -0.25);
    if (v == 2u) {
        if (i == 0) return vec2(-0.125, -0.375);
        if (i == 1) return vec2(0.375, -0.125);
        if (i == 2) return vec2(-0.375, 0.125);
        return vec2(0.125, 0.375);
    }
    if (i == 0) return vec2(0.0625, -0.1875);
    if (i == 1) return vec2(-0.0625, 0.1875);
    if (i == 2) return vec2(0.3125, 0.0625);
    if (i == 3) return vec2(-0.1875, -0.3125);
    if (i == 4) return vec2(-0.3125, 0.3125);
    if (i == 5) return vec2(-0.4375, -0.0625);
    if (i == 6) return vec2(0.1875, 0.4375);
    return vec2(0.4375, -0.4375);
}

// The stage. `dpdx` / `dpdy` are passed in rather than taken with `dFdx`,
// because a compute shader has no fragment quad -- and because making them an
// argument is what lets the gate prove that variant 0 does not read them.
vec4 rbDisplacementMap(float s, RBDispLayer source, RBDispLayer map, uint variant, vec2 p,
                       vec2 dpdx, vec2 dpdy) {
    int taps = rbDispTapCount(variant);
    // Variant 0 takes no derivatives at all in the target. Here that is spelled
    // as reading none of them.
    vec2 ddx = taps == 1 ? vec2(0.0) : dpdx;
    vec2 ddy = taps == 1 ? vec2(0.0) : dpdy;

    vec4 acc = vec4(0.0);
    for (int t = 0; t < taps; ++t) {
        vec2 o = rbDispTapOffset(variant, t);
        vec2 q = rbDispJitter(p, ddx, ddy, o.x, o.y);

        vec4 disp = rbDispSampleMap(rbDispLayerUV(map, q));
        vec2 offset = rbDispDecodeOffset(s, disp.xy);

        // `[BIN]` `fadd(offset, q)`: the displacement moves the JITTERED point.
        vec4 colour = rbDispSampleSource(rbDispLayerUV(source, offset + q));

        // `[BIN]` `.z` of THIS tap's displacement sample, splatted over all four
        // channels. Every tap re-reads it (`%97`, `%125`, `%147`, ...), so it is
        // a per-tap weight and hoisting it is the error this shape invites.
        precise vec4 weighted = t == 0 ? vec4(disp.z) * colour : fma(colour, vec4(disp.z), acc);
        acc = weighted;
    }
    precise vec4 result = acc * rbDispTapScale(variant);
    return result;
}

#endif
