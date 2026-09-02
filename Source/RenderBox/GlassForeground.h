#pragma once
// `glassForeground_v1` on the CPU -- the twin of `shaders/GlassForeground.glsl`.
//
// PRIMARY SOURCE. `[BIN]` `References/2.0-125/out/metallib-renderbox/
// default_mod99.ll`, 454 lines, `source_filename = "glassForeground_v1"`, read
// end to end. Every value number quoted below is that module's.
//
// WHAT THIS PASS IS. The background filter refracts what is BEHIND a layer; this
// one refracts the layer's OWN CONTENTS. `[BIN]` It samples exactly two
// textures: `t1` once, for the distance field, and `t0` seven times, along a
// ladder of chromatically dispersed offsets, through the SAME affine transform
// each time. There is no blur, no bleed, no shadow, no face matrix and no
// specular in this module -- the 22 non-trivial constants of `glassBackground_v1`
// are 6 here (doc 03 §28.5), and five of those six are the dispersion's.
//
// `[BIN]` THE ABI, from `!air.visible`:
//
//     half4 glassForeground_v1(float2 p, half4 src, constant float* params,
//                              device uchar* args, texture2d t0, t1, t2)
//
// `args` (arg 3) and `t2` (arg 6) carry `readnone captures(none)`.
//
// `src` IS NOT READ. `%1` appears in the signature and NOWHERE in the 280
// instructions of the body: the returned half4 is built out of the two textures
// and the uniforms alone. This was the trap `displacementMap_v1` set -- a
// transcription that blended `src` in would look reasonable and be wrong -- so it
// was checked rather than assumed, and the answer is the same here.
//
// `[BIN]` WHICH TEXTURE IS WHICH, read from the pointer each
// `air.sample_texture_2d` is handed and not from the naming. `%74` is given `%5`
// (arg 5, `t1`) with the UVs of the layer at `params + 28`: that is the FIELD,
// sampled once. `%187` and `%228` are given `%4` (arg 4, `t0`) with the UVs of
// the layer at `params + 18`: that is the layer's own CONTENTS, sampled seven
// times. Getting this backwards produces a picture, just not the target's.
//
// `[OBS]` WHO CALLS IT IS STILL OPEN. Spec §3.3 records that AquaKit never wired
// this pass into its compositor because which mask layer routes a layer's
// contents through it was never read, and nothing in mod99 answers that: a
// stitchable function names neither its caller nor its bindings. This file
// transcribes the function. It does not invent a caller.
//
// THE PRECISION THIS DOES NOT REPRODUCE. `[BIN]` mod99 does the band profile,
// the dispersion accumulation and the whole output assembly in HALF -- the
// accumulator is round-tripped through `half` at every one of the seven taps
// (`%200`, `%208`, `%242`, `%249`), and the alpha sum is a `fadd half`. This
// oracle and its GLSL twin are float32, which is what the RenderBox tower runs
// and what Vulkan gives without an extension, and it is the same choice
// `GlassOracle`, `DisplacementOracle` and `GradientOracle` all made. What is
// reproduced is the ARRANGEMENT; `[OBS]` the half rounding of the target is not.
// Bit-for-bit is demanded, and gated, only between this file and the GLSL.
//
// WHERE THE MULTIPLY-ADDS ARE FUSED AND WHERE THEY ARE NOT, and mod99 makes the
// rule for once instead of leaving it to taste. The module spells eight
// multiply-adds as `llvm.fmuladd` (which merely PERMITS fusion) and six as
// `air.fma` (which is a true fused multiply-add) -- and ALL SIX of the `air.fma`s
// are the two `RB::Layer` UV transforms (`%71`/`%72`, `%184`/`%185`,
// `%225`/`%226`). Nothing else in the module is fused. So:
//
//   * the UV transform is fused on both sides -- `fma()` in GLSL, and the
//     double-width `fma1` in the .cpp, which is the pairing `Displacement.glsl`
//     already proved bit-exact on this adapter for this exact expression;
//   * everything else is written `a * b + c`, unfused, on both sides, because
//     `[ART]` glslc does not decorate `OpExtInst Fma` with `NoContraction` even
//     under `precise` and this driver decomposes it (see `GlassOracle.h` for the
//     disassembly that found it), so a shader `fma()` is a request the GPU is
//     free to decline while `std::fma` on the CPU is not.
//
// `[OBS]` and the same hazard `GlassOracle.h` names: nothing in the C++ forbids
// a host compiler from contracting `a * b + c` itself. This build cannot -- MinGW
// x86-64 with no `-march` has no FMA to contract into -- but a build with
// `-mfma` would, and the fix that day is `-ffp-contract=off`, not a tolerance.
#include <cstdint>

