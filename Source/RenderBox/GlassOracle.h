#pragma once
// The shared glass math on the CPU -- the primitives `glassBackground_v1` and
// `glassForeground_v1` are both built from.
//
// PRIMARY SOURCE. `[BIN]` `References/2.0-125/out/metallib-renderbox/
// default_mod98.ll` (`glassBackground_v1`) and `default_mod99.ll`
// (`glassForeground_v1`). Every constant below carries the half bit pattern it
// was read from, so the number can be checked against the IR without trusting
// this file's decimal rendering.
//
// CORROBORATION. AquaKit transcribed the same functions from QuartzCore
// (`Source/QuartzCore/shaders/GlassCommon.glsl`,
// `include/QuartzCore/GlassShader.h`), and an IR differential already proved the
// two are the same source. Two independent readings of the same numbers is what
// makes these measurements rather than transcriptions of a transcription. Where
// they disagree RenderBox wins, because RenderBox is this project's target, and
// every disagreement is named at its site.
//
// THE PRECISION THIS DOES NOT REPRODUCE, AND WHY IT CANNOT.
// `[BIN]` mod98 and mod99 do this arithmetic in HALF: every literal is an
// `0xH....`, and the intermediates are `half`. This oracle and its GLSL twin are
// float32, because that is what the RenderBox tower runs and what Vulkan gives
// without an extension. So bit-for-bit agreement WITH THE TARGET is not on the
// table for any of these functions -- what is reproduced is the ARRANGEMENT (the
// operands, their order, the factoring, the guards) with each half literal
// carried at its exact decoded value. Bit-for-bit is demanded, and gated, only
// between this oracle and `shaders/Glass.glsl`.
//
// EVERY MULTIPLY-ADD IS UNFUSED, ON BOTH SIDES, AND THAT WAS MEASURED RATHER
// THAN CHOSEN. The IR spells its multiply-adds two ways -- `air.fma`, which is a
// true fused multiply-add, and `llvm.fmuladd`, which merely PERMITS fusion -- and
// the first version of this file transcribed the first kind with `std::fma` and
// GLSL's `fma()`. The differential then failed by one ulp in two stages, and the
// cause is a property of the toolchain worth writing down:
//
//   `[ART]` glslc emits `precise` as SPIR-V's `NoContraction`, and it decorates
//   `OpFMul` and `OpFAdd` -- but NOT `OpExtInst ... Fma`. Disassembling
//   `glass_probe.comp` shows the two `Fma` instructions carrying no decoration
//   at all. GLSL.std.450 lets an undecorated `Fma` be DECOMPOSED into `a*b + c`,
//   and this driver decomposes it, so `fma()` in a shader is a request the GPU
//   is free to decline while `std::fma` on the CPU is not.
//
// So both sides write `a * b + c` and get two roundings each. That is a legal
// reading of `llvm.fmuladd` outright; for `air.fma` it is a declared divergence
// in rounding, and a cheap one, because the target does this arithmetic in HALF
// and no float32 port reproduces its last bits regardless.
//
// `[OBS]` THE HAZARD THAT LEAVES. Nothing in the C++ forbids a compiler from
// contracting `a * b + c` itself. This build cannot -- MinGW x86-64 with no
// `-march` has no FMA instruction to contract into -- but a build on ARM, or one
// that turns on `-mfma`, would fuse on the CPU and not on the GPU, and this
// differential would start failing by an ulp for a reason that is not a
// transcription error. If that day comes the fix is `-ffp-contract=off`, not a
// tolerance.
#include <cstdint>

