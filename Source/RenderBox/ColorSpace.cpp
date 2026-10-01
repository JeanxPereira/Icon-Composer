#include "Source/RenderBox/ColorSpace.h"

#include <bit>
#include <cmath>
#include <cstdint>

namespace rb {
namespace {

// A `float` by its bit pattern. Every constant below is written this way and
// not as a decimal, because the decimal a person would type is not always the
// `float` the target holds -- the encode exponent is the standing example.
constexpr float bits(std::uint32_t pattern) { return std::bit_cast<float>(pattern); }

// `[BIN]` `RenderBox 0x76CB4`-`0x76D30`.
constexpr float kDecodeLinearScale = bits(0x3D9E8391);  // 1 / 12.92
constexpr float kDecodeScale = bits(0x3F72A76E);        // 1 / 1.055
constexpr float kDecodeOffset = bits(0x3D558919);       // 0.055 / 1.055
constexpr float kDecodeExponent = bits(0x4019999A);     // 2.4
constexpr float kDecodeKnee = bits(0x3D25AEE6);         // 0.04045

// `[BIN]` `RenderBox 0x76E10`-`0x76E90`.
constexpr float kEncodeLinearScale = bits(0x414EB852);  // 12.92
constexpr float kEncodeExponent = bits(0x3ED555C5);     // 0.41667
constexpr float kEncodeScale = bits(0x3F870A3D);        // 1.055
constexpr float kEncodeOffset = bits(0xBD6147AE);       // -0.055
constexpr float kEncodeKnee = bits(0x3B4D2E1C);         // 0.0031308

// `[BIN]` The matrices, COLUMN by column as `RB::ColorSpace::Matrix` stores
// them (`+0x00`, `+0x10`, `+0x20`, three `float`s each). The first two of each
// column are the low 64 bits of a 16-byte constant and the third is a
// `mov`/`movk` immediate written at `+8`.
//
// Key `0x12`, Display P3 -> sRGB, `RenderBox 0x760F8`:
//     0x15E7C0 + 0xBCA0C470 | 0x15E7D0 + 0xBDA0FE40 | 0x15E7E0 + 0x3F8C92F6
constexpr float kP3ToSrgb[3][3] = {
    {bits(0x3F9CCCE2), bits(0xBD2C68E1), bits(0xBCA0C470)},   //  1.22500253 -0.0420922078 -0.0196249187
    {bits(0xBE665FF6), bits(0x3F856294), bits(0xBDA0FE40)},   // -0.224975437 1.04207087   -0.0786099434
    {bits(0xB7E48000), bits(0x37B41400), bits(0x3F8C92F6)},   // -2.72393e-05 2.14670e-05   1.09823489
};

// Key `0x21`, sRGB -> Display P3, `RenderBox 0x761A8`:
//     0x15E7F0 + 0x3C8BDF25 | 0x15E800 + 0x3D94393B | 0x15E810 + 0x3F6919E0
constexpr float kSrgbToP3[3][3] = {
    {bits(0x3F528A86), bits(0x3D081183), bits(0x3C8BDF25)},   //  0.8224262    0.0332198255 0.017074177
    {bits(0x3E35D180), bits(0x3F778018), bits(0x3D94393B)},   //  0.177556992  0.966798306  0.0723747835
    {bits(0x378DC000), bits(0xB7979400), bits(0x3F6919E0)},   //  1.68979e-05 -1.80695e-05  0.910551071
};

// `[BIN]` `0x76DA8`: `fmul` by the first column, then two `fmla` -- so the two
// additions are fused, and written fused here.
void multiply(const float (&columns)[3][3], float (&rgba)[4]) {
    const float x = rgba[0];
    const float y = rgba[1];
    const float z = rgba[2];
    for (int row = 0; row < 3; ++row) {
        float acc = columns[0][row] * x;
        acc = std::fma(columns[1][row], y, acc);
        acc = std::fma(columns[2][row], z, acc);
        rgba[row] = acc;
    }
}

void convert(const float (&columns)[3][3], float (&rgba)[4]) {
    for (int k = 0; k < 3; ++k) rgba[k] = srgbDecode(rgba[k]);
    multiply(columns, rgba);
    for (int k = 0; k < 3; ++k) rgba[k] = srgbEncode(rgba[k]);
}

// The grey spaces carry (luminance, alpha) and the RGB spaces four components;
// `Values.h` leaves the widening to the renderer, and this is it.
void widen(const icf::Color& c, float (&rgba)[4]) {
    const bool grey = c.count < 3;
    rgba[0] = static_cast<float>(c.components[0]);
    rgba[1] = static_cast<float>(grey ? c.components[0] : c.components[1]);
    rgba[2] = static_cast<float>(grey ? c.components[0] : c.components[2]);
    rgba[3] = static_cast<float>(grey ? c.components[1] : c.components[3]);
}

}  // namespace

float srgbDecode(float encoded) {
    const float a = std::fabs(encoded);
    const float linear = a * kDecodeLinearScale;
    const float curve =
        std::exp2(std::log2(std::fma(a, kDecodeScale, kDecodeOffset)) * kDecodeExponent);
    // `fcmge knee, a`: a NaN fails the comparison and takes the curve, which
    // hands the NaN back. Written the same way round.
    const float magnitude = kDecodeKnee >= a ? linear : curve;
    return encoded < 0.0f ? -magnitude : magnitude;
}

float srgbEncode(float linear) {
    const float a = std::fabs(linear);
    const float straight = a * kEncodeLinearScale;
    const float curve =
        std::fma(kEncodeScale, std::exp2(std::log2(a) * kEncodeExponent), kEncodeOffset);
    const float magnitude = kEncodeKnee >= a ? straight : curve;
    return linear < 0.0f ? -magnitude : magnitude;
}

void displayP3ToSrgb(float (&rgba)[4]) { convert(kP3ToSrgb, rgba); }

void srgbToDisplayP3(float (&rgba)[4]) { convert(kSrgbToP3, rgba); }

void toDisplayP3(const icf::Color& c, float (&rgba)[4]) {
    widen(c, rgba);
    switch (c.space) {
        case icf::ColorSpace::DisplayP3:
            return;
        case icf::ColorSpace::SRGB:
        case icf::ColorSpace::ExtendedSRGB:
            srgbToDisplayP3(rgba);
            return;
        case icf::ColorSpace::Gray:
        case icf::ColorSpace::ExtendedGray:
            // `[BIN]` `0x10` to `0x12`: same transfer nibble, and a grey's
            // primaries are none -- the value is replicated and that is all.
            return;
    }
}

void toWorking(icf::ColorSpace space, float (&rgba)[4]) {
    switch (space) {
        case icf::ColorSpace::DisplayP3:
            displayP3ToSrgb(rgba);
            return;
        case icf::ColorSpace::SRGB:
        case icf::ColorSpace::ExtendedSRGB:
        case icf::ColorSpace::Gray:
        case icf::ColorSpace::ExtendedGray:
            return;
    }
}

void toWorking(const icf::Color& c, float (&rgba)[4]) {
    widen(c, rgba);
    toWorking(c.space, rgba);
}

}  // namespace rb