#include "Source/RenderBox/DisplacementOracle.h"   // SampledImage, sampleBilinear

namespace rb {

// `[BIN]` mod99 `%90` -- `air.fast_fmax(air.fwidth.f32(d), 0x3F1A36E2E0000000)`.
// The printer widened a float to a double; the float is `0x38D1B717`, which is
// the nearest float to 1e-4 and is written as such here.
//
// THIS IS THE ONE CONSTANT THAT IS NOT SHARED WITH `GlassOracle`, and the reason
// it is not is the whole of why it gets its own name. `rb::glass::kEpsilon` is
// the half `0xH1419` (0.001) and it is what mod98 floors ITS `fwidth` with,
// because mod98 computes coverage in `f16`. mod99 computes it in `f32` and
// floors it a hundred times lower. Both epsilons appear in this module, four
// instructions apart, doing different jobs: `kEpsilon` gates the early-out and
// the per-tap divisor, this one gates the derivative. Carrying one where the
// other belongs moves the antialiasing floor by two orders of magnitude, and it
// is exactly the kind of substitution that still produces a picture.
inline constexpr float kForegroundFwidthFloor = 9.99999974737875163555145e-05f;

// `[BIN]` `struct RB::Layer { texture2d tex; float2 m[5]; }` -- the same five
// `float2` `displacementMap_v1` and `distanceGradient_v1` read, stored here by
// the two identical unrolled loops at `%12` and `%32`. An affine 2x3 UV
// transform and a clamp rect:
//
//     uv(v) = clamp(fma(v.xx, m0, fma(v.yy, m1, m2)), m3, m4)
struct ForegroundLayer {
    float m[5][2]{};
};

// `[BIN]` `RB::Shader::Glass::ForegroundUniforms`, and the name is not inferred:
// the TBAA node `!35` in mod99 spells `_ZTSN2RB6Shader5Glass18ForegroundUniformsE`
// and lists its members -- two aggregates at 0 and 8, then fourteen floats at
// 16, 20, ... 68. 72 bytes, 16 fields.
//
// THE NAMES BELOW WERE READ, NOT MATCHED BY SOUND. Doc 03 §27 is the method: the
// ARM64 packer that FILLS the struct is a different artefact from the IR that
// READS it, and a name only counts when the same byte is on both sides.
// `scripts/glassbridge.py --foreground` resolves the packer at `0x0E7D48`, and
// disassembling it directly settles the fourteen floats:
//
//     `[BIN]` 0x0E7D84  stp xzr, xzr, [sp, #0x58]     <- the struct base is 0x58
//     `[BIN]` 0x0E7DA0  str s8,  [sp, #0x68]   +16    inputRefractionAmount
//     `[BIN]` 0x0E7DD4  str s0,  [sp, #0x6c]   +20    1/inputRefractionHeight
//     `[BIN]` 0x0E7DF0  str s0,  [sp, #0x70]   +24    inputRefractionOffset
//     `[BIN]` 0x0E7E34  str s9,  [sp, #0x74]   +28    inputAberrationAmount
//     `[BIN]` 0x0E7E5C  str s0,  [sp, #0x78]   +32    1/inputAberrationHeight
//     `[BIN]` 0x0E7EC0  stur q0, [sp, #0x7c]   +36..  inputAberrationOffset,
//                                                     cos/sin(inputAberrationAngle),
//                                                     inputEdgeStart
//     `[BIN]` 0x0E7F44  stur q1, [sp, #0x8c]   +52..  inputEdgeEnd,
//                                                     inputEdgeOpacityStart,
//                                                     inputEdgeOpacityEnd,
//                                                     cos(inputRefractionAngle)
//     `[BIN]` 0x0E7E18  str s0,  [sp, #0x9c]   +68    sin(inputRefractionAngle)
//
// `[ART]` THE CONTROL, and it is what turns this from a plausible table into a
// reading. Five of these are the five the tool already resolved, and it printed
// them at bytes 0, 4, 8, 12, 16 -- sixteen low, because it was configured with a
// struct base of `0x68` rather than the `0x58` the zeroing `stp` names. Shifted
// to the real base, all five land where mod99 uses them for exactly what the key
// says: both `...Height` keys, and ONLY those two, carry the packer's guarded
// reciprocal (`fdiv`/`fcmp`/`fcsel`), and both land on precisely the two floats
// mod99 MULTIPLIES the distance by. Five of five, on a property the names could
// not have produced.
//
// `[ART]` AND A SECOND, INDEPENDENT ONE. AquaKit read QuartzCore's
// `GlassForegroundUniforms` from a different binary for a different project and
// recorded 13 named fields; bytes 16 through 71 of that block are field for
// field these fourteen floats, in this order (`GlassForeground.frag` lines
// 27..39). Two teams, two binaries, one layout.
//
// `[BIN]` BYTES 0..15 ARE NOT KEY-BOUND, and this is where RenderBox and
// QuartzCore genuinely part. The packer ZEROES them at `0x0E7D84` and then fills
// them at `0x0E7F74` from a 20-byte descriptor a call at `0x0E8CE4` writes
// (`ldur d0, [sp,#0x44]` / `ldur d1, [sp,#0x4c]` / `stp d0, d1, [sp,#0x58]`),
// whose pool default at `0x16193C` is `{1, 1.0f, 0.0f, 1.0f, 0.0f}` -- an
// identity decode.
//
// `[OBS]` WHAT THAT CALL COMPUTES IS NOT READ. `0x0E8CE4` was followed far
// enough to see WHERE its four floats land and what they default to, and no
// further: whether it derives them from the field texture's format, from the
// document's own extents, or from something else is unread. So these four
// arrive as inputs here, defaulting to the identity the pool carries, and the
// gate drives them off the identity rather than assuming it.
//
// mod99 uses them as a scale/bias on the field's distance
// channel and a scale/bias on its gradient. QuartzCore's block instead holds a
// 2x2 `displacement_mat` in those sixteen bytes, which mod99 does not have at
// all: the rotation mod99 applies is that matrix already folded to the identity.
// The divergence is named at `refractionDir` below.
struct ForegroundUniforms {
    // `[BIN]` `%78`/`%80` -> `%81`: `d = fma(field.x, distanceScale, distanceBias)`.
    // Bytes 0 and 4, the first `[2 x float]`.
    float distanceScale = 1.0f;
    float distanceBias = 0.0f;

