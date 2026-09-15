#include "Source/RenderBox/IconRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>

#include "Source/CoreSVG/Document.h"
#include "Source/RenderBox/AutomaticGradient.h"
#include "Source/RenderBox/BlendFormula.h"
#include "Source/RenderBox/BlurKernel.h"
#include "Source/RenderBox/ChicletHighlights.h"
#include "Source/RenderBox/ChicletShape.h"
#include "Source/RenderBox/FillResolve.h"
#include "Source/RenderBox/GlassLayer.h"
#include "Source/RenderBox/GlassShadow.h"
#include "Source/RenderBox/GlassSpecular.h"
#include "Source/RenderBox/GradientOracle.h"
#include "Source/RenderBox/SystemFill.h"
#include "Source/IconComposerFoundation/Png.h"
#include "Source/IconComposerFoundation/Values.h"

namespace rb {
namespace {

std::string readAll(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}

// A number keeps its LEXEME in this model, deliberately -- the corpus carries
// digits no shortest-form double round-trips (doc 01 §2). Converting is the
// consumer's job, and a lexeme that will not convert is a fallback, not a zero.
double numberOr(const icf::json::Value* v, double fallback) {
    if (!v || v->kind() != icf::json::Value::Kind::Number) return fallback;
    try {
        return std::stod(v->number());
    } catch (...) {
        return fallback;
    }
}

bool boolOr(const icf::json::Value* v, bool fallback) {
    if (!v || v->kind() != icf::json::Value::Kind::Bool) return fallback;
    return v->boolean();
}

const std::string* textOf(const icf::json::Value* v) {
    if (!v || v->kind() != icf::json::Value::Kind::String) return nullptr;
    return &v->rawString();
}

LayerPlacement placementOf(const icf::json::Value* position) {
    LayerPlacement p;
    if (!position) return p;
    if (auto pos = icf::positionFrom(*position)) {
        p.scale = pos->scale;
        p.translateX = pos->translation.x;
        p.translateY = pos->translation.y;
    }
    return p;
}

// A sentence said once per render, however many layers provoke it.
void note(std::vector<std::string>& notes, const std::string& text) {
    if (text.empty()) return;
    for (const std::string& n : notes) {
        if (n == text) return;
    }
    notes.push_back(text);
}

// A whole GROUP's accumulator mixed into the canvas, both sides PREMULTIPLIED.
//
// Not the same function as `blendOver`: that one takes straight colour with a
// separate alpha, because that is the shape a layer's art arrives in. An
// accumulator is already premultiplied, and running it through the straight
// path would multiply the alpha in twice.
void blendPremulOver(std::vector<float>& dst, const std::vector<float>& src,
                     BlendMode mode) {
    for (std::size_t i = 0; i + 3 < dst.size(); i += 4) {
        BlendColour sc, dc;
        for (int k = 0; k < 4; ++k) {
            sc.rgba[k] = src[i + k];
            dc.rgba[k] = dst[i + k];
        }
        const BlendColour o = rb::blend(mode, sc, dc);
        for (int k = 0; k < 4; ++k) dst[i + k] = static_cast<float>(o.rgba[k]);
    }
}

// One layer's art drawn over the accumulator, with `alpha` applied to all of it.
void over(std::vector<float>& acc, const std::vector<float>& src, float alpha) {
    for (std::size_t i = 0; i + 3 < acc.size(); i += 4) {
        const float a = src[i + 3] * alpha;
        if (a <= 0.0f) continue;
        const float inv = 1.0f - a;
        for (int k = 0; k < 3; ++k) acc[i + k] = src[i + k] * a + acc[i + k] * inv;
        acc[i + 3] = a + acc[i + 3] * inv;
    }
}

// The same composite under a blend mode.
//
// `acc` is PREMULTIPLIED and `src` is straight with its alpha in the fourth
// channel -- which is what `over` above already assumes, and the reason this
// function premultiplies before calling `rb::blend` rather than after. Getting
// that backwards would be invisible wherever alpha is 1, which is most of the
// corpus.
//
// The `a <= 0` skip that `over` does is not repeated here, and it WOULD BE
// SAFE -- which is worth saying because the first version of this comment
// claimed the opposite.
//
// A source with zero alpha is the identity for all nine transcribed modes, by
// case analysis rather than by hope: the composition tail becomes
// `0*(1-ab) + d*(1-0) = d` and every `B` term carries a factor of `as`; screen
// gives `0 + d*(1-0)`; and the plus pair's slack is `saturate(0+ab) - (0+ab)`,
// which is zero for any `ab` in range. So skipping and blending write the same
// number.
//
// It is left out anyway, because the loop is not hot enough to buy an
// asymmetry with `over` that a reader would have to re-derive.
void blendOver(std::vector<float>& acc, const std::vector<float>& src, float alpha,
               BlendMode mode) {
    if (mode == BlendMode::Normal) {
        over(acc, src, alpha);
        return;
    }
    for (std::size_t i = 0; i + 3 < acc.size(); i += 4) {
        const float a = src[i + 3] * alpha;
        BlendColour s;
        s.rgba[3] = a;
        for (int k = 0; k < 3; ++k) s.rgba[k] = src[i + k] * a;
        BlendColour d;
        for (int k = 0; k < 4; ++k) d.rgba[k] = acc[i + k];
        const BlendColour out = rb::blend(mode, s, d);
        for (int k = 0; k < 4; ++k) acc[i + k] = static_cast<float>(out.rgba[k]);
    }
}

// A decoded raster placed on the canvas, sampled bilinearly.
//
// `[INF]` Bilinear is THIS renderer's choice. What the target resamples with is
// not decoded -- `ICRRenderingParameters` names an `SDFGeneration` and a
// `refractionSupersampling` but nothing about image sampling, and guessing
// nearest would be just as much a guess. It is named here rather than silently
// assumed.
std::vector<float> placeRaster(const icf::DecodedPng& img, const LayerPlacement& p,
                               std::uint32_t size) {
    std::vector<float> out(static_cast<std::size_t>(size) * size * 4, 0.0f);
    if (img.width == 0 || img.height == 0) return out;

    // The art enters at its pixel size read as canvas POINTS, scaled, and
    // centred before the translation -- the same rule the vector path uses.
    const double k = static_cast<double>(size) / kCanvasPoints;
    const double w = img.width * p.scale, h = img.height * p.scale;
    const double left = (kCanvasPoints - w) * 0.5 + p.translateX;
    const double top = (kCanvasPoints - h) * 0.5 + p.translateY;

    for (std::uint32_t y = 0; y < size; ++y) {
        // The pixel centre, back into the art's own space.
        const double cy = ((y + 0.5) / k - top) / p.scale - 0.5;
        if (cy < -1.0 || cy > img.height) continue;
        for (std::uint32_t x = 0; x < size; ++x) {
            const double cx = ((x + 0.5) / k - left) / p.scale - 0.5;
            if (cx < -1.0 || cx > img.width) continue;

            const double fx = std::floor(cx), fy = std::floor(cy);
            const double tx = cx - fx, ty = cy - fy;
            float acc[4] = {0, 0, 0, 0};
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const long sx = static_cast<long>(fx) + dx;
                    const long sy = static_cast<long>(fy) + dy;
                    if (sx < 0 || sy < 0 || sx >= static_cast<long>(img.width) ||
                        sy >= static_cast<long>(img.height)) {
                        continue;
                    }
                    const double wgt = (dx ? tx : 1.0 - tx) * (dy ? ty : 1.0 - ty);
                    const std::size_t s =
                        (static_cast<std::size_t>(sy) * img.width + sx) * 4;
                    // Weighted in PREMULTIPLIED form, or a transparent texel's
                    // colour bleeds into its opaque neighbours.
                    const float sa = img.rgba[s + 3];
                    for (int c = 0; c < 3; ++c) {
                        acc[c] += static_cast<float>(wgt) * img.rgba[s + c] * sa;
                    }
                    acc[3] += static_cast<float>(wgt) * sa;
                }
            }
            const std::size_t d = (static_cast<std::size_t>(y) * size + x) * 4;
            out[d + 3] = acc[3];
            for (int c = 0; c < 3; ++c) out[d + c] = acc[3] > 0.0f ? acc[c] / acc[3] : 0.0f;
        }
    }
    return out;
}

// The box `placeRaster` drops the art into, in target pixels -- the raster's
// answer to `artPlacementRect`, which does the same for a viewBox.
//
// It sits HERE, against `placeRaster`, for the reason `artPlacementRect`'s own
// comment gives: the art's box and the art's pixels have to agree, and two
// copies of the same arithmetic in two places is how they stop agreeing. The
// three lines below are `placeRaster`'s own `w`, `h`, `left`, `top` and `k`,
// with nothing added.
//
// It exists because the translucency's `bounds` is a rect, and until this front
// the only art that reached the translucency had a viewBox to give it one.
PlacementRect rasterPlacementRect(std::uint32_t imgW, std::uint32_t imgH,
                                  const LayerPlacement& p, std::uint32_t size) {
    const double k = static_cast<double>(size) / kCanvasPoints;
    const double w = imgW * p.scale, h = imgH * p.scale;
    PlacementRect r;
    r.x = ((kCanvasPoints - w) * 0.5 + p.translateX) * k;
    r.y = ((kCanvasPoints - h) * 0.5 + p.translateY) * k;
    r.width = w * k;
    r.height = h * k;
    return r;
}


