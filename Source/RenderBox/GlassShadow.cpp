#include "Source/RenderBox/GlassShadow.h"

#include <algorithm>
#include <cmath>

namespace rb {
namespace {

// A separable Gaussian over PREMULTIPLIED values with CLAMPED edges.
//
// This is deliberately a second copy of the one in `SvgFilter.cpp` and not a
// shared one. That function is an implementation detail of an SVG `<filter>`
// chain, in that file's anonymous namespace; lifting it into a public header to
// serve a glass shadow would couple two towers that have nothing to do with each
// other, and this front's brief is to touch as little as possible. The two are
// the same arithmetic on purpose -- see `GlassShadow.h` on why the edge rule and
// the premultiplication are not free choices.
std::vector<float> gaussian(const std::vector<float>& src, std::uint32_t w, std::uint32_t h,
                            double sigma) {
    if (sigma <= 0.0 || w == 0 || h == 0) return src;
    const std::size_t texels = static_cast<std::size_t>(w) * h;
    if (src.size() < texels * 4) return src;

    std::vector<float> pre(texels * 4);
    for (std::size_t t = 0; t < texels; ++t) {
        const float a = src[t * 4 + 3];
        for (int c = 0; c < 3; ++c) pre[t * 4 + c] = src[t * 4 + c] * a;
        pre[t * 4 + 3] = a;
    }

    const int radius = static_cast<int>(std::ceil(sigma * 3.0));
    std::vector<double> kernel(static_cast<std::size_t>(radius) * 2 + 1);
    double sum = 0.0;
    for (int i = -radius; i <= radius; ++i) {
        const double v = std::exp(-(static_cast<double>(i) * i) / (2.0 * sigma * sigma));
        kernel[static_cast<std::size_t>(i + radius)] = v;
        sum += v;
    }
    for (double& k : kernel) k /= sum;

    std::vector<float> tmp(texels * 4, 0.0f);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            double acc[4] = {0, 0, 0, 0};
            for (int i = -radius; i <= radius; ++i) {
                const int sx = std::clamp(static_cast<int>(x) + i, 0, static_cast<int>(w) - 1);
                const double k = kernel[static_cast<std::size_t>(i + radius)];
                const float* p = &pre[(static_cast<std::size_t>(y) * w + sx) * 4];
                for (int c = 0; c < 4; ++c) acc[c] += p[c] * k;
            }
            float* o = &tmp[(static_cast<std::size_t>(y) * w + x) * 4];
            for (int c = 0; c < 4; ++c) o[c] = static_cast<float>(acc[c]);
        }
    }

    std::vector<float> out(texels * 4, 0.0f);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            double acc[4] = {0, 0, 0, 0};
            for (int i = -radius; i <= radius; ++i) {
                const int sy = std::clamp(static_cast<int>(y) + i, 0, static_cast<int>(h) - 1);
                const double k = kernel[static_cast<std::size_t>(i + radius)];
                const float* p = &tmp[(static_cast<std::size_t>(sy) * w + x) * 4];
                for (int c = 0; c < 4; ++c) acc[c] += p[c] * k;
            }
            float* o = &out[(static_cast<std::size_t>(y) * w + x) * 4];
            const double a = acc[3];
            o[3] = static_cast<float>(a);
            for (int c = 0; c < 3; ++c) o[c] = a > 0.0 ? static_cast<float>(acc[c] / a) : 0.0f;
        }
    }
    return out;
}

