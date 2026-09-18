#include "Source/RenderBox/DisplacementOracle.h"

#include <cmath>
#include <cstddef>

namespace rb {
namespace {

// `[BIN]` The jitter, read out of mod97 one `llvm.fmuladd.v2f32` splat at a
// time. Each tap is two chained fmuladds -- `%100` is `air.dfdx(p)` and `%101`
// is `air.dfdy(p)` -- so the pair below is (coefficient of dfdx, coefficient of
// dfdy) in that order, and the taps are in the order the IR emits them.
//
//   v&7 == 1, block %102:  (+0.25, +0.25) then (-0.25, -0.25)
//   v&7 == 2, block %150:  (-0.125, -0.375) (+0.375, -0.125)
//                          (-0.375, +0.125) (+0.125, +0.375)
//   v&7 >= 3, block %242 (the switch default, so 3 4 5 6 and 7 all land here):
//                          (+0.0625, -0.1875) (-0.0625, +0.1875)
//                          (+0.3125, +0.0625) (-0.1875, -0.3125)
//                          (-0.3125, +0.3125) (-0.4375, -0.0625)
//                          (+0.1875, +0.4375) (+0.4375, -0.4375)
//
// `[INF]` Those eight are 1/16-pixel units -- (1,-3) (-1,3) (5,1) (-3,-5)
// (-5,5) (-7,-1) (3,7) (7,-7) -- which is exactly the standard 8x MSAA sample
// pattern. The identification is inference; the sixteen literals are not.
//
// The values below are written as the exact binary fractions the IR carries, so
// every one of them is representable and none of them rounds.
constexpr float kTaps1[1][2] = {{0.0f, 0.0f}};
constexpr float kTaps2[2][2] = {{0.25f, 0.25f}, {-0.25f, -0.25f}};
constexpr float kTaps4[4][2] = {
    {-0.125f, -0.375f}, {0.375f, -0.125f}, {-0.375f, 0.125f}, {0.125f, 0.375f}};
constexpr float kTaps8[8][2] = {{0.0625f, -0.1875f},  {-0.0625f, 0.1875f},
                                {0.3125f, 0.0625f},   {-0.1875f, -0.3125f},
                                {-0.3125f, 0.3125f},  {-0.4375f, -0.0625f},
                                {0.1875f, 0.4375f},   {0.4375f, -0.4375f}};

// `[BIN]` `RB::shaderVariant` is a Metal function constant (`!air.function_
// constants`, `MTL_FC_INIT_1_j`) copied into a module global by the static
// initialiser. Only the low three bits reach this switch.
std::uint32_t variantCase(std::uint32_t variant) { return variant & 7u; }

// THE FUSED MULTIPLY-ADD, AND WHY IT IS NOT `std::fma`.
//
// The target's arithmetic is `air.fma` / `llvm.fmuladd`, so the transcription
// wants a single rounding. `std::fma(float, float, float)` on this toolchain
// does NOT give one: at s = 0.35, d = -0.25 it answers -0.525000036 where the
// exactly-representable product makes the correctly rounded result
// -0.524999976, which is what the GPU returns. That is a one-ULP error in the
// C library on a case with no rounding in the product at all, and every tap of
// this stage runs four of these -- so the differential would have carried a
// permanent few-ULP allowance that existed only to hide a broken `fmaf`.
//
// `[OBS]` The defect is the host toolchain's, not the target's, and it is not
// characterised further here.
//
// Computing in double instead: `a * b` is exact in double (24 + 24 bits fit in
// 53) and `c` is exact, so the only rounding left is the sum's. Where the exact
// sum needs more than 53 bits this is a double rounding rather than a single
// one, which can differ from a true fma -- but only when the double result lands
// on a float tie, and the gate measures what is left rather than assuming it is
// nothing.
float fma1(float a, float b, float c) {
    return static_cast<float>(static_cast<double>(a) * static_cast<double>(b) +
                              static_cast<double>(c));
}

float lerp(float a, float b, float t) {
    // The target's `air.mix` is `x + (y - x) * a`. The same factoring is used in
    // Displacement.glsl, so the two sides round the same way -- GLSL's `mix` is
    // free to expand as `x*(1-a) + y*a`, which does not.
    return a + (b - a) * t;
}

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

DisplacementParams parseDisplacementParams(const float* twentyOne) {
    DisplacementParams p;
    p.scale = twentyOne[0];
    for (int i = 0; i < 5; ++i) {
        p.source.m[i][0] = twentyOne[1 + i * 2 + 0];
        p.source.m[i][1] = twentyOne[1 + i * 2 + 1];
    }
    for (int i = 0; i < 5; ++i) {
        p.map.m[i][0] = twentyOne[11 + i * 2 + 0];
        p.map.m[i][1] = twentyOne[11 + i * 2 + 1];
    }
    return p;
}

void displacementLayerUV(const DisplacementLayer& layer, float x, float y, float (&uv)[2]) {
    for (int k = 0; k < 2; ++k) {
        // The inner fma is the `y` term. The target evaluates it first and feeds
        // it to the `x` one as the addend; writing it the other way round is a
        // different rounding, and this stage is gated bit for bit.
        const float inner = fma1(y, layer.m[1][k], layer.m[2][k]);
        const float t = fma1(x, layer.m[0][k], inner);
        // `[BIN]` `air.fast_clamp`, with the low bound first.
        uv[k] = t < layer.m[3][k] ? layer.m[3][k] : (t > layer.m[4][k] ? layer.m[4][k] : t);
    }
}

void displacementDecodeOffset(float scale, float dispX, float dispY, float (&offset)[2]) {
    const float twice = scale * 2.0f;
    const float bias = -scale;
    offset[0] = fma1(dispX, twice, bias);
    offset[1] = fma1(dispY, twice, bias);
}

int displacementTapCount(std::uint32_t variant) {
    switch (variantCase(variant)) {
        case 0: return 1;
        case 1: return 2;
        case 2: return 4;
        default: return 8;
    }
}

float displacementTapScale(std::uint32_t variant) {
    switch (variantCase(variant)) {
        case 0: return 1.0f;    // no final fmul at all on this path
        case 1: return 0.5f;    // 0xH3800
        case 2: return 0.25f;   // 0xH3400
        default: return 0.125f; // 0xH3000
    }
}

void displacementTapOffsets(std::uint32_t variant, float (&out)[kDisplacementMaxTaps][2]) {
    const int n = displacementTapCount(variant);
    const float (*table)[2] = n == 1 ? kTaps1 : (n == 2 ? kTaps2 : (n == 4 ? kTaps4 : kTaps8));
    for (int i = 0; i < n; ++i) {
        out[i][0] = table[i][0];
        out[i][1] = table[i][1];
    }
    for (int i = n; i < kDisplacementMaxTaps; ++i) {
        out[i][0] = 0.0f;
        out[i][1] = 0.0f;
    }
}

void displacementJitter(float px, float py, const float dpdx[2], const float dpdy[2], float ox,
                        float oy, float (&q)[2]) {
    // Written per lane because the target's two fmuladds are `<2 x float>` with
    // a SPLATTED coefficient: the same scalar `ox` multiplies both lanes of
    // `dfdx(p)`, and likewise `oy` both lanes of `dfdy(p)`.
    q[0] = fma1(dpdy[0], oy, fma1(dpdx[0], ox, px));
    q[1] = fma1(dpdy[1], oy, fma1(dpdx[1], ox, py));
}

void sampleBilinear(const SampledImage& image, float u, float v, float (&out)[4]) {
    if (image.width <= 0 || image.height <= 0 || image.rgba == nullptr) {
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        return;
    }
    const float fx = u * static_cast<float>(image.width) - 0.5f;
    const float fy = v * static_cast<float>(image.height) - 0.5f;
    const float bx = std::floor(fx);
    const float by = std::floor(fy);
    const float tx = fx - bx;
    const float ty = fy - by;
    const int x0 = clampi(static_cast<int>(bx), 0, image.width - 1);
    const int x1 = clampi(static_cast<int>(bx) + 1, 0, image.width - 1);
    const int y0 = clampi(static_cast<int>(by), 0, image.height - 1);
    const int y1 = clampi(static_cast<int>(by) + 1, 0, image.height - 1);
    // O indice sai da grade por SUBTRACAO INTEIRA da origem: os quatro texels
    // ja foram grampeados na grade acima, que e onde o render cheio grampeia.
    const int bw = image.bufferWidth ? image.bufferWidth : image.width;
    const int bh = image.bufferHeight ? image.bufferHeight : image.height;
    auto at = [&](int x, int y, int k) {
        // Uma leitura fora do buffer e MARGEM CURTA. Grampear aqui esconderia
        // isso; o grampo so evita ler fora da memoria, e o gate acusa a
        // diferenca colada na borda interna (spec 2026-09-16, "O invariante").
        const int lx = clampi(x - image.originX, 0, bw - 1);
        const int ly = clampi(y - image.originY, 0, bh - 1);
        return image.rgba[(static_cast<std::size_t>(ly) * bw + lx) * 4 + k];
    };
    for (int k = 0; k < 4; ++k) {
        const float c00 = at(x0, y0, k);
        const float c10 = at(x1, y0, k);
        const float c01 = at(x0, y1, k);
        const float c11 = at(x1, y1, k);
        out[k] = lerp(lerp(c00, c10, tx), lerp(c01, c11, tx), ty);
    }
}

void displacementMap(const DisplacementParams& params, std::uint32_t variant, float px, float py,
                     const float dpdx[2], const float dpdy[2], const SampledImage& source,
                     const SampledImage& map, float (&out)[4]) {
    const int taps = displacementTapCount(variant);
    float offsets[kDisplacementMaxTaps][2];
    displacementTapOffsets(variant, offsets);

    // `[BIN]` Variant 0 reaches its block through an `icmp eq` taken before the
    // switch, and `air.dfdx` / `air.dfdy` are called only on the other side of
    // that branch. Here the derivatives are arguments rather than an intrinsic,
    // so "does not take them" shows up as "does not read them", which is the
    // same observable fact and the one a test can hold.
    const float zero[2] = {0.0f, 0.0f};
    const float* ddx = taps == 1 ? zero : dpdx;
    const float* ddy = taps == 1 ? zero : dpdy;

    float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    for (int t = 0; t < taps; ++t) {
        float q[2];
        displacementJitter(px, py, ddx, ddy, offsets[t][0], offsets[t][1], q);

        float uvMap[2];
        displacementLayerUV(params.map, q[0], q[1], uvMap);
        float disp[4];
        sampleBilinear(map, uvMap[0], uvMap[1], disp);

        float offset[2];
        displacementDecodeOffset(params.scale, disp[0], disp[1], offset);

        // `[BIN]` `fadd(offset, q)` -- the displacement moves the point that was
        // already jittered, not the unjittered `p`.
        float uvSource[2];
        displacementLayerUV(params.source, offset[0] + q[0], offset[1] + q[1], uvSource);
        float colour[4];
        sampleBilinear(source, uvSource[0], uvSource[1], colour);

        // `[BIN]` `.z` of the DISPLACEMENT sample, splatted over all four
        // channels, and it is re-read at every tap: `%97`, `%125`, `%147`, ...
        // each shuffles the `zzzz` out of that tap's own sample. Hoisting it out
        // of the loop is the transcription error this weighting invites.
        const float w = disp[2];
        for (int k = 0; k < 4; ++k) {
            // Tap 0 is a plain `fmul` and the rest are `llvm.fmuladd.v4f16`;
            // starting from an accumulator of zero makes the first one an fma
            // too, which is the same value because `x*w + 0 == x*w`.
            acc[k] = t == 0 ? w * colour[k] : fma1(colour[k], w, acc[k]);
        }
    }

    const float scale = displacementTapScale(variant);
    for (int k = 0; k < 4; ++k) {
        // `[BIN]` Variant 0 has no trailing `fmul` in the IR; the scale is 1 and
        // multiplying by it is exact, so the one path stays.
        out[k] = acc[k] * scale;
    }
}

}  // namespace rb
