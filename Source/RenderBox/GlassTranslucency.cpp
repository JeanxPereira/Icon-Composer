#include "Source/RenderBox/GlassTranslucency.h"

#include "Source/RenderBox/GradientOracle.h"
#include "Source/RenderBox/Parallel.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace rb {
namespace {

float saturate(float x) { return std::clamp(x, 0.0f, 1.0f); }

// `x + (y - x) * t`, which is what `air.mix` expands to and what the IR at
// `default_mod4.ll` performs. Written once so the three uses cannot drift.
float mixf(float x, float y, float t) { return x + (y - x) * t; }

}  // namespace

double sizeBasedValue(const SizeBasedValue& v, IconSizeClass sizeClass) {
    // THE INVERSION. See the header: the declaration order is
    // `display, large, medium, small` and the enum runs `small .. display`, so
    // the index is mirrored. Writing `slots[sizeClass]` here would be a bug that
    // no test of today can see, because all four read defaults are 1.0.
    const int i = 3 - static_cast<int>(sizeClass);
    return v.slots[i];
}

double translucencyFactor(const TranslucencyEffect& effect, IconSizeClass sizeClass,
                          double materialTranslucency) {
    return sizeBasedValue(effect.strength, sizeClass) * materialTranslucency;
}

double effectiveOpacity(double x, double f) {
    // `fsub`, `fmul`, `fsub` -- in that order, not folded into `1 - f + x*f`.
    const double oneMinusX = 1.0 - x;
    return 1.0 - oneMinusX * f;
}

bool translucencyDrawsMask(double materialTranslucency) {
    // `[BIN]` `b.le` at `0x4AD10`. After an `fcmp`, `le` is also taken when the
    // operands are unordered, so a NaN skips the mask -- which is `x > 0`.
    return materialTranslucency > 0.0;
}

OpacityMaskArguments opacityMaskArguments(const TranslucencyEffect& effect,
                                          IconSizeClass sizeClass,
                                          double materialTranslucency,
                                          double texelsPerPoint) {
    const double f = translucencyFactor(effect, sizeClass, materialTranslucency);

    OpacityMaskArguments args;
    // `[BIN]` `0x00010048`-`0x00010054`: the product in double, narrowed once.
    args.borderWidth = static_cast<float>(effect.borderWidth * texelsPerPoint);

    // Index 1, `0x10078`-`0x10088`: the LOWER goes through `eff`, the upper does
    // not.
    args.opacityBounds[0] = static_cast<float>(effectiveOpacity(effect.lowerOpacity, f));
    args.opacityBounds[1] = static_cast<float>(effect.upperOpacity);

    // Index 2, the `fcsel` at `0x0000FF5C`-`0x0000FF70`. At `borderWidth == 0`
    // the pair is a COPY of index 1; otherwise it is the contour pair, whose
    // UPPER is the one that goes through `eff` -- the mirror image of index 1,
    // and the asymmetry the header names.
    if (effect.borderWidth == 0.0) {
        args.contourOpacityBounds[0] = args.opacityBounds[0];
        args.contourOpacityBounds[1] = args.opacityBounds[1];
    } else {
        args.contourOpacityBounds[0] = static_cast<float>(effect.lowerContourOpacity);
        args.contourOpacityBounds[1] =
            static_cast<float>(effectiveOpacity(effect.upperContourOpacity, f));
    }

    // `[BIN]` `cbz w8, #0xfea8` at `0x0000FE00`: `useSimpleMask` takes the
    // gradient, and everything above this line except the body pair is then
    // unread.
    if (effect.useSimpleMask) {
        args.kind = OpacityMaskKind::Gradient;
        const double lowerEff = effectiveOpacity(effect.lowerOpacity, f);
        const double upper = effect.upperOpacity;

        // The stops' alphas, in double as the target computes them, narrowed
        // where it narrows them (`fcvtn`, `0x000105F0`).
        float alpha[kOpacityMaskRampSegments + 1];
        std::uint32_t stops = 0;
        if (effect.replicateBadSmoothing) {
            // `[BIN]` `0xD28C`: `x = k * 0.0625` (`fmadd`, `0xD38C`), then
            // `0xD3A4`-`0xD3B8` in this order.
            stops = kOpacityMaskRampSegments + 1;
            const double span = lowerEff - upper;
            for (std::uint32_t k = 0; k < stops; ++k) {
                const double x = static_cast<double>(k) * 0.0625;
                const double a = upper + span * x;
                alpha[k] = static_cast<float>((a * a) * (3.0 - (a + a)));
            }
        } else {
            // `[BIN]` `0x00010484`-`0x000104A8`.
            stops = 2;
            alpha[0] = static_cast<float>(upper);
            alpha[1] = static_cast<float>(lowerEff);
        }

        // `[BIN]` Interpolation code 4: one Fritsch-Carlson cubic per segment,
        // from the four stops around it, the two ends duplicated under `pad`
        // (`GradientOracle.h`). Black stops: only the alpha channel carries
        // anything.
        args.rampSegments = stops - 1;
        for (std::uint32_t s = 0; s + 1 < stops; ++s) {
            auto stop = [&alpha, stops](std::int64_t i, float (&out)[4]) {
                const std::int64_t last = static_cast<std::int64_t>(stops) - 1;
                const std::int64_t at = i < 0 ? 0 : (i > last ? last : i);
                out[0] = out[1] = out[2] = 0.0f;
                out[3] = alpha[at];
            };
            float p0[4], p1[4], p2[4], p3[4];
            const std::int64_t i = static_cast<std::int64_t>(s);
            stop(i - 1, p0);
            stop(i, p1);
            stop(i + 1, p2);
            stop(i + 2, p3);
            const CubicColor cc = smoothColorCoefficients(p0, p1, p2, p3);
            for (int k = 0; k < 4; ++k) args.ramp[s][k] = cc.c[k][3];
        }
    }
    return args;
}