// A document colour as four floats. The grey spaces carry two components
// (luminance, alpha) and the RGB spaces four, and `Values.h` deliberately does
// not normalise the two into one shape -- that is a rendering decision, and
// this is where it is made.
void asColour(const icf::Color& c, float (&rgba)[4]) {
    const bool grey = c.count < 3;
    rgba[0] = static_cast<float>(c.components[0]);
    rgba[1] = static_cast<float>(grey ? c.components[0] : c.components[1]);
    rgba[2] = static_cast<float>(grey ? c.components[0] : c.components[2]);
    rgba[3] = static_cast<float>(grey ? c.components[1] : c.components[3]);
}

// `RampStop` (the resolver's, in doubles) into `RampPoint` (the compositor's,
// in floats). Two types rather than one because the resolution is arithmetic on
// the binary's own constants and the composite is a shader's; converting once,
// here, is cheaper than a shared type that has to be right for both.
std::vector<RampPoint> rampPointsOf(const std::vector<RampStop>& stops) {
    std::vector<RampPoint> out;
    out.reserve(stops.size());
    for (const RampStop& s : stops) {
        RampPoint p;
        for (int k = 0; k < 4; ++k) p.rgba[k] = static_cast<float>(s.rgba[k]);
        p.location = static_cast<float>(s.location);
        out.push_back(p);
    }
    return out;
}

// A placed axis as the map the compositor evaluates: a target pixel to the
// ramp's parameter, which is the projection onto the axis divided by its own
// length. False when the axis has no length -- there is no direction to project
// onto, and picking one would be inventing the gradient's direction.
bool axisToMap(const GradientAxis& axis, double (&m)[6]) {
    const double dx = axis.end.x - axis.start.x;
    const double dy = axis.end.y - axis.start.y;
    const double len2 = dx * dx + dy * dy;
    if (len2 == 0.0) return false;
    m[0] = dx / len2;
    m[1] = dy / len2;
    m[2] = -(axis.start.x * dx + axis.start.y * dy) / len2;
    return true;
}

// The background, written into an accumulator that is still empty.
//
// It WRITES rather than composites, and that is a claim about the caller: the
// background is the first thing painted, so there is nothing underneath to hold
// back. The form is the premultiplied one the accumulator keeps while layers
// stack.
void paintBackground(std::vector<float>& acc, std::uint32_t size,
                     const FillOverride& paint) {
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            float colour[4] = {paint.colour[0], paint.colour[1], paint.colour[2],
                               paint.colour[3]};
            if (paint.kind == FillOverride::Kind::Ramp) {
                const double px = static_cast<double>(x) + 0.5;
                const double py = static_cast<double>(y) + 0.5;
                const double t = paint.m[0] * px + paint.m[1] * py + paint.m[2];
                rampAtPositions(paint.stops, static_cast<float>(t), colour);
            }
            const std::size_t i = (static_cast<std::size_t>(y) * size + x) * 4;
            for (int k = 0; k < 3; ++k) acc[i + k] = colour[k] * colour[3];
            acc[i + 3] = colour[3];
        }
    }
}

// Does this colour carry display-p3 components that nothing converts?
bool isUnconvertedP3(const icf::Color& c) { return c.space == icf::ColorSpace::DisplayP3; }

bool fillCarriesP3(const ResolvedFill& f) {
    switch (f.contents) {
        case ResolvedFill::Contents::Solid:
        case ResolvedFill::Contents::AutomaticGradient:
            return isUnconvertedP3(f.primary);
        case ResolvedFill::Contents::Gradient:
            return isUnconvertedP3(f.primary) || isUnconvertedP3(f.secondary);
        case ResolvedFill::Contents::System:
            // `[BIN]` The canned ramps are four bare `Double`s with no space
            // tag at all -- a different gap, named in `SystemFill.h`, and not
            // this one.
            return false;
    }
    return false;
}

}  // namespace

const char* const kChicletRectNote =
    "fill de sistema desenhado sobre o boundingRect da propria forma: `[BIN]` "
    "supportsChicletAlignmentForSystemFills e true por padrao e troca esse rect por "
    "origem (0,0) com um CGSize do contexto de desenho, e `[OBS]` se esse tamanho e o "
    "canvas, o chiclet ou o quadro full-bleed NAO FOI LIDO -- systemFillRect devolve "
    "nullopt nesse braco de proposito, entao nao ha numero a chutar";

const char* const kGradientAxisDirectionNote =
    "`[OBS]` o eixo padrao (0,0)->(0,1) foi lido, a DIRECAO dele nao: a lateralidade de "
    "y do display list do RB nunca foi estabelecida, entao qual ponta da forma recebe a "
    "primeira parada e qual recebe a segunda esta indeterminado -- quase invisivel na "
    "rampa clara (255->245), nao na escura (31->15)";

const char* const kDiscardedBackgroundOrientationNote =
    "`[BIN]` o conversor de fundo le primaryColor e secondaryColor e NUNCA toca em "
    "orientation: a orientacao que este documento nomeia no fill de raiz e descartada e "
    "o gradiente desenha no eixo vertical padrao -- honra-la seria mais correto que o "
    "alvo, e portanto um pixel diferente";

const char* const kBackgroundShapeNote =
    "o fundo e recortado ao chiclet: `[BIN]` canto continuo de raio 266.24 num canvas de "
    "1024 (0.26 do lado), e o regime da curva e o CANONICO -- add_rounded_rect 0x7F664 da "
    "t = 1.746 >= 1 porque Coverage::Primitive::add_path 0x96838 desfaz o x1.275 do "
    "armazenamento antes de montar o RBPathElement 9; `[OBS]` o recorte alcanca o FUNDO e "
    "so ele: se o alvo corta tambem a arte das camadas ao mesmo contorno nao foi lido";

const char* const kRasterFillNote =
    "o fill do documento nao alcanca a arte raster desta camada: o override so chega ao "
    "caminho vetorial, e `[OBS]` se o alvo repinta um elemento raster do mesmo jeito que "
    "repinta um vetorial nao foi lido";

const char* const kBackgroundP3Note =
    "fundo com componentes display-p3 desenhado SEM conversao de espaco -- a matriz "
    "nunca foi medida do alvo, e desenha-los como sRGB os desloca em silencio";

const char* const kTranslucencyBoundsNote =
    "a mascara de translucidez corre uma rampa VERTICAL dentro de um retangulo, e o "
    "retangulo nao foi lido: `[OBS]` 0x10140-0x101B8 chama quatro acessores de rect e o "
    "laudo nao seguiu ate o dono deles, entao aqui vale a viewBox da arte posta no canvas "
    "-- a mesma resposta que este renderizador ja da para o rect de um gradiente, e a "
    "mesma pergunta em aberto; `[OBS]` e qual ponta do rect recebe upperOpacity depende da "
    "lateralidade de y do display list do RB, que continua nao estabelecida";

const char* const kGlassRasterFieldNote =
    "vidro sobre arte raster: o campo de distancia desta camada foi construido a partir do "
    "ALFA da propria arte (contorno alpha >= 0.5, transformada euclidiana exata), e nao de um "
    "contorno achatado. E o que o alvo faz: sdfTextureWithBufferAllocator: (0x867F8) e enviado "
    "a um CUINamedLayerImage (classref 0xCC928), cujo image e pedido em 0x28AE0 e cuja ausencia "
    "ABORTA o caminho em 0x28AEC -- e IconRendering.SDF.SourceLayer (0xA3104) e "
    "{displayList, isOpaque}, um desenho e nao uma forma. O que NAO foi lido e a GRADE: o "
    "TXRTexture que gera a textura nao esta nem no IconRendering nem no RenderBox deste dump, e "
    "os tres botoes de ICRRenderingParameters.SDFGeneration (clampThreshold, "
    "precisePixelFormatThreshold, maxRelativeSmoothing, 0xA46E0) estao nomeados e nao lidos. "
    "Aqui o campo sai na resolucao do alvo.";

