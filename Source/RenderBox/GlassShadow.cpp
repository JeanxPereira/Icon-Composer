#include "Source/RenderBox/GlassShadow.h"

#include <algorithm>
#include <cmath>

#include "Source/RenderBox/BlurKernel.h"
#include "Source/RenderBox/DistanceField.h"
#include "Source/RenderBox/Parallel.h"

namespace rb {
namespace {

// A separable Gaussian over PREMULTIPLIED values with CLAMPED edges.
//
// This USED to be a deliberate second copy of the one in `SvgFilter.cpp`, on the
// grounds that lifting an SVG `<filter>` detail into a public header to serve a
// glass shadow would couple two towers that have nothing to do with each other.
// That reasoning still holds for `SvgFilter.cpp` and nothing there moved. What
// changed is that the kernel this shadow needs stopped being a convention: it is
// now READ, out of `RenderBox.arm64`, and a reading belongs in one place.
// `BlurKernel.h` is that place, and the difference it makes here is the
// truncation -- `ceil(2.8 * sigma)` as the binary does it, against the `3.0`
// this copy used to write. The edge rule and the premultiplication are unchanged
// and their reasons are in both headers.
std::vector<float> gaussian(const std::vector<float>& src, std::uint32_t w, std::uint32_t h,
                            double sigma) {
    return blurPremultipliedRgba(src, w, h, sigma);
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

    // Split by destination row: `pre` is finished and read-only from here, and
    // every iteration writes only its own four floats.
    parallelRanges(h, texels * 24, [&](std::size_t y0, std::size_t y1) {
    for (std::uint32_t y = static_cast<std::uint32_t>(y0); y < static_cast<std::uint32_t>(y1); ++y) {
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
    });
    return out;
}

// The 1D squared Euclidean distance transform this file used to carry its own
// copy of now lives in `DistanceField.h` as `edtSquared1d`, because the field
// generator for raster art needs the same primitive and two copies of an exact
// transform are two things to keep in step. `kEdtInf` is the sentinel that
// marks a texel with no seed in its line; it has to match the one the shared
// transform compares against.
constexpr double kEdtInf = 1e20;

}  // namespace

const char* const kShadowRingNote =
    "sombra: o anel de Shadow.ringWidth ([16,16,16,16], 0x5EC58) E aplicado -- a banda do "
    "addAlphaThresholdFilter de 0x11D64 colapsa em clamp(profundidade / (ringWidth x escala)) "
    "a partir da propria silhueta, com o maxDistance do SDF se cancelando. O que NAO foi lido e "
    "o CAMPO DE DISTANCIA que essa banda amostra: o alvo le um SDF gerado por "
    "sdfTextureWithBufferAllocator: num TXRTexture, que nao esta nem no IconRendering nem no "
    "RenderBox deste dump. Aqui a distancia e uma transformada euclidiana exata sobre o contorno "
    "alpha = 0.5 da propria arte, na resolucao do alvo, e uma eventual saturacao do SDF do alvo "
    "(ringWidth em texels acima do maxDistance dele) nao teria como aparecer.";

const char* const kShadowOverdrawNote =
    "sombra: a passagem de OVERDRAW E desenhada -- a MESMA imagem de sombra composta uma "
    "segunda vez (0x49ED4 com w1=1, de 0x46034 e 0x4AEB4), agora SOBRE a arte, com "
    "Shadow.overdrawBlendMode no lugar do byte de mescla (o tst w1,#1 de 0x49F94/0x4A004 e a "
    "unica coisa que w1 muda la dentro) e recortada pela cobertura da PROPRIA arte, que o alvo "
    "desenha na camada de recorte com 0x4B4EC -- a mesma funcao que a passagem de conteudo "
    "termina chamando (0x4AF20 -> 0x4B3EC) -- fechada por clipLayerWithAlpha:mode:0 em "
    "0x45FF4/0x4AEA8 com alpha = clamp01(translucency/0.3) x max<Neutral|Vibrant>"
    "OverdrawOpacity[3-c] (0x45F18-0x45F94). O que NAO foi lido e o PORTAO: 0x45F10 exige que o "
    "byte [descritor+0x31] (FinalizedIcon.Layer.blendMode) seja ZERO, e 0x4B518 despacha esse "
    "mesmo byte contra #8; aqui a passagem abre sempre que a aritmetica a abre, porque nenhuma "
    "chave de documento escolhe esse byte.";

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

std::vector<float> shadowRingMask(const std::vector<float>& art, std::uint32_t width,
                                  std::uint32_t height, double ringWidth) {
    const std::size_t texels = static_cast<std::size_t>(width) * height;
    if (width == 0 || height == 0 || art.size() < texels * 4) return {};
    // A non-positive width is the degenerate band: `maxAlpha == minAlpha`, and
    // the remap's `1 / (maxAlpha - minAlpha)` is infinite, so every alpha above
    // the contour saturates to one. That is the identity mask, not an empty one.
    if (!(ringWidth > 0.0)) return std::vector<float>(texels, 1.0f);

    const int w = static_cast<int>(width);
    const int h = static_cast<int>(height);
    const int n = std::max(w, h);

    // Seeds are the texels OUTSIDE the silhouette. Everything else starts at
    // infinity and the transform brings it down to its true distance.
    std::vector<double> sq(texels);
    for (std::size_t t = 0; t < texels; ++t) {
        sq[t] = art[t * 4 + 3] >= 0.5f ? kEdtInf : 0.0;
    }

    // ONE COLUMN, THEN ONE ROW, PER WORKER -- the same split `edt2d` makes for
    // the same reason: `edtSquared1d` is a pure function of the line it is
    // handed, and no line reads another's cells. The envelope's scratch
    // (`f`, `d`, `v`, `z`) moves inside the worker so it is not shared, which
    // is the whole of what changes here. No number does.
    parallelRanges(static_cast<std::size_t>(w), texels * 8, [&](std::size_t x0, std::size_t x1) {
        std::vector<double> f(static_cast<std::size_t>(n));
        std::vector<double> d(static_cast<std::size_t>(n));
        std::vector<int> v(static_cast<std::size_t>(n) + 1);
        std::vector<double> z(static_cast<std::size_t>(n) + 2);
        for (int x = static_cast<int>(x0); x < static_cast<int>(x1); ++x) {
            for (int y = 0; y < h; ++y) f[static_cast<std::size_t>(y)] = sq[static_cast<std::size_t>(y) * w + x];
            edtSquared1d(f, d, v, z, h);
            for (int y = 0; y < h; ++y) sq[static_cast<std::size_t>(y) * w + x] = d[static_cast<std::size_t>(y)];
        }
    });
    parallelRanges(static_cast<std::size_t>(h), texels * 8, [&](std::size_t y0, std::size_t y1) {
        std::vector<double> f(static_cast<std::size_t>(n));
        std::vector<double> d(static_cast<std::size_t>(n));
        std::vector<int> v(static_cast<std::size_t>(n) + 1);
        std::vector<double> z(static_cast<std::size_t>(n) + 2);
        for (int y = static_cast<int>(y0); y < static_cast<int>(y1); ++y) {
            double* row = &sq[static_cast<std::size_t>(y) * w];
            for (int x = 0; x < w; ++x) f[static_cast<std::size_t>(x)] = row[x];
            edtSquared1d(f, d, v, z, w);
            for (int x = 0; x < w; ++x) row[x] = d[static_cast<std::size_t>(x)];
        }
    });

    std::vector<float> mask(texels, 0.0f);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t t = static_cast<std::size_t>(y) * w + x;
            if (art[t * 4 + 3] < 0.5f) continue;   // outside: the mask is zero there
            double dist = std::sqrt(sq[t]);
            // The empty border ring of the target's SDF, as a distance to four
            // virtual outside rows one step beyond each edge.
            dist = std::min(dist, static_cast<double>(x) + 1.0);
            dist = std::min(dist, static_cast<double>(y) + 1.0);
            dist = std::min(dist, static_cast<double>(w - x));
            dist = std::min(dist, static_cast<double>(h - y));
            const double depth = dist - 0.5;   // centres to contour
            mask[t] = static_cast<float>(std::clamp(depth / ringWidth, 0.0, 1.0));
        }
    }
    return mask;
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