float gradientOpacityMask(const OpacityMaskArguments& args, float py) {
    if (args.rampSegments == 0) return 1.0f;
    // The parameter along the axis: 0 at `minY`, 1 at `maxY`, held past both.
    const float height = args.bounds[3];
    const float v = height != 0.0f ? saturate((py - args.bounds[1]) / height) : 0.0f;

    // `[BIN]` Ramp kind 3: `t` scaled by `stops - 1`, the integer part the
    // segment and the fraction the cubic's argument (`rampSmoothAtPositions`).
    const float scaled = v * static_cast<float>(args.rampSegments);
    std::uint32_t seg = static_cast<std::uint32_t>(scaled);
    if (seg >= args.rampSegments) seg = args.rampSegments - 1;
    const float f = scaled - static_cast<float>(seg);
    const float* c = args.ramp[seg];
    return c[0] + f * (c[1] + f * (c[2] + f * c[3]));
}

bool opacityMaskIsIdentity(const OpacityMaskArguments& args) {
    return args.opacityBounds[0] == 1.0f && args.opacityBounds[1] == 1.0f &&
           args.contourOpacityBounds[0] == 1.0f && args.contourOpacityBounds[1] == 1.0f;
}

float simplifiedShapeAwareGradientMask(const OpacityMaskArguments& args, float sd, float py) {
    // The vertical parameter inside `bounds`: 0 at the top edge, 1 at the
    // bottom. A zero-height rect has no ramp to run, and 0 there is the top of
    // it rather than an invented middle.
    const float height = args.bounds[3];
    const float v = height != 0.0f ? saturate((py - args.bounds[1]) / height) : 0.0f;

    const float body = mixf(args.opacityBounds[1], args.opacityBounds[0], v);
    const float contour =
        mixf(args.contourOpacityBounds[1], args.contourOpacityBounds[0], v);

    // `u = saturate(sd / borderWidth)`, except at `borderWidth == 0`, where the
    // two ends of the interpolation are equal and `u` would be a division by
    // zero. See the header for why that branch is taken rather than computed.
    float a = body;
    if (args.borderWidth != 0.0f) {
        const float u = saturate(sd / args.borderWidth);
        a = mixf(contour, body, u);
    }

    a = saturate(a);
    a = a * a * (3.0f - 2.0f * a);   // the smoothstep polynomial, as written

    const float cov = saturate(sd + 1.0f);
    return mixf(1.0f, a, cov);
}