    // `[BIN]` `%83`/`%85` -> `%100`: `g = fma(field.yz, gradientScale,
    // gradientBias)`, and BOTH are splatted -- one scalar over both lanes, not a
    // per-axis pair. Bytes 8 and 12, the second `[2 x float]`.
    float gradientScale = 1.0f;
    float gradientBias = 0.0f;

    // `[BIN]` bytes 16/20/24, read at `%122`/`%119`/`%115`.
    // `inputRefractionAmount` (default -150), `inputRefractionHeight` (default
    // 100) and `inputRefractionOffset` (default -10).
    float refractionAmount = 0.0f;
    float invRefractionHeight = 0.0f;
    float refractionOffset = 0.0f;

    // `[BIN]` bytes 28/32/36, read at `%157`/`%154`/`%150`.
    // `inputAberrationAmount` (default -15), `inputAberrationHeight` (default
    // 20) and `inputAberrationOffset` (default 0).
    float aberrationAmount = 0.0f;
    float invAberrationHeight = 0.0f;
    float aberrationOffset = 0.0f;

    // `[BIN]` bytes 40/44, read at `%137`/`%140`. The packer writes
    // `(cos, sin)` of `inputAberrationAngle` through a sincos call at
    // `0x0E7E8C`, so this is a unit direction and not an angle -- which is why
    // the shader has no trigonometry in it.
    float aberrationDir[2]{1.0f, 0.0f};

