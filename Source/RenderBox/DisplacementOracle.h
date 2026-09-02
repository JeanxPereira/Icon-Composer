#pragma once
// `displacementMap_v1` on the CPU -- the twin of `shaders/Displacement.glsl`.
//
// `[BIN]` `References/2.0-125/out/metallib-renderbox/default_mod97.ll`, whose
// `source_filename` is literally `displacementMap_v1`. 548 lines, one function,
// and it is the only stage of the glass layer that is decoded end to end: it
// reads a float and two `RB::Layer`s and nothing else, so it does not wait on
// the uniform bridge the other four shaders wait on.
//
// `[BIN]` The signature is RenderBox's stitchable ABI, read from `!air.visible`:
//
//     half4 displacementMap_v1(float2 p, half4 src, constant float* params,
//                              device uchar* args, texture2d t0, t1, t2)
//
// and of those, `args` (arg 3) and `t2` (arg 6) carry `readnone captures(none)`
// -- they are declared and never touched. So does `src`: the incoming colour is
// an argument of the ABI that this particular stage discards, and the returned
// half4 is built only from the two textures. A transcription that blended `src`
// in would look reasonable and be wrong.
//
// `[BIN]` The whole body is ONE expression, supersampled N times:
//
//     disp   = t1.sample(uvB(q)).xy
//     offset = disp * (2s, 2s) + (-s, -s)        // == (disp*2 - 1) * s
//     colour = t0.sample(uvA(q + offset))
//     acc   += colour * t1_sample.zzzz           // .z is a weight PER TAP
//
// with `q` the point jittered by `dfdx(p)*ox + dfdy(p)*oy`, and the sum scaled
// by 1/N at the end.
//
// `[BIN]` PRECISION. The target samples and accumulates in `half`
// (`air.sample_texture_2d.v4f16`, `llvm.fmuladd.v4f16`); only the UV arithmetic
// and the offset decode are float32. This transcription is float32 throughout,
// which is the same choice `GradientOracle` made for the same reason -- the
// gate here is "our GPU and our oracle agree on the arrangement of the
// arithmetic", and half quantisation is a separate question from whether the
// arrangement is right. `[OBS]` The half rounding of the target is therefore
// NOT reproduced.
#include <cstdint>