namespace rb::glass {

// ---------------------------------------------------------------- constants

// `[BIN]` mod98 `0xH1419`, and mod99 carries the same literal three times. It
// is the epsilon in every guarded divisor and every "is this worth drawing"
// comparison in both modules; `0xH068E` (1e-4) and `0x3EB0C6F7A0000000` (1e-6)
// appear NOWHERE in either.
//
// DIVERGENCE FROM AQUAKIT, and the first one the brief named: QuartzCore uses
// 1e-4 in the half variants and 1e-6 in the float ones. RenderBox uses neither.
// A port that carried AquaKit's number would put the antialiasing floor a
// hundred times lower than the target's, which is a visible edge, not a bit.
//
// The value carried is the EXACT half `0xH1419` decodes to. The source almost
// certainly wrote `0.001`; what the binary holds is the nearest half to it, and
// what was read is what this file states.
inline constexpr float kEpsilon = 0.00100040435791015625f;  // 0xH1419

// `[BIN]` mod98 `%285`/`%294`/`%342` -- the input mapping of the edge curve,
// with the shader's own immediates. Composed they are algebraically a clamp of
// `x` to [-2, 2], and they are NOT written that way here: `fma(x, 0.25, 0.5)`
// rounds, and `saturate(...) * 4 - 2` rounds again, so the simplification moves
// the last bits everywhere outside the clamp's dead zones.
inline constexpr float kEdgeInputScale = 0.25f;    // 0xH3400
inline constexpr float kEdgeInputBias = 0.5f;      // 0xH3800
inline constexpr float kEdgeDomainScale = 4.0f;    // 0xH4400
inline constexpr float kEdgeDomainBias = -2.0f;    // 0xHC000

// `[BIN]` mod98 `%289`..`%292`, and the same four appear again at `%298`..`%301`
// and `%346`..`%349` -- three call sites, one polynomial. Identical to AquaKit's
// `EdgeCoverageA..D`, read from a different binary.
inline constexpr float kEdgeC0 = 0.0029544830322265625f;  // 0xH1A0D
inline constexpr float kEdgeC1 = -0.034454345703125f;     // 0xHA869
inline constexpr float kEdgeC2 = 0.168212890625f;         // 0xH3162
inline constexpr float kEdgeC3 = -0.560546875f;           // 0xHB87C
inline constexpr float kEdgeBias = 0.5f;                  // 0xH3800

// `[BIN]` mod98 `%284`/`%293` -- the half nearest 1/sqrt(2), applied to the
// distance BEFORE the domain mapping. It is the caller's, not the curve's: the
// third call site (`%341`) has no such factor. Carrying it here is what lets a
// caller say "this distance is in sigma" in one place.
inline constexpr float kInvSqrt2 = 0.70703125f;  // 0xH39A8

// `[BIN]` mod98 `%878` and `%1126` -- the Rec. 709 luma triple the luminance
// compression uses, as three halves.
inline constexpr float kLumaCompress[3] = {
    0.212646484375f,     // 0xH32CE
    0.71533203125f,      // 0xH39B9
    0.07220458984375f,   // 0xH2C9F
};

// `[BIN]` mod98 `%1062` -- a SECOND Rec. 709 triple, one ulp of half below the
// first in red and in blue, used where the specular weighs a colour rather than
// where the compression measures one. Two roundings of the same three numbers
// live in the same function, and collapsing them into one constant would be a
// choice this reading has no basis for. Both are carried, both are gated.
inline constexpr float kLumaWeight[3] = {
    0.2125244140625f,      // 0xH32CD
    0.71533203125f,        // 0xH39B9  (the same half as above)
    0.07208251953125f,     // 0xH2C9D
};

// `[BIN]` mod98 `%886`/`%1134`. AquaKit reads the float variant's
// `0x3FD3333340000000` (0.3); mod98's is the half, and 0.300048828125 is what
// that half is.
inline constexpr float kChromaBoost = 0.300048828125f;  // 0xH34CD

// `[BIN]` mod98 `%656`/`%697` and mod99 `%213`/`%254`. The weight of the
// chromatic dispersion's taps is ACCUMULATED, one step per iteration, in float:
// `0xBFD5555560000000` and `0x3FD5555560000000` are float(-1/3) and float(1/3)
// widened to double by the printer. See `aberrationTap` for why the difference
// between accumulating and multiplying is not cosmetic.
inline constexpr float kAberrationStepDown = -0.3333333432674408f;
inline constexpr float kAberrationStepUp = 0.3333333432674408f;

// `[BIN]` mod98 `%702`, mod99 `%261` -- `0x3FC24924A0000000`, float(1/7), and
// the multiply is in float32 while everything around it is half.
inline constexpr float kAberrationAlphaScale = 0.14285714924335479736328125f;

// `[BIN]` mod98 `%705`, mod99 `%264` -- `<0xH3800, 0xH3555, 0xH3800>`. The green
// scale is the HALF nearest 1/3, not float(1/3), and it is green's normaliser
// because green is summed over all seven taps while red and blue are summed over
// four each.
//
// `[ART]` AND THAT HALF LEAVES GREEN SHORT. Red's and blue's weights sum to 2
// and their scale is exactly 1/2, so both normalise to one. Green's sum to 3 and
// `3 * 0.333251953125` is 0.999755859375, so the dispersion returns green
// **2.4414e-04 dark** against the other two. It is a colour cast, it is the
// target's, and it is not something to correct: float(1/3) here would be
// brighter than the target on every dispersed pixel. Gated in
// `Tests/test_rb_glass.cpp` as an equality with `1 - 3*half(1/3)`, so it cannot
// be tidied away by accident.
inline constexpr float kAberrationRgbScale[3] = {
    0.5f,                 // 0xH3800
    0.333251953125f,      // 0xH3555
    0.5f,                 // 0xH3800
};

// ------------------------------------------------------- 1. the edge curve

// `[BIN]` mod98 `%285`..`%287`. The input mapping, kept separate from the curve
// because the three call sites do not all reach it the same way.
float edgeDomain(float x);

// `[BIN]` mod98 `%288`..`%292`. The polynomial itself, on `u` ALREADY in
// [-2, 2]. The factoring is transcribed instruction for instruction and is not
// re-derived: `u2 = u*u`, then three `fma`s in `u2`, then one `fma` in `u`. Any
// other arrangement of Horner's form gives the same curve and different last
// bits, and the differential is bit-for-bit.
float edgePolynomial(float u);

// The two composed -- the form every call site in mod98 actually runs.
//
// WHAT IT APPROXIMATES. `edgeCoverage(u)` is a minimax fit of `0.5 * erfc(u)`,
// which is `1 - Phi(u * sqrt(2))`; with the `kInvSqrt2` pre-scale a caller gets
// `1 - Phi(z)` for a distance `z` measured in sigma.
//
// `[ART]` MEASURED maximum absolute error against the closed form:
// **3.9523e-03** at u = -1.8764 (400001 samples on [-2, 2] against `erfc`), and
// **3.9490e-03** against `1 - Phi(z)` through the pre-scale. That is the accuracy
// of the FIT, not of this transcription -- the same 4e-3 falls out of AquaKit's
// independently-read coefficients, from a different binary.
//
// `[ART]` AND IT IS NOT MONOTONE, which a function called a coverage invites you
// to assume. Its derivative turns positive at |u| = 1.9368: the curve dips to
// **-5.0563e-04** before climbing back to +2.4414e-04 (exactly 2^-12) at u = 2,
// and by symmetry it reaches **1.00051** on the other side. So this returns
// values slightly OUTSIDE [0, 1] near the ends of its domain. The target does not
// clamp them and neither does this: the two glass shaders multiply the value into
// colours, and a saturate would agree with the target everywhere except in the
// last three percent of the band -- which is exactly where an edge lives.
float edgeCoverage(float x);

// The `kInvSqrt2` pre-scale, named. `[BIN]` mod98 `%284` -> `%285`.
float edgeCoverageInSigma(float distanceInSigma);

// ---------------------------------------------------- 2. the height profile

// `[BIN]` The band profile, and it is the most repeated shape in the pair:
// **five sites in mod98** (`%324`..`%331`, `%483`..`%490`, `%594`..`%601`,
// `%780`..`%787`, `%963`..`%970`) and **two in mod99** (`%124`..`%131`,
// `%159`..`%166`), each with its own `(amount, invHeight)` pair of uniforms.
//
//     x    = saturate((-d - offset) * invHeight)
//     circ = saturate(sqrt(x * (2 - x)))
//     out  = fma(-circ, amount, amount)
//
// `x * (2 - x)` is `1 - (1 - x)^2` -- the quarter circle -- and the IR spells it
// as the product, so this does too.
//
// THE OFFSET. Three of the five mod98 sites and one of mod99's have none: they
// multiply `-d` straight by `invHeight`. Two have one, and both reach it by
// subtraction (`-(offset + d)` at `%317`, `-d - offset` at `%591`), which is why
// one function with `offset = 0` covers all seven rather than two functions.
//
// THE HEIGHT IS ALREADY A RECIPROCAL, and this is the detail doc 03 §27.2 found
// on the other side of the bridge: the ARM64 packer writes every `...Height` key
// as `1/height` with a compare-and-select that sends zero to ZERO. The shader
// never divides. Transcribing a division here would agree with the target
// everywhere except at height zero, where it would produce infinity instead of
// the constant band the target draws. See `reciprocalHeight`.
float bandAmount(float d, float amount, float invHeight, float offset);

// `[BIN]` doc 03 §27.2 -- the packer's guarded reciprocal, `fdiv`/`fcmp`/`fcsel`
// against zero, and the ONLY reason it lives in this file is that the guard is
// the thing a test has to be able to bite. Height zero gives zero.
float reciprocalHeight(float height);

// ------------------------------------------------- 3. luminance compression

// `[BIN]` mod98 `%877`..`%892`, and again at `%1125`..`%1140` -- one block, two
// call sites, identical instruction for instruction.
//
//     if (!(complement > 0)) return c;            // %876, the guard
//     Y  = dot(kLumaCompress, c)
//     t  = saturate(fma(-Y, complement, 1))
//     k  = fma(1 - t, kChromaBoost, 1)
//     out = mix(vec3(Y * t), c * t, vec3(k))
//
// `k` is at least 1 and grows as `t` falls, so the mix EXTRAPOLATES away from
// grey on purpose -- that is the chroma boost, and it is why the last argument
// is a vector the IR splats rather than a scalar.
//
// `[INF]` The IR's `air.dot.v3f16` does not fix the order the three products are
// summed in. This transcription fixes it as `(r + g) + b`, and its GLSL twin
// spells the same sum out by hand rather than calling `dot`, because a
// bit-for-bit differential cannot rest on an order neither language specifies.
void compressMaxLuma(const float rgb[3], float complement, float out[3]);

// ------------------------------------------------------ 4. the colour matrix

// `[BIN]` A 3x4 matrix of halves: `.rgb` are the coefficients over the input,
// `.bias` is the fourth half. mod98 loads three of these, at struct fields
// 17/18/19 (bytes 80..103), 20/21/22 and 23/24/25 -- the face, the specular and
// the outer lobe. Doc 03 §27.5 names those 76 bytes as the three matrices.
struct ColorMatrixRow {
    float r = 0.0f, g = 0.0f, b = 0.0f, bias = 0.0f;
};
struct ColorMatrix {
    ColorMatrixRow row[3];
};

// `[BIN]` mod98 `%906`/`%919`/`%932` then `%943` -- three dots and one vector
// add. No opacity: the specular at `%1075` mixes the result with a weight that
// is NOT the face opacity, so the raw form has to exist separately.
void faceColorMatrixRaw(const float rgb[3], const ColorMatrix& m, float out[3]);

// `[BIN]` mod98 `%946` -- the raw form mixed back over the input by
// `face_opacity`, transcribed as `air.mix` expands (`x + (y - x) * a`) rather
// than delegated to a library `mix` whose expansion is not specified.
void applyFaceColorMatrix(const float rgb[3], const ColorMatrix& m, float faceOpacity,
                          float out[3]);

// ----------------------------------------- 5. the YCC composite, CPU-only

// `[BIN]` `CA::ColorMatrix::set_ycc_composite(white, black, saturation, fill)`
// @ 0x18b14f774 in QuartzCore, transcribed by AquaKit
// (`GlassShader.h::MakeYCCCompositeMatrix`) together with the three constant
// blocks it reads: the RGB->YCbCr matrix @ 0x2406b38b0 / 0x18b3b3cb0 with its
// bias column, and the inverse @ 0x18b3b3d00 with the -0.5 folded in.
//
// THIS ONE IS CPU-ONLY AND HAS NO GLSL TWIN, and that is not an omission. It is
// what FILLS bytes +80..+151 of `BackgroundUniforms` -- doc 03 §27.5: `White`,
// `Black`, `Saturation` and `FillColor` are not written key by key, they go
// through this assembly and come out as the three matrices the shader reads. The
// shader knows only rows and biases.
//
// `[OBS]` WHAT IS NOT READ HERE. That RenderBox's ARM64 packer runs this same
// routine is doc 03 §27.5's reading of the 76 unbound bytes plus the fact that
// QuartzCore and RenderBox are the same source -- it is inference, not a store
// resolved to a key. The arithmetic below is `[BIN]` from QuartzCore; the claim
// that it is what fills RenderBox's bytes is `[INF]`, and a bridge task will
// have to settle it before this feeds a real pixel.
//
// `fill` is (r, g, b, a) and PREMULTIPLIED -- the routine's own convention.
inline constexpr float kYcc[3][3] = {
    {0.2126f, 0.7152f, 0.0722f},      // Y
    {-0.1146f, -0.3854f, 0.5000f},    // Cb
    {0.5000f, -0.4542f, -0.0458f},    // Cr
};
inline constexpr float kYccBias[3] = {0.0f, 0.5f, 0.5f};
inline constexpr float kYccInverse[3][3] = {
    {1.0f, 0.0f, 1.5748f},
    {1.0f, -0.187324f, -0.468124f},
    {1.0f, 1.8556f, 0.0f},
};
inline constexpr float kYccInverseBias[3] = {-0.7874f, 0.327724f, -0.9278f};

ColorMatrix makeYccCompositeMatrix(float white, float black, float saturation,
                                   const float fillPremultiplied[4]);

// --------------------------------------------- 6. the chromatic dispersion

// `[BIN]` mod98 `%618`..`%710`, mod99 `%175`..`%272` -- byte for byte the same
// two loops in both modules, which is the corroboration this piece gets for
// free: two entry points compiled separately agree on it.
//
// WHY THE SAMPLING IS NOT HERE. The dispersion reads a texture seven times, and
// a texture is exactly what an oracle cannot hold: the CPU side would need a
// sampler with the target's filtering, wrap and LOD selection, and the
// differential would then be measuring the sampler rather than the dispersion.
// What IS transcribed is everything the sampler does not decide -- WHERE each
// tap lands and WHAT weight it carries -- as pure functions of the tap index,
// plus the combine that turns seven accumulated samples into the returned
// colour. The caller supplies the seven fetches. That way every number this
// stage owns is gated, and nothing is gated that this stage does not own.
//
// THE CALLER'S CONTRACT, read off `%630`..`%655`:
//     acc = 0; alphaSum = 0;
//     for each tap t:
//         s  = sample(uv + t.offset * step)
//         a  = max(s.a, kEpsilon)                 // %633 -- the guarded divisor
//         acc += (s.rgb / a) * t.weight           // per component
//         alphaSum += s.a                         // the RAW alpha, NOT `a`
//     out = aberrationCombine(acc, alphaSum)
//
// The alpha sum taking the raw sample and not the floored one is `[BIN]` (`%654`
// adds `%632`, the value before the `fmax`) and it is the same correction
// AquaKit's file records having made in the other direction.
struct AberrationTap {
    float offset = 0.0f;   // multiplies the caller's `step`; sign included
    float weight[3]{0.0f, 0.0f, 0.0f};
};

inline constexpr int kAberrationTaps = 7;

// `[BIN]` Loop one is THREE iterations with the offset positive and the weight
// starting at 1, loop two is FOUR with the offset negated and the weight
// starting at 0. Red is carried only by the first loop, blue only by the second,
// green by both -- which is the whole of the dispersion: red pulled one way,
// blue the other, green held in the middle.
//
// THE WEIGHT IS ACCUMULATED, NOT INDEXED, and that is a divergence from AquaKit,
// which writes `1 - i * (1/3)`. `[BIN]` mod98 `%656` is `fadd float %620,
// -1/3` -- a running sum. The two disagree at the third tap: accumulating gives
// 0.33333329, multiplying gives 0.33333331. One float apart, and the rule is
// that the arrangement is part of what is reproduced.
AberrationTap aberrationTap(int index);

// `[BIN]` mod98 `%701`..`%710`, mod99 `%260`..`%272`:
//     alpha = alphaSum * float(1/7)
//     rgb   = acc * kAberrationRgbScale * alpha
//
// The second multiply is a DIVERGENCE from AquaKit, which returns the weighted
// sums un-premultiplied beside the alpha. RenderBox re-premultiplies by the
// alpha it just computed, in a separate multiply the IR keeps separate from the
// scale (`%705` then `%708`). RenderBox wins.
void aberrationCombine(const float acc[3], float alphaSum, float out[4]);

}  // namespace rb::glass