// HOW MUCH FINER THAN THE FIELD THE MASK IS RASTERISED, and the number is ONE.
//
// It is a knob because a contour, unlike a bitmap, HAS something finer to
// offer, and the question of whether to spend it deserved a measurement rather
// than a preference. It got one, on `Apollo-Reborn__Apollo-Reborn__AppIcon` at
// 512 px, against the brute force this replaces -- max delta per channel over
// VISIBLE pixels, and the render at 1024 px:
//
//   ss   changed  mean |d|  p90  p99   1024 px render
//    1    14.21 %     4.73    3  130     2.49 s
//    3    12.97 %     3.22    2   98     6.49 s
//    9    11.66 %     2.25    1   60    ~20 s
//   15    10.94 %     1.92    1   45    ~52 s
//
// The ladder does not reach zero, and that is the whole finding: the floor at
// ~10.9 % is NOT the grid. It is the crease the brute force put in the field
// wherever one painted subpath is buried under another -- it measured to the
// buried edge, which is inside the shape and invisible in the picture. `[ART]`
// On `stem.svg` the two fields differ by up to 3.22 px for that reason, and the
// worst 8x8 block of the whole render (121.9 levels, at 256,144) is exactly the
// antenna base where the stem enters the ellipse. `DistanceField.h` had already
// written that crease down as a known wrong; the rasterised field does not have
// it, and `[BIN]` the target, which rasterises, cannot have it either.
//
// So `ss = 3` buys 1.5 levels of mean delta for 2.6x the render, and cannot buy
// the floor at any price. One sample per pixel is also the grid
// `generateFieldFromAlpha` already runs on for raster art, which leaves ONE
// convention in the tower rather than two. The cheap way to the sub-texel
// distance without `ss^2` -- seeding the transform from an anti-aliased
// coverage instead of a binary mask -- is named as open in the laudo and is not
// done here.
//
// If it is ever raised it must be ODD (`DistanceField.h` says why: only an odd
// factor has a sub-texel whose centre IS the pixel centre).
constexpr std::uint32_t kFieldSuperSample = 1;

const char* const kGlassVectorFieldNote =
    "vidro sobre arte vetorial: o campo de distancia desta camada NAO vem mais de um argmin "
    "exato sobre os segmentos do contorno -- a arte e rasterizada na propria grade do campo "
    "(mesma regra de preenchimento e mesmo teste de cruzamento meio-aberto que o argmin usava) "
    "e o campo sai da mesma transformada euclidiana exata que a arte raster ja usava. `[BIN]` "
    "E o que o alvo faz nos dois casos: sdfTextureWithBufferAllocator: (0x867F8) vai a um "
    "CUINamedLayerImage (classref 0xCC928) que ABORTA sem image (0x28AE0/0x28AEC), e "
    "IconRendering.SDF.SourceLayer (0xA3104) e {displayList, isOpaque}. O QUE SE PERDE e "
    "sub-texel: `[ART]` na arte do Apollo a distancia difere do argmin em 0,27 a 0,34 px de "
    "media e 0,78 px no pior caso, e a direcao do gradiente fica quantizada pela grade. O QUE "
    "SE GANHA alem do tempo e que some a dobra que o argmin punha no campo sobre todo "
    "subcaminho ENTERRADO por outro -- ele media ate uma aresta que esta dentro da forma e nao "
    "aparece no desenho (em stem.svg, 3,22 px de diferenca por isso). `[OBS]` A GRADE do alvo "
    "continua nao lida (TXRTexture, e os tres botoes de ICRRenderingParameters.SDFGeneration em "
    "0xA46E0), entao uma amostra por pixel e escolha deste projeto, medida e nao lida.";

PlacementRect artPlacementRect(const icf::svg::ViewBox& box, const LayerPlacement& p,
                               std::uint32_t size) {
    // Derived from `placeOnCanvas` rather than recomputed beside it: the art's
    // box and the art's pixels have to agree, and two copies of the same
    // arithmetic is how they stop agreeing.
    const PathGlobals g = placeOnCanvas(box, p, size);
    const double sx = static_cast<double>(g.m0[0]);
    const double sy = static_cast<double>(g.m1[1]);
    PlacementRect r;
    r.x = static_cast<double>(g.m2[0]) + sx * box.x;
    r.y = static_cast<double>(g.m2[1]) + sy * box.y;
    r.width = sx * (box.width > 0 ? box.width : 1.0);
    r.height = sy * (box.height > 0 ? box.height : 1.0);
    return r;
}

FillOverride fillPaint(const ResolvedFill& fill, const PlacementRect& shapeRect,
                       std::string& why) {
    FillOverride out;
    switch (fill.contents) {
        case ResolvedFill::Contents::Solid:
            out.kind = FillOverride::Kind::Solid;
            asColour(fill.primary, out.colour);
            return out;

        case ResolvedFill::Contents::Gradient:
            // `[BIN]` `.gradient(primary, secondary, placement:)` -- two
            // colours and nothing between them, which is the target's ramp kind
            // 0, the two-colour mix (doc 03 §23.4).
            out.stops.resize(2);
            out.stops[0].location = 0.0f;
            out.stops[1].location = 1.0f;
            asColour(fill.primary, out.stops[0].rgba);
            asColour(fill.secondary, out.stops[1].rgba);
            break;

        case ResolvedFill::Contents::AutomaticGradient:
            // THE REFUSAL THAT IS BEING WITHDRAWN HERE.
            // `2026-09-01-gradiente.md` §4.4 left `automatic-gradient` undrawn
            // and said why: the six parameters of the derivation were measured
            // and THE AXIS HAD NEVER BEEN READ, so deriving the colours would
            // have put real colours in invented places.
            //
            // `[BIN]` The axis is read, and it was not on the gradient at all:
            // `IconColor.LinearGradient` carries only `stops`, the geometry is
            // a separate `GradientPlacement`, all three `automaticGradient`
            // sites pass `placement: nil`, and nil is a defined value --
            // `GradientPlacement.default`, `(0,0)->(0,1)`, substituted by the
            // draw path at `0x1BAB8`. §4.4's reason is gone, which is why this
            // case now draws instead of naming itself.
            out.stops = rampPointsOf(automaticGradient(fill.primary));
            break;

        case ResolvedFill::Contents::System:
            // `[BIN]` The two canned chiclet ramps, with every stop's alpha
            // REWRITTEN by the fill's opacity rather than multiplied
            // (`SystemFill.h`). The placement a `.system` resolve leaves behind
            // is always nil, so this always takes the default axis below.
            out.stops = rampPointsOf(resolveSystemFill(fill.ramp, fill.opacity).stops);
            break;
    }

    // `placeGradient` performs the substitution itself, so the nil arrives here
    // as a nil and is not pre-resolved by the caller. That the substitution is
    // the finding is exactly why it is not spread across call sites.
    const GradientAxis axis = placeGradient(fill.placement, shapeRect);
    if (!axisToMap(axis, out.m)) {
        why = "gradiente cujo eixo colapsa num ponto: nao ha direcao onde projetar";
        out.stops.clear();
        return out;
    }
    out.kind = FillOverride::Kind::Ramp;
    return out;
}

LayerPlacement compose(const LayerPlacement& g, const LayerPlacement& l) {
    LayerPlacement out;
    out.scale = g.scale * l.scale;
    out.translateX = g.scale * l.translateX + g.translateX;
    out.translateY = g.scale * l.translateY + g.translateY;
    return out;
}

PathGlobals placeOnCanvas(const icf::svg::ViewBox& box, const LayerPlacement& p,
                          std::uint32_t size) {
    PathGlobals g;
    const double k = static_cast<double>(size) / kCanvasPoints;
    const double s = p.scale * k;
    const double w = box.width > 0 ? box.width : 1.0;
    const double h = box.height > 0 ? box.height : 1.0;

    // Centred on the canvas, then translated. `[INF]` The origin is the centre
    // and +y is down -- see the header note; neither is stated by the document.
    const double left = (kCanvasPoints - w * p.scale) * 0.5 + p.translateX;
    const double top = (kCanvasPoints - h * p.scale) * 0.5 + p.translateY;

    g.m0[0] = static_cast<float>(s);
    g.m0[1] = 0.0f;
    g.m1[0] = 0.0f;
    g.m1[1] = static_cast<float>(s);
    g.m2[0] = static_cast<float>(left * k - s * box.x);
    g.m2[1] = static_cast<float>(top * k - s * box.y);
    g.twoOverSize[0] = 2.0f / static_cast<float>(size);
    g.twoOverSize[1] = 2.0f / static_cast<float>(size);
    g.urx = static_cast<float>(size);
    return g;
}