    // STEP 4 FIRST. The clip is installed on the state the art is drawn into and
    // the blur is a filter on that state's layer, so the clip is inside it --
    // see the header, where that ordering is `[INF]` and argued. It multiplies
    // ALPHA only: `clipLayerWithAlpha:` masks coverage, it does not tint.
    if (geometry.ringWidth) {
        const std::vector<float> mask =
            shadowRingMask(img, width, height, *geometry.ringWidth);
        if (mask.size() == texels) {
            for (std::size_t t = 0; t < texels; ++t) img[t * 4 + 3] *= mask[t];
        }
    }

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

std::vector<float> shadowOverdrawImage(const std::vector<float>& shadow,
                                       const std::vector<float>& content,
                                       std::uint32_t width, std::uint32_t height,
                                       double clipAlpha) {
    const std::size_t texels = static_cast<std::size_t>(width) * height;
    if (shadow.size() < texels * 4 || content.size() < texels * 4) return {};
    if (!(clipAlpha > 0.0)) return {};

    std::vector<float> out(shadow.begin(),
                           shadow.begin() + static_cast<std::ptrdiff_t>(texels * 4));
    const float k = static_cast<float>(clipAlpha);
    for (std::size_t t = 0; t < texels; ++t) {
        // The clip multiplies COVERAGE, so it lands on alpha alone -- the same
        // rule `shadowImage` already follows for the ring. The content's own
        // colour never enters: `clipLayerWithAlpha:` clips, it does not tint.
        out[t * 4 + 3] *= content[t * 4 + 3] * k;
    }
    return out;
}

}  // namespace rb
