#pragma once
// The backdrop mip pyramid that `glassBackground_v1` refracts.
//
// WHAT IS TRANSCRIBED HERE AND WHAT IS OURS
// -----------------------------------------
// Two things in this file are readings of the target, and everything else is
// this project's own construction. Saying which is which is the whole point of
// the seals, and getting it backwards here would be easy: a mip chain looks
// like the kind of thing a binary would specify, and it is not.
//
//   TRANSCRIBED  `[BIN]` the `RB::MultiLevelLayer` UV transform and clamp rect,
//                and the LOD formula that turns a blur radius into an explicit
//                level of detail. Both read out of
//                `References/2.0-125/out/metallib-renderbox/default_mod98.ll`.
//
//   OURS         the pyramid itself -- how a level is reduced to the next, what
//                happens to an odd dimension, and how a fractional LOD is
//                interpolated. **Apple's mip generation was never read.** The
//                target hands a `texture2d` that already HAS levels to a
//                sampler and asks for one; who built those levels, with which
//                kernel, is outside every module in `References`. So this is a
//                box filter of our choosing, declared as ours, and it is gated
//                against a closed-form oracle rather than against the target.
//
// PREMULTIPLIED, AND THIS ONE IS NOT A JUDGEMENT CALL
// ---------------------------------------------------
// `[BIN]` mod98 un-premultiplies every backdrop fetch, immediately after it:
//
//     %385 = <4 x half> sampled                       ; the LOD sample
//     %386 = extractelement <4 x half> %385, i64 3    ; alpha
//     %387 = air.fmax.f16(half %386, half 0xH1419)    ; the same 1e-3 floor
//     %390 = shufflevector %385 -> <3 x half>         ; rgb
//     %391 = fdiv <3 x half> %390, splat(%387)        ; rgb / alpha
//
// A divide by alpha AFTER the filter is only meaningful if what came out of the
// filter was premultiplied. So the texture the target samples is premultiplied,
// its mip levels are averages of premultiplied texels, and this pyramid is
// built the same way.
//
// It is also the only correct reduction independent of the target. Averaging
// STRAIGHT colour weights the colour of a nearly-transparent texel exactly as
// much as an opaque one, so a 50/50 mix of opaque red and transparent-anything
// takes half its colour from a texel that contributes no light. The right
// average is the premultiplied one, and un-premultiplying afterwards recovers
// the straight colour that the surviving alpha implies.
//
// This repository already paid for that lesson once, in the other direction:
// `IconRenderer::renderIcon` accumulated premultiplied and never un-multiplied,
// and every test stayed green while semi-transparent output came out too dark,
// because the tests looked at ALPHA and never at colour. The tests in
// `test_rb_mip.cpp` therefore assert the level-1 COLOUR of an image whose alpha
// varies across the footprint, where the two conventions disagree by 2x.
//
// The accumulator this eventually reads -- `acc` in `IconRenderer.cpp` -- is
// premultiplied at exactly the moment the glass would sample it, so it feeds
// this class with no conversion.
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Source/RenderBox/Buffer.h"
#include "Source/RenderBox/Device.h"

