#include "Source/RenderBox/GlassTranslucency.h"

#include <algorithm>
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

OpacityMaskArguments opacityMaskArguments(const TranslucencyEffect& effect,
                                          IconSizeClass sizeClass,
                                          double materialTranslucency) {
    const double f = translucencyFactor(effect, sizeClass, materialTranslucency);

    OpacityMaskArguments args;
    args.borderWidth = static_cast<float>(effect.borderWidth);

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
    return args;
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
    for (std::uint32_t y = 0; y < field.height; ++y) {
        // ABSOLUTO: `bounds` chega na grade de `size` -- e o retangulo da arte
        // colocada no canvas --, nao na do buffer, entao a rampa tem de ser
        // medida no ponto `(y + origem) + 0.5` dessa mesma grade (spec
        // 2026-09-16, "O invariante que governa o desenho"). Com origem zero e
        // exatamente `y + 0.5`, a aritmetica de antes.
        const float py =
            static_cast<float>(static_cast<std::int64_t>(y) + field.originY) + 0.5f;
        for (std::uint32_t x = 0; x < field.width; ++x) {
            // `FieldSample::distance` is NEGATIVE INSIDE and the shader's `sd` is
            // positive inside.
            const float sd = -field.at(x, y)[0];
            const std::size_t i = static_cast<std::size_t>(y) * field.width + x;
            out.a[i] = simplifiedShapeAwareGradientMask(args, sd, py);
            out.coverage[i] = saturate(sd + 1.0f);
        }
    }
    return out;
}

void applyOpacityMask(std::vector<float>& rgba, const OpacityMask& mask) {
    const std::size_t n = std::min(mask.a.size(), rgba.size() / 4);
    for (std::size_t i = 0; i < n; ++i) rgba[i * 4 + 3] *= mask.a[i];
}

std::size_t opacityMaskMissedPixels(const std::vector<float>& rgba, const OpacityMask& mask,
                                    std::size_t& painted, float alphaFloor) {
    painted = 0;
    std::size_t missed = 0;
    const std::size_t n = std::min(mask.coverage.size(), rgba.size() / 4);
    for (std::size_t i = 0; i < n; ++i) {
        if (rgba[i * 4 + 3] <= alphaFloor) continue;
        ++painted;
        if (mask.coverage[i] == 0.0f) ++missed;
    }
    return missed;
}

}  // namespace rb
