#include "Source/RenderBox/IconRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>

#include "Source/CoreSVG/Document.h"
#include "Source/RenderBox/Parallel.h"
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
#include "Source/RenderBox/IconSurface.h"
#include "Source/RenderBox/SystemFill.h"
#include "Source/RenderBox/ViewportPlan.h"
#include "Source/IconComposerFoundation/Png.h"
#include "Source/IconComposerFoundation/Values.h"

namespace rb {

// `[BIN]` `ClearMode.contentLighteningStrength` (laudo de 20/09 §2): o R da
// matriz do conteudo do Clear (0x4AF20). Compartilhado com o shader de blend.
constexpr float kClearContentLightening = 0.85f;
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

// O LUGAR DE UMA NOTA OU LACUNA QUE UMA CONTAGEM AINDA VAI DECIDIR (IconSurface.h,
// "as contagens chegam depois"). A entrada entra NA POSICAO em que a CPU a teria
// posto, com este marcador; o `sink` troca pelo texto ou por `kDroppedEntry`, e o
// fim de `renderIconOn` tira as descartadas -- entao a ordem das notas e das
// lacunas e a mesma nos dois caminhos. Os dois comecam com um byte de controle,
// que nenhuma nota real tem.
const char* const kPendingEntry = "\x01" "pendente";
const char* const kDroppedEntry = "\x02" "descartada";

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
    // Per texel, so split into texel ranges: no texel reads another's floats.
    const std::size_t texels = acc.size() / 4;
    parallelRanges(texels, texels * 12, [&](std::size_t t0, std::size_t t1) {
    for (std::size_t i = t0 * 4; i < t1 * 4; i += 4) {
        const float a = src[i + 3] * alpha;
        if (a <= 0.0f) continue;
        const float inv = 1.0f - a;
        for (int k = 0; k < 3; ++k) acc[i + k] = src[i + k] * a + acc[i + k] * inv;
        acc[i + 3] = a + acc[i + 3] * inv;
    }
    });
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
    const std::size_t texels = acc.size() / 4;
    parallelRanges(texels, texels * 40, [&](std::size_t t0, std::size_t t1) {
    for (std::size_t i = t0 * 4; i < t1 * 4; i += 4) {
        const float a = src[i + 3] * alpha;
        BlendColour s;
        s.rgba[3] = a;
        for (int k = 0; k < 3; ++k) s.rgba[k] = src[i + k] * a;
        BlendColour d;
        for (int k = 0; k < 4; ++k) d.rgba[k] = acc[i + k];
        const BlendColour out = rb::blend(mode, s, d);
        for (int k = 0; k < 4; ++k) acc[i + k] = static_cast<float>(out.rgba[k]);
    }
    });
}

