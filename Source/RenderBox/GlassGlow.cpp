#include "Source/RenderBox/GlassGlow.h"

#include <cstdint>

#include "Source/RenderBox/Parallel.h"
#include "Source/RenderBox/RenderingParameters.h"

namespace rb {
namespace {

// `half 0xH1400` and `half 0xH3AAA` of the fragment.
constexpr double kEps = 0.0009765625;
constexpr double kBandWidth = 0.8330078125;

double sat(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

}  // namespace

const char* const kGlowNote =
    "brilho interno desenhado (geracao 26): `[BIN]` ICRRenderingParameters.glow = {bias 0.5, "
    "innerRadius 42.38, innerOpacity 0.03} (0x77E5C-0x77E64; nil na geracao 27), lido em "
    "0x48C20-0x48DA8 -- por grupo, depois do conteudo e do overdraw da sombra e antes dos "
    "realces, recortado ao effectsFrame do grupo com um pixel de folga (0x48D54-0x48D78) -- e "
    "desenhado por 0xF534 com o shader `glow` (default_mod10.ll): branco x edge x "
    "exp(-0.5 (profundidade / innerRadius)^2) / max(1 + (1 - g)(1/bias - 2), 2^-10), a alfa "
    "innerOpacity x opacidade do grupo, em plusLighter cru (0x1B, sem clampedPlusL). `[BIN]` A "
    "profundidade SATURA no alcance do campo do grupo: o SDF e a distancia codificada pelo "
    "filtro de distancia do RenderBox, que grampeia (default_mod74.ll %94-%99), e o alcance e o "
    "do maior realce (0x1C6B8) -- 12 pontos contra um raio de 42.38, entao o brilho e quase um "
    "clareamento chapado do interior do vidro. `[OBS]` fwidth e a inclinacao de um campo de um "
    "por pixel; a conta e em double (o alvo, em half); e a saida do fragmento e tomada como "
    "branco pre-multiplicado.";

const char* const kGlowNoReachNote =
    "brilho interno (geracao 26) NAO desenhado neste grupo: o campo dele nao tem alcance -- sem "
    "especular, 0x1C6B8 devolve zero como maxDistance do SDF. `[OBS]` Com maxDistance zero o "
    "sdfScale do shader `glow` e zero e o fragmento como lido daria metade da opacidade sobre o "
    "retangulo de recorte inteiro, com forma ou sem; se o SDF do alvo sequer existe para um "
    "grupo assim nao foi lido.";

GlowArguments glowArguments(const GlowParameters& glow, double groupOpacity,
                            double pixelsPerPoint, double maxDistancePixels,
                            const double frame[4]) {
    GlowArguments a;
    // `[BIN]` `0xF620`: `innerRadius x (sdfTexelsW - 2) / canvasWidth`, and the
    // texture is the render plus one texel a side.
    a.radius = glow.innerRadius * pixelsPerPoint;
    // `[BIN]` `0xF650`-`0xF65C`: `1.0 / bias + (-2.0)`.
    a.biasAmount = 1.0 / glow.bias + -2.0;
    // `[BIN]` `0x48D8C`: `innerOpacity x [descriptor+0x38]`.
    a.alpha = glow.innerOpacity * groupOpacity;
    a.maxDistance = maxDistancePixels;
    if (a.radius > 0.0) {
        const double slope = 1.0 / a.radius;
        const double clamped = slope < kEps ? kEps : (slope > 2.0 ? 2.0 : slope);
        a.edgeWidth = clamped * kBandWidth;
    }
    a.edgeWidthFlat = kEps * kBandWidth;
    // `[BIN]` `CGRectInset(rect, -pixelUnit, -pixelUnit)`, `0x48D50`-`0x48D5C`.
    a.clip[0] = frame[0] - 1.0;
    a.clip[1] = frame[1] - 1.0;
    a.clip[2] = frame[0] + frame[2] + 1.0;
    a.clip[3] = frame[1] + frame[3] + 1.0;
    return a;
}

bool glowDraws(const GlowArguments& a) {
    return a.alpha > 0.0 && a.radius > 0.0 && a.maxDistance > 0.0;
}

double glowGaussian(double x) {
    constexpr double kInvLn2 = 1.4426950408889634;
    // `ln 2` in two pieces, the first with its low bits zero, so that `k x hi`
    // is exact for every `k` this can meet.
    constexpr double kLn2Hi = 0.693147180369123816490;
    constexpr double kLn2Lo = 1.90821492927058770002e-10;
    const double y = 0.5 * (x * x);
    // Past this the answer is below the smallest normal double (and a NaN
    // lands here too).
    if (!(y < 700.0)) return 0.0;
    // The nearest multiple, by truncation: `y` is not negative.
    const int halvings = static_cast<int>(y * kInvLn2 + 0.5);
    const double k = static_cast<double>(halvings);
    const double r = (y - k * kLn2Hi) - k * kLn2Lo;
    // exp(-r), |r| <= 0.35: Taylor to the thirteenth power, by Horner.
    double p = 1.0;
    for (int n = 13; n >= 1; --n) p = 1.0 - (r / static_cast<double>(n)) * p;
    for (int i = 0; i < halvings; ++i) p = p * 0.5;
    return p;
}

double glowFragment(double depth, const GlowArguments& a) {
    // The encoded distance saturates at the field's reach (the header).
    double d = depth;
    bool flat = false;
    if (d >= a.maxDistance) {
        d = a.maxDistance;
        flat = true;
    } else if (d <= -a.maxDistance) {
        d = -a.maxDistance;
        flat = true;
    }
    const double x = d / a.radius;
    const double g = glowGaussian(x);
    const double denom = 1.0 + (1.0 - g) * a.biasAmount;
    const double den = denom > kEps ? denom : kEps;
    const double fw = flat ? a.edgeWidthFlat : a.edgeWidth;
    const double edge = sat(x / fw + 0.5);
    return edge * g / den;
}

double glowClipCoverage(double x, double y, const GlowArguments& a) {
    const double x0 = x > a.clip[0] ? x : a.clip[0];
    const double x1 = x + 1.0 < a.clip[2] ? x + 1.0 : a.clip[2];
    const double y0 = y > a.clip[1] ? y : a.clip[1];
    const double y1 = y + 1.0 < a.clip[3] ? y + 1.0 : a.clip[3];
    return sat(x1 - x0) * sat(y1 - y0);
}

std::size_t drawGlow(std::vector<float>& rgba, const FieldImage& field, const GlowArguments& a) {
    const std::size_t n = static_cast<std::size_t>(field.width) * field.height;
    if (rgba.size() < n * 4 || !glowDraws(a)) return 0;
    std::vector<char> hit(n, 0);
    parallelRanges(field.height, n * 24, [&](std::size_t y0, std::size_t y1) {
    for (std::uint32_t y = static_cast<std::uint32_t>(y0); y < static_cast<std::uint32_t>(y1); ++y) {
        for (std::uint32_t x = 0; x < field.width; ++x) {
            // The field is NEGATIVE INSIDE and the shader's depth is positive
            // inside -- the flip `drawSpecular` makes.
            const double depth = -static_cast<double>(field.at(x, y)[0]);
            const double f = glowFragment(depth, a);
            if (f <= 0.0) continue;
            const double clip = glowClipCoverage(
                static_cast<double>(static_cast<std::int64_t>(x) + field.originX),
                static_cast<double>(static_cast<std::int64_t>(y) + field.originY), a);
            if (clip <= 0.0) continue;
            // White at this coverage, premultiplied, under the draw's alpha.
            const double alpha = (f * clip) * a.alpha;
            const std::size_t px = static_cast<std::size_t>(y) * field.width + x;
            const std::size_t i = px * 4;
            // `[BIN]` Plus-lighter, raw: the colours add and the alpha
            // saturates (`BlendFormula.cpp`, case 43).
            for (int c = 0; c < 3; ++c) {
                const float v = static_cast<float>(alpha + static_cast<double>(rgba[i + c]));
                if (v != rgba[i + c]) hit[px] = 1;
                rgba[i + c] = v;
            }
            const float va = static_cast<float>(sat(alpha + static_cast<double>(rgba[i + 3])));
            if (va != rgba[i + 3]) hit[px] = 1;
            rgba[i + 3] = va;
        }
    }
    });
    std::size_t touched = 0;
    for (std::size_t i = 0; i < n; ++i) touched += static_cast<std::size_t>(hit[i]);
    return touched;
}

}  // namespace rb
