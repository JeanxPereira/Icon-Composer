#include "Source/RenderBox/BlendFormula.h"

#include <algorithm>

namespace rb {
namespace {

// `[BIN]` `default_mod52.ll`, block %272: `air.fmax.f16(dst.a, 0xH1D1F)`.
// `0xH1D1F` is the half nearest 0.005 -- the guard that keeps soft-light's
// division from blowing up on a transparent backdrop. It is written as the
// decimal the constant rounds from, not as a prettier 0.001 or 0.01.
constexpr double kSoftLightAlphaFloor = 0.005;

double sat(double v) { return std::clamp(v, 0.0, 1.0); }

// `[BIN]` The tail shared by cases 25-32, read from block %215 (%231..%238) and
// from `pdf_mode<true>` (%12..%19), which are the same three lines.
BlendColour composeWith(const BlendColour& src, const BlendColour& dst,
                        const double (&b)[3], const BlendOptions& options) {
    const double as = src.rgba[3], ab = dst.rgba[3];
    BlendColour out;
    out.rgba[3] = as + ab - as * ab;
    for (int k = 0; k < 3; ++k) {
        out.rgba[k] = src.rgba[k] * (1.0 - ab) + dst.rgba[k] * (1.0 - as) + b[k];
        // `[BIN]` The clamp runs when the bit is ON, and its upper bound is the
        // OUTPUT alpha, not 1.0.
        if (options.extendedColor) {
            out.rgba[k] = std::clamp(out.rgba[k], 0.0, out.rgba[3]);
        }
    }
    return out;
}

}  // namespace

bool blendIsTranscribed(BlendMode mode) {
    switch (mode) {
        case BlendMode::Normal:
        case BlendMode::Screen:
        case BlendMode::Multiply:
        case BlendMode::Overlay:
        case BlendMode::Darken:
        case BlendMode::Lighten:
        case BlendMode::SoftLight:
        case BlendMode::HardLight:
        case BlendMode::PlusLighter:
        case BlendMode::PlusDarker:
            return true;
        default:
            return false;
    }
}

BlendColour blend(BlendMode mode, const BlendColour& src, const BlendColour& dst,
                  const BlendOptions& options) {
    const double as = src.rgba[3], ab = dst.rgba[3];
    BlendColour out;

    switch (mode) {
        // `[BIN]` Case 2, `source_over`.
        case BlendMode::Normal: {
            for (int k = 0; k < 4; ++k) {
                out.rgba[k] = src.rgba[k] + dst.rgba[k] * (1.0 - as);
            }
            return out;
        }

        // `[BIN]` Case 12, block %110. The CHEAP band: four channels, no tail.
        case BlendMode::Screen: {
            for (int k = 0; k < 4; ++k) {
                out.rgba[k] = src.rgba[k] + dst.rgba[k] * (1.0 - src.rgba[k]);
            }
            return out;
        }

        // `[BIN]` Cases 43 and 44 SHARE a block (%532) and are told apart by
        // `icmp eq i32 %9, 44` inside it. The alpha saturates for both; what
        // 44 adds is the negative slack `saturate(as+ab) - (as+ab)`, pushed
        // into rgb -- which is why it is the darker of the two.
        case BlendMode::PlusLighter:
        case BlendMode::PlusDarker: {
            const double sum = as + ab;
            const double a = sat(sum);
            out.rgba[3] = a;
            const double slack = (mode == BlendMode::PlusDarker) ? (a - sum) : 0.0;
            for (int k = 0; k < 3; ++k) {
                out.rgba[k] = src.rgba[k] + dst.rgba[k] + slack;
            }
            return out;
        }

        default:
            break;
    }

    // The separable modes: a term `b`, then the shared tail.
    double b[3] = {0.0, 0.0, 0.0};
    for (int k = 0; k < 3; ++k) {
        const double s = src.rgba[k], d = dst.rgba[k];
        switch (mode) {
            // `[BIN]` Case 25, block %143.
            case BlendMode::Multiply:
                b[k] = s * d;
                break;
            // `[BIN]` Case 27, block %191: `fmin(as*dst, ab*src)`.
            case BlendMode::Darken:
                b[k] = std::min(as * d, ab * s);
                break;
            // `[BIN]` Case 28, block %215: the same with `fmax`.
            case BlendMode::Lighten:
                b[k] = std::max(as * d, ab * s);
                break;
            // `[BIN]` Case 26, block %155. Branches on the BACKDROP.
            case BlendMode::Overlay:
                b[k] = (d > 0.5 * ab) ? (2.0 * (s * ab + d * (as - s)) - as * ab)
                                      : (2.0 * s * d);
                break;
            // `[BIN]` Case 32, block %289. Same two arms, branching on the
            // SOURCE -- which is the only thing that separates it from overlay,
            // and the reason the two could not be told apart by formula alone.
            case BlendMode::HardLight:
                b[k] = (s > 0.5 * as) ? (as * ab - 2.0 * (ab - d) * (as - s))
                                      : (2.0 * s * d);
                break;
            // `[BIN]` Case 31, block %272. NOT the W3C formula: it is the low
            // branch applied unconditionally, with no square root and no cubic
            // anywhere in the module -- which the full W3C definition would
            // need. Doc 03 §26.4.
            case BlendMode::SoftLight: {
                const double floorA = std::max(ab, kSoftLightAlphaFloor);
                b[k] = 2.0 * d * s - (2.0 * s - as) * d * d / floorA;
                break;
            }
            default:
                // Not transcribed. `blendIsTranscribed` is how a caller asks
                // BEFORE getting here; reaching this point means it did not,
                // and source-over is the honest answer -- it is what the
                // renderer would have drawn anyway, and it is not silent
                // because the caller was given a way to know.
                for (int j = 0; j < 4; ++j) {
                    out.rgba[j] = src.rgba[j] + dst.rgba[j] * (1.0 - as);
                }
                return out;
        }
    }
    return composeWith(src, dst, b, options);
}

}  // namespace rb