namespace rb {

// ---- the layer form, transcribed ----------------------------------------
//
// `[BIN]` `struct RB::MultiLevelLayer { texture2d tex; float2 m[6]; }`. The six
// float2 are loaded by the unrolled `i < 6` loop at the head of mod98 from
// `params` float index 64 -- byte offset 256, which is where §5.1 of the spec
// puts the `MultiLevelLayer` inside the 344-byte buffer. The plain `RB::Layer`
// that follows at byte 304 is the same thing with five.
//
// The first five are the affine UV transform and the clamp rect that every
// `RB::Layer` carries. The SIXTH is what makes it multi-level.
struct MultiLevelLayer {
    // m[0], m[1]: the two columns. m[2]: the translation.
    // m[3], m[4]: the clamp rect, low and high.
    // m[5]:      (LOD bias, LOD cap).
    //
    // The default is the identity over the unit square with no bias and a cap
    // that no chain this tower builds can reach, so a caller that only wants a
    // pyramid does not have to invent a transform.
    float m[6][2] = {{1, 0}, {0, 1}, {0, 0}, {0, 0}, {1, 1}, {0, 64}};
};

// `[BIN]` uv(v) = clamp(fma(v.xx, m0, fma(v.yy, m1, m2)), m3, m4).
//
// The `y` term is the INNER fma. That order is transcribed rather than
// simplified: mod98 emits `%377 = fma(v.yy, m1, m2)` and only then
// `%378 = fma(v.xx, m0, %377)`, and the two arrangements do not round alike.
// The clamp is `air.fast_clamp`, which is `clamp(x, lo, hi)` with no ordering
// guarantee for an inverted rect -- so an inverted rect is not defined here
// either, rather than being given a meaning the target does not have.
void layerUv(const MultiLevelLayer& layer, float x, float y, float (&uv)[2]);

// `[BIN]` The half nearest 1e-3, which is the literal mod98 actually carries:
// `0xH1419` is sign 0, exponent 5, mantissa 25, i.e. 2^-10 * (1 + 25/1024).
//
// It is spelled as the exact half rather than as `1e-3` because the target's
// value is the half and `1e-3f` is a different number. AquaKit reads a
// different epsilon on the QuartzCore side; RenderBox is the target.
inline constexpr float kBackdropRadiusFloor = 1.00040435791015625e-3f;

// The value of the nearest `half` to `v`, round-to-nearest-even, returned as a
// float. Exposed because it is load-bearing below, not as a utility.
//
// `mip_reduce.comp` spells the same rounding out in integer ops rather than
// calling `packHalf2x16`, and that is a measurement rather than a preference:
// written with the built-in, the differential failed on 56 of 90 LOD queries,
// every one by exactly one half ULP and every one toward zero. GLSL leaves that
// conversion's rounding implementation defined, so the built-in is not a
// definition of anything.
float quantizeToHalf(float v);

// `[BIN]` lod = min(log2(max(1e-3, radius)) - bias, cap), and the PRECISION of
// each step is part of the reading:
//
//     %371 = fptrunc float %370 to half               ; the radius narrows
//     %372 = air.fmax.f16(half 0xH1419, half %371)    ; the floor, in HALF
//     %373 = air.log2.f16(half %372)                  ; the log, in HALF
//     %374 = fpext half %373 to float                 ; and only now, float
//     %381 = fsub float %374, %380                    ; %380 = m5.x, the bias
//     %383 = air.fast_fmin.f32(float %381, float %382); %382 = m5.y, the cap
//     %384 = air.sample_texture_2d(..., i1 true, %383, 0.0, 0)  ; explicit LOD
//
// Fifteen sample sites in mod98 and every one of them is this shape. Two of
// them (`%372` and `%558`) put the constant on the LEFT of the `fmax` and the
// rest on the right; with `nnan` set that is the same function, which is why
// this is one function and not two.
//
// The half narrowing is reproduced rather than skipped because it QUANTISES THE
// LEVEL: `log2` at eleven bits of mantissa is what decides which pair of mips a
// radius lands between, and a float log2 would land between a different pair
// near a boundary. `[OBS]` What is NOT reproduced is the last-ULP behaviour of
// the target's half `log2` unit -- that was not measured. This computes log2 in
// float and rounds the result to half, which is correct for every radius whose
// log2 is exactly representable and is within one half ULP everywhere else.
//
// `radius == 0` is a real input, not a guard: the floor is what keeps it from
// producing -inf, and it is tested.
float backdropLod(float radius, float bias, float cap);

// The same, taking the bias and cap from the layer's sixth float2.
float backdropLod(const MultiLevelLayer& layer, float radius);

// `[BIN]` `rgb / max(alpha, 1e-3)`, the un-premultiply mod98 does after every
// fetch, with the SAME epsilon as the radius floor -- one literal serving as a
// divide guard and as a log2 guard.
//
// `[OBS]` The target does this in half (`fdiv <3 x half>`); this is float, the
// same choice `DisplacementOracle` made and for the same reason. Half rounding
// of a colour is a separate question from whether the arrangement is right, and
// unlike the LOD's half rounding it cannot change which texel is read.
void unpremultiplyBackdrop(const float (&rgba)[4], float (&rgb)[3]);

// ---- the pyramid, which is ours -----------------------------------------

// One level. `rgba` is PREMULTIPLIED, row major, four floats per texel.
struct MipLevel {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> rgba;
};

// The chain stops when both extents reach 1, and each extent halves rounding
// DOWN but never below 1. A 3x8 image therefore goes 3x8 -> 1x4 -> 1x2 -> 1x1:
// an axis that has bottomed out is carried, not resampled.
std::uint32_t nextMipExtent(std::uint32_t extent);
std::uint32_t mipLevelCount(std::uint32_t width, std::uint32_t height);

// A hard ceiling, because the GPU path declares its level table as a fixed
// array and a silent overrun is worse than a refusal. 16 levels is a 32768
// canvas; the largest this tower renders is 1024.
inline constexpr std::size_t kMaxMipLevels = 16;

// ---- what an odd dimension does, which is the interesting half ----------
//
// Halving an ODD extent cannot be a 2-tap box: three source texels have to
// become one destination texel somewhere, and a 2-tap kernel that steps by two
// simply DROPS the last one. That is the silent truncation this filter refuses
// to do -- a 1024-wide canvas reduced through an odd level would lose its right
// edge entirely by the bottom of the chain, and nothing in a mean-of-the-image
// test would notice.
//
// So an odd extent `2n+1` uses the standard three-tap box, which is what the
// two-tap one becomes when the destination grid no longer nests in the source:
//
//     taps    2i, 2i+1, 2i+2
//     weights (n-i)/(2n+1), n/(2n+1), (i+1)/(2n+1)
//
// Every source texel is covered, adjacent destination texels SHARE their
// boundary tap (which is what makes the filter continuous rather than blocky),
// and the weights sum to one. For 2n+1 == 3 it degenerates to a plain mean of
// three, which is exact and is what the gate checks.
//
// An even extent uses the 2-tap box with weights 1/2, and extent 1 is copied.
//
// Fills `offset` and `weight` for one destination index along one axis and
// returns the tap count, 1, 2 or 3. Exposed because the kernel IS the choice,
// and a test should be able to read the weights rather than infer them from a
// blurred image.
int mipReduceTaps(std::uint32_t srcExtent, std::uint32_t dstIndex, int (&offset)[3],
                  float (&weight)[3]);

// One level reduced to the next, on the CPU.
//
// The 2D kernel is the outer product of the two axes' taps, and the sum is
// formed as `ref + sum(w * (s - ref))` with `ref` the first tap rather than as
// `sum(w * s)`. The two are the same expression rearranged, and the rearranged
// one is EXACT FOR A CONSTANT IMAGE in floating point: every difference is
// exactly zero, so the accumulator is exactly zero and the result is exactly
// `ref`. Written the other way, `1/3 + 1/3 + 1/3` does not have to come back to
// one, and "a flat backdrop stays flat at every level" would hold to a
// tolerance instead of holding.
MipLevel reduceMipLevel(const MipLevel& source);

class MipPyramid {
public:
    // From a full-canvas PREMULTIPLIED RGBA float image, `width * height * 4`
    // floats, row major -- which is the layout and the convention of
    // `IconRenderer`'s accumulator at the moment a glass layer would read it.
    static MipPyramid build(const float* premultipliedRgba, std::uint32_t width,
                            std::uint32_t height);