    // `[BIN]` bytes 48/52, read at `%278`/`%280`. `inputEdgeStart` (default
    // -4.5) and `inputEdgeEnd` (default -3).
    float edgeStart = 0.0f;
    float edgeEnd = 0.0f;

    // `[BIN]` bytes 56/60, read at `%274`/`%276`. `inputEdgeOpacityStart`
    // (default 0) and `inputEdgeOpacityEnd` (default 1), each clamped to [0, 1]
    // by the packer (`fcsel mi` then `fcsel gt`). They are what the range MIXES
    // between, and the result is SUBTRACTED FROM ONE (`%289`), so they are an
    // amount removed and not an opacity kept.
    float edgeOpacityStart = 0.0f;
    float edgeOpacityEnd = 0.0f;

    // `[BIN]` bytes 64/68, read at `%102`/`%105` -- `(cos, sin)` of
    // `inputRefractionAngle`, from the sincos at `0x0E7E0C`.
    //
    // DIVERGENCE FROM AQUAKIT, and it is the only structural one in the module.
    // QuartzCore rotates the ROWS of a `displacement_mat` by this direction and
    // then dots each rotated row with the gradient. mod99 dots the gradient
    // directly with `(cos, -sin)` and `(sin, cos)`, which is QuartzCore's
    // expression with that matrix equal to the identity: `RotateRow((1,0), r)`
    // is `(r.x, -r.y)` and `RotateRow((0,1), r)` is `(r.y, r.x)`. RenderBox
    // does not carry the matrix, and the sixteen bytes QuartzCore keeps it in
    // hold the field decode instead. RenderBox wins; the two agree everywhere
    // the matrix is the identity, which is everywhere RenderBox can express.
    float refractionDir[2]{1.0f, 0.0f};
};

// `[BIN]` The params buffer is 152 bytes -- 38 floats. The uniforms are floats
// 0..17; `%11` is `getelementptr float, ptr %2, i64 18` and `%30` is `i64 28`,
// which is what fixes 18..27 as the layer sampled through `t0` and 28..37 as the
// layer sampled through `t1`.
struct ForegroundParams {
    ForegroundUniforms uniforms;
    ForegroundLayer source;   // floats 18..27, texture t0 -- the layer's contents
    ForegroundLayer field;    // floats 28..37, texture t1 -- the distance field
};

// The 38 floats, in the target's order. Exposed so a test can pin the 152-byte
// layout instead of trusting the struct's member order.
ForegroundParams parseForegroundParams(const float* thirtyEight);

// `[BIN]` `uv(v) = clamp(fma(v.xx, m0, fma(v.yy, m1, m2)), m3, m4)`, with the
// `y` term as the INNER fma and `air.fast_clamp` outside it. Fused on both
// sides; the header says why this one and only this one.
void foregroundLayerUV(const ForegroundLayer& layer, float x, float y, float (&uv)[2]);

// `[BIN]` `%81`. The field's `.x` decoded into a distance.
float foregroundDistance(const ForegroundUniforms& u, float fieldX);

// `[BIN]` `%95`/`%100`. The field's `.yz` widened to float and decoded. One
// scalar scale and one scalar bias over both lanes.
void foregroundGradient(const ForegroundUniforms& u, float fieldY, float fieldZ,
                        float (&g)[2]);

// `[BIN]` `%86`/`%90`/`%91`/`%92`: `0.5 - d / max(fwidth(d), 1e-4)`, NOT
// saturated -- mod99 saturates it 170 instructions later, at `%258`, after the
// seven taps. It is returned raw so the gate can see the value the target
// actually carries between those two points.
//
// TWO THINGS HERE ARE NOT WHAT `glassBackground_v1` DOES, and both were checked
// rather than carried across:
//
//   * THE COVERAGE IS ANALYTIC, NOT THE POLYNOMIAL. `rbGlassEdgeCoverage` -- the
//     quartic minimax fit of `0.5 * erfc` that mod98 uses at three sites -- does
//     not appear in mod99 at all. This module derives its edge from `fwidth`.
//   * THE FLOOR IS `1e-4` IN FLOAT. mod98 floors `fwidth` with the half
//     `0xH1419` (0.001); mod99 calls `air.fwidth.f32` and floors it with
//     `0x3F1A36E2E0000000`, which is `float(1e-4)`, bit pattern `0x38D1B717`.
//     Different type, different value, and ten times finer. AquaKit's reading of
//     QuartzCore's foreground agrees: `max(fwidth(d), 1e-4)`.
//
// `fwidthD` is a parameter and not taken with a derivative, for the reason
// `DistanceField` and `DisplacementOracle` already give: a derivative exists
// only inside a fragment quad, and both this oracle and the compute probe that
// gates it live outside one.
float foregroundEdgeRaw(float d, float fwidthD);

// `[BIN]` `%107`..`%113`: `(dot(g, (cos, -sin)), dot(g, (sin, cos)))`, the
// gradient turned by `refractionDir`, then scaled by the band profile
// `rbGlassBandAmount(d, refractionAmount, invRefractionHeight,
// refractionOffset)` at `%135`.
void foregroundRefractionDisplacement(const ForegroundUniforms& u, float d, const float g[2],
                                      float (&out)[2]);

// `[BIN]` `%142`..`%148`, and the SAME rotation with its two components SWAPPED:
// `%143` is inserted at lane 1 and `%147` at lane 0, so the pair is
// `(dot(g, (sin, cos)), dot(g, (cos, -sin)))`. Written out, the 2x2 it applies
// is `[[sin, cos], [cos, -sin]]` -- determinant negative, a REFLECTION rather
// than a rotation.
//
// That looks like a transposition bug in the reading, so it was corroborated
// before it was carried: `[ART]` AquaKit read the same swap out of QuartzCore's
// `glass_foreground_base` from a different binary and wrote it down as such
// ("SAME form, components SWAPPED", `GlassForeground.frag` line 82), and records
// that the background filter's aberration carries it too. Two independent
// readings agree, so it is the target's and it stays.
//
// Then scaled by `rbGlassBandAmount(d, aberrationAmount, invAberrationHeight,
// aberrationOffset)` at `%170` -- the same profile, its own three uniforms.
void foregroundAberrationDisplacement(const ForegroundUniforms& u, float d, const float g[2],
                                      float (&out)[2]);

// `[BIN]` `%171`: `base = refractionDisplacement + p`. The refraction moves the
// point once; the dispersion swings about the point it moved to.
void foregroundBase(float px, float py, const float dispR[2], float (&base)[2]);

// `[BIN]` `%181` and `%222`: `q = fma(dispA, tapOffset, base)`, unfused here.
// `tapOffset` is `rb::glass::aberrationTap(i).offset` -- the ladder itself is
// `GlassOracle`'s, because mod98 and mod99 carry it byte for byte identically
// and it is already gated there.
//
// `[BIN]` The seven positions this produces are `base + dispA * s` for `s` in
// (1, 2/3, 1/3) from the first loop and (0, -1/3, -2/3, -1) from the second: an
// evenly spaced sweep from `+dispA` to `-dispA` with the layer's own contents
// read at each. Red is carried only by the first three, blue only by the last
// three, green by all seven -- which is the whole of the dispersion.
void foregroundTapPoint(const float base[2], const float dispA[2], float tapOffset,
                        float (&q)[2]);

// `[BIN]` `%258`..`%267`, in the target's order and grouping:
//
//     edge  = saturate(edgeRaw)                    // air.fast_saturate, %258
//     alpha = (edge * coverage) * (alphaSum / 7)   // %259, %261, %266
//     rgb   = (acc * kAberrationRgbScale) * alpha  // %264, %270
//
// WHY THIS IS NOT `rb::glass::aberrationCombine`, which is otherwise the same
// three lines. `[BIN]` mod99 computes `alphaSum * float(1/7)` at `%261`, then
// multiplies the coverage into it at `%266`, and only THEN premultiplies the rgb
// by it at `%270`. `aberrationCombine` premultiplies by the un-covered alpha,
// because that is what mod98 does at its own call site. The two differ by a
// factor of `edge * coverage` inside a multiply, so delegating would be wrong in
// the ordinary case, not just in the last bit. The two constants ARE the shared
// ones -- only the grouping is local.
//
// `coverage` is the field sample's `.w` WIDENED FROM HALF (`%173`) -- the raw
// value, not the floored one the taps divide by.
void foregroundResolve(const float acc[3], float alphaSum, float coverage, float edgeRaw,
                       float (&out)[4]);

// `[BIN]` `%281`..`%289`. The multiplier the whole result is scaled by:
//
//     span = edgeEnd - edgeStart
//     t    = saturate(fma(d, 1/span, -edgeStart/span))
//     out  = 1 - mix(edgeOpacityStart, edgeOpacityEnd, t)
//
// THE SECOND TERM IS A DIVISION, NOT A MULTIPLY BY THE RECIPROCAL. `%284` is
// `fdiv(fneg(edgeStart), span)`, a second `fdiv` beside `%282`'s. AquaKit writes
// `-edge_start * invSpan`, one division and one multiply, which is the same
// number to within a rounding and not the same rounding. mod99 wins.
//
// `air.mix(x, y, a)` is `x + (y - x) * a`, expanded here rather than delegated
// to a library `mix` whose expansion is not specified.
//
// `[OBS]` NO GUARD ON A ZERO SPAN. `edgeEnd == edgeStart` divides by zero, and
// mod99 has no `fcsel` around it the way the packer has around every height. Not
// a transcription choice -- there is nothing there to transcribe. The packer's
// defaults (-4.5 and -3) do not collide, and a document that sets them equal
// gets whatever the hardware's infinity does. Recorded, not repaired.
float foregroundEdgeFade(const ForegroundUniforms& u, float d);

// The whole stage, in the order mod99 runs it.
//
// `[BIN]` THE EARLY-OUT, `%88`: if the field sample's `.w` is below the epsilon
// the function returns half4(0) outright -- `%293`'s phi takes `zeroinitializer`
// from `%48`. The epsilon is the half `0xH1419`, the same literal
// `rb::glass::kEpsilon` carries, and NOT the `1e-4` that floors `fwidth`
// eighteen instructions later. Two different epsilons, four instructions apart,
// and mixing them is the mistake this comment exists to prevent.
//
// The two `SampledImage`s are `DisplacementOracle`'s, and so is the bilinear
// they are read with: `[OBS]` the target's `@__air_sampler_state`
// (`0x807BFF0000080A49`) is an undecoded packed descriptor, so the sampler is an
// explicit input on both sides of the gate rather than hidden state. What IS
// read is that both layers clamp their UV into their own rect before every
// sample, so the address mode is dead inside the rect the layer declares.
void glassForeground(const ForegroundParams& params, float px, float py, float fwidthD,
                     const SampledImage& source, const SampledImage& field, float (&out)[4]);

}  // namespace rb
