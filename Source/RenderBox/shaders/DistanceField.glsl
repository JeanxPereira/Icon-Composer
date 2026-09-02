// `distanceGradient_v1`, transcribed from mod95.
//
// `[BIN]` `References/2.0-125/out/metallib-renderbox/default_mod95.ll`. The
// value numbers in the comments below are that file's own, so a reader can put
// the two side by side.
//
// This is the file the renderer will include AND the file the differential is
// gated against, so what draws and what is verified cannot drift apart. That is
// the arrangement every transcribed stage in this repository uses.
//
// WHAT IS NOT IN HERE. The FIELD GENERATOR is not: it runs on the CPU, it is
// this project's own algorithm rather than a transcription, and it lives in
// `DistanceField.cpp` under a header that says so. Nothing on this page
// produces a distance -- it only reads one back and differentiates it.
#ifndef RB_DISTANCEFIELD_GLSL
#define RB_DISTANCEFIELD_GLSL

// The texture fetch is supplied BY THE INCLUDER, and that is a deliberate seam.
//
// `[OBS]` mod95 samples through `@__air_sampler_state = 0x807BFF0000080A49`, a
// packed Metal sampler descriptor whose bit layout is not published. The filter
// and address modes were NOT decoded. Rather than invent a bilinear and then
// gate our invention against itself, the fetch is left to the caller:
// `field_probe.comp` fetches the nearest texel out of a storage buffer, by the
// same rule `rb::FieldTexture::sampleX` uses on the CPU, so the differential
// measures the TRANSCRIPTION -- the tap positions, the half-precision central
// difference, the guard, the zero branch -- and not a filter nobody read.
vec4 rbFieldSample(vec2 uv);

// `[BIN]` The five `float2` of an `RB::Layer` (spec §5.1), in the order the
// IR's own copy loop `%11` stores them out of `params + 2`.
struct RbFieldLayer {
    vec2 m0;   // the column p.x multiplies
    vec2 m1;   // the column p.y multiplies
    vec2 m2;   // the translation
    vec2 m3;   // clamp low
    vec2 m4;   // clamp high
};

// The half narrowing the target gets for free from its `half4` texture and
// `half4` return, and which this has to perform explicitly. It is not cosmetic:
// `%102` and `%105` are `fsub half`, so the central difference ROUNDS TO HALF
// before it is widened and normalized.
//
// The one caveat, stated rather than discovered later: Vulkan permits
// `packHalf2x16` to flush half denormals, so a difference below 6.1e-5 is not
// guaranteed to survive identically on both sides. The gate keeps its values
// away from that floor.
float rbHalf(float v) { return unpackHalf2x16(packHalf2x16(vec2(v, 0.0))).x; }

// `[BIN]` `uv(p) = clamp(fma(p.xx, m0, fma(p.yy, m1, m2)), m3, m4)`.
//
// `%44` is the inner fma over `p.yy` and `%45` the outer over `p.xx`, and both
// are `air.fma` -- the TRUE fused multiply-add, not the contractible
// `llvm.fmuladd` that turns up later at `%121`. `precise` is what stops the
// compiler from re-contracting or reassociating them; the differential here is
// bit-for-bit and would not survive either.
vec2 rbFieldUv(RbFieldLayer L, vec2 p) {
    precise vec2 inner = fma(vec2(p.y), L.m1, L.m2);
    precise vec2 outer = fma(vec2(p.x), L.m0, inner);
    return clamp(outer, L.m3, L.m4);   // `air.fast_clamp`, `%46`
}

// `[BIN]` The whole of `distanceGradient_v1`, in the IR's order.
//
// `dpdx` and `dpdy` are the target's `dfdx(p.x)` and `dfdy(p.y)`. They are
// parameters rather than calls because a derivative exists only inside a
// fragment shader's quad, and the probe that gates this is compute. The `abs`
// stays here, on this side of the seam, because it is `air.fast_fabs` at
// `%60`/`%63` and dropping it would move two of the four taps to the wrong
// side of `p`.
vec4 rbDistanceGradient(vec2 p, float dpdx, float dpdy, float scale, float fallback,
                        RbFieldLayer L) {
    vec4 centre = rbFieldSample(rbFieldUv(L, p));
    float sdf = centre.x;

    // `%50`: `fcmp oeq half %49, 0`, on the HALF the texture returned.
    if (sdf == 0.0) {
        // `%51`: `(0, u1, u1, 0)`. The zero in `.w` is the load-bearing part --
        // `glassBackground_v1` multiplies its mask by this channel, so a 1 here
        // would paint glass where the field says there is no shape.
        float u1 = rbHalf(fallback);
        return vec4(0.0, u1, u1, 0.0);
    }

    vec2 hx = vec2(abs(dpdx), 0.0);
    vec2 hy = vec2(0.0, abs(dpdy));
    // `%65`, `%74`, `%84`, `%93`: four more taps, each through the same layer.
    float left  = rbFieldSample(rbFieldUv(L, p - hx)).x;
    float right = rbFieldSample(rbFieldUv(L, hx + p)).x;
    float down  = rbFieldSample(rbFieldUv(L, p - hy)).x;
    float up    = rbFieldSample(rbFieldUv(L, hy + p)).x;

    // `%102`/`%105`: the difference is taken in HALF, then widened. And it is
    // NOT divided by the step -- the normalize below would divide the scale
    // straight back out.
    vec2 delta = vec2(rbHalf(right - left), rbHalf(up - down));

    // `%108`/`%109`: `any(delta != 0)`. The guard is not decoration.
    // `rsqrt(0)` is an infinity and the multiply after it is a NaN, and a flat
    // neighbourhood -- deep inside a large shape, or a tap step too small to
    // leave one texel -- reaches it on ordinary input.
    vec2 g = delta;
    if (any(notEqual(delta, vec2(0.0)))) {
        g = delta * inversesqrt(dot(delta, delta));   // `%111`..`%115`
    }

    // `%121`: `llvm.fmuladd(g, scale.xx, fallback.xx)` -- the CONTRACTIBLE
    // intrinsic. Whether the target's back end fuses it is `[OBS]`, decided
    // after this IR. This side fuses, and `%122` narrows to half immediately,
    // where a one-ULP float disagreement almost always vanishes.
    precise vec2 scaled = fma(g, vec2(scale), vec2(fallback));
    // `%124`/`%125`: `.x` is the CENTRE tap passed straight through, unscaled,
    // and `.w` is `0xH3C00` -- the half 1.0.
    return vec4(sdf, rbHalf(scaled.x), rbHalf(scaled.y), 1.0);
}

#endif