Result<RenderedIcon> renderIcon(Device& device, const icf::IconBundle& bundle,
                                IconRenderOptions options) {
    if (options.size == 0) return std::unexpected("a canvas of zero size was asked for");

    RenderedIcon out;
    out.width = out.height = options.size;
    const std::size_t texels = static_cast<std::size_t>(options.size) * options.size;
    // The accumulator is PREMULTIPLIED while layers stack -- `over` is only
    // associative in that form -- and is un-multiplied once at the end, which is
    // what `RenderedIcon::rgba` promises and what a PNG wants.
    //
    // This step was missing until the mutation sweep pointed at the `over`
    // operator. The test that should have caught it checked only the ALPHA of a
    // half-transparent layer, never its colour, so a premultiplied result read
    // as straight -- a semi-transparent icon came out too dark and every test
    // stayed green.
    std::vector<float> acc(texels * 4, 0.0f);

    const icf::IconDocument doc = bundle.document();
    const std::vector<icf::Group> groups = doc.groups();

    // ---- the background, before anything else --------------------------
    //
    // `[ART]` All 145 corpus documents carry a root `fill`, and until today
    // none of them was read: the layers were composited over nothing. The
    // BACKGROUND converter is a different function from the layer one, in the
    // target as here, and the two disagree about the two words that matter --
    // `none` takes the system path on the background and answers nil on a
    // layer; `automatic` is a chiclet ramp on the background and an inheritance
    // operator on a layer.
    //
    // The rect is the whole canvas. That is what "the shape's own bounding
    // rect" means for a shape that is the background -- see `kBackgroundShapeNote`
    // for the part of it that is not read.
    {
        const FillResolution bg = resolveBackgroundFill(doc.json(), options.context);
        if (bg.outcome == FillOutcome::Refused) {
            out.backgroundGap = "fill de raiz que este renderizador nao pinta: " + bg.why;
        } else if (bg.outcome == FillOutcome::Resolved) {
            const PlacementRect canvas{0.0, 0.0, static_cast<double>(options.size),
                                       static_cast<double>(options.size)};
            std::string why;
            const FillOverride paint = fillPaint(bg.fill, canvas, why);
            if (!why.empty()) {
                out.backgroundGap = why;
            } else {
                paintBackground(acc, options.size, paint);
                // The one line this front adds to this file. The background is
                // painted over the whole square and then CUT, which is the order
                // that keeps the ramp's parameter mapped to the canvas -- see
                // `ChicletShape.h` for the reading, and `kBackgroundShapeNote`
                // for what it leaves open.
                clipToChiclet(acc, options.size);
                out.backgroundPainted = true;
                note(out.notes, kBackgroundShapeNote);

                // A UNICA LINHA QUE A FRENTE DOS REALCES DO CHICLET ACRESCENTA
                // A ESTE ARQUIVO. Ate aqui o fundo era uma rampa chapada: a
                // cadeia `chiclet*` de `Highlights` estava lida e nada a
                // consumia (`[OBS] 6` de `Docs/Laudos/2026-09-15-highlights.md`).
                //
                // A classe de aparencia sai da LUMINANCIA do proprio fill, que
                // e o que `0x0001A920` mede e `0x00062744` consome -- ver
                // `ChicletHighlights.h` §2.1. Um fill de sistema nao expoe as
                // paradas aqui, e o alvo tambem so classifica um fill SIMPLES,
                // entao esse caso entra como `simpleFill == false` e cai em
                // `chicletDefault`, que e o `mov w8, #0` de `0x0001A8C0`.
                {
                    const bool simpleFill =
                        bg.fill.contents != ResolvedFill::Contents::System;
                    ChicletLuminance lum;
                    if (paint.kind == FillOverride::Kind::Ramp) {
                        lum = chicletFillLuminance(paint.stops);
                    } else if (paint.kind == FillOverride::Kind::Solid) {
                        lum = chicletFillLuminance(paint.colour);
                    }
                    const ChicletAppearance appearance =
                        classifyChicletAppearance(lum, simpleFill);
                    SpecularArguments chicletArgs;
                    chicletArgs.sizeClass = options.sizeClass;
                    chicletArgs.pixelsPerPoint =
                        static_cast<double>(options.size) / kCanvasPoints;
                    if (drawChicletHighlights(acc, options.size, chicletArgs) > 0) {
                        note(out.notes, chicletHighlightsNote(appearance, lum));
                    }
                }
                if (paint.kind == FillOverride::Kind::Ramp) {
                    note(out.notes, kGradientAxisDirectionNote);
                }
                if (bg.fill.contents == ResolvedFill::Contents::System) {
                    note(out.notes, kChicletRectNote);
                }
                if (fillCarriesP3(bg.fill)) note(out.notes, kBackgroundP3Note);

                // THE CORRECTION OF §5.2, and it is deliberately a step
                // BACKWARDS in cleverness. `[BIN]` The background converter
                // reads `primaryColor` and `secondaryColor` and never touches
                // `orientation`; `[ART]` 61 background resolutions over the
                // corpus name one anyway, and `backgroundPlacements` is 0. So
                // this renderer must not honour it -- being more correct than
                // the target is a different pixel, which for a reproduction is
                // the wrong kind of right. Said out loud when a document
                // actually asked, so the discarding is visible in the report
                // and not only in the source.
                if (const icf::json::Value* node =
                        icf::resolve(doc.json(), "fill", options.context)) {
                    if (auto parsed = icf::fillFrom(*node)) {
                        if (parsed->orientation) {
                            note(out.notes, kDiscardedBackgroundOrientationNote);
                        }
                    }
                }
            }
        }
    }

    // THE ARRAY RUNS FRONT TO BACK, so compositing walks it BACKWARDS.
    //
    // `[ART]` Measured two ways and then proved against a published icon.
    // Across the corpus, a group whose name or whose layer's name says
    // "background" sits LAST in the array in 19 documents and first in 1. And
    // `insidegui/AssetCatalogTinkerer` names its four groups `Layer4, Layer3,
    // Layer2, Layer1` in array order -- descending, which is how an editor
    // lists a stack top-first.
    //
    // `[ART]` THE PROOF is a render. Composited in array order, that icon came
    // out as a flat blue gradient: `background` is the last group, drawn last,
    // over everything. Walked backwards it draws the actual logo. And
    // `Apollo-Reborn` -- checked against the icon its authors ship -- only puts
    // the magenta eyes on the visor when BOTH levels are reversed; in array
    // order `Apollo Helmet Space` is painted after `Eyes` and buries them.
    //
    // `[OBS]` What the TARGET does was not read: no `reversed` symbol survives
    // in `IconComposerKit`, and the converter at `0x10B7EC` was not followed
    // far enough to see the iteration direction. This is the corpus and a
    // reference picture agreeing, not a transcription -- which is why it is
    // `[ART]` and not `[BIN]`.
    for (std::size_t gr = 0; gr < groups.size(); ++gr) {
        const std::size_t gi = groups.size() - 1 - gr;
        const icf::Group& group = groups[gi];
        const LayerPlacement gp = placementOf(group.resolve("position", options.context));

        // `[ART]` THE GLASS PARAMETERS ARE ON THE GROUP, NOT ON THE LAYER. All
        // 280 occurrences of `glass` / `glass-specializations` in the 145 corpus
        // documents sit inside `groups[].layers[]` and every one of them is a
        // Bool; no layer carries a glass NUMBER. The group carries the material
        // (spec §4.1), and the layer's bit says which elements participate --
        // which is exactly the shape `Icon.Layer.material` +
        // `Icon.Element.participatesInGlass` predicts.
        //
        // Read once per group rather than once per layer: it resolves six keys
        // through the specialization machinery and the answer cannot differ
        // between two layers of the same group.
        GlassMaterialReadError materialError;
        const std::optional<GlassMaterialDocument> materialDoc =
            readGlassMaterial(group, options.context, &materialError);
        const DenormalisedGlass glassNumbers =
            materialDoc ? denormaliseGlass(glassMaterialFrom(*materialDoc)) : DenormalisedGlass{};
        const GlassRefraction refraction = glassRefractionFor(glassNumbers, options.size);

        // ---- the translucency, which is the OTHER half of the material -----
        //
        // `[BIN]` Until 2026-09-15 `material.translucency` reached this loop and
        // stopped: `GlassMaterial.h` transported it and named it as a field with
        // no known consumer. `Docs/Laudos/2026-09-15-translucencia.md` read the
        // consumer end to end, so it now draws.
        //
        // The arguments are per GROUP because the material is: `f = strength x
        // translucency` has nothing in it that varies between two layers of the
        // same group. What varies per layer is the `bounds` rect the ramp runs
        // in, which is filled in below, once the layer's art has a box.
        // THE SWITCH IS APPLIED HERE, not in the transport. `glassMaterialFrom`
        // hands over the value and the bit side by side, because the document
        // keeps both -- `[ART]` 33 corpus groups are `(false, 0.5)`, a value
        // remembered behind a closed switch. `[INF]` A closed switch collapses
        // the value to zero rather than to its default: `f = strength * 0 = 0`
        // makes `eff(x) = 1.0` everywhere, which is exactly "no translucency".
        // The SITE of this fold was not found in the target, so it is an
        // inference about where, not about what.
        const double groupTranslucency =
            glassNumbers.translucencyEnabled ? glassNumbers.translucency : 0.0;
        const OpacityMaskArguments groupMaskArgs = opacityMaskArguments(
            kGlyphTranslucency, options.sizeClass, groupTranslucency);
        const bool groupWantsMask = !opacityMaskIsIdentity(groupMaskArgs);

        // ---- the specular, which NOW DRAWS ---------------------------------
        //
        // `Docs/Laudos/2026-09-15-especular.md` closed where the highlight goes
        // -- the `glassHighlight` shader of the IconRendering metallib -- and
        // closed no number it is handed. `Docs/Laudos/2026-09-15-highlights.md`
        // read the 16113 bytes of `ICRRenderingParameters.Highlights` those
        // numbers come from, so this stops being a note and starts being pixels.
        //
        // FIVE highlights, not one: `0x00030E88` expands one `HighlightsSet`
        // into seven candidates and this version's glyph defaults leave five
        // alive -- a sharp key rim, a diffuse key wash, a sharp fill rim
        // opposite it, and the dark one drawn twice at +-90 degrees.
        //
        // `[ART]` It fires for real: over the 145 corpus documents 67 carry a
        // group-level `specular`, and of the 103 values 64 are `true` and 3 are
        // the string `"inside"`.
        const bool wantsSpecular = documentAsksForSpecular(glassNumbers);
        SpecularArguments specularArgs;
        specularArgs.sizeClass = options.sizeClass;
        specularArgs.pixelsPerPoint = static_cast<double>(options.size) / kCanvasPoints;
        specularArgs.placement = glassNumbers.specularPlacement;

        // ---- `blur-material`, which is now ONE unread thing and not two -----
        //
        // `[BIN]` The radius has been transported since `GlassMaterial.cpp`
        // existed -- `denormaliseBlurRadius` is `min(b, 1) * 64` and
        // `DenormalisedGlass::blurRadiusPoints` has carried the answer all along
        // -- and as of `Source/RenderBox/BlurKernel.h` the KERNEL that radius
        // feeds is read too: the radius IS the Gaussian's sigma. What is still
        // unread is the SURFACE: the target wraps the filter in a layer flagged
        // `needs-background` and clips it with a rect built from a frame this
        // renderer does not model. `BlurKernel.h` carries the three reasons and
        // the note carries the short version.
        //
        // `[ART]` It fires for real, and for a smaller number than the raw key
        // count suggests: 123 corpus GROUPS over 73 documents carry
        // `blur-material`, but 48 of the 123 values are an explicit `null` and a
        // null asks for no blur at all. What is left is 75 positive numbers --
        // `0.05` to `1.0`, so `3.2` to `64` points of radius -- in 46 documents.
        // Gating on a POSITIVE radius is what keeps those 48 out of the
        // report -- "the author left the blur off" and "the blur is not drawn"
        // have to stay distinguishable, which is the rule `shadowDraws` already
        // follows for a zero alpha.
        if (glassNumbers.blurRadiusPoints > 0.0) {
            note(out.notes, kBlurMaterialSurfaceNote);
        }

        // `blend-mode` LIVES ON THE GROUP TOO, and the group is where it is
        // actually used: over the 145 documents `plus-lighter` appears **17
        // times on a group against 5 on a layer** (spec 2026-09-03 §2.5).
        //
        // Until 2026-09-03 this loop read the key only off the LAYER, so a
        // group-level blend was composited as `normal` and NOTHING was
        // reported. That is the failure this project treats as worse than not
        // drawing: a plausible picture that is wrong, with a clean report.
        // Twelve corpus documents were coming out that way.
        //
        // Blending a group is not blending a layer. The whole group has to be
        // drawn into a target of its own and only then mixed, so it cannot be
        // handled by choosing a blend function per layer -- which is why the
        // answer here is a named refusal and not a quiet approximation.
        const std::string* groupBlend = nullptr;
        std::optional<BlendMode> groupMode;
        if (const icf::json::Value* gbm = group.resolve("blend-mode", options.context)) {
            if (const std::string* s = textOf(gbm)) {
                if (*s != "normal") {
                    groupBlend = s;
                    groupMode = blendModeFromKey(*s);
                    if (groupMode && !blendIsTranscribed(*groupMode)) groupMode.reset();
                }
            }
        }

        // A BLENDED GROUP THAT CONTAINS GLASS USED TO BE REFUSED HERE, and the
        // refusal's premise turned out to be false.
        //
        // The reasoning was: glass refracts its BACKDROP, so a group composited
        // into a target of its own would refract an empty one, and a group
        // composited into the canvas would mix the backdrop in twice. Which of
        // the two the target does was `[OBS]` -- unread -- so the group was not
        // drawn at all.
        //
        // `[BIN]` It was read (doc 03 §34.3), and NEITHER happens, because the
        // glass does not sample the destination in the first place.
        // `GlassDisplacementStyle::draw` (`0x000F3B38`) builds a
        // `GenericFilter<GlassDisplacementEffect>` over the ITEM being drawn and
        // calls `Builder::apply_filter_`; when that will not go inline it calls
        // `ensure_layer` on that same item. Both are the item's own content, and
        // neither reaches `make_backdrop_item` -- a route that exists for this
        // effect and is not the one this path takes.
        //
        // BUT OUR GLASS IS NOT APPLE'S, and a test caught the difference.
        // `glassOver` displaces the accumulation buffer IN PLACE -- it snapshots
        // whatever has been drawn so far and refracts that. Point it at a
        // group's fresh target and it has nothing to displace, so the group
        // would draw with the refraction silently gone. That is the defect this
        // file already names as the worst of the three: drawing silently.
        //
        // So the refusal survives, with its reason corrected: it is not that
        // Apple's behaviour is unread, it is that OUR glass reads the
        // destination and Apple's does not. Aligning the two -- making the
        // glass filter the item it wears -- would dissolve the coupling, and it
        // is a front of its own.
        //
        // WHAT NARROWS IT: the coupling only bites when the refraction actually
        // MOVES something. `[ART]` Only 5 of the corpus's 271 groups carry
        // `refractivity` at all and only 2 of those a non-zero strength, so a
        // glass layer whose refraction is the identity draws as ordinary art
        // and its group can blend like any other. Refusing every group that
        // merely CONTAINS glass refused those too, for nothing.
        //
        // `[ART]` 8 corpus documents put a non-normal blend and a glass layer on
        // the same group, `insidegui/AssetCatalogTinkerer` and `RuntimeViewer`
        // among them.
        bool groupWouldRefract = false;
        if (groupBlend && !glassRefractionIsIdentity(refraction)) {
            for (const icf::Layer& l : group.layers()) {
                // Same default as the layer gate below, and for the same read:
                // a missing `glass` is `true`. See the `[BIN]` note there.
                if (boolOr(l.resolve("glass", options.context), true)) {
                    groupWouldRefract = true;
                    break;
                }
            }
        }

        const bool blendTheGroup = groupBlend && groupMode && !groupWouldRefract;
        std::vector<float> groupAcc;
        if (blendTheGroup) groupAcc.assign(texels * 4, 0.0f);
        std::vector<float>& target = blendTheGroup ? groupAcc : acc;

        // ...and so does the layer array inside a group, for the same reason
        // and by the same proof. The corpus signal here is weak on its own --
        // three documents name a layer "background", two of them first -- so
        // what carries it is the Apollo render: `Eyes` sits at index 1 and
        // `Apollo Helmet Space` at index 3, and only the reversed order puts
        // the eyes on top, where the shipped icon has them.
        std::vector<icf::Layer> backToFront = group.layers();
        std::reverse(backToFront.begin(), backToFront.end());
        for (const icf::Layer& layer : backToFront) {
            ++out.total;
            const std::string name(layer.name());

            auto skip = [&](const std::string& why) {
                out.skipped.push_back({gi, name, why});
            };

            if (boolOr(layer.resolve("hidden", options.context), false)) {
                continue;  // hidden is an instruction, not a gap
            }
            // The group's blend outranks the layer's: a layer drawn `normal`
            // inside a group that is itself mixed with `plus-lighter` is not
            // this layer's pixel either. Reported per layer, because `skipped`
            // is a per-layer list and a group-shaped gap would be invisible in
            // the count that the ruler and the report both read.
            if (groupBlend && !blendTheGroup) {
                skip(groupWouldRefract
                         ? ("mescla de grupo '" + *groupBlend + "' sobre um grupo cujo"
                            " vidro refrata -- o nosso glassOver desloca o buffer de"
                            " acumulacao no lugar, e o alvo proprio do grupo chega vazio")
                         : ("mescla de grupo '" + *groupBlend + "' -- grafia ou modo"
                            " que este leitor nao desenha"));
                continue;
            }

            // THE MISSING KEY IS `true`, AND IT IS READ, NOT GUESSED.
            //
            // `[BIN]` `IconComposerFoundation.arm64`. The document field is
            // `IconComposition.Layer.Snapshot.isGlass`, and its CodingKey spells
            // itself `"glass"` at `0xBF84C` -- a Swift SMALL string, built by
            // immediates and absent from the constant pool:
            //
            //     0xBF84C  mov  x1, #-0x1b00000000000000   ; 0xE5<<56 == count 5
            //     0xBF850  mov  x0, #0x6c67                ; 'g','l'
            //     0xBF854  movk x0, #0x7361, lsl #16       ; 'a','s'
            //     0xBF858  movk x0, #0x73,   lsl #32       ; 's'
            //
            // `[BIN]` The field's TYPE settles what absence means. The field
            // descriptor at `0x12D2E4` gives `isGlass` the mangled type
            // `SpecializableProperty<Swift.Bool>.Snapshot` followed by `Sg` --
            // an OPTIONAL. Absent decodes to `nil`, not to `false`. (The same
            // dump spells a plain optional bool `SbSg` elsewhere, so the
            // distinction is legible, not assumed.)
            //
            // `[BIN]` The LIVE model's field is NOT optional: the class
            // descriptor at `0x12C604` types `Layer._isGlass` as
            // `SpecializableProperty<Swift.Bool>`. So a `nil` snapshot leaves
            // whatever `Layer.init()` installed -- and `Layer.init()` at
            // `0x95C10` installs `true`:
            //
            //     0x95CDC  mov  w8, #1
            //     0x95CE0  strb w8, [x20, #0x118]       ; _isGlass.defaultValue
            //
            // The offset is not guessed either. `Layer.init()` writes every
            // stored property in declaration order and the neighbours identify
            // themselves: `+0xF8 = 0` is `_blendMode == .normal`, `+0x108 = 1.0`
            // is `_opacity`, `+0x138 = 0` is `_isHidden == false`, and `+0x58 /
            // +0x68` is `_position == (translate (0,0), scale 1.0)`. `_isGlass`
            // is the one between `_opacity` and `_assetMirroring`, and it is 1.
            //
            // `[ART]` This is why Apple writes `"glass": false` EXPLICITLY 90
            // times over the 145 corpus documents. Under the old reading that
            // spelling was redundant; under this one it is the only way to turn
            // the glass OFF.
            //
            // `[INF]` The onward link -- document `glass` becoming
            // `Icon.Element.participatesInGlass` in `IconRendering` -- is still
            // inference and still marked as such below. What is `[BIN]` here is
            // narrower and enough: the DOCUMENT's own answer for a missing key.
            const bool isGlass = boolOr(layer.resolve("glass", options.context), true);

            // The LAYER's blend mode, resolved rather than refused. The eight
            // modes the format cannot spell cannot appear here; what can is a
            // spelling this project does not know, and that is a gap, never a
            // silent `normal`.
            BlendMode layerBlend = BlendMode::Normal;
            if (const icf::json::Value* bm = layer.resolve("blend-mode", options.context)) {
                if (const std::string* s = textOf(bm)) {
                    const std::optional<BlendMode> parsed = blendModeFromKey(*s);
                    if (!parsed) {
                        skip("mescla '" + *s + "' -- grafia que este leitor nao conhece");
                        continue;
                    }
                    if (!blendIsTranscribed(*parsed)) {
                        skip("mescla '" + *s + "' -- o modo existe no motor e nao"
                             " esta transcrito aqui");
                        continue;
                    }
                    layerBlend = *parsed;
                }
            }

            const std::string* imageName =
                textOf(layer.resolve("image-name", options.context));
            if (!imageName || imageName->empty()) {
                skip("a camada nao nomeia arte neste contexto");
                continue;
            }
            const std::filesystem::path art = bundle.assetPath(*imageName);
            if (!std::filesystem::is_regular_file(art)) {
                skip("referencia pendurada: " + *imageName);
                continue;
            }

            const LayerPlacement lp =
                compose(gp, placementOf(layer.resolve("position", options.context)));
            const double opacity =
                numberOr(layer.resolve("opacity", options.context), 1.0);
            if (opacity <= 0.0) continue;

            std::string ext = art.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            // The layer's own fill, if it has one, through the LAYER converter
            // -- which is a different function from the background's, and
            // answers differently to the same word.
            //
            // `NoFill` is not a gap and not a failure: it is the target's
            // `Icon.Element.fill == nil`, which is what `none` produces and
            // what an `automatic` produces when it terminates. The element
            // keeps the art's own colours. `[ART]` That is 152 of the 156 firings
            // of `automatic` in the corpus, so a renderer that painted
            // something here would repaint 152 layers that must stay as drawn.
            const FillResolution fill = resolveLayerFill(layer, options.context);
            if (fill.outcome == FillOutcome::Refused) {
                skip("fill de camada que este renderizador nao pinta: " + fill.why);
                continue;
            }

            // ---- the glass, before the layer's own art -------------------
            //
            // THE ORDERING, AND IT IS A DECISION. `[INF]` The accumulator as it
            // stands IS the backdrop -- that is exactly what the target hands
            // its glass as a texture (spec §4.3: `glassBackground_v1` receives
            // the backdrop wrapped in an `RB::MultiLevelLayer`, and in an
            // isolated icon the only possible content of that texture is the
            // document's own stack so far). So: refract what is underneath
            // through this layer's shape, composite that, THEN draw this
            // layer's art over the result.
            //
            // THE ALTERNATIVE, which is not what this does: the shape refracts
            // and the layer's art is NOT painted -- the layer contributing only
            // a lens. Both readings survive what was measured. `[OBS]` Spec
            // §4.3 records the question as open, and nothing read settles it.
            //
            // Why this one. `Icon.Element.participatesInGlass` is a
            // PARTICIPATION flag on an element that still carries `contents`
            // and `fill` -- a lens-only element would not need either. And
            // `[ART]` 138 of the corpus's 171 glass layers carry their own
            // `fill`, which under the lens-only reading would be 138 authored
            // values that nothing consumes. Painting the art is the reading
            // that leaves no dead data.
            std::optional<icf::svg::SvgDocument> svg;
            if (ext == ".svg") {
                svg = icf::svg::SvgDocument::parse(readAll(art));
                if (!svg) {
                    skip("SVG que este leitor nao abre: " + *imageName);
                    continue;
                }
                // WHAT THE SVG READER SAW AND DID NOT DRAW, said out loud.
                //
                // `SvgDocument::unsupported()` already names every element and
                // value the reader walked past -- `filter`, `mask`, `pattern`,
                // `use`, a class no stylesheet matched. `icrender` prints them
                // for a LOOSE svg and this path threw them away, so a bundle
                // whose art carries a drop shadow reported "4 of 4 layers
                // drawn" and said nothing about the shadow being gone.
                //
                // `[ART]` It is not hypothetical: 5 of the 8 corpus documents
                // outside the drawable slice reported N of N with a filter, a
                // mask or a pattern silently ignored -- PDF-Archiver, PiStats,
                // CommE2E, quick-push and Delta. The ruler saw it because it
                // reads the SVG itself; the renderer's own report did not.
                //
                // These are gaps, not skips: the shape IS drawn, and dropping
                // it would trade a wrong picture for a missing one. They go to
                // `shapeGaps` for the same reason the per-shape reasons do.
                for (const std::string& u : svg->unsupported()) {
                    out.shapeGaps.push_back(name + " / " + *imageName +
                                            ": elemento nao desenhado: " + u);
                }
            }

            // THE RASTER IS DECODED AND PLACED HERE, AND NOT WHERE IT IS DRAWN.
            //
            // It moved up one block for one reason: the distance field that the
            // refraction, the translucency and the specular all eat is built
            // from this raster's ALPHA, so the pixels have to exist before the
            // glass block runs. `[BIN]` That is the target's own order --
            // `0x0002881C` casts the layer to `CUINamedLayerImage` (`0x000CC928`),
            // asks it for `image` and BAILS if it is nil (`0x00028AEC`), and only
            // then sends `sdfTextureWithBufferAllocator:` (`0x000867F8`). The
            // image comes first there too.
            //
            // It is decoded ONCE and reused by the draw below, so this is not an
            // extra read.
            std::optional<std::vector<float>> rasterPlaced;
            std::uint32_t rasterW = 0, rasterH = 0;
            if (!svg && ext == ".png") {
                const icf::DecodedPng png = icf::readPng(art.string());
                if (!png.error.empty()) {
                    skip(*imageName + ": " + png.error);
                    continue;
                }
                rasterW = png.width;
                rasterH = png.height;
                rasterPlaced = placeRaster(png, lp, options.size);
            }

            // The paint is built AFTER the art, because a gradient needs the
            // rect it is placed against and that rect is the art's own box.
            FillOverride paint;
            if (fill.outcome == FillOutcome::Resolved) {
                if (svg) {
                    std::string why;
                    paint = fillPaint(fill.fill,
                                      artPlacementRect(svg->viewBox, lp, options.size), why);
                    if (!why.empty()) {
                        skip(why);
                        continue;
                    }
                    if (paint.kind == FillOverride::Kind::Ramp && !fill.fill.placement) {
                        note(out.notes, kGradientAxisDirectionNote);
                    }
                    if (fill.fill.contents == ResolvedFill::Contents::System) {
                        note(out.notes, kChicletRectNote);
                    }
                } else {
                    // Not a skip: the raster IS drawn, with its own colours.
                    // Skipping it would trade a wrong colour for a missing
                    // layer, which is a bigger lie in a picture.
                    note(out.notes, kRasterFillNote);
                }
            }

            // A material key that is PRESENT and unreadable is an error, not a
            // default. Falling through to "no refraction" would be the silent
            // default this project refuses -- and it would look identical to
            // the (very common, and legitimate) zero-strength case.
            if (isGlass && !materialDoc) {
                skip("vidro: a chave '" + materialError.key + "' do grupo nao le -- " +
                     materialError.why);
                continue;
            }

            // BOTH GLASS EFFECTS EAT THE SAME DISTANCE FIELD, so it is built
            // once. The refraction displaces the backdrop through it; the
            // translucency mask is cut by it. Two fields would be the same
            // arithmetic twice over the most expensive function in this tower.
            //
            // WHY THE MASK IS GATED ON `glass` AND NOT ON THE GROUP ALONE, and
            // it is `[INF]`. `translucency` is a field of `Icon.GlassMaterial`,
            // and `[ART]` the document's `glass` bit is
            // `Icon.Element.participatesInGlass` -- the format's own answer to
            // "which elements does this material act on". `[OBS]` The laudo did
            // not read the caller side of `0x0000FD28` far enough to say which
            // elements the function runs for; its three callers were counted and
            // not followed.
            //
            // THE ALTERNATIVE, and it is not absurd: the parameter block calls
            // the effect `glyphTranslucency`, and a reader could take "glyph" to
            // mean the whole foreground. `[ART]` That reading loses on the
            // corpus, though -- all 271 groups carry a `translucency` key
            // whether or not they contain glass, so honouring it everywhere
            // would fade layers whose author never asked for glass at all.
            const bool wantsRefraction = isGlass && !glassRefractionIsIdentity(refraction);
            const bool wantsTranslucency = isGlass && groupWantsMask;
            // The highlight rides the SAME distance field as the other two, so
            // it joins the same gate rather than building a second one.
            const bool wantsHighlight = isGlass && wantsSpecular;
            std::optional<OpacityMask> mask;
            std::optional<FieldImage> specularField;

            if (wantsRefraction || wantsTranslucency || wantsHighlight) {
                // THE RASTER TAKES THE SAME DOOR AS THE VECTOR, AS OF THIS FRONT.
                //
                // This block used to open with "a raster has no path to flatten,
                // so there is no shape to build a field from", and it skipped the
                // refraction, muted the specular and left the translucency opaque
                // for `[ART]` 45 of the corpus's 171 glass layers. That sentence
                // was true about OUR generator and false about the format.
                //
                // `[BIN]` The target does not build its field from geometry
                // either. `sdfTextureWithBufferAllocator:` (`0x000867F8`, inside
                // `0x0008658C`) is sent to the object loaded at `0x00029334` from
                // the frame slot written at `0x00028AF4` -- the result of a cast
                // to the class ref at `0x000CC928`, which the chained-fixup
                // imports bind to `_OBJC_CLASS_$_CUINamedLayerImage`. That object
                // is asked for `image` at `0x00028AE0` and the whole path BAILS
                // if it is nil (`0x00028AEC cbz x0, #0x291BC`). A
                // `CUINamedLayerImage` carries a bitmap and no contour. The
                // target's field is the RASTERISED ALPHA, and the reflection says
                // it twice: `IconRendering.SDF.SourceLayer` (`0xA3104`) is
                // `{displayList, isOpaque}` -- a drawing, not a shape.
                //
                // So the distinction this renderer drew here was ours, and it is
                // gone. A vector still goes through `flattenSvgToContours`,
                // because when we DO have the contour the exact brute force is a
                // better answer than a bitmap's quantised one; a raster goes
                // through `generateFieldFromAlpha`, which is the same exact
                // transform `shadowRingMask` already ran over the same
                // `alpha >= 0.5` contour. The shadow front had already proved a
                // raster needs no contour; this only says so for the other three.
                //
                // `[OBS]` What still is NOT read is the GRID: the target's SDF
                // texture is made inside `TXRTexture`, which is in neither
                // `IconRendering.arm64` nor `RenderBox.arm64`, and the three
                // `ICRRenderingParameters.SDFGeneration` knobs that would pin its
                // resolution and its threshold down (`clampThreshold`,
                // `precisePixelFormatThreshold`, `maxRelativeSmoothing`, `0xA46E0`)
                // are named and unread. The field here is built at the target's
                // own render resolution, which is the same choice `shadowRingMask`
                // records.
                const bool haveShape = svg.has_value();
                std::string fieldGap;

                std::optional<FieldImage> field;
                if (haveShape) {
                    const GlassContours shape =
                        flattenSvgToContours(*svg, placeOnCanvas(svg->viewBox, lp, options.size),
                                             options.subdivisions);
                    if (shape.mixedRules) {
                        fieldGap =
                            "vidro: a arte mistura non-zero e even-odd e o campo assina com uma "
                            "regra so -- escolher uma inverteria o dentro/fora de parte da forma";
                    } else if (shape.contours.empty()) {
                        fieldGap =
                            "vidro: a arte nao fecha nenhum contorno pintado (" + *imageName + ")";
                    } else {
                        // THE VECTOR TAKES THE RASTER'S DOOR TOO, AS OF THIS
                        // FRONT. It used to call `generateField`, which walks
                        // every segment for every pixel: exact, and `[ART]`
                        // 73 % of a 1024 px render of the ten-layer
                        // `Apollo-Reborn__Apollo-Reborn__AppIcon` (10 455 ms of
                        // it) -- and 56.7 s of the 57.2 s the user's own grunge
                        // icon took, whose 16 788 segments in ONE layer are the
                        // worst case there is. `generateFieldFromContours`
                        // rasterises the same contours, under the same fill
                        // rule and the same half-open crossing rule, and runs
                        // the SAME exact Euclidean transform the raster branch
                        // below already runs. `[BIN]` It is also what the
                        // target does -- `DistanceField.h` carries the
                        // addresses -- so the cheaper path is the more faithful
                        // one and not a trade.
                        FieldOptions fo;
                        fo.rule = shape.rule;
                        FieldImage fromShape = generateFieldFromContours(
                            shape.contours, options.size, options.size, fo, kFieldSuperSample);
                        if (fromShape.width == 0) {
                            // Contours that close but cover no sample point --
                            // a hairline, a shape smaller than a texel. The
                            // brute force would have signed it anyway, from
                            // outside; a grid cannot, and saying so is better
                            // than a field that is everywhere-outside.
                            fieldGap = "vidro: a arte fecha contornos mas nenhum ponto de amostra "
                                       "cai dentro deles nesta resolucao (" + *imageName + ")";
                        } else {
                            field = std::move(fromShape);
                            note(out.notes, kGlassVectorFieldNote);
                        }
                    }
                } else if (rasterPlaced) {
                    FieldImage fromAlpha =
                        generateFieldFromAlpha(*rasterPlaced, options.size, options.size);
                    if (fromAlpha.width == 0) {
                        // No texel reaches `alpha >= 0.5`: there is no contour to
                        // sign, so there is no field. A raster that faint has
                        // nothing for the glass to bend around, and saying so is
                        // better than handing the effects an all-outside field
                        // that would draw nothing without a word.
                        fieldGap = "vidro sobre raster: nenhum texel da arte chega a alpha >= 0.5, "
                                   "entao nao ha contorno para assinar (" + *imageName + ")";
                    } else {
                        field = std::move(fromAlpha);
                        note(out.notes, kGlassRasterFieldNote);
                    }
                } else {
                    fieldGap = "vidro sobre arte que este leitor nao abre como raster nem como "
                               "vetor (" + *imageName + ")";
                }

                if (!field) {
                    if (wantsRefraction) {
                        skip(fieldGap);
                        continue;
                    }
                    // A specular with no field still cannot draw -- but "no
                    // field" no longer means "raster". It now means the ONE
                    // remaining shape of failure: art whose contour cannot be
                    // signed at all (mixed fill rules, nothing painted, an alpha
                    // that never reaches the threshold). `fieldGap` says which.
                    if (wantsHighlight) note(out.notes, specularDoesNotDrawNote());
                    // Translucency alone: draw opaque, and say so.
                    if (wantsTranslucency) note(out.notes, fieldGap);
                } else {
                    if (wantsHighlight) specularField = field;
                    if (wantsRefraction) {
                        glassOver(target, options.size, options.size,
                                  glassDisplacementMap(*field, refraction), refraction);
                        note(out.notes, glassRulerNote(options.size));
                        ++out.glassRefracted;
                    }
                    if (wantsTranslucency) {
                        // The `bounds` of shader argument 6 -- the rect the
                        // vertical ramp is measured in. `[OBS]` Which rect the
                        // target reports there was not read; this is the art's
                        // viewBox placed on the canvas, the SAME answer this
                        // file already gives a gradient's placement rect, so the
                        // two do not disagree about one unread thing.
                        //
                        // A RASTER'S BOX IS ITS PIXELS, and that is the only
                        // difference the two art paths make here: a vector's
                        // rect comes from its viewBox placed on the canvas, a
                        // raster's from `placeRaster`'s own placement of its
                        // pixel dimensions. Both are the box the art occupies,
                        // which is what the ramp is measured against.
                        OpacityMaskArguments args = groupMaskArgs;
                        const PlacementRect r =
                            svg ? artPlacementRect(svg->viewBox, lp, options.size)
                                : rasterPlacementRect(rasterW, rasterH, lp, options.size);
                        args.bounds[0] = static_cast<float>(r.x);
                        args.bounds[1] = static_cast<float>(r.y);
                        args.bounds[2] = static_cast<float>(r.width);
                        args.bounds[3] = static_cast<float>(r.height);
                        mask = glassOpacityMask(*field, args);
                        note(out.notes, kTranslucencyBoundsNote);
                    }
                }
            }

            // ---- THE SHADOW, UNDER THE ART ------------------------------
            //
            // `[BIN]` Until 2026-09-15 this renderer drew NO shadow at all:
            // `GlassMaterial.h` transported `shadowStyle` and `shadowOpacity`
            // and named them as fields with no known consumer, and the one
            // `shadow` in this file was a comment about an SVG's own drop
            // shadow. `Docs/Laudos/2026-09-15-sombra.md` read the alpha and the
            // geometry end to end, so it now draws. `GlassShadow.h` carries the
            // whole of it -- the three-factor alpha, the size-class inversion,
            // the offset, the blur, and the two steps that are named instead of
            // drawn.
            //
            // GATED ON `glass`, FOR THE SAME REASON THE MASK IS, and it is the
            // same `[INF]`. The shadow is a field of `Icon.GlassMaterial` and
            // `[ART]` the document's `glass` bit is
            // `Icon.Element.participatesInGlass`. `[ART]` All 271 corpus groups
            // carry a `shadow` key while only 113 contain glass, so honouring
            // it everywhere would put a drop shadow under 158 groups whose
            // author never asked for glass.
            //
            // CAST FROM THE ART AS IT WILL BE COMPOSITED -- after the
            // translucency mask, if there is one. `[OBS]` Which image the target
            // feeds its shadow (`[descriptor+0xB0]`, fetched through `0x85ED8`)
            // was not traced, so the order of mask and shadow is unread; with
            // an identity mask, which is every corpus group whose translucency
            // is absent or switched off, the two readings are the same pixel.
            //
            // THE LAYER'S `opacity` GOES IN TWICE, and that is the transcription
            // and not a slip. `[BIN]` The third factor of the shadow's alpha is
            // `FinalizedIcon.Layer.opacity` (`GlassShadow.h` names the three
            // readings), and the SAME field multiplies the element's own draw at
            // `0x495F0` -- which is the `opacity` this loop has always handed to
            // `blendOver` below. One field, two draws, one multiplication each.
            const ShadowInputs shadowIn{glassNumbers.shadowStyle, glassNumbers.shadowOpacity,
                                        opacity, options.sizeClass};
            const bool castsShadow = isGlass && shadowDraws(shadowIn);
            auto castShadow = [&](const std::vector<float>& artRgba) {
                if (!castsShadow) return;
                const ShadowGeometry geometry =
                    shadowGeometry(options.size, options.sizeClass);
                blendOver(target,
                          shadowImage(artRgba, options.size, options.size,
                                      shadowIn.style, geometry),
                          static_cast<float>(shadowAlpha(shadowIn)),
                          shadowBlendMode(shadowIn.style));
                if (geometry.ringWidth) note(out.notes, kShadowRingNote);
                if (kShadow.drawOverContent &&
                    shadowOverdrawAlpha(groupTranslucency, shadowIn.style,
                                        options.sizeClass) > 0.0) {
                    note(out.notes, kShadowOverdrawNote);
                }
                ++out.glassShadowed;
            };

            if (svg) {
                RenderOptions ro;
                ro.width = ro.height = options.size;
                ro.subdivisions = options.subdivisions;
                ro.override = paint;
                auto drew = renderSvgPlaced(
                    device, *svg, placeOnCanvas(svg->viewBox, lp, options.size), ro);
                if (!drew) return std::unexpected(drew.error());
                for (const auto& s : drew->skipped) {
                    out.shapeGaps.push_back(name + " / " + *imageName + ": " + s.why);
                }
                // THE TRANSLUCENCY, ON THE ART AND NOT ON THE COMPOSITE. The
                // mask multiplies this layer's own alpha BEFORE the layer's
                // `opacity` and blend mode are applied, because it is a property
                // of the glyph and not of how the glyph meets what is under it.
                // `[BIN]` The target agrees from the other side: the `alpha` of
                // its `drawShape:fill:alpha:blendMode:` is the constant 1.0
                // (`0x103F4`), so the translucency is already inside the pixel
                // by the time the composite sees it.
                if (mask) {
                    // THE BLIND SPOT, MEASURED BEFORE THE MASK IS APPLIED.
                    // Where the field says "outside", the shader's own
                    // `mix(1.0, a, cov)` returns 1.0 and the pixel keeps its
                    // opacity -- correct for a pixel that really is outside, and
                    // a silent miss for one the art painted anyway. Counted
                    // against the art's alpha so the sentence carries a number
                    // instead of a worry.
                    std::size_t painted = 0;
                    const std::size_t missed =
                        opacityMaskMissedPixels(drew->rgba, *mask, painted);
                    if (missed > 0 && painted > 0) {
                        char buf[420];
                        std::snprintf(
                            buf, sizeof(buf),
                            "translucidez aplicada com buraco: %zu de %zu pixels pintados "
                            "(%.1f%%) caem FORA do campo de distancia e ficaram opacos -- "
                            "flattenSvgToContours funde todo subcaminho pintado num conjunto "
                            "so, assinado por uma regra so, entao subcaminhos sobrepostos de "
                            "orientacao contraria se cancelam sob non-zero (a ressalva de "
                            "DistanceField.h). O mesmo buraco vale para a refracao.",
                            missed, painted,
                            100.0 * static_cast<double>(missed) / static_cast<double>(painted));
                        out.shapeGaps.push_back(name + " / " + *imageName + ": " + buf);
                    }
                    applyOpacityMask(drew->rgba, *mask);
                    ++out.glassTranslucent;
                }
                castShadow(drew->rgba);
                // THE HIGHLIGHT GOES ON AFTER THE SHADOW IS CAST, ON PURPOSE.
                // It is `plusLighter`/`plusDarker` over the layer's own pixels,
                // so it adds alpha where it lands; casting the shadow from the
                // art plus its own highlight would swell the silhouette by the
                // rim. `[BIN]` The target keeps them apart too -- the highlight
                // is its own `drawShape:fill:alpha:blendMode:` at `0x0000ED00`,
                // inside a pass gated by `hasSpecular` at `0x00049200`, and the
                // shadow is a different `drawShape:` in a different function.
                if (specularField) {
                    // `layerOpacity` STAYS AT ONE here and it is not an
                    // oversight: `[BIN]` the target's `alpha:` carries
                    // `[descriptor+0x38]` because its highlight is a sibling of
                    // the layer in one display list, and here the highlight is
                    // composited INTO the layer's own buffer, which `blendOver`
                    // then multiplies by `opacity` two lines down. Passing it
                    // twice would square it.
                    const std::size_t moved = drawSpecular(drew->rgba, *specularField, specularArgs);
                    if (moved > 0) ++out.glassSpecular;
                    note(out.notes, specularDrawnNote());
                }
                blendOver(target, drew->rgba, static_cast<float>(opacity), layerBlend);
                ++out.drawn;
            } else if (rasterPlaced) {
                // THE RASTER NOW RUNS THE WHOLE GLASS, in the same order the
                // vector branch above runs it: mask, then shadow, then
                // highlight, then composite. The asymmetry this branch used to
                // carry -- "a raster CAN cast a shadow, where it cannot carry a
                // translucency mask" -- is gone, because the reason for it was
                // our missing generator and not the format. `[BIN]`
                // `DistanceField.h` carries the addresses.
                //
                // `[ART]` It is 45 of the corpus's 171 glass layers under the
                // any-appearance reading of the `glass` key, across 31
                // documents; 39 of 146 across 29 documents if only a
                // specialization's BASE entry counts. Both counts were recounted
                // for this front and both are in the laudo.
                std::vector<float> placed = std::move(*rasterPlaced);
                if (mask) {
                    std::size_t painted = 0;
                    const std::size_t missed =
                        opacityMaskMissedPixels(placed, *mask, painted);
                    if (missed > 0 && painted > 0) {
                        char buf[420];
                        std::snprintf(
                            buf, sizeof(buf),
                            "translucidez aplicada com buraco: %zu de %zu pixels pintados "
                            "(%.1f%%) caem FORA do campo de distancia e ficaram opacos -- "
                            "num raster o campo e assinado pelo contorno alpha >= 0.5, entao "
                            "todo pixel pintado com alpha ABAIXO do limiar fica de fora.",
                            missed, painted,
                            100.0 * static_cast<double>(missed) / static_cast<double>(painted));
                        out.shapeGaps.push_back(name + " / " + *imageName + ": " + buf);
                    }
                    applyOpacityMask(placed, *mask);
                    ++out.glassTranslucent;
                }
                castShadow(placed);
                if (specularField) {
                    const std::size_t moved = drawSpecular(placed, *specularField, specularArgs);
                    if (moved > 0) ++out.glassSpecular;
                    note(out.notes, specularDrawnNote());
                }
                blendOver(target, placed, static_cast<float>(opacity), layerBlend);
                ++out.drawn;
            } else {
                skip("arte com extensao que este leitor nao le: " + *imageName);
            }
        }

        if (blendTheGroup) blendPremulOver(acc, groupAcc, *groupMode);
    }

    out.rgba.assign(texels * 4, 0.0f);
    for (std::size_t t = 0; t < texels; ++t) {
        const float a = acc[t * 4 + 3];
        for (int k = 0; k < 3; ++k) {
            out.rgba[t * 4 + k] = a > 0.0f ? acc[t * 4 + k] / a : 0.0f;
        }
        out.rgba[t * 4 + 3] = a;
    }
    return out;
}

}  // namespace rb