namespace rb {

// `[BIN]` `struct RB::Layer { texture2d tex; float2 m[5]; }` -- 5 float2 loaded
// by the two identical unrolled loops at the top of the function. The five are
// an affine UV transform and a clamp rect, and the ORDER of the two fma calls
// is transcribed rather than simplified: the target does the `y` term first.
//
//     uv(v) = clamp(fma(v.xx, m0, fma(v.yy, m1, m2)), m3, m4)
struct DisplacementLayer {
    float m[5][2]{};
};

// `[BIN]` The params buffer is 84 bytes: `params[0]` is the displacement scale,
// then two `RB::Layer` transforms back to back. The second layer's base is
// `getelementptr float, ptr %2, i64 11`, which is what fixes 1..10 as the first
// and 11..20 as the second.
//
// `[BIN]` Layer A is bound to texture argument 4 (`t0`) and layer B to argument
// 5 (`t1`); B is the one sampled first, and it is the DISPLACEMENT MAP. A is the
// source image. That direction is read from which pointer each
// `air.sample_texture_2d` call is given, not assumed from the naming.
struct DisplacementParams {
    float scale = 0.0f;        // params[0]
    DisplacementLayer source;  // params[1..10],  sampled through t0
    DisplacementLayer map;     // params[11..20], sampled through t1
};

// The 21 floats, in the target's order. Exposed so a test can pin the 84-byte
// layout instead of trusting the struct.
DisplacementParams parseDisplacementParams(const float* twentyOne);

// `[BIN]` uv(v) = clamp(fma(v.xx, m0, fma(v.yy, m1, m2)), m3, m4).
void displacementLayerUV(const DisplacementLayer& layer, float x, float y, float (&uv)[2]);

// `[BIN]` `offset = fma(disp, (2s, 2s), (-s, -s))`. The IR builds the two
// constants as `<2 x float>` from `fmul(s, 2.0)` splatted and `fneg(s)`
// splatted, so the multiplier is 2s on BOTH lanes and the bias is -s on both.
//
// This is the neutral point of the whole stage: a map of exactly 0.5 gives an
// offset of exactly zero, and every plausible sign or bias error breaks that or
// the endpoints around it.
void displacementDecodeOffset(float scale, float dispX, float dispY, float (&offset)[2]);

// ---- the supersampling pattern -------------------------------------------
//
// `[BIN]` The tap count is a `switch` on `RB::shaderVariant & 7`. The IR takes
// the `== 0` case with an `icmp` BEFORE the switch, and only the other path
// calls `air.dfdx` / `air.dfdy` at all -- so variant 0 does not merely use a
// zero jitter, it never asks for the derivatives.
//
// The offsets below were read one `llvm.fmuladd.v2f32` splat at a time out of
// mod97; the seals are on `kDisplacementTaps` in the .cpp.
constexpr int kDisplacementMaxTaps = 8;

// 1, 2, 4 or 8.
int displacementTapCount(std::uint32_t variant);

// `[BIN]` The final `fmul` on the accumulated half4: 1, 0xH3800 (0.5),
// 0xH3400 (0.25), 0xH3000 (0.125). It is 1/N, but it is transcribed as the
// literal the IR carries rather than computed, because the literal is what was
// measured.
float displacementTapScale(std::uint32_t variant);

// Fills the first `displacementTapCount(variant)` entries of `out` with the
// jitter, in pixel units and IN THE TARGET'S ORDER. The order matters even
// though addition commutes: the first tap is a plain `fmul` and the rest are
// `fmuladd`, so a permutation changes the rounding, and -- much more to the
// point -- a permuted or sign-flipped set would still average to the same place
// and pass any test that only looked at the mean.
void displacementTapOffsets(std::uint32_t variant, float (&out)[kDisplacementMaxTaps][2]);

// `[BIN]` q = fma(dfdy(p), oy, fma(dfdx(p), ox, p)). The derivatives are
// vectors, not scalars: the target jitters along `dfdx(p)` and `dfdy(p)`, which
// for a rotated or skewed transform are not axis aligned.
void displacementJitter(float px, float py, const float dpdx[2], const float dpdy[2],
                        float ox, float oy, float (&q)[2]);

// ---- the sampler ---------------------------------------------------------
//
// `[OBS]` The target samples through `@__air_sampler_state`, a single packed
// descriptor word `0x807bff0000080a49`. That word was NOT decoded: the filter
// mode, the address mode and the mip selection are unread. What IS read is that
// the shader clamps the UV to the layer's own rect (`m3`, `m4`) BEFORE every
// sample, so the address mode is dead inside the rect the layer declares.
//
// So the sampler is an explicit input here rather than hidden state: this
// bilinear, edge-clamped, normalised-coordinate sampler is what the gate runs on
// both sides, and replacing it when the descriptor is decoded touches nothing in
// the math above.
struct SampledImage {
    int width = 0;
    int height = 0;
    const float* rgba = nullptr;  // width * height * 4, row major
};

// Bilinear, normalised UV, half-texel centres, clamped at the edge. Written in
// the same `a + (b - a) * t` form the GLSL uses, so the two sides round
// identically rather than relying on whatever `mix` expands to.
void sampleBilinear(const SampledImage& image, float u, float v, float (&out)[4]);

// The whole stage. `dpdx` / `dpdy` are the screen-space derivatives of `p`;
// they are parameters because a compute shader has no fragment quad to take
// them from, and because making them an input is what lets a test prove that
// variant 0 does not read them.
void displacementMap(const DisplacementParams& params, std::uint32_t variant, float px,
                     float py, const float dpdx[2], const float dpdy[2],
                     const SampledImage& source, const SampledImage& map, float (&out)[4]);

}  // namespace rb