    // The same chain, reduced by `shaders/mip_reduce.comp`, one dispatch per
    // level. The differential between this and `build` is the gate.
    static Result<MipPyramid> buildOnGpu(Device& device, const float* premultipliedRgba,
                                         std::uint32_t width, std::uint32_t height);

    bool empty() const { return levels_.empty(); }
    std::size_t levelCount() const { return levels_.size(); }
    const MipLevel& level(std::size_t index) const;
    const std::vector<MipLevel>& levels() const { return levels_; }

    // Bilinear inside one level. Normalised UV, half-texel centres, clamped at
    // the edge.
    //
    // `[OBS]` The target samples through `@__air_sampler_state.13`, and the
    // descriptor word was NOT decoded -- filter mode, address mode and mip
    // filter are all unread. Two sampler constants exist in mod98 differing in
    // bit `0x4000`, and the mip-enabled one is the one every explicit-LOD
    // sample uses; which bit means what is unread. What IS read is that the
    // shader clamps UV into the layer's own rect before every sample, so the
    // address mode is dead inside the rect the layer declares. Bilinear and
    // edge-clamped is therefore ours, stated, and replaceable.
    void sampleLevel(std::size_t index, float u, float v, float (&out)[4]) const;

    // Trilinear at an explicit, possibly fractional LOD: bilinear in the two
    // bracketing levels, then linear between them. `lod` is clamped into
    // [0, levelCount-1], so an integral LOD is exactly that level's bilinear
    // sample and nothing between the levels leaks into it.
    void sample(float lod, float u, float v, float (&out)[4]) const;

private:
    std::vector<MipLevel> levels_;
};

// ---- the GPU side of the differential ------------------------------------
//
// `shaders/mip_reduce.comp` carries three stages, and these run them. They live
// here rather than in the test because the buffer layout is the shader's
// contract, and a contract written down twice is a contract that drifts.

// Stage 1: the LOD formula, one `(radius, bias, cap)` triple per invocation.
Result<std::vector<float>> backdropLodOnGpu(Device& device,
                                            const std::vector<std::array<float, 3>>& queries);

// Stage 2: the trilinear sample, one `(u, v, lod)` per invocation, against a
// pyramid packed into the input buffer.
Result<std::vector<std::array<float, 4>>> sampleOnGpu(
    Device& device, const MipPyramid& pyramid,
    const std::vector<std::array<float, 3>>& queries);

}  // namespace rb
