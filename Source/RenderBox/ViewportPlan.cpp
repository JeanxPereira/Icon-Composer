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
// GUARDA DE LACO, e nada mais -- nenhum sigma real a alcanca. Cada nivel
// divide a variancia por `f²` (e ainda subtrai a variancia da propria
// reducao), entao o produto dos fatores cresce como o LOGARITMO de sigma:
// chegar a 4096 pede seis niveis de `f == 4`, e cada nivel abaixo do ultimo
// paga um `x 16` (mais a variancia da propria reducao), entao o sexto so
// acontece com `variance > 165 x 16⁵ ~ 1,73e8` -- sigma ~1,3 x 10⁴ PIXELS. O
// teto da formula da sombra e 64 pontos de
// sigma (`GlassShadow.cpp:164`, o teto que a spec chama de inalcancavel), logo
// `sigmaPixels = 64 x size/1024`: chegar la pediria uma `size` de centenas de
// milhares de pixels, muito alem do teto de area e do teto do aparelho. O
// valor esta aqui para que um `residual` que nunca caia (um NaN, uma constante
// trocada) pare o laco em vez de o travar.
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

// ESTA FUNCAO OMITE `kBlurMinReducedSide`, E ISSO E SEGURO -- dito aqui para
// que o proximo leitor nao tenha de rederivar. `blurLadder`
// (`BlurKernel.cpp:209-210`) so reduz se os DOIS lados reduzidos ficarem >= 8;
// aqui so a variancia decide. Um buffer pequeno demais faria `blurLadder`
// parar um nivel antes do que este alinhamento supoe, e a origem estaria
// alinhada a um multiplo maior do que o necessario -- o que custa area, nunca
// o invariante (uma origem multipla de 8 tambem e multipla de 4 e de 2).
//
// E o caso nem chega a acontecer: alinhamento diferente de 1 exige sombra, e a
// sombra poe `blurKernelHalfWidth(sigma) = ceil(2,8 x sigma)` na margem, que
// cresce mais rapido que o lado que a reducao pede (~8 x o produto dos
// fatores). Nivel a nivel: `a = 2` pede lado >= 15 e da margem >= 21;
// `a = 4` pede 29 e da >= 37; `a = 8` pede 57 e da >= 84; `a = 16` pede 113 e
// da >= 145. E o lado do buffer e >= `min(size, margem + 1)`. Em `size = 512`
// com a sombra default isso e margem 57 px contra os 15 px que o unico degrau
// alcancavel (`a == 2`) pede.
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
    // O TETO OLHA O QUE FOI PEDIDO, NAO O QUE FOI PLANEJADO (spec 2026-09-16,
    // "O teto de area"). A isencao existe para um so caso: quem pediu o CANVAS
    // INTEIRO nao tem para onde cair -- a queda e justamente para a base, com
    // `crop` cheio -- entao recusa-lo travaria o renderizador. Quem diz isso e
    // `crop`, que e o pedido.
    //
    // Testar `buffer.isFull()` aqui -- como o plano escrevia, e como o codigo
    // fez ate esta correcao -- confundia os dois: um LADRILHO cuja margem
    // crescesse ate `(viewport ⊕ margem) ∩ canvas` cobrir o canvas ficava
    // indistinguivel de um render cheio e escapava do teto inteiro. Alcancavel
    // pela UI: com refracao forte a margem e ~439 pontos por lado, e em
    // `size = 8192` (base 512 a 1600%) basta o retangulo visivel passar de
    // ~1168 px por eixo -- uma janela maximizada em 1440p -- para o buffer
    // saturar e um render de 8192x8192 sair no lugar da recusa.
    //
    // `crop.isFull()` implica `buffer.isFull()` (margem sobre o canvas inteiro
    // volta ao canvas inteiro), entao esta linha so pode RECUSAR mais do que a
    // anterior, nunca menos.
    p.overCap = !p.crop.isFull() && p.buffer.texels() > kViewportAreaCap;
    return p;
}

}  // namespace rb