// A decoded raster placed on the canvas, sampled bilinearly.
//
// `[INF]` Bilinear is THIS renderer's choice. What the target resamples with is
// not decoded -- `ICRRenderingParameters` names an `SDFGeneration` and a
// `refractionSupersampling` but nothing about image sampling, and guessing
// nearest would be just as much a guess. It is named here rather than silently
// assumed.
std::vector<float> placeRaster(const icf::DecodedPng& img, const LayerPlacement& p,
                               const PixelGrid& grid) {
    std::vector<float> out(grid.texels() * 4, 0.0f);
    if (img.width == 0 || img.height == 0) return out;

    // The art enters at its pixel size read as canvas POINTS, scaled, and
    // centred before the translation -- the same rule the vector path uses.
    // A ESCALA SAI DE `grid.size`, e nao da extensao: o buffer pode ser um
    // pedaco do canvas, e a arte tem que cair onde o render cheio a poe.
    const double k = static_cast<double>(grid.size) / kCanvasPoints;
    const double w = img.width * p.scale, h = img.height * p.scale;
    const double left = (kCanvasPoints - w) * 0.5 + p.translateX;
    const double top = (kCanvasPoints - h) * 0.5 + p.translateY;

    for (std::uint32_t y = 0; y < grid.height; ++y) {
        // The pixel centre, back into the art's own space. ABSOLUTO: o centro
        // amostrado e o da grade de `size`, e so o indice no buffer anda (spec
        // 2026-09-16, "O invariante que governa o desenho").
        const double gy = static_cast<double>(static_cast<std::int64_t>(y) + grid.originY);
        const double cy = ((gy + 0.5) / k - top) / p.scale - 0.5;
        if (cy < -1.0 || cy > img.height) continue;
        for (std::uint32_t x = 0; x < grid.width; ++x) {
            const double gx = static_cast<double>(static_cast<std::int64_t>(x) + grid.originX);
            const double cx = ((gx + 0.5) / k - left) / p.scale - 0.5;
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
            const std::size_t d = (static_cast<std::size_t>(y) * grid.width + x) * 4;
            out[d + 3] = acc[3];
            for (int c = 0; c < 3; ++c) out[d + c] = acc[3] > 0.0f ? acc[c] / acc[3] : 0.0f;
        }
    }
    return out;
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
void paintBackground(std::vector<float>& acc, const PixelGrid& grid,
                     const FillOverride& paint) {
    // One row per worker: every pixel is written from `paint` and its own
    // position alone.
    parallelRanges(grid.height, grid.texels() * 16, [&](std::size_t y0, std::size_t y1) {
    for (std::uint32_t y = static_cast<std::uint32_t>(y0); y < static_cast<std::uint32_t>(y1); ++y) {
        for (std::uint32_t x = 0; x < grid.width; ++x) {
            float colour[4] = {paint.colour[0], paint.colour[1], paint.colour[2],
                               paint.colour[3]};
            if (paint.kind == FillOverride::Kind::Ramp) {
                // ABSOLUTO: o mesmo ponto que o render cheio avalia, e a soma
                // inteira antes do `+ 0.5` e exata (spec 2026-09-16, "O
                // invariante que governa o desenho").
                const double px =
                    static_cast<double>(static_cast<std::int64_t>(x) + grid.originX) + 0.5;
                const double py =
                    static_cast<double>(static_cast<std::int64_t>(y) + grid.originY) + 0.5;
                const double t = paint.m[0] * px + paint.m[1] * py + paint.m[2];
                if (paint.smooth) {
                    rampSmoothAtPositions(paint.stops, static_cast<float>(t), colour);
                } else {
                    rampAtPositions(paint.stops, static_cast<float>(t), colour);
                }
            }
            const std::size_t i = (static_cast<std::size_t>(y) * grid.width + x) * 4;
            for (int k = 0; k < 3; ++k) acc[i + k] = colour[k] * colour[3];
            acc[i + 3] = colour[3];
        }
    }
    });
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

// ---- the cache keys (`RenderCache.h`) ----------------------------------------
//
// One hasher per parameter struct, naming every field. The static_assert beside
// each is the tripwire: a field added to the struct changes its size and stops
// the build HERE, which is where the field has to be added to the key -- a key
// that silently lacks a field serves a stale picture for every edit of it.

void hashInto(KeyHasher& h, const FillOverride& f) {
    static_assert(sizeof(FillOverride) == 104, "a FillOverride field is missing from the key");
    h.value(f.kind).span(f.colour, 4).span(f.m, 6).value(f.smooth);
    h.value(f.stops.size());
    for (const RampPoint& p : f.stops) h.value(p.location).span(p.rgba, 4);
}

void hashInto(KeyHasher& h, const PathGlobals& g) {
    static_assert(sizeof(PathGlobals) == 52, "a PathGlobals field is missing from the key");
    h.span(g.m0, 2).span(g.m1, 2).span(g.m2, 2).span(g.twoOverSize, 2).span(g.origin, 2);
    h.value(g.depth).value(g.urx).value(g.arg);
}

void hashInto(KeyHasher& h, const RenderOptions& o) {
    static_assert(sizeof(RenderOptions) == 136, "a RenderOptions field is missing from the key");
    h.value(o.width).value(o.height).value(o.originX).value(o.originY);
    h.value(o.projectionWidth).value(o.projectionHeight).value(o.subdivisions);
    hashInto(h, o.override);
}

void hashInto(KeyHasher& h, const FieldOptions& o) {
    static_assert(sizeof(FieldOptions) == 20, "a FieldOptions field is missing from the key");
    h.value(o.rule).value(o.aaWidth).value(o.originX).value(o.originY).value(o.subpixelSeed);
}

void hashInto(KeyHasher& h, const std::vector<FieldContour>& contours) {
    h.value(contours.size());
    for (const FieldContour& c : contours) h.span(c.xy.data(), c.xy.size());
}

void hashInto(KeyHasher& h, const ShadowGeometry& g) {
    static_assert(sizeof(ShadowGeometry) == 40, "a ShadowGeometry field is missing from the key");
    h.value(g.offsetX).value(g.offsetY).value(g.blurRadius).value(g.ringWidth.has_value());
    if (g.ringWidth) h.value(*g.ringWidth);
}

void hashInto(KeyHasher& h, const PixelGrid& g) {
    static_assert(sizeof(PixelGrid) == 20, "a PixelGrid field is missing from the key");
    h.value(g.size).value(g.originX).value(g.originY).value(g.width).value(g.height);
}

void hashInto(KeyHasher& h, const SpecularArguments& a) {
    static_assert(sizeof(SpecularArguments) == 120,
                  "a SpecularArguments field is missing from the key");
    h.value(a.sizeClass).value(a.pixelsPerPoint).value(a.lightLongitude);
    h.value(a.lightIntensity).value(a.lightLatitude).value(a.layerOpacity);
    h.value(a.placement).value(a.identityRecolour).value(a.clampPlusLighter).value(a.useVCM);
    const SpatialHighlighting& s = a.spatial;
    h.value(s.alignmentRange).value(s.intensityPower).value(s.minIntensity);
    h.value(s.spreadPower).value(s.heightPower).value(s.maxExtraHeight).value(s.read);
}

std::size_t bytesOf(const std::vector<float>& v) { return v.size() * sizeof(float); }

// ---- the four cached steps ----------------------------------------------------
//
// Each is the step it wraps when `cache` is null. With a cache, the key is every
// input of the step; `kShadow` and the other `constexpr` parameter tables are
// not inputs -- they are part of this binary, and so is the cache.

// The SVG's own TEXT is the key, not the parsed document: `SvgDocument::parse`
// reads nothing else, and the text is what the bundle holds. The value is the
// render BEFORE the translucency mask, which the caller applies to its copy.
Result<RenderedImage> svgRenderCached(Device& device, RenderCache* cache,
                                      const std::string& text,
                                      const icf::svg::SvgDocument& svg,
                                      const PathGlobals& placement, const RenderOptions& ro) {
    if (!cache) return renderSvgPlaced(device, svg, placement, ro);
    KeyHasher h("svg-render");
    h.bytes(text.data(), text.size());
    hashInto(h, placement);
    hashInto(h, ro);
    const CacheKey key = h.finish();
    if (auto hit = cache->find<RenderedImage>(key)) return *hit;
    auto drew = renderSvgPlaced(device, svg, placement, ro);
    if (!drew) return drew;   // a failure is not a value, and is not kept
    const std::size_t bytes = bytesOf(drew->rgba);
    cache->store(key, *drew, bytes);
    return drew;
}

}  // namespace

std::shared_ptr<const FieldImage> fieldFromContoursCached(
    RenderCache* cache, const std::vector<FieldContour>& contours, std::uint32_t width,
    std::uint32_t height, const FieldOptions& fo, std::uint32_t superSample) {
    if (!cache) {
        return std::make_shared<const FieldImage>(
            generateFieldFromContours(contours, width, height, fo, superSample));
    }
    KeyHasher h("field-contours");
    hashInto(h, contours);
    h.value(width).value(height).value(superSample);
    hashInto(h, fo);
    const CacheKey key = h.finish();
    if (auto hit = cache->find<FieldImage>(key)) return hit;
    FieldImage made = generateFieldFromContours(contours, width, height, fo, superSample);
    const std::size_t bytes = bytesOf(made.rgba);
    return cache->store(key, std::move(made), bytes);
}

std::shared_ptr<const FieldImage> fieldFromAlphaCached(RenderCache* cache,
                                                       const std::vector<float>& rgba,
                                                       std::uint32_t width, std::uint32_t height,
                                                       const FieldOptions& fo) {
    if (!cache) {
        return std::make_shared<const FieldImage>(
            generateFieldFromAlpha(rgba, width, height, fo));
    }
    KeyHasher h("field-alpha");
    h.span(rgba.data(), rgba.size());
    h.value(width).value(height);
    hashInto(h, fo);
    const CacheKey key = h.finish();
    if (auto hit = cache->find<FieldImage>(key)) return hit;
    FieldImage made = generateFieldFromAlpha(rgba, width, height, fo);
    const std::size_t bytes = bytesOf(made.rgba);
    return cache->store(key, std::move(made), bytes);
}

std::shared_ptr<const std::vector<float>> shadowImageCached(
    RenderCache* cache, const std::vector<float>& art, std::uint32_t width,
    std::uint32_t height, ShadowStyle style, const ShadowGeometry& geometry) {
    if (!cache) {
        return std::make_shared<const std::vector<float>>(
            shadowImage(art, width, height, style, geometry));
    }
    KeyHasher h("shadow-image");
    h.span(art.data(), art.size());
    h.value(width).value(height).value(style);
    hashInto(h, geometry);
    const CacheKey key = h.finish();
    if (auto hit = cache->find<std::vector<float>>(key)) return hit;
    std::vector<float> made = shadowImage(art, width, height, style, geometry);
    const std::size_t bytes = bytesOf(made);
    return cache->store(key, std::move(made), bytes);
}

namespace {

// The chiclet highlights draw INTO the accumulator, so the cached value is the
// accumulator after them, keyed by the accumulator before them.
struct ChicletDrawn {
    std::vector<float> acc;
    std::size_t drawn = 0;
};

std::size_t chicletHighlightsCached(RenderCache* cache, std::vector<float>& acc,
                                    const PixelGrid& grid, const SpecularArguments& args,
                                    IconPlatform platform) {
    if (!cache) return drawChicletHighlights(acc, grid, args, platform);
    KeyHasher h("chiclet-highlights");
    h.span(acc.data(), acc.size());
    hashInto(h, grid);
    hashInto(h, args);
    h.value(platform);
    const CacheKey key = h.finish();
    if (auto hit = cache->find<ChicletDrawn>(key)) {
        acc = hit->acc;
        return hit->drawn;
    }
    const std::size_t drawn = drawChicletHighlights(acc, grid, args, platform);
    cache->store(key, ChicletDrawn{acc, drawn}, bytesOf(acc));
    return drawn;
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
// factor has a sub-texel whose centre IS the pixel centre). E `2` NAO e um meio
// termo: o gerador arredonda par para o impar ABAIXO, entao `ss = 2` E `ss = 1`,
// bit a bit -- medido, as quatro faixas de angulo saem identicas.
//
// FECHANDO O `[OBS]` DO NORMAL (799411c), E A RESPOSTA E NAO LIGAR.
//
// O commit do Sobel deixou aberto que `ss = 3` levaria o erro angular do normal
// a "<=1 grau (max 7)" e que o tempo disso em 1024 nunca fora medido. As duas
// metades foram medidas agora (laudo `2026-09-15-supersample-campo.md`).
//
// A QUALIDADE DO ANGULO: o `[OBS]` estava certo, e por baixo. Circulo r=180 num
// campo de 512 contra o normal radial em forma fechada, mean/pior em graus:
//
//   faixa (px)   ss=1          ss=3         residuo |d| medio: ss=1 -> ss=3
//   0,5-1        0,96 / 4,13   0,54 / 2,02      0,069 -> 0,027
//   2-4          2,51 / 15,74  0,66 / 4,83      0,086 -> 0,022
//   4-8          2,77 / 19,86  0,64 / 5,76      0,074 -> 0,020
//   8-16         2,38 / 13,11  0,62 / 5,15      0,061 -> 0,019
//
// O PIXEL: nao mexe. Erro medio por canal contra `apple-512.png`, 412 em 512,
// quatro casas porque em duas os tres numeros sao os MESMOS:
//
//   ss=1 (base)       R 8,8416  G 10,1735  B 9,8461  A 4,9520
//   ss=3 so na arte   R 8,8411  G 10,1738  B 9,8484  A 4,9520
//   ss=3 so no chiclet R 8,8468 G 10,1786  B 9,8513  A 4,9560
//
// A arte move 5,00 % dos pixels (pior delta de canal 13) e paga 2,4x-2,6x o
// render -- AppIcon-27 de 0,269 s para 0,653 s em 412 e de 1,669 s para 4,401 s
// em 1024, Release -- para andar 0,0005 na direcao certa em R e 0,0023 na
// ERRADA em B. O chiclet custa um campo fixo por render (+0,09 s em 412,
// +0,59 s em 1024, o mesmo nos dois documentos) e piora os QUATRO canais.
//
// POR QUE quatro vezes menos erro de angulo nao vale um nivel de cor: o resto
// contra a Apple e ~10 niveis por canal e ele nao vem daqui. Vem do
// blur-material desligado, do grampo `plusLighter` desligado e do overdraw da
// sombra -- todos anotados como `[OBS]` nos seus proprios sitios. Um residuo de
// 0,06 px no campo esta tres ordens de grandeza abaixo disso, e o cone do
// especular e um cosseno largo demais para revelar dois graus de normal. O
// gabarito ARBITROU: a leitura mais precisa do campo nao e a mais parecida com
// o alvo, entao o knob fica onde estava.
//
// `[OBS]` O QUE CONTINUA ABERTO e a GRADE do alvo -- `ICRRenderingParameters`
// `.SDFGeneration` (0xA46E0) tem tres botoes que ninguem leu, e um deles pode
// ser justamente este fator. Uma amostra por pixel segue sendo escolha MEDIDA
// deste projeto, nao leitura do binario.
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

// The box `placeRaster` drops the art into, in target pixels -- the raster's
// answer to `artPlacementRect`, which does the same for a viewBox.
//
// It sits beside `artPlacementRect`, for the reason that one's own comment
// gives: the art's box and the art's pixels have to agree, and two copies of
// the same arithmetic in two places is how they stop agreeing. The three lines
// below are `placeRaster`'s own `w`, `h`, `left`, `top` and `k`, with nothing
// added.
//
// It exists because the translucency's `bounds` is a rect, and until this front
// the only art that reached the translucency had a viewBox to give it one.
//
// E DESDE 19/09 ELA E DECLARADA NO CABECALHO (era interna a este arquivo),
// porque ganhou um segundo consumidor fora dele: o hit-test do canvas para uma
// camada `.png` (`ick::canvasLayerRect`), cujo caso cobra contra ESTA funcao em
// vez de contra si mesmo -- exatamente o que os casos 12 e 16 de
// test_kit_canvas.cpp ja fazem com `artPlacementRect`.
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

PlacementRect artPlacementRect(const icf::svg::ViewBox& box, const LayerPlacement& p,
                               std::uint32_t size) {
    // Derived from `placeOnCanvas` rather than recomputed beside it: the art's
    // box and the art's pixels have to agree, and two copies of the same
    // arithmetic is how they stop agreeing.
    //
    // O retangulo e ABSOLUTO, como o do fundo: quem avalia a rampa -- o
    // compositor de `SvgRenderer` -- passou a medir no ponto
    // `(x + origem) + 0.5`, que e a regra de todo passo de CPU (spec
    // 2026-09-16, "O invariante que governa o desenho").
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
    // `[BIN]` Every ramp this function can build is a DOCUMENT fill, and every
    // document fill reaches `RBFill` through `IconRendering 0x1BC74` with
    // `flags = 0x400` -- interpolation code 4, ramp kind 3, word0 bit 25, the
    // cubic. The four cases above differ in where the stops come from and not in
    // how they are sampled, so the flag is set once, here.
    out.smooth = true;
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
    // A COLOCACAO E SEMPRE A DO CANVAS, mesmo quando o alvo e um pedaco dele
    // (spec 2026-09-16, "O invariante que governa o desenho"). A primeira
    // versao desta frente subtraiu a origem aqui, que e a forma obvia, e ela
    // NAO fecha o invariante: `[ART]` medido em 18/09, com `m2` menos a origem
    // o disco do gate acusava 11 e 15 pixels da borda antialiasada com
    // `max |d| = 0,00048828125`, que e UM ULP de `float16` -- a cobertura e
    // `VK_FORMAT_R16G16_SFLOAT` (`Image.cpp`) --, porque `world = p.x*m0 + m2`
    // arredonda noutro expoente quando `m2` encolhe e a cobertura cai do outro
    // lado de um degrau de meia precisao. Um viewport de origem ZERO e
    // extensao menor fechava em zero na mesma medida, o que isola a subtracao,
    // e as duas formas de escrever `m2` davam o mesmo numero.
    //
    // Entao a translacao saiu da matriz e virou "subtracao inteira DEPOIS da
    // colocacao", que e o que a spec pede: o deslocamento entra como um inteiro
    // no viewport do Vulkan (`CoveragePass::draw`) e o fragmento soma a mesma
    // origem de volta em `gl_FragCoord` (`path_exterior.frag`) -- somar um
    // inteiro a um `x.5` e exato. Com isso o gate fecha em ZERO.
    g.m2[0] = static_cast<float>(left * k - s * box.x);
    g.m2[1] = static_cast<float>(top * k - s * box.y);
    g.twoOverSize[0] = 2.0f / static_cast<float>(size);
    g.twoOverSize[1] = 2.0f / static_cast<float>(size);
    g.urx = static_cast<float>(size);
    return g;
}

namespace {

// OS PIXELS DE `renderIcon`, NA CPU -- cada metodo e a chamada que a linha
// correspondente de `renderIconOn` fazia antes de a interface existir, com os
// mesmos argumentos (IconSurface.h diz por que ela existe). O gate de SHA do
// corpus e quem prova que nenhum byte andou.
class CpuSurface final : public IconSurface {
public:
    explicit CpuSurface(Device& device) : device_(device) {}

    Result<void> begin(const PixelGrid& grid, const PixelGrid&) override {
        grid_ = grid;
        acc_.assign(grid.texels() * 4, 0.0f);
        return {};
    }
    Result<void> paintBackground(const FillOverride& paint) override {
        rb::paintBackground(acc_, grid_, paint);
        return {};
    }
    Result<void> clipToChiclet(IconPlatform platform) override {
        rb::clipToChiclet(acc_, grid_, platform);
        return {};
    }
    Result<void> chicletHighlights(RenderCache* cache, const SpecularArguments& args,
                                   IconPlatform platform, CountSink sink) override {
        sink(chicletHighlightsCached(cache, target(), grid_, args, platform));
        return {};
    }
    Result<void> beginGroup(bool isolated) override {
        isolated_ = isolated;
        groupAcc_.clear();
        if (isolated) groupAcc_.assign(grid_.texels() * 4, 0.0f);
        return {};
    }
    Result<void> endGroup(std::optional<BlendMode> blend) override {
        if (isolated_ && blend) blendPremulOver(acc_, groupAcc_, *blend);
        isolated_ = false;
        return {};
    }
    Result<SurfaceArt> drawSvg(RenderCache* cache, const std::string& text,
                               const icf::svg::SvgDocument& svg, const PathGlobals& placement,
                               const RenderOptions& ro) override {
        auto drew = svgRenderCached(device_, cache, text, svg, placement, ro);
        if (!drew) return std::unexpected(drew.error());
        SurfaceArt art;
        art.rgba = std::move(drew->rgba);
        art.skipped = std::move(drew->skipped);
        return art;
    }
    Result<SurfaceArt> placeRaster(RenderCache*, const icf::DecodedPng& png,
                                   const LayerPlacement& placement, bool) override {
        SurfaceArt art;
        art.rgba = rb::placeRaster(png, placement, grid_);
        return art;
    }
    Result<SurfaceField> contourField(RenderCache* cache, const std::vector<FieldContour>& contours,
                                      std::uint32_t width, std::uint32_t height,
                                      const FieldOptions& fo, std::uint32_t ss,
                                      const FieldBands&) override {
        return fieldOf(fieldFromContoursCached(cache, contours, width, height, fo, ss));
    }
    Result<SurfaceField> alphaField(RenderCache* cache, SurfaceArt& art, std::uint32_t width,
                                    std::uint32_t height, const FieldOptions& fo) override {
        return fieldOf(fieldFromAlphaCached(cache, art.rgba, width, height, fo));
    }
    Result<void> refract(const SurfaceField& field, const GlassRefraction& refraction) override {
        glassOver(target(), grid_, glassDisplacementMap(*field.cpu, refraction), refraction);
        return {};
    }
    Result<SurfaceMask> opacityMask(const SurfaceField& field,
                                    const OpacityMaskArguments& args) override {
        SurfaceMask m;
        m.cpu = std::make_shared<const OpacityMask>(glassOpacityMask(*field.cpu, args));
        m.field = field;
        m.args = args;
        return m;
    }
    Result<void> specular(const SurfaceField& field, const SpecularArguments& args,
                          CountSink sink) override {
        sink(drawSpecular(target(), *field.cpu, args));
        return {};
    }
    Result<void> applyMask(SurfaceArt& art, const SurfaceMask& mask, MaskSink sink) override {
        std::size_t painted = 0;
        const std::size_t missed = opacityMaskMissedPixels(art.rgba, *mask.cpu, painted);
        applyOpacityMask(art.rgba, *mask.cpu);
        sink(missed, painted);
        return {};
    }
    Result<void> blendArt(const SurfaceArt& art, float alpha, BlendMode mode,
                          bool clearContent) override {
        if (!clearContent) {
            blendOver(target(), art.rgba, alpha, mode);
            return {};
        }
        std::vector<float> m = art.rgba;
        for (std::size_t i = 0; i < m.size(); i += 4) {
            m[i + 0] = kClearContentLightening * m[i + 0];
            m[i + 1] = 1.0f;
            m[i + 2] = 0.0f;
        }
        blendOver(target(), m, alpha, mode);
        return {};
    }
    Result<SurfaceShadow> makeShadow(RenderCache* cache, SurfaceArt& art, ShadowStyle style,
                                     const ShadowGeometry& geometry,
                                     double overdrawAlpha) override {
        SurfaceShadow s;
        s.image = shadowImageCached(cache, art.rgba, grid_.width, grid_.height, style, geometry);
        if (overdrawAlpha > 0.0) {
            s.overdraw = shadowOverdrawImage(*s.image, art.rgba, grid_.width, grid_.height,
                                             overdrawAlpha);
        }
        s.hasOverdraw = !s.overdraw.empty();
        return s;
    }
    Result<void> blendShadow(const SurfaceShadow& shadow, bool overdraw, float alpha,
                             BlendMode mode) override {
        blendOver(target(), overdraw ? shadow.overdraw : *shadow.image, alpha, mode);
        return {};
    }
    Result<std::vector<float>> finish(std::int32_t cropX, std::int32_t cropY,
                                      std::uint32_t viewW, std::uint32_t viewH) override {
        std::vector<float> rgba(static_cast<std::size_t>(viewW) * viewH * 4, 0.0f);
        for (std::uint32_t y = 0; y < viewH; ++y) {
            for (std::uint32_t x = 0; x < viewW; ++x) {
                const std::size_t s =
                    ((static_cast<std::size_t>(y) + cropY) * grid_.width + x + cropX) * 4;
                const std::size_t d = (static_cast<std::size_t>(y) * viewW + x) * 4;
                const float a = acc_[s + 3];
                for (int k = 0; k < 3; ++k) rgba[d + k] = a > 0.0f ? acc_[s + k] / a : 0.0f;
                rgba[d + 3] = a;
            }
        }
        return rgba;
    }

private:
    std::vector<float>& target() { return isolated_ ? groupAcc_ : acc_; }

    static SurfaceField fieldOf(std::shared_ptr<const FieldImage> image) {
        SurfaceField f;
        f.width = image->width;
        f.height = image->height;
        f.originX = image->originX;
        f.originY = image->originY;
        f.cpu = std::move(image);
        return f;
    }

    Device& device_;
    PixelGrid grid_;
    // The accumulator is PREMULTIPLIED while layers stack -- `over` is only
    // associative in that form -- and is un-multiplied once at the end, which is
    // what `RenderedIcon::rgba` promises and what a PNG wants.
    //
    // This step was missing until the mutation sweep pointed at the `over`
    // operator. The test that should have caught it checked only the ALPHA of a
    // half-transparent layer, never its colour, so a premultiplied result read
    // as straight -- a semi-transparent icon came out too dark and every test
    // stayed green.
    std::vector<float> acc_;
    std::vector<float> groupAcc_;
    bool isolated_ = false;
};

}  // namespace

std::vector<float> placeRasterOnCpu(const icf::DecodedPng& png, const LayerPlacement& placement,
                                    const PixelGrid& grid) {
    return placeRaster(png, placement, grid);
}

Result<RenderedIcon> renderIcon(Device& device, const icf::IconBundle& bundle,
                                IconRenderOptions options) {
    CpuSurface surface(device);
    return renderIconOn(surface, bundle, options);
}

Result<RenderedIcon> renderIconOn(IconSurface& surface, const icf::IconBundle& bundle,
                                  IconRenderOptions options) {
    if (options.size == 0) return std::unexpected("a canvas of zero size was asked for");

    // O DOCUMENTO E LIDO AQUI porque a margem sai DELE: `documentReach` mede o
    // alcance de cada efeito nos parametros ja denormalizados dos grupos, e o
    // plano transforma isso no buffer, na origem alinhada e no teto de area
    // (spec 2026-09-16, "A margem" e "O teto de area").
    const icf::IconDocument doc = bundle.document();
    const std::vector<icf::Group> groups = doc.groups();

    auto planned = planViewport(options.viewport, options.size,
                                documentReach(doc, options.context, options.sizeClass),
                                surface.bufferLattice());
    if (!planned) return std::unexpected(planned.error());
    const PixelGrid grid = planned->buffer;

    RenderedIcon out;
    out.size = options.size;
    out.originX = planned->crop.originX;
    out.originY = planned->crop.originY;
    out.buffer = grid;
    if (planned->overCap) {
        out.viewportRefused = true;
        note(out.notes, "viewport acima do teto de area: nada desenhado (spec 2026-09-16, "
                        "\"O teto de area\")");
        return out;
    }
    // O acumulador (pre-multiplicado) mora na superficie -- `CpuSurface` diz
    // por que ele e pre-multiplicado.
    if (auto began = surface.begin(grid, planned->narrow); !began) {
        return std::unexpected(began.error());
    }

    // Qual pastilha este contexto pede -- `ChicletShape.h`, `platformOverrides`.
    const IconPlatform platform = iconPlatformOf(options.context.idiom);

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
                if (auto ok = surface.paintBackground(paint); !ok) {
                    return std::unexpected(ok.error());
                }
                // The one line this front adds to this file. The background is
                // painted over the whole square and then CUT, which is the order
                // that keeps the ramp's parameter mapped to the canvas -- see
                // `ChicletShape.h` for the reading, and `kBackgroundShapeNote`
                // for what it leaves open.
                // E A FORMA E A DA PLATAFORMA, desde 20/09: `platformOverrides`
                // da ao watchOS um raio de 512 num canvas de 1024 -- um circulo
                // -- e meio pixel de recuo. Ate aqui todo idioma recortava ao
                // mesmo quadrado de 0,26, que era a metade visual do "todo modo
                // alem de Square vira um quadrado desalinhado".
                if (auto ok = surface.clipToChiclet(platform); !ok) {
                    return std::unexpected(ok.error());
                }
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
                    // A nota depende de quantos pixels mudaram: o lugar dela fica
                    // guardado ate a contagem chegar (na GPU, no `finish`).
                    const std::size_t noteAt = out.notes.size();
                    out.notes.push_back(kPendingEntry);
                    std::vector<std::string>* notes = &out.notes;
                    if (auto ok = surface.chicletHighlights(
                            options.cache, chicletArgs, platform,
                            [notes, noteAt, appearance, lum](std::size_t highlighted) {
                                (*notes)[noteAt] = highlighted > 0
                                                       ? chicletHighlightsNote(appearance, lum)
                                                       : std::string(kDroppedEntry);
                            });
                        !ok) {
                        return std::unexpected(ok.error());
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

        // ---- `blur-material`, WHICH NOW DRAWS -------------------------------
        //
        // `[BIN]` The radius has been transported since `GlassMaterial.cpp`
        // existed -- `denormaliseBlurRadius` is `min(b, 1) * 64` -- and the
        // KERNEL it feeds was read on 2026-09-15: the radius IS the Gaussian's
        // sigma. The SURFACE was three open questions and is now one:
        // `BlurKernel.h` carries the transcription of `0x4A2D4`-`0x4AC84`, the
        // branch (it is always the `needs-background` one, because the only
        // thing that would pick the other is a `refractionStrength` no document
        // has), the clip (`0x4A4A4`-`0x4A56C`, an outset of exactly one pixel),
        // and the order (content first, so the backdrop includes the group).
        // What is left `[OBS]` is the layer FRAME, and it is an argument below
        // rather than a constant inside the blur.
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
        //
        // THE FRAME IS THE UNIT RECT HERE, and that is still a placeholder --
        // but it is now a placeholder for a NAMED field. `[BIN]` The rect at
        // descriptor `+0x70` is `FinalizedIcon.Layer.effectsFrame`, a `CGRect?`
        // (field-offset vector `0xBD400`, field descriptor `0xA306C`), and the
        // target only opens the whole blur block when it is non-nil (the
        // Optional tag at `+0x90`, tested at `0x4A344`-`0x4A348`). Who computes
        // it was not read and it is not a document key, so this renderer keeps
        // computing the surface for the report and keeps the DRAW off -- see
        // `BlurKernel.h`. A tighter frame can only ever SHRINK this region,
        // never move it.
        //
        // A ESCALA DESTA CHAMADA SAI DA EXTENSAO: `blurMaterialSurface` faz
        // `min(w, h) / 1024` (`BlurKernel.cpp:279`). Por isso ela recebe o
        // CANVAS e nao o buffer -- um buffer de viewport aqui mudaria a escala
        // sem erro nenhum (spec 2026-09-16, "Os sitios"). O desenho esta
        // desligado; a superficie so vai para o relatorio.
        const BlurMaterialSurface blurSurface = blurMaterialSurface(
            glassNumbers.blurRadiusPoints, 0.0, 0.0, 1.0, 1.0, options.size, options.size);

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
        // THE NEXT PARAGRAPH IS SUPERSEDED. It is kept because the correction
        // below only makes sense against it, but read the two together or not
        // at all -- stopping at the end of it leaves you with the false half.
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
        // THAT PARAGRAPH IS NOW KNOWN TO BE HALF A READING, and the correction
        // runs the other way -- `Docs/Laudos/2026-09-15-refracao.md`. It is
        // right about `GlassDisplacementStyle::draw`, and that function is the
        // MAP GENERATOR: style 3 turns the shape's field into a displacement
        // map, so of course it filters the item it wears. The DISPLACEMENT is a
        // different object, installed right after it, and it does read the
        // backdrop. `[BIN]` `0x10C14` closes the map layer with
        // `addFilterLayerWithShader:` (`RenderBox 0x40A18` = `end_layer` +
        // `restore` + `State::add_custom_effect`), and then `IconRendering`
        // begins the layer that wears the shader with `beginLayerWithFlags: 1`
        // -- `0x4A794` and `0x4AA00`, both immediately on return. Bit 0 is the
        // background bit the VCM front read (`0x3BCA0` keeps it through the
        // `0x7B` mask; `null_style_draw` tests it at `0xCE02C` and hangs a
        // `BackdropFilterItem` on the PARENT layer), and `CustomEffectStyle::draw`
        // (`0xF4030`) ends in `Builder::draw` (`0xCDAA0`), which tail-calls
        // exactly that `null_style_draw` at `0xCDAF4`.
        //
        // So the target's refraction displaces what is already underneath, which
        // is what `glassOver` does. The refusal below still stands -- but its
        // reason is the FIRST one again, not the corrected one: a group drawn
        // into a target of its own really would refract an empty backdrop, for
        // Apple as much as for us. Making the glass filter the item it wears
        // would NOT dissolve the coupling, because that is not what the target
        // does either.
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
        // O alvo proprio do grupo, quando ele mescla: a superficie passa a
        // desenhar nele ate `endGroup`.
        if (auto ok = surface.beginGroup(blendTheGroup); !ok) return std::unexpected(ok.error());

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
            std::string svgText;   // the cache key of the SVG render
            if (ext == ".svg") {
                svgText = readAll(art);
                svg = icf::svg::SvgDocument::parse(svgText);
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
            std::optional<SurfaceArt> rasterPlaced;
            std::uint32_t rasterW = 0, rasterH = 0;
            if (!svg && ext == ".png") {
                const icf::DecodedPng png = icf::readPng(art.string());
                if (!png.error.empty()) {
                    skip(*imageName + ": " + png.error);
                    continue;
                }
                rasterW = png.width;
                rasterH = png.height;
                // O mesmo portao do bloco de vidro abaixo: o campo vai sair do
                // alfa desta arte.
                const bool feedsField =
                    isGlass && (!glassRefractionIsIdentity(refraction) || groupWantsMask ||
                                wantsSpecular);
                auto placed = surface.placeRaster(options.cache, png, lp, feedsField);
                if (!placed) return std::unexpected(placed.error());
                rasterPlaced = std::move(*placed);
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
            std::optional<SurfaceMask> mask;
            std::optional<SurfaceField> specularField;

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

                std::optional<SurfaceField> field;
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
                        // ABSOLUTO: os contornos ja estao no CANVAS (
                        // `placeOnCanvas` acima), entao a rasterizacao do campo
                        // tem de amostrar em `(x + origem) + 0.5` e so deslocar
                        // o indice (spec 2026-09-16, "O invariante que governa o
                        // desenho"). Com origem zero a aritmetica e a de antes.
                        fo.originX = grid.originX;
                        fo.originY = grid.originY;
                        // Na GPU e `icon_field.comp`: o sinal na CPU, a distancia la.
                        //
                        // ATE ONDE O CAMPO PRECISA SER EXATO (a CPU ignora). O
                        // especular zera alem de `inset + height` + a banda de AA do
                        // realce mais largo (`glassHighlightFragment`); a mascara de
                        // translucidez satura alem de `borderWidth` e de 1 px para
                        // fora (`cov`). A refracao le o campo ate a altura dela, e ela
                        // e rara (2 documentos do corpus): com ela o campo e exato
                        // onde o acumulador o le. No zoom profundo a GPU deixa de
                        // buscar o pe de pixels a milhares de pixels do glifo.
                        auto bandOf = [](double reach) {
                            return static_cast<float>(std::ceil(reach)) + 2.0f;
                        };
                        const double maskReach =
                            std::max(std::fabs(static_cast<double>(groupMaskArgs.borderWidth)),
                                     1.0) + 1.0;
                        double accReach = 1.0;   // ninguem le: qualquer banda serve
                        if (wantsHighlight) {
                            std::size_t n = 0;
                            const HighlightSlot* slots = glyphHighlightSlots(n);
                            for (std::size_t s = 0; s < n; ++s) {
                                const GlassHighlightSettings g =
                                    resolveHighlight(slots[s], specularArgs);
                                if (g.opacity <= 0.0 || g.height <= 0.0) continue;
                                accReach = std::max(accReach, std::fabs(g.inset) + g.height + 2.0);
                            }
                        }
                        FieldBands bands;
                        bands.art = bandOf(wantsTranslucency ? maskReach : 1.0);
                        bands.accumulator =
                            wantsRefraction
                                ? 0.0f
                                : bandOf(std::max(accReach, wantsTranslucency ? maskReach : 1.0));
                        auto fromShape = surface.contourField(options.cache, shape.contours,
                                                              grid.width, grid.height, fo,
                                                              kFieldSuperSample, bands);
                        if (!fromShape) return std::unexpected(fromShape.error());
                        if (fromShape->empty()) {
                            // Contours that close but cover no sample point --
                            // a hairline, a shape smaller than a texel. The
                            // brute force would have signed it anyway, from
                            // outside; a grid cannot, and saying so is better
                            // than a field that is everywhere-outside.
                            fieldGap = "vidro: a arte fecha contornos mas nenhum ponto de amostra "
                                       "cai dentro deles nesta resolucao (" + *imageName + ")";
                        } else {
                            field = std::move(*fromShape);
                            note(out.notes, kGlassVectorFieldNote);
                        }
                    }
                } else if (rasterPlaced) {
                    // O raster ja foi COLOCADO no buffer por `placeRaster`, que
                    // amostrou absoluto, entao aqui a origem so viaja no eco de
                    // `FieldImage` -- que e o que a translucidez e a refracao
                    // leem para saber em que coordenada o indice esta.
                    FieldOptions fo;
                    fo.originX = grid.originX;
                    fo.originY = grid.originY;
                    // Na GPU o campo de um raster segue na CPU, sobre a copia de
                    // [UP3] -- sem readback.
                    auto fromAlpha = surface.alphaField(options.cache, *rasterPlaced, grid.width,
                                                        grid.height, fo);
                    if (!fromAlpha) return std::unexpected(fromAlpha.error());
                    if (fromAlpha->empty()) {
                        // No texel reaches `alpha >= 0.5`: there is no contour to
                        // sign, so there is no field. A raster that faint has
                        // nothing for the glass to bend around, and saying so is
                        // better than handing the effects an all-outside field
                        // that would draw nothing without a word.
                        fieldGap = "vidro sobre raster: nenhum texel da arte chega a alpha >= 0.5, "
                                   "entao nao ha contorno para assinar (" + *imageName + ")";
                    } else {
                        field = std::move(*fromAlpha);
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
                    if (wantsHighlight) specularField = *field;
                    if (wantsRefraction) {
                        if (auto ok = surface.refract(*field, refraction); !ok) {
                            return std::unexpected(ok.error());
                        }
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
                        auto made = surface.opacityMask(*field, args);
                        if (!made) return std::unexpected(made.error());
                        mask = std::move(*made);
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
            // Num modo tingido a sombra e `neutral` (`[BIN]` 0x49F40); `none`
            // continua `none`, porque a saida de `none` vem antes do portao.
            const ShadowInputs shadowIn{
                shadowEffectiveStyle(glassNumbers.shadowStyle, options.tint.has_value()),
                glassNumbers.shadowOpacity, opacity, options.sizeClass};
            const bool castsShadow = isGlass && shadowDraws(shadowIn);
            // THE OVERDRAW PASS IS A SECOND COMPOSITE OF THE SAME IMAGE, so the
            // image is kept between the two draws instead of being rebuilt: the
            // blur behind it is the most expensive thing this loop does.
            // `GlassShadow.h` carries the addresses for all of it.
            std::optional<SurfaceShadow> shadowOverdraw;
            auto castShadow = [&](SurfaceArt& artDrawn) -> Result<void> {
                if (!castsShadow) return {};
                // Na GPU a sombra e feita da arte residente, sem descer nada.
                const ShadowGeometry geometry =
                    shadowGeometry(options.size, options.sizeClass);
                const double overdraw =
                    kShadow.drawOverContent
                        ? shadowOverdrawAlpha(groupTranslucency, shadowIn.style,
                                              options.sizeClass)
                        : 0.0;
                auto made = surface.makeShadow(options.cache, artDrawn, shadowIn.style, geometry,
                                               overdraw);
                if (!made) return std::unexpected(made.error());
                if (auto ok = surface.blendShadow(*made, false,
                                                  static_cast<float>(shadowAlpha(shadowIn)),
                                                  shadowBlendMode(shadowIn.style));
                    !ok) {
                    return ok;
                }
                if (made->hasOverdraw) shadowOverdraw = std::move(*made);
                if (geometry.ringWidth) note(out.notes, kShadowRingNote);
                ++out.glassShadowed;
                return {};
            };
            // `[BIN]` AFTER THE CONTENT AND BEFORE THE HIGHLIGHTS, which is the
            // order of `0x48B74`: `0x4A2D4` (the glass pass, where the main
            // shadow lives) at `0x48BD4`, then `0x4AC84` at `0x48C08` -- whose
            // body is the content draw (`0x4ADA4`) followed by this pass
            // (`0x4ADBC`-`0x4AEB4`) -- then `0x491C0`, the highlights, at
            // `0x48DB4`, on the path every branch merges into (`0x48DAC`). The
            // per-element loop at `0x48E20` runs the same three in the same
            // order (`0x48F30`, `0x48F5C`, `0x48EAC`).
            auto castShadowOverdraw = [&]() -> Result<void> {
                if (!shadowOverdraw) return {};
                if (auto ok = surface.blendShadow(*shadowOverdraw, true,
                                                  static_cast<float>(shadowAlpha(shadowIn)),
                                                  kShadow.overdrawBlendMode);
                    !ok) {
                    return ok;
                }
                shadowOverdraw.reset();
                note(out.notes, kShadowOverdrawNote);
                ++out.glassShadowOverdrawn;
                return {};
            };

            if (svg) {
                RenderOptions ro;
                ro.width = grid.width;
                ro.height = grid.height;
                ro.originX = grid.originX;
                ro.originY = grid.originY;
                ro.projectionWidth = grid.size;
                ro.projectionHeight = grid.size;
                ro.subdivisions = options.subdivisions;
                ro.override = paint;
                auto drew = surface.drawSvg(options.cache, svgText, *svg,
                                            placeOnCanvas(svg->viewBox, lp, options.size), ro);
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
                    const std::size_t gapAt = out.shapeGaps.size();
                    out.shapeGaps.push_back(kPendingEntry);
                    std::vector<std::string>* gaps = &out.shapeGaps;
                    const std::string prefix = name + " / " + *imageName + ": ";
                    auto counted = surface.applyMask(
                        *drew, *mask,
                        [gaps, gapAt, prefix](std::size_t missed, std::size_t painted) {
                            if (!(missed > 0 && painted > 0)) {
                                (*gaps)[gapAt] = kDroppedEntry;
                                return;
                            }
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
                                100.0 * static_cast<double>(missed) /
                                    static_cast<double>(painted));
                            (*gaps)[gapAt] = prefix + buf;
                        });
                    if (!counted) return std::unexpected(counted.error());
                    ++out.glassTranslucent;
                }
                if (auto ok = castShadow(*drew); !ok) return std::unexpected(ok.error());
                // THE HIGHLIGHT GOES ON AFTER THE SHADOW IS CAST, ON PURPOSE.
                // `[BIN]` The target keeps them apart -- the highlight is its
                // own clip + backdrop colour matrix inside a pass gated by
                // `hasSpecular` at `0x00049200`, and the shadow is a different
                // `drawShape:` in a different function.
                if (auto ok = surface.blendArt(*drew, static_cast<float>(opacity), layerBlend,
                                               options.clearMask);
                    !ok) {
                    return std::unexpected(ok.error());
                }
                if (auto ok = castShadowOverdraw(); !ok) return std::unexpected(ok.error());
                // THE HIGHLIGHT FILTERS THE BACKDROP, SO IT GOES ON AFTER THE
                // LAYER IS IN IT. `[BIN]` `beginLayerWithFlags:1` sets bit 0 of
                // `RB::DisplayList::Layer::Flag`, and `Builder::null_style_draw`
                // (`0x000CE028`) turns that layer's colour matrix into a
                // `BackdropFilterItem` appended to the PARENT -- so the matrix
                // reads the composite, not the layer's own buffer (GlassSpecular.cpp
                // has the addresses). `layerOpacity` is now `opacity` because the
                // highlight is a sibling of the layer again, as `[descriptor+0x38]`
                // (`0x000495F0`) says it is in the target.
                if (specularField) {
                    SpecularArguments layerSpecular = specularArgs;
                    layerSpecular.layerOpacity = opacity;
                    std::size_t* specularCount = &out.glassSpecular;
                    if (auto ok = surface.specular(*specularField, layerSpecular,
                                                   [specularCount](std::size_t moved) {
                                                       if (moved > 0) ++*specularCount;
                                                   });
                        !ok) {
                        return std::unexpected(ok.error());
                    }
                    note(out.notes, specularDrawnNote());
                }
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
                SurfaceArt& placed = *rasterPlaced;
                if (mask) {
                    const std::size_t gapAt = out.shapeGaps.size();
                    out.shapeGaps.push_back(kPendingEntry);
                    std::vector<std::string>* gaps = &out.shapeGaps;
                    const std::string prefix = name + " / " + *imageName + ": ";
                    auto counted = surface.applyMask(
                        placed, *mask,
                        [gaps, gapAt, prefix](std::size_t missed, std::size_t painted) {
                            if (!(missed > 0 && painted > 0)) {
                                (*gaps)[gapAt] = kDroppedEntry;
                                return;
                            }
                            char buf[420];
                            std::snprintf(
                                buf, sizeof(buf),
                                "translucidez aplicada com buraco: %zu de %zu pixels pintados "
                                "(%.1f%%) caem FORA do campo de distancia e ficaram opacos -- "
                                "num raster o campo e assinado pelo contorno alpha >= 0.5, entao "
                                "todo pixel pintado com alpha ABAIXO do limiar fica de fora.",
                                missed, painted,
                                100.0 * static_cast<double>(missed) /
                                    static_cast<double>(painted));
                            (*gaps)[gapAt] = prefix + buf;
                        });
                    if (!counted) return std::unexpected(counted.error());
                    ++out.glassTranslucent;
                }
                if (auto ok = castShadow(placed); !ok) return std::unexpected(ok.error());
                if (auto ok = surface.blendArt(placed, static_cast<float>(opacity), layerBlend,
                                               options.clearMask);
                    !ok) {
                    return std::unexpected(ok.error());
                }
                if (auto ok = castShadowOverdraw(); !ok) return std::unexpected(ok.error());
                if (specularField) {
                    // The same backdrop reading as the vector branch above.
                    SpecularArguments layerSpecular = specularArgs;
                    layerSpecular.layerOpacity = opacity;
                    std::size_t* specularCount = &out.glassSpecular;
                    if (auto ok = surface.specular(*specularField, layerSpecular,
                                                   [specularCount](std::size_t moved) {
                                                       if (moved > 0) ++*specularCount;
                                                   });
                        !ok) {
                        return std::unexpected(ok.error());
                    }
                    note(out.notes, specularDrawnNote());
                }
                ++out.drawn;
            } else {
                skip("arte com extensao que este leitor nao le: " + *imageName);
            }
        }

        // THE GROUP'S `blur-material` WOULD BE DRAWN HERE, AND IS NOT.
        //
        // This is the one point in the loop where the backdrop is what the
        // target says it is: `[BIN]` `0x4A488` draws the group's content and
        // only then `0x4A48C`-`0x4A5D0` opens the `needs-background` layer, so
        // the background being blurred is everything beneath the group PLUS the
        // group -- which is exactly `acc` at the end of the group's layer loop,
        // before the `blendPremulOver` on the next line. Wall 3 of
        // `BlurKernel.h` is answered, and the call would go on this line.
        //
        // IT IS NOT MADE, because the gabarito refused the only reading of the
        // layer frame this front had. `drawBlurMaterial(acc, ..., blurSurface)`
        // with the frame at the unit rect was rendered and measured: the mean
        // channel error against `apple-512.png` goes from `8.95 / 10.03 / 9.89`
        // to `15.58 / 20.09 / 22.49` and the alpha error from `4.95` to `7.09`,
        // with the four corners of the squircle the worst blocks in the frame.
        // The luma profile says the same thing in the shape the highlight front
        // taught: down the centre column the gabarito falls `157 -> 49` over
        // nine rows and then holds a flat `49`, while the blurred render is flat
        // at `84` and never reaches `49` at all. A canvas-wide backdrop blur at
        // this radius erases structure the target keeps, so the frame is NOT the
        // unit rect.
        //
        // AND IT IS NOT THE APEX RING EITHER, measured with the instrument that
        // killed the other two candidates -- the luma profile by DEPTH at the
        // apex, not the frame-wide mean. Drawing it moves the profile by
        // `-87.0 / -90.2 / -69.0 / -52.2 / -47.2 / -43.1 / -39.4 / -36.0` luma
        // at `0.0 .. 17.4` canvas units of depth, while the gabarito asks for
        // `-64.0 / -47.1` and then `+0.3 / +47.2 / +43.7 / +35.5 / +27.5 /
        // +15.9`. It is monotone in magnitude, it never turns around, it
        // overshoots the ring itself by a third, and from five units down it
        // pushes the wrong WAY by 35 to 52 luma. A blanket, not a ring.
        //
        // The arithmetic stays transcribed and switched OFF, which is the same
        // thing `BlendFormula.h` does with `shouldClampPlusLBlending` and for
        // the same reason: turning it on without the reading would move pixels
        // on the measurer's authority instead of the target's.
        if (blurSurface.draws) {
            note(out.notes, blendTheGroup ? kBlurMaterialBlendedGroupNote
                                          : kBlurMaterialFrameNote);
        }

        if (auto ok = surface.endGroup(blendTheGroup ? groupMode : std::nullopt); !ok) {
            return std::unexpected(ok.error());
        }
    }

    // O RECORTE. O buffer e `(viewport + margem) ∩ canvas`, entao o pedaco
    // pedido comeca onde a origem do recorte passa da origem do buffer -- uma
    // subtracao de inteiros, que e a unica coisa que anda (spec 2026-09-16,
    // "O invariante que governa o desenho").
    const std::int32_t cropX = planned->crop.originX - grid.originX;
    const std::int32_t cropY = planned->crop.originY - grid.originY;
    const std::uint32_t viewW = planned->crop.width;
    const std::uint32_t viewH = planned->crop.height;
    out.width = viewW;
    out.height = viewH;
    auto cropped = surface.finish(cropX, cropY, viewW, viewH);
    if (!cropped) return std::unexpected(cropped.error());
    out.rgba = std::move(*cropped);
    // As contagens ja chegaram (a GPU as entrega no `finish`): o que nao virou
    // texto sai.
    for (std::vector<std::string>* list : {&out.notes, &out.shapeGaps}) {
        list->erase(std::remove_if(list->begin(), list->end(),
                                   [](const std::string& e) {
                                       return e == kDroppedEntry || e == kPendingEntry;
                                   }),
                    list->end());
    }
    return out;
}

void applyTintedDark(RenderedIcon& icon, const IconRenderOptions::TintRecolour& tint) {
    const float s = static_cast<float>(std::max(0.0, tint.saturation));
    const float tr = static_cast<float>(tint.r), tg = static_cast<float>(tint.g),
                tb = static_cast<float>(tint.b);
    float* p = icon.rgba.data();
    const std::size_t n = icon.rgba.size() / 4;
    for (std::size_t i = 0; i < n; ++i, p += 4) {
        const float l = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
        p[0] = (l + s * (p[0] - l)) * tr;
        p[1] = (l + s * (p[1] - l)) * tg;
        p[2] = (l + s * (p[2] - l)) * tb;
    }
}

void applyClear(RenderedIcon& icon, const ClearBackdrop& backdrop, double squareX,
                double squareY, double squareSide, std::uint32_t canvasSize) {
    // `[BIN]` O ClearMode (laudo de 20/09 §2): total L/D/H 1.0 / 0.3 / 1.0;
    // lighteningVCM [0.9, 2.5, 2.0], highlightsVCM [0.2, 1.35, 1.4].
    constexpr double kTotalLightening = 1.0, kTotalDarkening = 0.3, kTotalHighlights = 1.0;
    GlyphVCM lighten;
    lighten.lumaFloor = 0.9;
    lighten.lumaCeiling = 2.5;
    lighten.saturation = 2.0;
    GlyphVCM highlight;
    highlight.lumaFloor = 0.2;
    highlight.lumaCeiling = 1.35;
    highlight.saturation = 1.4;
    if (backdrop.width == 0 || backdrop.height == 0 || canvasSize == 0) return;
    const double k = squareSide / static_cast<double>(canvasSize);
    auto texel = [&](int x, int y, int c) {
        x = std::clamp(x, 0, static_cast<int>(backdrop.width) - 1);
        y = std::clamp(y, 0, static_cast<int>(backdrop.height) - 1);
        return backdrop.rgba[(static_cast<std::size_t>(y) * backdrop.width + x) * 4 + c] / 255.0;
    };
    float* p = icon.rgba.data();
    for (std::uint32_t y = 0; y < icon.height; ++y) {
        for (std::uint32_t x = 0; x < icon.width; ++x, p += 4) {
            const double A = std::clamp(static_cast<double>(p[3]), 0.0, 1.0);
            if (A <= 0.0) {
                p[0] = p[1] = p[2] = p[3] = 0.0f;
                continue;
            }
            // A matriz total, na cor reta, presa em 0..1 (`addStyle:9`).
            const double mr = std::clamp(kTotalLightening * p[0], 0.0, 1.0);
            const double mg = std::clamp(kTotalDarkening * (1.0 - p[1]), 0.0, 1.0);
            const double mb = std::clamp(kTotalHighlights * p[2], 0.0, 1.0);
            // O fundo sob este pixel, bilinear.
            const double bx = squareX + (icon.originX + x + 0.5) * k - 0.5;
            const double by = squareY + (icon.originY + y + 0.5) * k - 0.5;
            const int x0 = static_cast<int>(std::floor(bx)), y0 = static_cast<int>(std::floor(by));
            const double fx = bx - x0, fy = by - y0;
            double b[3];
            for (int c = 0; c < 3; ++c) {
                b[c] = (texel(x0, y0, c) * (1 - fx) + texel(x0 + 1, y0, c) * fx) * (1 - fy) +
                       (texel(x0, y0 + 1, c) * (1 - fx) + texel(x0 + 1, y0 + 1, c) * fx) * fy;
            }
            double o[3] = {b[0], b[1], b[2]};
            auto vibrant = [&](const GlyphVCM& vcm, double a) {
                if (a <= 0.0) return;
                double v[3] = {o[0], o[1], o[2]};
                applyGlyphVCM(vcm, v);
                for (int c = 0; c < 3; ++c) o[c] += (std::clamp(v[c], 0.0, 1.0) - o[c]) * a;
            };
            vibrant(lighten, mr * A);                                          // L
            for (int c = 0; c < 3; ++c) o[c] = std::max(0.0, o[c] - mg * A);   // D, plusD
            vibrant(highlight, mb * A);                                        // H
            for (int c = 0; c < 3; ++c) {
                p[c] = static_cast<float>(std::clamp((o[c] - b[c] * (1.0 - A)) / A, 0.0, 1.0));
            }
            p[3] = static_cast<float>(A);
        }
    }
}

}  // namespace rb