OpacityMask glassOpacityMask(const FieldImage& field, const OpacityMaskArguments& args) {
    OpacityMask out;
    out.width = field.width;
    out.height = field.height;
    const std::size_t n = static_cast<std::size_t>(field.width) * field.height;
    out.a.assign(n, 1.0f);
    out.coverage.assign(n, 0.0f);
    // One row per worker: each pixel is its own field texel and its own row.
    parallelRanges(field.height, n * 24, [&](std::size_t y0, std::size_t y1) {
    for (std::uint32_t y = static_cast<std::uint32_t>(y0); y < static_cast<std::uint32_t>(y1); ++y) {
        // ABSOLUTO: `bounds` chega na grade de `size` -- e o retangulo da arte
        // colocada no canvas --, nao na do buffer, entao a rampa tem de ser
        // medida no ponto `(y + origem) + 0.5` dessa mesma grade (spec
        // 2026-09-16, "O invariante que governa o desenho"). Com origem zero e
        // exatamente `y + 0.5`, a aritmetica de antes.
        const float py =
            static_cast<float>(static_cast<std::int64_t>(y) + field.originY) + 0.5f;
        if (args.kind == OpacityMaskKind::Gradient) {
            // The gradient is laid on an infinite shape: the same value across
            // the row, and nothing for the field to say about it.
            const float m = gradientOpacityMask(args, py);
            for (std::uint32_t x = 0; x < field.width; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * field.width + x;
                out.a[i] = m;
                out.coverage[i] = 1.0f;
            }
            continue;
        }
        for (std::uint32_t x = 0; x < field.width; ++x) {
            // `FieldSample::distance` is NEGATIVE INSIDE and the shader's `sd` is
            // positive inside.
            const float sd = -field.at(x, y)[0];
            const std::size_t i = static_cast<std::size_t>(y) * field.width + x;
            out.a[i] = simplifiedShapeAwareGradientMask(args, sd, py);
            out.coverage[i] = saturate(sd + 1.0f);
        }
    }
    });
    return out;
}

void applyOpacityMask(std::vector<float>& rgba, const OpacityMask& mask) {
    const std::size_t n = std::min(mask.a.size(), rgba.size() / 4);
    parallelRanges(n, n * 2, [&](std::size_t i0, std::size_t i1) {
        for (std::size_t i = i0; i < i1; ++i) rgba[i * 4 + 3] *= mask.a[i];
    });
}

std::size_t opacityMaskMissedPixels(const std::vector<float>& rgba, const OpacityMask& mask,
                                    std::size_t& painted, float alphaFloor) {
    // Counted per range and added up after: these are integer counts, so the
    // order of the additions cannot change the total.
    const std::size_t n = std::min(mask.coverage.size(), rgba.size() / 4);
    std::atomic<std::size_t> paintedSum{0};
    std::atomic<std::size_t> missedSum{0};
    parallelRanges(n, n * 2, [&](std::size_t i0, std::size_t i1) {
        std::size_t p = 0;
        std::size_t m = 0;
        for (std::size_t i = i0; i < i1; ++i) {
            if (rgba[i * 4 + 3] <= alphaFloor) continue;
            ++p;
            if (mask.coverage[i] == 0.0f) ++m;
        }
        paintedSum += p;
        missedSum += m;
    });
    painted = paintedSum.load();
    return missedSum.load();
}

}  // namespace rb