// The translation of step 2, by a possibly fractional number of pixels.
//
// Bilinear, and premultiplied for the same reason the blur is. `[INF]` The
// target translates a DISPLAY LIST -- a resolution-independent object that gets
// rasterised once, afterwards -- so it has no resampling to do at all, and this
// resampling is an artefact of working on a raster. It is named here rather than
// passed off as a transcription. It costs nothing whenever the offset lands on
// whole pixels, which `s * 32` does at every power-of-two target size.
std::vector<float> translate(const std::vector<float>& src, std::uint32_t w, std::uint32_t h,
                             double dx, double dy) {
    const std::size_t texels = static_cast<std::size_t>(w) * h;
    std::vector<float> out(texels * 4, 0.0f);
    if (src.size() < texels * 4) return out;
    if (dx == 0.0 && dy == 0.0) return src;

    std::vector<float> pre(texels * 4);
    for (std::size_t t = 0; t < texels; ++t) {
        const float a = src[t * 4 + 3];
        for (int c = 0; c < 3; ++c) pre[t * 4 + c] = src[t * 4 + c] * a;
        pre[t * 4 + 3] = a;
    }

    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            // Where this destination pixel came FROM.
            const double sxf = static_cast<double>(x) - dx;
            const double syf = static_cast<double>(y) - dy;
            const double fx = std::floor(sxf), fy = std::floor(syf);
            const double tx = sxf - fx, ty = syf - fy;
            double acc[4] = {0, 0, 0, 0};
            for (int j = 0; j < 2; ++j) {
                for (int i = 0; i < 2; ++i) {
                    const double px = fx + i, py = fy + j;
                    // OUTSIDE IS TRANSPARENT here, and it is not the blur's
                    // clamp: this is the shadow sliding INTO the frame, so the
                    // area it vacates has nothing in it. Clamping would smear
                    // the art's edge row across the gap.
                    if (px < 0 || py < 0 || px >= w || py >= h) continue;
                    const double wgt = (i ? tx : 1.0 - tx) * (j ? ty : 1.0 - ty);
                    if (wgt <= 0.0) continue;
                    const float* p = &pre[(static_cast<std::size_t>(py) * w +
                                           static_cast<std::size_t>(px)) * 4];
                    for (int c = 0; c < 4; ++c) acc[c] += p[c] * wgt;
                }
            }
            float* o = &out[(static_cast<std::size_t>(y) * w + x) * 4];
            const double a = acc[3];
            o[3] = static_cast<float>(a);
            for (int c = 0; c < 3; ++c) o[c] = a > 0.0 ? static_cast<float>(acc[c] / a) : 0.0f;
        }
    }
    return out;
}

}  // namespace

const char* const kShadowRingNote =
    "sombra desenhada SEM o anel: Shadow.ringWidth vem preenchido nesta versao ([16,16,16,16], "
    "0x5EC58), entao o alvo recorta a sombra com uma forma inset de -(16 x escala) via "
    "clipLayerWithAlpha: em 0x20CE8. A largura esta lida; a GEOMETRIA da forma de recorte "
    "(construida em 0x11C40) nao foi transcrita, entao a sombra sai sem esse corte e cobre "
    "tambem a area sob o proprio glifo.";

const char* const kShadowBlurKernelNote =
    "sombra: o raio do desfoque e medido (escala x blurStrengthMax x clamp(Shadow.radius[3-c],"
    " 0, 1), 0x20C38) e o KERNEL que o addBlurFilterWithRadius: constroi a partir dele nao foi "
    "lido. Aqui o raio e tomado como o suporte de tres sigmas de uma gaussiana separavel "
    "(sigma = raio/3), que e o inverso da regra que o gaussiano deste repositorio ja usa; "
    "sigma = raio daria uma sombra cerca de tres vezes mais larga e e igualmente nao lido.";

const char* const kShadowOverdrawNote =
    "sombra: este grupo abriria a passagem de OVERDRAW do alvo (Shadow.drawOverContent e true "
    "por padrao e a translucency do grupo e nao nula), que e um segundo composite recortado por "
    "clipLayerWithAlpha: com alpha = clamp01(translucency / 0.3) x max<Neutral|Vibrant>"
    "OverdrawOpacity[3-c] (0x45F18-0x45F94). Ela NAO esta desenhada aqui -- so a passagem "
    "principal de 0x49ED4 esta.";

bool shadowUsesVibrantTable(ShadowStyle style) {
    // `automatic` and `vibrant` both fall into `w8 = 0` at `0x4A254`/`0x4A260`.
    // `none` never reaches the table; it is folded in with `vibrant` here only
    // so the function is total, and `shadowDraws` is what keeps it unreachable.
    return style != ShadowStyle::Neutral;
}

ShadowStyle shadowEffectiveStyle(ShadowStyle style, bool recolouring) {
    if (style == ShadowStyle::None) return style;   // the `none` exit precedes the gate
    return recolouring ? ShadowStyle::Neutral : style;
}

double shadowAlpha(const ShadowInputs& in, const ShadowParameters& p) {
    if (in.style == ShadowStyle::None) return 0.0;   // `0x49F74`: returns before the `fmul`s
    const SizeBasedValue& table =
        shadowUsesVibrantTable(in.style) ? p.vibrantOpacity : p.neutralOpacity;
    // `slots[3 - sizeClass]`, through the one function that owns the inversion.
    const double scaled = sizeBasedValue(table, in.sizeClass);
    // The order is the binary's: `d8 * d10` first (`0x4A06C`), then `d9 * that`
    // (`0x4A070`). Floating point multiplication is not associative, and this
    // project's differentials are bit for bit.
    return in.layerOpacity * (in.shadowOpacity * scaled);
}

