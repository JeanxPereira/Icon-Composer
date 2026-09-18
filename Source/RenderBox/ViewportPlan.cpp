#include "Source/RenderBox/ViewportPlan.h"

#include "Source/RenderBox/BlurKernel.h"
#include "Source/RenderBox/GlassLayer.h"
#include "Source/RenderBox/GlassMaterial.h"
#include "Source/RenderBox/GlassShadow.h"
#include "Source/RenderBox/GlassSpecular.h"

#include <algorithm>
#include <cmath>

namespace rb {

namespace {

// As duas variancias que a reducao da escada ja carrega
// (`BlurKernel.cpp:80-83`). Duplicadas aqui porque la sao internas; se uma
// mudar, `blurLadderAlignment` passa a mentir, e o gate acusa.
constexpr double kReduce4Variance = 0.47265625;
constexpr double kReduce2Variance = 0.765625;
constexpr std::uint32_t kMaxAlignment = 1u << 12;

std::int64_t alignDown(std::int64_t v, std::uint32_t a) { return v - (v % a); }

}  // namespace

DocumentReach documentReach(const icf::IconDocument& doc, const icf::Context& ctx,
                            IconSizeClass sizeClass) {
    DocumentReach r;
    // A pastilha primeiro, porque ela nao depende de grupo nenhum -- ela e
    // desenhada sempre que o fundo pinta, e o campo dela e lido ate
    // `inset + height`.
    r.localPoints = kChicletHighlightBandPoints;
    const ShadowGeometry unit = shadowGeometry(static_cast<std::uint32_t>(kCanvasPoints), sizeClass);
    const std::vector<icf::Group> groups = doc.groups();
    for (const icf::Group& group : groups) {
        const std::optional<GlassMaterialDocument> m = readGlassMaterial(group, ctx);
        if (!m) continue;
        const DenormalisedGlass g = denormaliseGlass(glassMaterialFrom(*m));
        // Conservador: `layerOpacity == 1` e todo grupo tratado como vidro. Uma
        // margem larga demais so custa area; uma curta custa o invariante.
        const ShadowInputs in{g.shadowStyle, g.shadowOpacity, 1.0, sizeClass};
        if (shadowDraws(in)) {
            r.shadowSigmaPoints = std::max(r.shadowSigmaPoints,
                                           unit.blurRadius * kShadowBlurSigmaPerRadius);
            r.shadowShiftPoints = std::max(
                r.shadowShiftPoints,
                std::hypot(unit.offsetX, unit.offsetY) + unit.ringWidth.value_or(0.0));
        }
        if (g.refractionStrengthPoints != 0.0) {
            r.chainedPoints +=
                std::max(std::fabs(g.refractionStrengthPoints), g.refractionHeightPoints);
        }
        const bool translucent = g.translucencyEnabled && g.translucency != 0.0;
        if (documentAsksForSpecular(g) || translucent) {
            r.localPoints = std::max(r.localPoints, kLocalFieldBandPoints);
        }
    }
    return r;
}

std::uint32_t blurLadderAlignment(double sigmaPixels) {
    std::uint32_t a = 1;
    double variance = sigmaPixels * sigmaPixels;
    while (variance > 0.0 && a < kMaxAlignment) {
        const int f = blurReduceFactorForVariance(variance);
        if (f <= 1) break;
        const double residual =
            variance / (f * f) - (f == 4 ? kReduce4Variance : kReduce2Variance);
        if (!(residual > 0.0)) break;
        a *= static_cast<std::uint32_t>(f);
        variance = residual;
    }
    return a;
}

Result<ViewportPlan> planViewport(const IconViewport& v, std::uint32_t size,
                                  const DocumentReach& reach) {
    ViewportPlan p;
    const std::uint32_t w = v.width ? v.width : size;
    const std::uint32_t h = v.height ? v.height : size;
    if (v.originX < 0 || v.originY < 0 ||
        static_cast<std::uint64_t>(v.originX) + w > size ||
        static_cast<std::uint64_t>(v.originY) + h > size) {
        return std::unexpected("viewport fora do canvas");
    }
    p.crop = PixelGrid{size, v.originX, v.originY, w, h};

    const double k = static_cast<double>(size) / kCanvasPoints;
    const double sigmaPx = reach.shadowSigmaPoints * k;
    p.alignment = blurLadderAlignment(sigmaPx);
    const double shadowPx =
        sigmaPx > 0.0 ? blurKernelHalfWidth(sigmaPx) + std::ceil(reach.shadowShiftPoints * k) : 0.0;
    const double localPx = std::max(std::ceil(reach.localPoints * k), shadowPx);
    const double marginPx = localPx + std::ceil(reach.chainedPoints * k) + 2.0 * p.alignment + 2.0;
    p.marginPixels = static_cast<std::uint32_t>(std::min(marginPx, static_cast<double>(size)));

    const std::int64_t m = p.marginPixels;
    const std::int64_t x0 = alignDown(std::max<std::int64_t>(0, v.originX - m), p.alignment);
    const std::int64_t y0 = alignDown(std::max<std::int64_t>(0, v.originY - m), p.alignment);
    const std::int64_t x1 = std::min<std::int64_t>(size, static_cast<std::int64_t>(v.originX) + w + m);
    const std::int64_t y1 = std::min<std::int64_t>(size, static_cast<std::int64_t>(v.originY) + h + m);
    p.buffer = PixelGrid{size, static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0),
                         static_cast<std::uint32_t>(x1 - x0), static_cast<std::uint32_t>(y1 - y0)};
    p.overCap = !p.buffer.isFull() && p.buffer.texels() > kViewportAreaCap;
    return p;
}

}  // namespace rb