bool shadowDraws(const ShadowInputs& in, const ShadowParameters& p) {
    return in.style != ShadowStyle::None && shadowAlpha(in, p) > 0.0;
}

BlendMode shadowBlendMode(ShadowStyle style, bool recolouringDim, const ShadowParameters& p) {
    if (shadowUsesVibrantTable(style) && recolouringDim) return p.blendModeForVibrantOnDim;
    return p.blendMode;
}

ShadowGeometry shadowGeometry(std::uint32_t size, IconSizeClass sizeClass,
                              const ShadowParameters& p, const GlassRenderingParameters& g) {
    // `s = min(width, height) / 1024`, and the target is square.
    const double s = static_cast<double>(size) / 1024.0;

    ShadowGeometry out;
    out.offsetX = s * p.offsetX;
    out.offsetY = s * p.offsetY;

    // Clamped on BOTH sides, `0x20C14`-`0x20C28`. `fmin` first so a NaN loses to
    // the ceiling the way `fminnm` makes it, then the floor as a compare -- the
    // same shape `denormaliseRefractionHeight` already writes for the same
    // reason.
    const double capped = std::fmin(sizeBasedValue(p.radius, sizeClass), 1.0);
    const double clamped = capped >= 0.0 ? capped : 0.0;
    out.blurRadius = s * g.blurStrengthMax * clamped;

    if (p.ringWidth) out.ringWidth = s * sizeBasedValue(*p.ringWidth, sizeClass);
    return out;
}

std::vector<float> shadowImage(const std::vector<float>& art, std::uint32_t width,
                               std::uint32_t height, ShadowStyle style,
                               const ShadowGeometry& geometry, const ShadowParameters& p) {
    const std::size_t texels = static_cast<std::size_t>(width) * height;
    if (art.size() < texels * 4) return {};

    // STEP 1, THE COLOUR, and the two branches are not two shades of the same
    // thing. The neutral one multiplies the ALPHA by black -- which leaves a
    // black silhouette and keeps the alpha -- so the glyph's own colours are
    // gone. The vibrant one multiplies the COLOUR by a grey and keeps the alpha
    // untouched, so the glyph's colours survive, dimmed. That difference IS
    // `GlassMaterial::shadowInfusesGlyphColor`.
    //
    // The composite's `tintColor:` then agrees with whichever branch ran -- white
    // for vibrant (`0xCDE70`, the identity) and black for neutral (`0xCDE98`,
    // already true of a black silhouette) -- so it is folded in here rather than
    // applied twice.
    std::vector<float> img(art.begin(), art.begin() + static_cast<std::ptrdiff_t>(texels * 4));
    if (shadowUsesVibrantTable(style)) {
        const double v = p.vibrantBrightness;
        // `0x20BC0` SKIPS the filter when `v == 1.0`. Transcribed as a branch
        // and not as a multiply by one, because the two differ on a NaN and
        // because the skip is what the binary does.
        if (v != 1.0) {
            for (std::size_t t = 0; t < texels; ++t) {
                for (int c = 0; c < 3; ++c) {
                    img[t * 4 + c] = static_cast<float>(img[t * 4 + c] * v);
                }
            }
        }
    } else {
        for (std::size_t t = 0; t < texels; ++t) {
            for (int c = 0; c < 3; ++c) img[t * 4 + c] = 0.0f;
        }
    }

    // STEPS 2 AND 3. They commute -- a Gaussian is shift invariant -- so the blur
    // runs first, on the art's own grid, and the translation resamples once
    // afterwards instead of the resampling being fed into the kernel.
    img = gaussian(img, width, height, geometry.blurRadius * kShadowBlurSigmaPerRadius);
    img = translate(img, width, height, geometry.offsetX, geometry.offsetY);

    // STEP 4, the ring clip, is NOT applied. `geometry.ringWidth` carries the
    // width for the caller to report through `kShadowRingNote`.
    return img;
}

double shadowOverdrawAlpha(double translucency, ShadowStyle style, IconSizeClass sizeClass,
                           const ShadowParameters& p) {
    if (style == ShadowStyle::None) return 0.0;
    if (p.translucencyForMaxOverdraw == 0.0) return 0.0;
    const double raw = translucency / p.translucencyForMaxOverdraw;
    const double t = std::clamp(raw, 0.0, 1.0);
    const SizeBasedValue& quad =
        shadowUsesVibrantTable(style) ? p.maxVibrantOverdrawOpacity : p.maxNeutralOverdrawOpacity;
    const double a = t * sizeBasedValue(quad, sizeClass);
    return a > 0.0 ? a : 0.0;   // the binary's own `alpha <= 0` exit
}

}  // namespace rb
