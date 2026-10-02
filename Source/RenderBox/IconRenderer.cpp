#include "Source/RenderBox/IconRenderer.h"
#include "Source/RenderBox/ColorSpace.h"

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
#include "Source/RenderBox/GlassGlow.h"
#include "Source/RenderBox/GlassLayer.h"
#include "Source/RenderBox/GlassShadow.h"
#include "Source/RenderBox/GlassSpecular.h"
#include "Source/RenderBox/SimulatedGlass.h"
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

// Whether a layer's resolved fill paints with an alpha below one anywhere. It is
// the one case where `Shadow.ignoreFillOpacity` changes the shadow's source: the
// art is then drawn a second time, with `fillWithOpaqueAlpha` of the same paint.
bool fillIsTranslucent(const FillOverride& paint) {
    if (paint.kind == FillOverride::Kind::Solid) return paint.colour[3] < 1.0f;
    if (paint.kind == FillOverride::Kind::Ramp) {
        for (const RampPoint& p : paint.stops) {
            if (p.rgba[3] < 1.0f) return true;
        }
    }
    return false;
}

// Whether a resolved fill puts any alpha on the pixel at all. A background that
// does not -- `automatic` under `tinted` resolves to a clear colour -- leaves a
// pastille with no surface, and the chiclet's highlights are not drawn over one.
bool fillPaintsSomething(const FillOverride& paint) {
    if (paint.kind == FillOverride::Kind::Solid) return paint.colour[3] > 0.0f;
    if (paint.kind == FillOverride::Kind::Ramp) {
        for (const RampPoint& p : paint.stops) {
            if (p.rgba[3] > 0.0f) return true;
        }
    }
    return false;
}

// `[BIN]` WHAT `Shadow.ignoreFillOpacity` DOES TO A FILL, in the list the shadow
// is cast from (`0x1C0DC`; the flag is the byte at `Shadow+0xA8`, tested at
// `0x1C260`). The loop of `0x1C3E8`-`0x1C430` copies every stop of a gradient
// -- three `Double`s of colour and the location, a stride of `0x28` -- and
// writes the literal `0x3FF0000000000000` where the fourth component was
// (`str x10, [x9, #0x38]`): alpha 1.0. The solid colour and the opacity of the
// other two fill kinds get the same constant. Nothing else about the fill moves.
FillOverride fillWithOpaqueAlpha(FillOverride paint) {
    paint.colour[3] = 1.0f;
    for (RampPoint& p : paint.stops) p.rgba[3] = 1.0f;
    return paint;
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
//
// `options` is the default everywhere but one call: the group's image under a
// plus-lighter blend, which is the one composite `shouldClampPlusLBlending`
// reaches (`BlendFormula.h`).
void blendOver(std::vector<float>& acc, const std::vector<float>& src, float alpha,
               BlendMode mode, const BlendOptions& options = {}) {
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
        const BlendColour out = rb::blend(mode, s, d, options);
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
//
// And it is where the colour SPACE is settled: `toWorking` (`ColorSpace.h`)
// takes Display P3 components to the working space and leaves the rest alone.
void asColour(const icf::Color& c, float (&rgba)[4]) { toWorking(c, rgba); }

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
    // `overrideForcesHiddenPaint` took one of the three padding bytes after
    // `untaggedColoursAreDisplayP3`, so the size above did not move -- and it
    // decides which shapes are painted, so it is in the key.
    h.value(o.untaggedColoursAreDisplayP3).value(o.overrideForcesHiddenPaint);
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

// The whole `Shadow` block, field by field: it is what a design generation
// changes about the shadow's image, and the step takes it as a parameter.
void hashInto(KeyHasher& h, const SizeBasedValue& v) { h.span(v.slots, 4); }

void hashInto(KeyHasher& h, const ShadowParameters& p) {
    static_assert(sizeof(ShadowParameters) == 248,
                  "a ShadowParameters field is missing from the key");
    h.value(p.offsetX).value(p.offsetY).value(p.ringWidth.has_value());
    if (p.ringWidth) hashInto(h, *p.ringWidth);
    hashInto(h, p.radius);
    hashInto(h, p.vibrantOpacity);
    hashInto(h, p.neutralOpacity);
    h.value(p.blendMode).value(p.blendModeForVibrantOnDim).value(p.overdrawBlendMode);
    h.value(p.vibrantBrightness).value(p.ignoreFillOpacity).value(p.drawOverContent);
    h.value(p.translucencyForMaxOverdraw);
    hashInto(h, p.maxNeutralOverdrawOpacity);
    hashInto(h, p.maxVibrantOverdrawOpacity);
}

void hashInto(KeyHasher& h, const PixelGrid& g) {
    static_assert(sizeof(PixelGrid) == 20, "a PixelGrid field is missing from the key");
    h.value(g.size).value(g.originX).value(g.originY).value(g.width).value(g.height);
}

void hashInto(KeyHasher& h, const SpecularArguments& a) {
    static_assert(sizeof(SpecularArguments) == 120,
                  "a SpecularArguments field is missing from the key");
    // `generation` and `set` sit in what was padding after `sizeClass`, so the
    // size above did not move when they arrived -- they are named here all the
    // same: they pick the whole list of highlights.
    h.value(a.sizeClass).value(a.generation).value(a.set);
    h.value(a.pixelsPerPoint).value(a.lightLongitude);
    h.value(a.lightIntensity).value(a.lightLatitude).value(a.layerOpacity);
    h.value(a.placement).value(a.identityRecolour).value(a.clampPlusLighter).value(a.useVCM);
    h.value(a.clearPaint);
    const SpatialHighlighting& s = a.spatial;
    h.value(s.alignmentRange).value(s.intensityPower).value(s.minIntensity);
    h.value(s.spreadPower).value(s.heightPower).value(s.maxExtraHeight).value(s.read);
}

std::size_t bytesOf(const std::vector<float>& v) { return v.size() * sizeof(float); }

// ---- the four cached steps ----------------------------------------------------
//
// Each is the step it wraps when `cache` is null. With a cache, the key is every
// input of the step. A parameter the DESIGN GENERATION changes is an input: it
// is in the key through the struct that carries it into the step -- the
// generation and the set of a highlight pass (`SpecularArguments`), the flag
// that lets a fill reach hidden shapes (`RenderOptions`), the stops and the
// rect of a fill (`FillOverride`), and the `Shadow` block the shadow's image is
// drawn with (`ShadowParameters`, in `shadow-image`).

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
    std::uint32_t height, ShadowStyle style, const ShadowGeometry& geometry,
    const ShadowParameters& parameters) {
    if (!cache) {
        return std::make_shared<const std::vector<float>>(
            shadowImage(art, width, height, style, geometry, parameters));
    }
    KeyHasher h("shadow-image");
    h.span(art.data(), art.size());
    h.value(width).value(height).value(style);
    hashInto(h, geometry);
    hashInto(h, parameters);
    const CacheKey key = h.finish();
    if (auto hit = cache->find<std::vector<float>>(key)) return hit;
    std::vector<float> made = shadowImage(art, width, height, style, geometry, parameters);
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

const char* const kTranslucencyBoundsNote =
    "a mascara de translucidez tem dois ramos (0xFE00, useSimpleMask) e os dois estao "
    "transcritos: `[BIN]` na geracao 27 e um gradiente axial numa forma INFINITA -- 17 paradas "
    "de preto com alfa S(upper + (lowerEff - upper) k/16), interpoladas pela cubica monotona "
    "(flags 0x400), sem campo de distancia, sem cobertura e com o rect do quadro no canvas "
    "(0xFE04-0x1064C) --, que esmaece a imagem inteira do grupo, dentro e fora do vidro; na "
    "geracao 26 (0x77F20) e o shader simplifiedShapeAwareGradientMask sobre o SDF, com a "
    "borda de 25.8 pt e o par de contorno. `[OBS]` No gradiente, os coeficientes da cubica "
    "sao half no alvo e float aqui. A mascara corre uma rampa VERTICAL dentro da caixa do que "
    "os elementos de vidro do GRUPO desenham, e cobre a imagem inteira do grupo: `[BIN]` o "
    "rect do shader "
    "(0xFEA8-0x101B8) e o FinalizedIcon.Layer.effectsFrame "
    "-- ou (0,0,1,1) quando nil -- vezes o tamanho da textura do SDF, e o finalizador o "
    "calcula como [displayList boundingRect] / canvas, recortado no quadro unitario "
    "(0x18234-0x182E4), sobre o display list dos elementos com participatesInGlass (0x1BFFC, "
    "filtrado em 0x180FC); `[OBS]` a caixa de uma arte raster e a dos pixels dela, nao a do alfa; "
    "`[OBS]` e qual ponta do rect recebe upperOpacity depende da lateralidade de y do "
    "display list do RB, que continua nao estabelecida";

const char* const kPlusLighterClampNote =
    "mescla plus-lighter de grupo SEM o grampo clampedPlusL: `[BIN]` "
    "ICRRenderingParameters.shouldClampPlusLBlending e true nesta geracao (0x5EAE8; a geracao 26 "
    "o zera, 0x77080) e o desenho da imagem de um grupo de mescla plus-lighter e o unico que ele "
    "alcanca (0x4B530-0x4B57C e 0x44614-0x44654: setBlendShader: clampedPlusL = max(dest, "
    "(min(1, source + dest).rgb, saturate(source.a + dest.a)))). `[OBS]` Mas o alvo so troca o "
    "composite quando um SEGUNDO byte tambem vale 1 -- ctx+0x528, um Bool da tupla do contexto de "
    "desenho (lista, Bool, fill, forma em ctx+0x520..0x538) -- e quem o escreve nao foi achado: "
    "os tres strb em +0x528 do binario (0x4DB38, 0x4E3C8, 0x4F08C) sao copias de value witness. "
    "Entao o grampo esta ligado ao render (IconRenderOptions::drawingContextClampsPlusLighter) "
    "e DESLIGADO por padrao: a soma passa de 1 onde o alvo, se o byte for 1, pararia em 1.";

const char* const kClearModeNilNote =
    "rendicao Clear / Tinted Light pedida na geracao 26: `[BIN]` ICRRenderingParameters.clearMode "
    "e nil nesta geracao (0x77064 grava a tag de nil), entao o modo efetivo do Clear e nil em "
    "todo modo de renderizacao: a passagem raiz dela (0x43150) so instala o headroom e o grampo "
    "de cor -- a matriz total do Clear e de 0x47D2C, o corpo da geracao 27 --, o conteudo nao "
    "passa pela matriz do conteudo, e os realces do glifo saem do conjunto glyphsScreened pelo "
    "ramo simples. Aqui o render e o do icone, sem a mascara, e a rendicao termina com o icone "
    "sobre o vidro simulado. `[OBS]` O que mais o alvo faz com uma rendicao .clear nesta geracao "
    "-- o filtro de tons de cinza sobre a imagem e a fonte da sombra de cada grupo (0x1AA10, "
    "0x1C128) e a composicao que o sistema faz por cima -- nao esta transcrito.";

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

// HOW MANY SAMPLES A SIDE the silhouette of a vector takes per pixel, when a
// group is lit as ONE shape and its field comes from the union of its elements'
// silhouettes (`fieldCoverageFromContours`, DistanceField.h).
//
// OURS, and not the knob above: that one picks where the SIGN of an exact field
// is read, this one is how fine an ALPHA is. `[OBS]` The target rasterises its
// silhouettes with RenderBox's own antialiased coverage, which was not read.
// Sixteen levels are what the sub-texel seed of `generateFieldFromAlpha` needs
// to place the contour inside a texel instead of on its edge; one sample would
// hand it a hard step and give back the texel-quantised normals the exact field
// was written to get rid of.
constexpr std::uint32_t kSilhouetteSamples = 4;

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

const char* const kFieldStackNote =
    "vidro: o campo deste grupo e o EMPILHAMENTO dos campos dos seus elementos de vidro. "
    "`[BIN]` Com iluminacao por elemento o alvo da um DistanceFilter a cada elemento "
    "(IconRendering 0x1CE98-0x1D084) e os empilha de tras para a frente em 0x11480: o campo de "
    "cima e desenhado dentro de um recorte feito dele mesmo por um GradientMap de duas paradas, "
    "a um degrau de meia precisao uma da outra, em d = alcance + 1 (0x11580-0x115D0), e com "
    "useAdvancedStacking ele entra antes pela mescla 0x3F9, que e sdf_maximum -- src.r > dst.r "
    "? src : dst (RenderBox default_mod66.ll %582-%586). `[INF]` O que NAO foi lido e que o campo "
    "de cima SUBSTITUI o de baixo dentro do recorte: o fragmento do DistanceFilter escreve a "
    "distancia codificada nos quatro canais, alfa inclusive (default_mod74.ll %96-%99), e se a "
    "camada dele chega opaca a mescla normal nao foi seguido. O que essa leitura mostra: numa "
    "faixa de alcance + 1 pixels em volta de cada elemento de cima o campo diz 'fora' mesmo "
    "sobre a arte do elemento de baixo, entao ali a translucidez do grupo nao age e o realce "
    "do de baixo e interrompido.";

const char* const kInvisibleGlassNote =
    "vidro: ha neste documento uma camada de vidro com opacity <= 0, e ela foi deixada de fora "
    "por inteiro. `[BIN]` O alvo so a pula ao desenhar a IMAGEM do grupo (0x1ABB8); as fontes do "
    "SDF e a lista do effectsFrame sao desenhadas com w5 = 1 -- opacidade forcada a 1 e sem o "
    "teste de opacidade (0x1BFFC, 0x1CB90) --, entao la uma camada de vidro invisivel ainda molda "
    "o campo do grupo: o realce, a translucidez e a refracao seguem a forma dela. `[OBS]` Isso "
    "nao e reproduzido aqui.";

const char* const kCombinedFieldNote =
    "vidro: o campo deste grupo e o da UNIAO dos seus elementos de vidro (lighting `combined`). "
    "`[BIN]` Com performsLightingByElement falso o alvo desenha as silhuetas de todos os "
    "elementos de vidro num display list so -- opacidade forcada a 1 e fill forcado a preto "
    "(w5 = 1), numa camada quando sao dois ou mais -- e aplica UM DistanceFilter (IconRendering "
    "0x1CDDC-0x1D1DC), entao nao ha aro onde dois elementos se encontram. Aqui a silhueta e "
    "montada na CPU: a cobertura de um vetor sai dos contornos dele em 4 x 4 amostras por pixel, "
    "a de um raster e o alfa da arte colocada, as duas juntadas por source-over, e o campo e a "
    "transformada euclidiana exata do contorno alpha >= 0.5 dessa uniao. `[OBS]` A cobertura do "
    "alvo e a do rasterizador do RenderBox, que nao foi lida; 4 x 4 amostras e escolha deste "
    "projeto.";

const char* const kCombinedRasterNote =
    "vidro: neste grupo de lighting `combined` ha elemento de vidro DEPOIS de um de arte raster, "
    "e ele nao entra na silhueta do campo. `[BIN]` E o que o alvo faz: o laco de "
    "0x1D15C-0x1D194 e isOpaque = isOpaque && desenha(elemento), o desenho (0x1AACC) devolve 0 "
    "para conteudo raster (0x1B6FC), e o && nao avalia o lado direito depois disso. O elemento "
    "e desenhado normalmente; so nao molda o campo, entao nem a translucidez, nem a refracao, "
    "nem o realce seguem a forma dele.";

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

// A CAIXA DO CONTEUDO DA ARTE, no canvas e recortada nele.
//
// `[BIN]` E o retangulo em que o alvo mede a rampa da translucidez. O argumento
// 5 do `simplifiedShapeAwareGradientMask` e montado em `0xFEA8`-`0x101B8`
// (IconRendering.arm64): o `CGRect` que a funcao RECEBE, multiplicado pela
// largura e pela altura em pixels da textura do SDF (`0x84304`, dois inteiros).
// Os tres chamadores (`0x44118`, `0x45EC8`, `0x4AD70`) passam o mesmo: o
// `FinalizedIcon.Layer.effectsFrame` (`+0x70`), ou `(0, 0, 1, 1)` quando o tag
// dele (`+0x90`) diz nil. E o finalizador (`0x17F38`) o calcula em
// `0x18234`-`0x182E4` e de novo em `0x183C8`-`0x18450`: `[displayList
// boundingRect]`, cada lado dividido pelo tamanho do canvas (`fdiv` por
// `[sp,#0x4b8]` e `[sp,#0x4c0]`), e `CGRectIntersection` com `(0, 0, 1, 1)`.
// Um retangulo normalizado vezes o tamanho da textura: a caixa do que a camada
// desenha, em pixels -- e nao o canvas inteiro, que e o que a viewBox de uma
// arte de 1024 x 1024 dava.
//
// `[OBS]` Sao DOIS display lists e dois retangulos (`0x18234` e `0x183C8`), e
// qual dos dois e o `effectsFrame` nao foi lido; o segundo e construido por
// `0x1C0DC`, e se ele desenha algo alem da arte a caixa dele e maior do que
// esta. `[OBS]` O `boundingRect` do RenderBox tambem nao foi lido: aqui a
// caixa e a das curvas amostradas, sem a meia largura de um traco.
PlacementRect artContentRect(const icf::svg::SvgDocument& svg, const LayerPlacement& p,
                             std::uint32_t size) {
    using icf::svg::SegmentKind;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool any = false;
    auto grow = [&](double x, double y) {
        if (!any) {
            x0 = x1 = x;
            y0 = y1 = y;
            any = true;
            return;
        }
        x0 = std::min(x0, x);
        x1 = std::max(x1, x);
        y0 = std::min(y0, y);
        y1 = std::max(y1, y);
    };
    for (const icf::svg::Shape& shape : svg.shapes) {
        icf::svg::Point cur{};
        for (const icf::svg::Segment& s : shape.path.segments) {
            switch (s.kind) {
                case SegmentKind::Move:
                case SegmentKind::Line:
                    cur = s.p[0];
                    grow(cur.x, cur.y);
                    break;
                case SegmentKind::Cubic:
                    for (int i = 1; i <= 16; ++i) {
                        const double t = i / 16.0, u = 1.0 - t;
                        const double a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
                        grow(a * cur.x + b * s.p[0].x + c * s.p[1].x + d * s.p[2].x,
                             a * cur.y + b * s.p[0].y + c * s.p[1].y + d * s.p[2].y);
                    }
                    cur = s.p[2];
                    break;
                case SegmentKind::Close:
                    break;
            }
        }
    }
    // Uma arte sem forma nenhuma nao tem caixa propria: a da viewBox.
    if (!any) return artPlacementRect(svg.viewBox, p, size);

    const PathGlobals g = placeOnCanvas(svg.viewBox, p, size);
    const double sx = static_cast<double>(g.m0[0]), sy = static_cast<double>(g.m1[1]);
    const double tx = static_cast<double>(g.m2[0]), ty = static_cast<double>(g.m2[1]);
    const double limit = static_cast<double>(size);
    const double l = std::clamp(tx + sx * x0, 0.0, limit), r = std::clamp(tx + sx * x1, 0.0, limit);
    const double t = std::clamp(ty + sy * y0, 0.0, limit), b = std::clamp(ty + sy * y1, 0.0, limit);
    PlacementRect out;
    out.x = l;
    out.y = t;
    out.width = r - l;
    out.height = b - t;
    return out;
}

FillOverride fillPaint(const ResolvedFill& fill, const PlacementRect& shapeRect,
                       std::string& why, const RenderingParameters& params) {
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
            //
            // `[BIN]` The derivation is one function with no generation branch
            // (`0x5864`); what the generation changes is its six constants
            // (`ICRRenderingParameters+0x50`..`+0x78`, `0x770B0`/`0x770BC`).
            out.stops = rampPointsOf(
                automaticGradientInWorkingSpace(fill.primary, params.automaticGradient));
            break;

        case ResolvedFill::Contents::System:
            // `[BIN]` The two canned chiclet ramps, with every stop's alpha
            // REWRITTEN by the fill's opacity rather than multiplied
            // (`SystemFill.h`). The placement a `.system` resolve leaves behind
            // is always nil, so this always takes the default axis below. The
            // four greys are the generation's (`+0x80`, `+0x88`).
            out.stops = rampPointsOf(
                resolveSystemFill(fill.ramp, fill.opacity, params.systemGradients).stops);
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
    // `max |d| = 0,00048828125`, que e UM ULP de `float16` -- a cobertura era
    // `VK_FORMAT_R16G16_SFLOAT` naquele dia, e o fragment continua estreitando
    // para meia precisao (`PathFragment.glsl`) --, porque `world = p.x*m0 + m2`
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

    Result<void> begin(const PixelGrid& grid, const PixelGrid&,
                       const RenderingParameters& params) override {
        grid_ = grid;
        params_ = &params;
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
    Result<void> addToGroupImage(SurfaceGroupImage& g, const SurfaceArtRef& art, double opacity,
                                 BlendMode mode) override {
        if (g.count == 0) {
            // The first element is only HELD: a group of one never gets a
            // target, and its image is the element's own art.
            g.first = art;
            g.firstOpacity = opacity;
            g.count = 1;
            return {};
        }
        if (g.count == 1) {
            // The second element is what makes it a group image. The held one
            // goes in first, `normal`: it meets nothing.
            g.target.assign(grid_.texels() * 4, 0.0f);
            blendOver(g.target, g.first->rgba, static_cast<float>(g.firstOpacity),
                      BlendMode::Normal);
            g.first.reset();
        }
        blendOver(g.target, art->rgba, static_cast<float>(opacity), mode);
        ++g.count;
        return {};
    }
    Result<SurfaceGroupTaken> takeGroupImage(SurfaceGroupImage& g) override {
        SurfaceGroupTaken taken;
        if (g.count <= 1) {
            taken.image = std::move(g.first);
            taken.opacity = g.firstOpacity;
            return taken;
        }
        // The un-multiply `finish` does, for the same reason: what reads the
        // image -- the mask, the shadow, `blendOver` -- reads STRAIGHT colour.
        auto image = std::make_shared<SurfaceArt>();
        image->rgba = std::move(g.target);
        std::vector<float>& px = image->rgba;
        parallelRanges(grid_.height, grid_.texels() * 4, [&](std::size_t y0, std::size_t y1) {
            for (std::size_t i = y0 * grid_.width * 4; i < y1 * grid_.width * 4; i += 4) {
                const float a = px[i + 3];
                for (int k = 0; k < 3; ++k) px[i + k] = a > 0.0f ? px[i + k] / a : 0.0f;
            }
        });
        taken.image = std::move(image);
        return taken;
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
    Result<const std::vector<float>*> artPixels(SurfaceArt& art) override { return &art.rgba; }
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
    Result<SurfaceField> stackField(const SurfaceField& lower, const SurfaceField& upper,
                                    float reach, bool advanced) override {
        return fieldOf(std::make_shared<const FieldImage>(
            stackFields(*lower.cpu, *upper.cpu, reach, advanced)));
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
    Result<void> glow(const SurfaceField& field, const GlowArguments& args) override {
        drawGlow(target(), *field.cpu, args);
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
                          bool clearContent, bool clampPlusLighter) override {
        BlendOptions blendOptions;
        blendOptions.clampPlusLighter = clampPlusLighter;
        if (!clearContent) {
            blendOver(target(), art.rgba, alpha, mode, blendOptions);
            return {};
        }
        std::vector<float> m = art.rgba;
        for (std::size_t i = 0; i < m.size(); i += 4) {
            m[i + 0] = kClearContentLightening * m[i + 0];
            m[i + 1] = 1.0f;
            m[i + 2] = 0.0f;
        }
        blendOver(target(), m, alpha, mode, blendOptions);
        return {};
    }
    Result<SurfaceShadow> makeShadow(RenderCache* cache, SurfaceArt& source, ShadowStyle style,
                                     const ShadowGeometry& geometry) override {
        SurfaceShadow s;
        s.image = shadowImageCached(cache, source.rgba, grid_.width, grid_.height, style,
                                    geometry, params_->shadow);
        return s;
    }
    Result<void> clipShadowOverdraw(SurfaceShadow& shadow, SurfaceArt& content,
                                    double clipAlpha) override {
        shadow.overdraw.clear();
        if (clipAlpha > 0.0) {
            shadow.overdraw = shadowOverdrawImage(*shadow.image, content.rgba, grid_.width,
                                                  grid_.height, clipAlpha);
        }
        shadow.hasOverdraw = !shadow.overdraw.empty();
        return {};
    }
    Result<void> blendShadow(const SurfaceShadow& shadow, bool overdraw, float alpha,
                             BlendMode mode) override {
        blendOver(target(), overdraw ? shadow.overdraw : *shadow.image, alpha, mode);
        return {};
    }
    Result<void> tint(const IconRenderOptions::TintRecolour& tint) override {
        tintDark(acc_.data(), acc_.size() / 4, tint);
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
    // The accumulator, always: since the group image, a blended group no longer
    // redirects what is drawn "on the picture" to a target of its own.
    std::vector<float>& target() { return acc_; }

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
    // The block of the generation this render runs with (`begin`). The step
    // whose parameters do not arrive as an argument -- the shadow's image --
    // reads them from it.
    const RenderingParameters* params_ = nullptr;
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

IconSizeClass effectiveSizeClass(const IconRenderOptions& options) {
    if (options.sizeClass) return *options.sizeClass;
    return sizeClassFor(kReferenceIconSidePoints,
                        renderingParameters(options.generation).thresholds);
}

Result<RenderedIcon> renderIconOn(IconSurface& surface, const icf::IconBundle& bundle,
                                  IconRenderOptions options) {
    if (options.size == 0) return std::unexpected("a canvas of zero size was asked for");

    // THE PARAMETER BLOCK OF THE GENERATION ASKED FOR, read once
    // (`RenderingParameters.h`), and the size class every per-size table below
    // is indexed with. Nothing under this line reaches for a module's own
    // default where the block carries the field.
    const RenderingParameters& params = renderingParameters(options.generation);
    const IconSizeClass sizeClass = effectiveSizeClass(options);

    // O DOCUMENTO E LIDO AQUI porque a margem sai DELE: `documentReach` mede o
    // alcance de cada efeito nos parametros ja denormalizados dos grupos, e o
    // plano transforma isso no buffer, na origem alinhada e no teto de area
    // (spec 2026-09-16, "A margem" e "O teto de area").
    const icf::IconDocument doc = bundle.document();
    const std::vector<icf::Group> groups = doc.groups();

    auto planned = planViewport(options.viewport, options.size,
                                documentReach(doc, options.context, sizeClass, params),
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
    if (auto began = surface.begin(grid, planned->narrow, params); !began) {
        return std::unexpected(began.error());
    }

    // Qual pastilha este contexto pede -- `ChicletShape.h`, `platformOverrides`.
    const IconPlatform platform = iconPlatformOf(options.context.idiom);

    // THE RENDERING MODE, as far as the two highlight selectors ask about it
    // (`GlassSpecular.h`, `glyphHighlightsSetFor`). `.color` is the document as
    // authored: no recolouring and no Clear mask. The effective clear mode is
    // the block's `clearMode` under the Clear mask and nil everywhere else --
    // `prepareMono` sends Clear and Tinted Light through the mask and Tinted
    // Dark through the recolouring alone, which is the split `0x0005E59C` makes
    // (`[INF]` a Tinted Light whose `applyToLightTintToo` were false would be
    // nil too; it is `true` in the one block that has a clear mode).
    const bool colourMode = !options.tint.has_value() && !options.clearMask;
    // `[BIN]` THE CLEAR MASK IS DRAWN ONLY BY A GENERATION THAT HAS A CLEAR MODE.
    // `ICRRenderingParameters.clearMode` is an Optional (`params+0x120`), and
    // `0x76FC0` writes its nil tag (`0x77064`): in generation 26 the resolved
    // clear mode is nil in every rendering mode. The root pass of that
    // generation (`0x43150`) installs the content headroom and the colour clamp
    // and never the Clear's total matrix -- that is `0x47D2C`, generation 27's
    // body -- and the highlight passes take their plain branches. So under
    // generation 26 a render asked for the mask draws the icon itself:
    // `kClearModeNilNote`, and `RenderedIcon::clearMask` tells whoever finishes
    // the rendition (`finishMono`) that there is no mask to read.
    const bool clearMask = options.clearMask && params.clearMode.has_value();
    const bool effectiveClearModeIsNil = !clearMask;
    out.clearMask = clearMask;
    if (options.clearMask && !clearMask) note(out.notes, kClearModeNilNote);
    // `[BIN]` `iconBrightness` (`+0x5B`), which BOTH selectors switch on and
    // the shadow's blend consults: classified below from the background's fill,
    // and `Default` when no background is painted (the byte is zeroed at
    // `0x0001A8C0`).
    ChicletAppearance iconBrightness = ChicletAppearance::Default;

    // THE CHICLET'S HIGHLIGHTS, settled where the background's fill is known and
    // DRAWN AFTER THE GROUPS -- the pass below the group loop says why.
    struct ChicletPass {
        SpecularArguments args;
        HighlightsSetKind set = HighlightsSetKind::Default;
        ChicletAppearance appearance = ChicletAppearance::Default;
        ChicletLuminance lum;
    };
    std::optional<ChicletPass> chicletPass;

    // `[BIN]` The root key `color-space-for-untagged-svg-colors` has one case,
    // `display-p3` (`IconComposition.assumedSVGColorSpace`); with it, every SVG
    // colour that carries no tag of its own is Display P3 (`SvgRenderer.h`).
    bool untaggedSvgIsDisplayP3 = false;
    if (const icf::json::Value* v = doc.json().find("color-space-for-untagged-svg-colors")) {
        untaggedSvgIsDisplayP3 =
            v->kind() == icf::json::Value::Kind::String && v->rawString() == "display-p3";
    }

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
            FillOverride paint = fillPaint(bg.fill, canvas, why, params);
            // Na MASCARA DO CLEAR o fundo do documento vira um solido (0, 1, 0, 1)
            // `[BIN]` (0x486D0-0x486E8 -> 0x48920-0x48964): fora da matriz do
            // conteudo, e pela matriz total vira (0, 0, 0, 1) -- cobre sem
            // clarear, escurecer nem realcar.
            if (clearMask && why.empty()) {
                paint = FillOverride{};
                paint.kind = FillOverride::Kind::Solid;
                paint.colour[0] = 0.0f;
                paint.colour[1] = 1.0f;
                paint.colour[2] = 0.0f;
                paint.colour[3] = 1.0f;
            }
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

                // OS REALCES DO CHICLET SAO PREPARADOS AQUI E DESENHADOS DEPOIS
                // DOS GRUPOS (`chicletPass`). Ate 01/10 eles eram desenhados
                // nesta linha, logo sobre o fundo recortado -- que e onde a
                // frente que os trouxe os pos, sem ter lido a ordem.
                //
                // A classe de aparencia sai da LUMINANCIA do proprio fill, que
                // e o que `0x0001A920` mede e `0x00062744` consome -- ver
                // `ChicletHighlights.h` §2.1.
                //
                // `[BIN]` O PORTAO DA CLASSIFICACAO E O MODO DE RENDERIZACAO, e
                // nao o tipo do fill, como este bloco dizia ate 01/10 (um fill
                // de sistema entrava como "nao simples" e caia em `Default`). O
                // teste de `0x0001A89C`-`0x0001A8B4` e sobre as cinco palavras e
                // a tag que `0x0001A80C`-`0x0001A824` gravam em `+0x68..+0x90`
                // -- o mesmo campo que `0x0005E59C` le como o modo -- enquanto
                // a faixa de leveza sai do fill resolvido em `+0x10`
                // (`0x0001A928`-`0x0001A944`), seja ele qual for. Entao um fill
                // de sistema classifica pelas duas paradas dele, e fora de
                // `.color` ninguem classifica.
                {
                    ChicletLuminance lum;
                    if (paint.kind == FillOverride::Kind::Ramp) {
                        lum = chicletFillLuminance(paint.stops);
                    } else if (paint.kind == FillOverride::Kind::Solid) {
                        lum = chicletFillLuminance(paint.colour);
                    }
                    const HighlightParameters& hp = params.highlights;
                    const ChicletAppearance appearance = classifyChicletAppearance(
                        lum, colourMode, hp.iconBrightnessOnlyUsesMax, hp.maxDimChicletLuminance,
                        hp.minBrightChicletLuminance);
                    iconBrightness = appearance;
                    // `[BIN]` QUAL CONJUNTO, pelo seletor `0x00062588`
                    // (`chicletHighlightsSetFor`). Na geracao 26 e a aparencia do
                    // estilo que escolhe, e ela e escura no contexto `dark` e na
                    // rendicao Tinted Dark -- a unica recoloracao que nao passa
                    // pela mascara do Clear (`prepareMono`). `[OBS]` O Clear Dark
                    // nao chega aqui como aparencia: as opcoes do render nao
                    // dizem se o Clear e o claro ou o escuro.
                    const bool appearanceIsDark =
                        options.context.appearance == icf::Appearance::Dark ||
                        (options.tint.has_value() && !options.clearMask);
                    // `[BIN]` FORA DE `.color` A GERACAO 27 DESENHA O CONJUNTO QUE
                    // O SELETOR MANDA: `chicletClear` (`+0x13A8`) quando o modo
                    // efetivo do Clear nao e nil -- as rendicoes que passam pela
                    // mascara -- e `chicletScreened` (`+0x19D8`) quando e -- o
                    // Tinted Dark (`0x62684`-`0x62740`). Ate 01/10 este render
                    // desenhava `chicletDefault` ali, com o seletor ja transcrito
                    // e uma linha que o desfazia. Sob a mascara os realces
                    // continuam repintados pela cor dela (`clearPaint`).
                    const HighlightsSetKind chicletSet = chicletHighlightsSetFor(
                        hp.chicletHighlightsAppearanceMode, appearanceIsDark, colourMode,
                        appearance, effectiveClearModeIsNil);
                    SpecularArguments chicletArgs;
                    chicletArgs.sizeClass = sizeClass;
                    chicletArgs.generation = options.generation;
                    chicletArgs.set = chicletSet;
                    chicletArgs.pixelsPerPoint =
                        static_cast<double>(options.size) / kCanvasPoints;
                    // `[BIN]` A luz do chiclet e `defaultChicletLight`, e nao a do
                    // glifo (`ctx+0x5F0`, `0x0004761C`).
                    chicletArgs.lightLongitude = hp.defaultChicletLightLongitude;
                    if (clearMask) chicletArgs.clearPaint = 2;
                    // `[INF]` Uma pastilha SEM SUPERFICIE nao e acesa: o fundo que
                    // `automatic` sob `tinted` resolve e `IconColor.clear`, alfa
                    // zero, e o realce poria luz sobre o nada. Era o que o recorte
                    // pelo alfa do fundo dava enquanto a passada vinha antes dos
                    // grupos; agora que ela vem depois, e dito aqui.
                    if (fillPaintsSomething(paint)) {
                        chicletPass = ChicletPass{chicletArgs, chicletSet, appearance, lum};
                    }
                }
                if (paint.kind == FillOverride::Kind::Ramp) {
                    note(out.notes, kGradientAxisDirectionNote);
                }

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

        // `[BIN]` A HIDDEN GROUP NEVER BECOMES AN `Icon.Layer`. The converter
        // (`IconComposerKit.arm64` `0x10C494`) answers nil for it at
        // `0x10C608`-`0x10C6D8`, before one of its layers is looked at -- so it
        // has no image, no field and no shadow. Like a hidden layer it is an
        // instruction and not a gap: nothing is skipped and nothing is noted.
        // Its layers still COUNT, because `total` is the document's ruler and
        // the document has them.
        if (boolOr(group.resolve("hidden", options.context), false)) {
            out.total += group.layers().size();
            continue;
        }

        // `[BIN]` THE GROUP'S OWN `opacity`, which nothing here read until
        // 2026-10-01. The converter calls `Group.opacity.getter` at `0x10C91C`
        // and hands the value to the `Icon.Layer` init at `0x10CB08`, and the
        // finaliser copies it verbatim into `FinalizedIcon.Layer.opacity`
        // (`0x19934`-`0x19940`) -- the `Double` at `+0x38` that `GlassShadow.h`
        // names. It is the `alpha:` of the group's content draw (`0x4B4EC`),
        // the third factor of the shadow (`0x4A070`) and the multiplier of
        // every highlight (`0x495F0`). The LAYER's `opacity` is a different
        // field, `Icon.Element.opacity`, and it goes into the group's image
        // (`0x1AD9C`/`0x1B448`) and nowhere else.
        const double groupOpacity = numberOr(group.resolve("opacity", options.context), 1.0);

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
        // `[BIN]` THE DENORMALISATION RUNS ON THE GENERATION'S OWN BLOCK
        // (`ICRRenderingParameters+0x1E8`..). The one number of it a generation
        // changes is `refractionStrengthMax`: 640 in 27, and `0x77F64` stores
        // zero over it in 26 -- so there every strength denormalises to zero
        // and no group refracts, whatever the document says.
        const DenormalisedGlass glassNumbers =
            materialDoc ? denormaliseGlass(glassMaterialFrom(*materialDoc), params.glass)
                        : DenormalisedGlass{};
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
        // The profile is the generation's (`ICRRenderingParameters+0x2B8`), and
        // its border is handed over in texels of the SDF -- `size / 1024` of
        // them per canvas point (`0x00010048`).
        const OpacityMaskArguments groupMaskArgs = opacityMaskArguments(
            params.glyphTranslucency, sizeClass, groupTranslucency,
            static_cast<double>(options.size) / kCanvasPoints);
        // `[BIN]` THE GATE IS THE RAW NUMBER (`0x4AD08`-`0x4AD10`), not whether
        // the profile it opens is flat: `GlassTranslucency.h` says what the
        // difference is in generation 26.
        const bool groupWantsMask = translucencyDrawsMask(groupTranslucency);

        // ---- the specular, which NOW DRAWS ---------------------------------
        //
        // `Docs/Laudos/2026-09-15-especular.md` closed where the highlight goes
        // -- the `glassHighlight` shader of the IconRendering metallib -- and
        // closed no number it is handed. `Docs/Laudos/2026-09-15-highlights.md`
        // read the 16113 bytes of `ICRRenderingParameters.Highlights` those
        // numbers come from, so this stops being a note and starts being pixels.
        //
        // SIX highlights, not one, in generation 27: `0x00030E88` expands one
        // `HighlightsSet` into seven candidates and that generation's glyph
        // sets leave six alive -- a sharp key rim, a diffuse key wash, a sharp
        // fill rim and a diffuse fill wash opposite them, and the dark one
        // drawn twice at +-90 degrees. Generation 26's leave two: the key rim
        // and a full ring. WHICH set is the selector's answer (`0x000627B4`).
        //
        // `[ART]` It fires for real: over the 145 corpus documents 67 carry a
        // group-level `specular`, and of the 103 values 64 are `true` and 3 are
        // the string `"inside"`.
        const bool wantsSpecular = documentAsksForSpecular(glassNumbers);
        SpecularArguments specularArgs;
        specularArgs.sizeClass = sizeClass;
        specularArgs.generation = options.generation;
        specularArgs.set =
            glyphHighlightsSetFor(colourMode, iconBrightness, effectiveClearModeIsNil);
        specularArgs.pixelsPerPoint = static_cast<double>(options.size) / kCanvasPoints;
        specularArgs.lightLongitude = params.highlights.defaultGlyphLightLongitude;
        specularArgs.useVCM = params.highlights.glyphHighlightsUseVCM;
        specularArgs.placement = glassNumbers.specularPlacement;
        // The list those two pick, for the two loops below that measure it.
        const std::vector<HighlightSlot>& glyphHighlights = expandedHighlights(
            specularArgs.generation, HighlightFamily::Glyph, specularArgs.set);
        if (clearMask) {
            specularArgs.clearPaint = 1;
            specularArgs.useVCM = false;   // `[BIN]` 0x4967C-0x4969C: o VCM nao roda
        }

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
        // and the order (the group's SHADOW first, so the backdrop includes it
        // -- and not the group's art, as this line said until 2026-10-01; see
        // the correction in `BlurKernel.h`). What is left `[OBS]` is the layer
        // FRAME, and it is an argument below rather than a constant inside the
        // blur.
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
        // Blending a group is not blending a layer. The whole group is
        // flattened into ONE image first and that image is what meets the
        // mode -- `[BIN]` `0x4B4EC`, a `drawShape:fill:alpha:blendMode:` whose
        // blend is the byte at `[descriptor+0x31]` (`0x4B518`-`0x4BA6C`). A
        // spelling this reader does not know is still a named refusal, never a
        // quiet `normal`.
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
        const bool groupBlendRefused = groupBlend && !groupMode;
        const BlendMode groupBlendMode = groupMode ? *groupMode : BlendMode::Normal;

        // A BLENDED GROUP WHOSE GLASS REFRACTS USED TO BE REFUSED HERE, and the
        // refusal went away with the thing it was protecting against.
        //
        // It stood for as long as a blended group was drawn into a target of
        // its own and mixed at the end. `glassOver` displaces whatever buffer it
        // is handed, a fresh group target has nothing in it, and such a group
        // would have drawn with its refraction silently missing -- the defect
        // this file names as the worst of the three. Two earlier paragraphs
        // here argued about whether the TARGET had the same problem, one by
        // reading `GlassDisplacementStyle::draw` (`0x000F3B38`, the map
        // generator, which does filter the item it wears) and one by reading
        // the displacement installed after it (`0x10C14` closing the map layer
        // with `addFilterLayerWithShader:`, then `beginLayerWithFlags: 1` at
        // `0x4A794`/`0x4AA00`, whose bit 0 hangs a `BackdropFilterItem` on the
        // PARENT -- `Docs/Laudos/2026-09-15-refracao.md`). The second reading
        // holds: the refraction displaces what is already underneath.
        //
        // `[BIN]` What neither paragraph had was the group's own pipeline, and
        // it dissolves the question. `0x48B74` runs once per group and puts
        // everything on the icon's own list (`[ctx+0x520]`), in this order: the
        // glass pass `0x4A2D4` -- the refraction of what is underneath, and the
        // shadow --, then the content `0x4AC84`, then the highlights `0x491C0`.
        // The ONLY draw in it that takes the group's blend byte is the content's
        // `0x4B4EC`. There is no target of the group's own to starve: its
        // refraction, shadow and highlights meet the icon exactly as a normal
        // group's do, and only its flattened image is mixed with the mode.
        //
        // `[ART]` 8 corpus documents put a non-normal blend and a glass layer on
        // the same group, `insidegui/AssetCatalogTinkerer` and `RuntimeViewer`
        // among them; 18 groups in all.

        // ---- what the group IS, read before any of it is drawn ---------------
        //
        // Two facts about the layers decide how the group's buffers are built,
        // and both are answers of the document: how many of them take part in
        // the glass, and whether any does not. (A missing `glass` is `true` --
        // the `[BIN]` note at the layer gate below.)
        std::size_t glassLayers = 0;
        bool hasPlainLayer = false;
        for (const icf::Layer& l : group.layers()) {
            if (boolOr(l.resolve("hidden", options.context), false)) continue;
            if (boolOr(l.resolve("glass", options.context), true)) {
                ++glassLayers;
            } else {
                hasPlainLayer = true;
            }
        }

        // `[BIN]` THE REACH OF THE GROUP'S FIELD, `0x1C6B8`: the largest
        // `max(distance[sizeClass] * min(w, h) / 1024, minDistancePixels[sizeClass])`
        // over the enabled highlights, and zero for a group with no specular.
        // `resolveHighlight` already computes that maximum per highlight, as
        // `height`, in pixels. `[OBS]` The gates of `0x1C72C`-`0x1C774` were read
        // as branches only. It is how far an upper element's field reaches over
        // a lower one's when the two are stacked (`DistanceField.h`, part four).
        float fieldReach = 0.0f;
        if (wantsSpecular) {
            for (const HighlightSlot& slot : glyphHighlights) {
                const GlassHighlightSettings g = resolveHighlight(slot, specularArgs);
                fieldReach = std::max(fieldReach, static_cast<float>(g.height));
            }
        }
        // `[BIN]` `SDFGeneration.useAdvancedStacking`, `true` in generation 27's
        // parameter block (`0x5EAB0`) and `false` in generation 26's
        // (`0x77F68`). `[OBS]` The `RBProjectVersion` gate beside it (`0x11B00`)
        // was not read; it is taken as open. See `DistanceField.h` for why
        // neither can move a pixel here.
        const bool advancedStacking = params.useAdvancedStacking;

        // ---- `lighting`, WHICH NOW DRAWS ------------------------------------
        //
        // `[BIN]` The key is `Icon.Layer.performsLightingByElement`, and the
        // mapping is read: the converter (`IconComposerKit` `0x10CA38`-
        // `0x10CB08`) takes `Group.lighting.getter` through
        // `effectiveSpecialization(for:)` to a byte and passes
        // `cmp w19, #0; cset w3, eq` -- case 0 is `individual`
        // (`IconComposerFoundation`'s enum order is `individual, combined`) and
        // a missing key defaults to 0 (`0x90978`). So `individual` AND a missing
        // key light element by element; only `combined` does not. Until
        // 2026-10-01 `readGlassMaterial` read the key and nothing consumed it.
        //
        // `[BIN]` WHAT IT SWITCHES is how the group's ONE field is built,
        // `0x1CB90` at `0x1CDD8`. Element by element: a `DistanceFilter` per
        // glass element, stacked (`stackFields`). Combined
        // (`0x1CDDC`-`0x1D1DC`): ONE source list -- every glass element drawn
        // into it as a silhouette, opacity forced to 1 and fill to black
        // (`w5 = 1`), wrapped in a layer when there are two or more -- and one
        // `DistanceFilter` over that: the field of the UNION, with no rim where
        // two elements meet.
        //
        // It only matters with two glass elements or more: the union of one
        // silhouette is that silhouette, and such a group keeps the field it
        // always had. `[ART]` In the default context 73 of the corpus's groups
        // resolve `individual` and 22 `combined` (`test_glass_material.cpp` pins
        // both counts), and the rest carry no key; 10 groups are `combined` with
        // two or more glass layers.
        const bool combinedLighting = materialDoc && materialDoc->lighting &&
                                      *materialDoc->lighting == icf::Lighting::Combined;
        const bool combinedField = combinedLighting && glassLayers > 1;
        // THE UNION'S ALPHA, one float per texel, built on the CPU on both
        // render paths. The field is taken from it with `alpha >= 0.5` as the
        // contour (`generateFieldFromAlpha`), and a threshold tolerates no ulp:
        // a silhouette composited on the GPU would put a texel on the other side
        // of it now and then, and the field around that texel would differ. So
        // each element contributes a coverage both paths compute identically --
        // a vector its contours' (`fieldCoverageFromContours`), a raster the
        // alpha of its exact CPU placement -- and they are joined source-over,
        // which is what drawing opaque silhouettes into one list does.
        //
        // Not the minimum of the elements' fields, and not their contours
        // concatenated: both leave a crease along the seam where two elements
        // abut, which is the one thing this mode exists to remove.
        std::vector<float> silhouette;
        std::size_t silhouetteParts = 0;
        // `[BIN]` THE QUIRK of `0x1D15C`-`0x1D194`: the loop that draws the
        // silhouettes is `isOpaque = isOpaque && draw(element)`, and the draw
        // (`0x1AACC`) answers 0 for raster content (`0x1B6FC`). `&&` does not
        // evaluate its right side once the left is false, so the first raster
        // element is drawn and every glass element AFTER it is not.
        bool silhouetteClosed = false;
        bool silhouetteLeftOut = false;
        // The key of the union for the resident cache (`gpu-field-stack`): what
        // went into it, in order. The CPU path ignores it.
        KeyHasher silhouetteKey("gpu-field-stack");
        silhouetteKey.bytes("combined", 8);
        bool silhouetteKeyed = true;

        // ---- the shadow, which is the GROUP's --------------------------------
        //
        // `[BIN]` Until 2026-09-15 this renderer drew NO shadow at all:
        // `GlassMaterial.h` transported `shadowStyle` and `shadowOpacity` and
        // named them as fields with no known consumer.
        // `Docs/Laudos/2026-09-15-sombra.md` read the alpha and the geometry end
        // to end, so it now draws. `GlassShadow.h` carries the whole of it.
        //
        // ONE SHADOW PER GROUP, CAST FROM A LIST OF ITS OWN. `[BIN]`
        // `FinalizedIcon.Layer.shadowImage` is one image, and the finaliser
        // makes it from a third display list (`0x1C0DC`): the elements that
        // take part in the glass -- or ALL of them when `Shadow.ringWidth` is
        // nil (`0x18338`-`0x18354`) -- drawn with their real opacity and blend,
        // then colour, translate, blur and the ring clip (`0x19468`-`0x195CC`).
        // The translucency mask is not in that chain: the shadow is cast from
        // the art BEFORE the mask.
        //
        // That closes the gate this block used to carry as `[INF]`. A group with
        // no glass element has an empty source list and casts nothing -- `[ART]`
        // all 271 corpus groups carry a `shadow` key while only 113 contain
        // glass -- and it is the ring's presence, a parameter, that says so.
        //
        // Num modo tingido a sombra e `neutral` (`[BIN]` 0x49F40); `none`
        // continua `none`, porque a saida de `none` vem antes do portao.
        //
        // THE NUMBERS ARE THE GENERATION'S (`ICRRenderingParameters+0x2B0`), every
        // one of them: generation 26 moves the offset to (16, 16), widens the
        // blur, drops the ring, lowers both opacity tables, blends with
        // `plusDarker` and draws no overdraw (`0x77E80`-`0x77ED0`).
        const ShadowParameters& shadowParams = params.shadow;
        const ShadowStyle shadowStyle =
            shadowEffectiveStyle(glassNumbers.shadowStyle, options.tint.has_value());
        const ShadowGeometry shadowGeom =
            shadowGeometry(options.size, sizeClass, shadowParams, params.glass);
        // `[BIN]` THE RING DECIDES WHO CASTS (`0x18338`-`0x18354`): the tag of
        // `Shadow.ringWidth` is tested, and nil replaces the glass-filtered list
        // with the list of ALL the group's elements. So in generation 26 a group
        // with no glass element at all casts a shadow, and nothing is clipped to
        // a ring (`0x20C48`).
        const bool shadowFromGlassOnly = shadowGeom.ringWidth.has_value();
        // Whether the group asks for a shadow at all, before any element is
        // known: the element's own opacity can scale the alpha and cannot change
        // its sign.
        const bool wantsShadow = shadowDraws(
            ShadowInputs{shadowStyle, glassNumbers.shadowOpacity, groupOpacity, sizeClass},
            shadowParams);
        // `[BIN]` `Shadow.ignoreFillOpacity` (`0x1C260`): whether some element
        // of the source is drawn AGAIN for the shadow, its fill's alpha rewritten
        // to one (`fillWithOpaqueAlpha`). Asked of the document before the
        // element pass, because it decides whether the source can share the
        // group's image. The rect a fill is placed against does not change its
        // alpha, so a unit rect answers the question; an element this pass counts
        // and the element pass then skips only costs a buffer.
        bool shadowSourceRewritten = false;
        if (wantsShadow && shadowParams.ignoreFillOpacity) {
            for (const icf::Layer& l : group.layers()) {
                if (boolOr(l.resolve("hidden", options.context), false)) continue;
                if (shadowFromGlassOnly && !boolOr(l.resolve("glass", options.context), true)) {
                    continue;
                }
                const std::string* image = textOf(l.resolve("image-name", options.context));
                if (!image || image->empty()) continue;
                std::string e = bundle.assetPath(*image).extension().string();
                std::transform(e.begin(), e.end(), e.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (e != ".svg") continue;   // a fill does not reach a raster (`kRasterFillNote`)
                const FillResolution f = resolveLayerFill(l, options.context);
                if (f.outcome != FillOutcome::Resolved) continue;
                std::string why;
                const FillOverride probe =
                    fillPaint(f.fill, PlacementRect{0.0, 0.0, 1.0, 1.0}, why, params);
                if (why.empty() && fillIsTranslucent(probe)) {
                    shadowSourceRewritten = true;
                    break;
                }
            }
        }
        // The source is a buffer of its own only when it is NOT the group's
        // image: some element stays out of it, or some element goes into it
        // drawn differently.
        const bool separateShadowSource =
            wantsShadow && ((shadowFromGlassOnly && hasPlainLayer) || shadowSourceRewritten);

        // ---- the group's `effectsFrame`, the rect the translucency ramp runs in
        //
        // `[BIN]` ONE RECT PER GROUP, and it is the union of the boxes of the
        // elements that participate in the glass. The finaliser (`0x17F38`)
        // builds three display lists: all the elements (`0x1A9D0`), which gives
        // `contentFrame`; only the elements whose byte `+0x19` is set
        // (`0x1BFFC`, filtered at `0x180FC`), which gives `effectsFrame`
        // (`0x18234`-`0x182E4`, stored at `0x19960`); and the shadow's source
        // (`0x1C0DC`). Nothing in the second one outsets the content, and its
        // opacity is forced to one. The tag is nil exactly when that box is
        // empty (`CGRectIsEmpty` at `0x182F0`, stored at `0x19968`).
        //
        // Parsed here a second time, and only for a group that has a
        // translucency to draw. `[OBS]` A glass RASTER contributes its whole
        // placement rect in the target (`setRect:` at `0x1B090`); the size of a
        // raster is only known further down, so for a group that has one the
        // box is gathered in the element pass instead (`glassBox`) -- which
        // sees the elements that are DRAWN, and so leaves out a glass layer at
        // zero opacity that the target's list still measures (`w5 = 1`, no
        // opacity skip).
        //
        // `[BIN]` THE GLOW OF GENERATION 26 READS THE SAME RECT, as its clip
        // (`0x48C6C`-`0x48D78`), and unlike the mask it does not draw at all
        // when the rect is nil (`0x48C60`-`0x48C68`). It needs a field with a
        // reach, which only a group with a specular has (`GlassGlow.h`).
        const bool wantsGlow = params.glow.has_value() && wantsSpecular;
        const bool wantsEffectsRect = groupWantsMask || wantsGlow;
        std::optional<PlacementRect> groupEffectsRect;
        // The box was settled here and it is empty: a nil `effectsFrame`.
        bool groupEffectsNil = false;
        if (wantsEffectsRect) {
            bool vectorOnly = true, any = false;
            double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            for (const icf::Layer& l : group.layers()) {
                if (!boolOr(l.resolve("glass", options.context), true)) continue;
                if (boolOr(l.resolve("hidden", options.context), false)) continue;
                const std::string* image = textOf(l.resolve("image-name", options.context));
                if (!image || image->empty()) continue;
                const std::filesystem::path file = bundle.assetPath(*image);
                if (!std::filesystem::is_regular_file(file)) continue;
                std::string e = file.extension().string();
                std::transform(e.begin(), e.end(), e.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (e != ".svg") {
                    vectorOnly = false;
                    break;
                }
                const auto doc = icf::svg::SvgDocument::parse(readAll(file));
                if (!doc) continue;
                const PlacementRect r = artContentRect(
                    *doc, compose(gp, placementOf(l.resolve("position", options.context))),
                    options.size);
                if (!(r.width > 0.0 && r.height > 0.0)) continue;
                x0 = any ? std::min(x0, r.x) : r.x;
                y0 = any ? std::min(y0, r.y) : r.y;
                x1 = any ? std::max(x1, r.x + r.width) : r.x + r.width;
                y1 = any ? std::max(y1, r.y + r.height) : r.y + r.height;
                any = true;
            }
            if (vectorOnly) {
                // An empty box is a nil `effectsFrame`, and the callers of the
                // mask then pass `(0, 0, 1, 1)`: the whole canvas.
                const double whole = static_cast<double>(options.size);
                groupEffectsRect = any ? PlacementRect{x0, y0, x1 - x0, y1 - y0}
                                       : PlacementRect{0.0, 0.0, whole, whole};
                groupEffectsNil = !any;
            }
        }

        // ---- THE GROUP'S OWN BUFFERS, filled by the element pass below --------
        //
        // `[BIN]` EVERY EFFECT IS PER GROUP BY CONSTRUCTION. `FinalizedIcon.Layer`
        // carries ONE image, ONE SDF and ONE shadow image, and the compositor
        // (`0x48B74`) has no element loop at all. So the pass below composites
        // nothing: each element it draws goes INTO these, and the group is put
        // on the picture once, after the last one.
        //
        // `[BIN]` THE IMAGE. The finaliser draws every element of a group into
        // ONE display list (`0x1A9D0`, in array order -- which the converter has
        // already turned back to front, `0x10C7A8`-`0x10C7C4`) and rasterises
        // that alone into the group's own image (`0x13590`, cropped to
        // `contentFrame`). The element's `opacity` and `blendMode` are applied
        // INSIDE it (`drawLayerWithAlpha:blendMode:` at `0x1AD9C`/`0x1B448`), so
        // an element's blend meets only what its own group drew before it, over
        // transparent -- and the first one meets nothing, which is `normal`.
        // Until 2026-10-01 only that first case was applied here and a later
        // element blended against the whole icon.
        SurfaceGroupImage groupImage;
        // The shadow's source, when it is not the image (see above).
        SurfaceGroupImage shadowSource;
        std::size_t elements = 0;        // drawn into the image
        std::size_t shadowElements = 0;  // in the shadow's source list
        // The ring's silhouette, where the source here is not the target's:
        // `kShadowSourceNote`.
        bool shadowSourceDiffers = false;
        // `[BIN]` THE FIELD. One per group too (`0x1CB90`): the fields of the
        // glass elements stacked back to front (`DistanceField.h`, part four),
        // or -- lit as one shape -- the field of their union, taken after the
        // loop from `silhouette`.
        std::optional<SurfaceField> groupField;
        // The box of the glass elements drawn, for a group whose
        // `effectsFrame` was not settled above.
        bool glassBoxAny = false;
        double glassBox[4] = {0.0, 0.0, 0.0, 0.0};   // x0, y0, x1, y1
        // The one element of a group of one, for the sentences that used to be
        // per layer and still should name it.
        std::string soleLabel;
        bool soleIsVector = false;

        // ...and so does the layer array inside a group, for the same reason
        // and by the same proof. The corpus signal here is weak on its own --
        // three documents name a layer "background", two of them first -- so
        // what carries it is the Apollo render: `Eyes` sits at index 1 and
        // `Apollo Helmet Space` at index 3, and only the reversed order puts
        // the eyes on top, where the shipped icon has them.
        //
        // `[BIN]` AT THIS LEVEL IT IS NOW READ AS WELL. The converter's loop at
        // `0x10C7A8`-`0x10C7C4` (`IconComposerKit.arm64`) walks the document's
        // layer array BACKWARDS and appends, so `Icon.Layer.elements` is back to
        // front and the finaliser draws it in that order. The order of the
        // GROUPS across the document is still the `[ART]` of the outer loop.
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
            if (groupBlendRefused) {
                skip("mescla de grupo '" + *groupBlend + "' -- grafia ou modo"
                     " que este leitor nao desenha");
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
            if (opacity <= 0.0) {
                // `[BIN]` `0x1ABB8`: an element at zero opacity is skipped when
                // the group's image is drawn. It is NOT skipped when the SDF's
                // sources and the `effectsFrame` list are (`w5 = 1`: opacity
                // forced to 1, and no opacity test), so in the target an
                // invisible glass element still shapes the group's field.
                // `[OBS]` That is not reproduced -- the element is dropped
                // whole here -- and it is said where it could show.
                if (isGlass && (!glassRefractionIsIdentity(refraction) || groupWantsMask ||
                                wantsSpecular)) {
                    note(out.notes, kInvisibleGlassNote);
                }
                continue;
            }

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

            // ---- the element's ART, parsed and placed ---------------------
            //
            // (The decision that the glass refracts what is underneath and the
            // art is then painted over it sits with the group's composite,
            // below the loop.)
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
                // `use`, a class no stylesheet matched. `IconComposerCli` prints them
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
                    // THE RECT THE FILL IS PLACED AGAINST: the art's own box --
                    // except for a `.system` fill in a generation that aligns
                    // them to the chiclet, where it is the whole canvas and
                    // every layer of the icon shares one ramp (`[BIN]` `0x1BA98`;
                    // `SystemFill.h`). Generation 27 aligns; 26 does not.
                    PlacementRect fillRect = artPlacementRect(svg->viewBox, lp, options.size);
                    if (fill.fill.contents == ResolvedFill::Contents::System) {
                        const double whole = static_cast<double>(options.size);
                        fillRect = systemFillRect(
                            params.supportsChicletAlignmentForSystemFills
                                ? SystemFillRectSource::ChicletAligned
                                : SystemFillRectSource::BoundingRect,
                            fillRect, PlacementRect{0.0, 0.0, whole, whole});
                    }
                    paint = fillPaint(fill.fill, fillRect, why, params);
                    if (!why.empty()) {
                        skip(why);
                        continue;
                    }
                    if (paint.kind == FillOverride::Kind::Ramp && !fill.fill.placement) {
                        note(out.notes, kGradientAxisDirectionNote);
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

            // ALL THREE GLASS EFFECTS EAT THE SAME DISTANCE FIELD, so it is built
            // once: the refraction displaces the backdrop through it, the
            // translucency mask is cut by it and the highlights are drawn along
            // it. `[BIN]` And that field is the GROUP's -- what is built here is
            // this element's contribution to it.
            //
            // WHICH ELEMENTS CONTRIBUTE IS READ NOW, and the `[INF]` this comment
            // used to carry is closed. The SDF's sources are the elements whose
            // `participatesInGlass` byte (`+0x19`) is set -- the filter loop of
            // `0x180A4`-`0x181A8` -- and that byte is the document's `glass`
            // bit. A group with none of them has an EMPTY SDF, and for an empty
            // SDF the compositor opens neither the glass pass
            // (`0x4A338`-`0x4A348`) nor the translucency clip (`0x4ACB4`).
            // `[ART]` All 271 corpus groups carry a `translucency` key whether
            // or not they contain glass, and this is why the key does not fade
            // the ones that do not.
            //
            // WHAT THE BIT DOES NOT DO is keep the mask off an element. The clip
            // is laid over the group's whole image (the composite below), so a
            // plain element that shares its group with a glass one is faded
            // wherever the group's field covers it.
            const bool wantsRefraction = isGlass && !glassRefractionIsIdentity(refraction);
            const bool wantsTranslucency = isGlass && groupWantsMask;
            // The highlight rides the SAME distance field as the other two, so
            // it joins the same gate rather than building a second one.
            const bool wantsHighlight = isGlass && wantsSpecular;
            // The element's own field, kept past the block that builds it: it
            // goes into the group's once the element's art exists.
            std::optional<SurfaceField> field;

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

                // IN A GROUP LIT AS ONE SHAPE the element gives its SILHOUETTE to
                // the union instead of building a field of its own
                // (`combinedField`, above the loop). `inSilhouette` says it
                // joined; `leftOut` says the target's own quirk kept it out.
                bool inSilhouette = false;
                bool leftOut = false;
                auto joinSilhouette = [&](const float* alpha, std::size_t stride) {
                    const std::size_t texels = grid.texels();
                    if (silhouette.empty()) silhouette.assign(texels, 0.0f);
                    // Source-over of opaque black at this coverage: only the
                    // alpha is kept, and that is all the field reads.
                    for (std::size_t t = 0; t < texels; ++t) {
                        const float a = std::clamp(alpha[t * stride], 0.0f, 1.0f);
                        silhouette[t] = a + silhouette[t] * (1.0f - a);
                    }
                    ++silhouetteParts;
                    inSilhouette = true;
                };

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
                    } else if (combinedField) {
                        if (silhouetteClosed) {
                            leftOut = true;
                        } else {
                            FieldOptions fo;
                            fo.rule = shape.rule;
                            fo.originX = grid.originX;
                            fo.originY = grid.originY;
                            std::vector<float> coverage;
                            if (fieldCoverageFromContours(shape.contours, grid.width, grid.height,
                                                          fo, kSilhouetteSamples, coverage) == 0) {
                                fieldGap = "vidro: a arte fecha contornos mas nenhum ponto de amostra "
                                           "cai dentro deles nesta resolucao (" + *imageName + ")";
                            } else {
                                joinSilhouette(coverage.data(), 1);
                                hashInto(silhouetteKey, shape.contours);
                                silhouetteKey.value(shape.rule);
                            }
                        }
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
                        // O brilho interno le a profundidade ate o alcance do
                        // campo, onde ela satura (`glowFragment`).
                        if (wantsGlow) accReach = static_cast<double>(fieldReach) + 1.0;
                        if (wantsHighlight) {
                            for (const HighlightSlot& slot : glyphHighlights) {
                                const GlassHighlightSettings g =
                                    resolveHighlight(slot, specularArgs);
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
                        // E ATE ONDE O EMPILHAMENTO O LE. Num grupo com mais de um
                        // elemento de vidro o campo de cima substitui o de baixo
                        // onde `d < alcance + 1` (`stackFields`), no buffer INTEIRO
                        // -- a mascara le o resultado tambem fora da estreita --,
                        // entao as duas bandas tem de cobrir essa borda, ou a GPU
                        // escolheria por um valor saturado.
                        if (glassLayers > 1) {
                            const float stackBand = bandOf(static_cast<double>(fieldReach) + 1.0);
                            bands.art = std::max(bands.art, stackBand);
                            if (!wantsRefraction) {
                                bands.accumulator = std::max(bands.accumulator, stackBand);
                            }
                        }
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
                } else if (rasterPlaced && combinedField) {
                    if (silhouetteClosed) {
                        leftOut = true;
                    } else {
                        // The alpha of the art as `placeRaster` put it: on the
                        // resident path that is the exact CPU placement of [UP3],
                        // so both paths read the same floats.
                        auto pixels = surface.artPixels(*rasterPlaced);
                        if (!pixels) return std::unexpected(pixels.error());
                        const std::vector<float>& rgba = **pixels;
                        bool reaches = false;
                        for (std::size_t i = 3; i < rgba.size() && !reaches; i += 4) {
                            reaches = rgba[i] >= 0.5f;
                        }
                        if (!reaches) {
                            fieldGap = "vidro sobre raster: nenhum texel da arte chega a alpha >= 0.5, "
                                       "entao nao ha contorno para assinar (" + *imageName + ")";
                        } else {
                            joinSilhouette(rgba.data() + 3, 4);
                            if (rasterPlaced->keyed) {
                                silhouetteKey.value(rasterPlaced->key.a).value(rasterPlaced->key.b);
                            } else {
                                silhouetteKeyed = false;
                            }
                        }
                        // Drawn or not, a raster is where the target's loop stops.
                        silhouetteClosed = true;
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

                if (leftOut) {
                    // Not a gap of this renderer: the element is drawn, and the
                    // target's own loop keeps it out of the field.
                    silhouetteLeftOut = true;
                } else if (!field && !inSilhouette) {
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
                }
            }

            // ---- THE ELEMENT'S ART -------------------------------------
            SurfaceArt drawnArt;
            // Kept past the draw: the shadow's source may ask for the same art
            // under another paint.
            RenderOptions ro;
            if (svg) {
                ro.width = grid.width;
                ro.height = grid.height;
                ro.originX = grid.originX;
                ro.originY = grid.originY;
                ro.projectionWidth = grid.size;
                ro.projectionHeight = grid.size;
                ro.subdivisions = options.subdivisions;
                ro.untaggedColoursAreDisplayP3 = untaggedSvgIsDisplayP3;
                ro.override = paint;
                // `[BIN]` `recreateRadar153477135` (`SvgRenderer.h`): whether a
                // fill reaches the shapes the art left unpainted.
                ro.overrideForcesHiddenPaint = params.recreateRadar153477135;
                auto drew = surface.drawSvg(options.cache, svgText, *svg,
                                            placeOnCanvas(svg->viewBox, lp, options.size), ro);
                if (!drew) return std::unexpected(drew.error());
                for (const auto& s : drew->skipped) {
                    out.shapeGaps.push_back(name + " / " + *imageName + ": " + s.why);
                }
                drawnArt = std::move(*drew);
            } else if (rasterPlaced) {
                // THE RASTER NOW RUNS THE WHOLE GLASS, exactly as the vector
                // does: from here on the two are one element. The asymmetry
                // this branch used to carry -- "a raster CAN cast a shadow,
                // where it cannot carry a translucency mask" -- is gone,
                // because the reason for it was our missing generator and not
                // the format. `[BIN]` `DistanceField.h` carries the addresses.
                //
                // `[ART]` It is 45 of the corpus's 171 glass layers under the
                // any-appearance reading of the `glass` key, across 31
                // documents; 39 of 146 across 29 documents if only a
                // specialization's BASE entry counts. Both counts were recounted
                // for this front and both are in the laudo.
                drawnArt = std::move(*rasterPlaced);
            } else {
                skip("arte com extensao que este leitor nao le: " + *imageName);
                continue;
            }

            // ==== INTO THE GROUP ==========================================
            //
            // Everything above PREPARES the element: its art and its field.
            // Nothing is put on the picture here. The element goes into the
            // group's buffers, and the group is composited once, after the
            // last one.
            const SurfaceArtRef elementArt = std::make_shared<SurfaceArt>(std::move(drawnArt));
            if (auto ok = surface.addToGroupImage(groupImage, elementArt, opacity,
                                                  elements == 0 ? BlendMode::Normal : layerBlend);
                !ok) {
                return std::unexpected(ok.error());
            }
            if (elements == 0) {
                soleLabel = name + " / " + *imageName;
                soleIsVector = svg.has_value();
            }
            ++elements;

            // THE SHADOW'S SOURCE takes the element when the target's list
            // would: a glass element always, any element when there is no ring.
            if (wantsShadow && (isGlass || !shadowFromGlassOnly)) {
                // THE ART THE SHADOW IS CAST FROM, named apart from the art that
                // is drawn because in the target they are two renders: with
                // `Shadow.ignoreFillOpacity` the source list is drawn again with
                // every fill's alpha rewritten to 1.0 (`0x1C260`,
                // `0x1C3E8`-`0x1C430`). `[BIN]` So a translucent fill casts the
                // shadow of an opaque one in generation 27; generation 26 clears
                // the flag (`0x77ED0`) and casts from the art as drawn. The
                // second render is the same SVG under `fillWithOpaqueAlpha` of
                // the same paint -- a different key in the cache -- and only an
                // element whose fill is translucent pays for it.
                SurfaceArtRef shadowSourceArt = elementArt;
                if (separateShadowSource && shadowParams.ignoreFillOpacity && svg &&
                    fillIsTranslucent(paint)) {
                    RenderOptions opaque = ro;
                    opaque.override = fillWithOpaqueAlpha(paint);
                    auto drew = surface.drawSvg(options.cache, svgText, *svg,
                                                placeOnCanvas(svg->viewBox, lp, options.size),
                                                opaque);
                    if (!drew) return std::unexpected(drew.error());
                    shadowSourceArt = std::make_shared<SurfaceArt>(std::move(*drew));
                }
                // More than one element at less than full opacity: the ring here
                // is cut from the flattened source's own alpha, the target's from
                // silhouettes at opacity 1 (`kShadowSourceNote`, second half).
                if (shadowFromGlassOnly && glassLayers > 1 && opacity < 1.0) {
                    shadowSourceDiffers = true;
                }
                if (separateShadowSource) {
                    if (auto ok = surface.addToGroupImage(
                            shadowSource, shadowSourceArt, opacity,
                            shadowElements == 0 ? BlendMode::Normal : layerBlend);
                        !ok) {
                        return std::unexpected(ok.error());
                    }
                }
                ++shadowElements;
            }

            // THE FIELD, stacked on what the group has so far. `[BIN]` The upper
            // element's field replaces the lower one's inside its own footprint
            // dilated by the field's reach (`0x11480`; `DistanceField.h`, part
            // four). The first glass element's field IS the group's until a
            // second one arrives, so a group with one glass element keeps the
            // field it always had.
            if (field) {
                if (!groupField) {
                    groupField = std::move(*field);
                } else {
                    auto stacked =
                        surface.stackField(*groupField, *field, fieldReach, advancedStacking);
                    if (!stacked) return std::unexpected(stacked.error());
                    groupField = std::move(*stacked);
                    note(out.notes, kFieldStackNote);
                }
            }

            // The element's box, for the group's `effectsFrame` when the pass
            // over the documents above could not settle it (a raster's size is
            // only known here). A RASTER'S BOX IS ITS PIXELS: `[OBS]` the box of
            // a raster's non-transparent pixels is not computed, so a raster
            // answers with `placeRaster`'s own placement of its pixel dimensions.
            if (isGlass && wantsEffectsRect && !groupEffectsRect) {
                const PlacementRect r =
                    svg ? artContentRect(*svg, lp, options.size)
                        : rasterPlacementRect(rasterW, rasterH, lp, options.size);
                if (r.width > 0.0 && r.height > 0.0) {
                    glassBox[0] = glassBoxAny ? std::min(glassBox[0], r.x) : r.x;
                    glassBox[1] = glassBoxAny ? std::min(glassBox[1], r.y) : r.y;
                    glassBox[2] =
                        glassBoxAny ? std::max(glassBox[2], r.x + r.width) : r.x + r.width;
                    glassBox[3] =
                        glassBoxAny ? std::max(glassBox[3], r.y + r.height) : r.y + r.height;
                    glassBoxAny = true;
                }
            }
            ++out.drawn;
        }

        // ---- the field of a group lit as ONE shape --------------------------
        //
        // The union is complete only now, so its field is taken here: the
        // exact Euclidean transform of the `alpha >= 0.5` contour of the joined
        // silhouettes, through the same door a raster's field takes.
        if (combinedField && silhouetteParts > 0) {
            SurfaceArt united;
            united.rgba.assign(grid.texels() * 4, 0.0f);
            for (std::size_t t = 0; t < silhouette.size(); ++t) {
                united.rgba[t * 4 + 3] = silhouette[t];
            }
            silhouette = std::vector<float>();
            if (silhouetteKeyed) {
                united.key = silhouetteKey.finish();
                united.keyed = true;
            }
            FieldOptions fo;
            fo.originX = grid.originX;
            fo.originY = grid.originY;
            auto made = surface.alphaField(options.cache, united, grid.width, grid.height, fo);
            if (!made) return std::unexpected(made.error());
            if (made->empty()) {
                // The union never reaches half coverage anywhere -- hairlines,
                // art smaller than a texel. No contour, so no field, and it is
                // said the way a single element's missing field is.
                if (wantsSpecular) note(out.notes, specularDoesNotDrawNote());
                note(out.notes, "vidro: a silhueta unida dos elementos deste grupo nao chega a "
                                "alpha >= 0.5 em nenhum texel, entao nao ha contorno para "
                                "assinar (grupo " + std::to_string(gi) + ")");
            } else {
                groupField = std::move(*made);
                note(out.notes, kCombinedFieldNote);
                if (silhouetteLeftOut) note(out.notes, kCombinedRasterNote);
            }
        }

        // ==== THE GROUP, ON THE PICTURE ===================================
        //
        // `[BIN]` The order is `0x48B74`'s, which runs once per group and puts
        // everything on the icon's own list: the glass pass `0x4A2D4` at
        // `0x48BD4` -- the refraction of what is underneath, and the shadow --,
        // the content `0x4AC84` at `0x48C08` -- the translucency clip, the
        // group's image under its opacity and blend, the shadow's overdraw --,
        // the glow (`0x48C4C`-`0x48DA8`), and the highlights `0x491C0` at
        // `0x48DB4`, on the path every branch merges into (`0x48DAC`). The loop
        // at `0x48E20` that an earlier note here took for a per-element one
        // strides `0xC0`, the size of a `FinalizedIcon.Layer`: it walks GROUPS.
        if (elements > 0) {
            auto takenImage = surface.takeGroupImage(groupImage);
            if (!takenImage) return std::unexpected(takenImage.error());
            SurfaceArt& image = *takenImage->image;
            // What the image is still to be drawn with: the GROUP's opacity and,
            // in a group of one -- whose image is the element's own art -- the
            // element's. In a group of several the elements' are already inside
            // the image.
            const double imageOpacity = takenImage->opacity * groupOpacity;

            // ---- the refraction, before the group's own art -------------
            //
            // THE ORDERING, AND IT IS A DECISION. `[INF]` The accumulator as it
            // stands IS the backdrop -- that is exactly what the target hands
            // its glass as a texture (spec §4.3: `glassBackground_v1` receives
            // the backdrop wrapped in an `RB::MultiLevelLayer`, and in an
            // isolated icon the only possible content of that texture is the
            // document's own stack so far). So: refract what is underneath
            // through the group's shape, THEN draw the group's art over the
            // result.
            //
            // THE ALTERNATIVE, which is not what this does: the shape refracts
            // and the art is NOT painted -- the glass contributing only a lens.
            // Both readings survive what was measured. `[OBS]` Spec §4.3 records
            // the question as open, and nothing read settles it.
            //
            // Why this one. `Icon.Element.participatesInGlass` is a
            // PARTICIPATION flag on an element that still carries `contents`
            // and `fill` -- a lens-only element would not need either. And
            // `[ART]` 138 of the corpus's 171 glass layers carry their own
            // `fill`, which under the lens-only reading would be 138 authored
            // values that nothing consumes. Painting the art is the reading
            // that leaves no dead data.
            //
            // `[BIN]` ONE refraction per group, through the group's field
            // (`0x4A5E4`-`0x4A7AC`): until 2026-10-01 each glass layer refracted
            // on its own, so a second one bent the first one's art.
            if (groupField && !glassRefractionIsIdentity(refraction)) {
                if (auto ok = surface.refract(*groupField, refraction); !ok) {
                    return std::unexpected(ok.error());
                }
                note(out.notes, glassRulerNote(options.size));
                ++out.glassRefracted;
            }

            // ---- THE SHADOW, UNDER THE ART ------------------------------
            //
            // THE THIRD FACTOR IS THE GROUP'S OPACITY, and an element's own
            // reaches the shadow by another door. `[BIN]` The third factor of
            // the shadow's alpha is `FinalizedIcon.Layer.opacity` (`GlassShadow.h`
            // names the three readings), which is the GROUP's. The element's
            // `opacity` is inside the shadow's SOURCE instead: the finaliser
            // draws that list with the element's real opacity (`0x1C0DC`,
            // `w5 = 0`). Everything between the source and the composite is
            // linear in the source's alpha, so where the source is one
            // element's own art -- unscaled -- the element's opacity is
            // multiplied in here.
            //
            // THE OVERDRAW PASS IS A SECOND COMPOSITE OF THE SAME IMAGE, so the
            // image is kept between the two draws instead of being rebuilt: the
            // blur behind it is the most expensive thing this loop does.
            // `GlassShadow.h` carries the addresses for all of it.
            std::optional<SurfaceShadow> shadow;
            float shadowCompositeAlpha = 0.0f;
            if (wantsShadow && shadowElements > 0) {
                SurfaceGroupTaken source = *takenImage;
                if (separateShadowSource) {
                    auto takenSource = surface.takeGroupImage(shadowSource);
                    if (!takenSource) return std::unexpected(takenSource.error());
                    source = std::move(*takenSource);
                }
                const ShadowInputs shadowIn{shadowStyle, glassNumbers.shadowOpacity,
                                            source.opacity * groupOpacity, sizeClass};
                if (shadowDraws(shadowIn, shadowParams)) {
                    // Na GPU a sombra e feita da fonte residente, sem descer nada.
                    // BEFORE the mask below touches the image: the source is
                    // read as it is now.
                    auto made = surface.makeShadow(options.cache, *source.image, shadowIn.style,
                                                   shadowGeom);
                    if (!made) return std::unexpected(made.error());
                    shadowCompositeAlpha =
                        static_cast<float>(shadowAlpha(shadowIn, shadowParams));
                    // `[BIN]` THE BLEND BYTE (`0x49F94`, `0x49FF4`-`0x4A008`):
                    // `Shadow.blendMode`, except for a VIBRANT shadow in an icon
                    // whose `iconBrightness` (`ctx+0x463`) is `dim`, which takes
                    // `blendModeForVibrantOnDim`. Until 2026-10-01 the byte was
                    // carried as unnamed and the second operand was always false.
                    const BlendMode shadowMode = shadowBlendMode(
                        shadowIn.style, iconBrightness == ChicletAppearance::Dim, shadowParams);
                    if (auto ok = surface.blendShadow(*made, false, shadowCompositeAlpha,
                                                      shadowMode);
                        !ok) {
                        return std::unexpected(ok.error());
                    }
                    shadow = std::move(*made);
                    if (shadowGeom.ringWidth) note(out.notes, kShadowRingNote);
                    if (shadowSourceDiffers) note(out.notes, kShadowSourceNote);
                    ++out.glassShadowed;
                }
            }

            // ---- THE TRANSLUCENCY, ON THE GROUP'S IMAGE -------------------
            //
            // `[BIN]` The content pass opens with it: when the material's
            // `translucency` is positive and the SDF is not empty, `0x4ACB4`-
            // `0x4AD88` does `beginLayer`, draws the mask (`0xFD28`, from the
            // SDF and the `effectsFrame`) and closes with
            // `clipLayerWithAlpha:1 mode:0` -- a CLIP, under which the image is
            // then drawn. So the mask multiplies the alpha of the whole group
            // image, before the group's opacity and blend meet it, and the
            // `alpha` of the mask's own draw is the constant 1.0 (`0x103F4`).
            // Until 2026-10-01 it multiplied each glass layer's art apart, with
            // that layer's own field.
            if (groupWantsMask && groupField) {
                // The `bounds` of the shader's rect argument -- the rect the
                // vertical ramp is measured in. `[BIN]` It is the box of what
                // the group's glass elements DRAW, or the whole canvas when
                // that box is empty: a nil `effectsFrame`, for which the callers
                // of the mask pass `(0, 0, 1, 1)`.
                OpacityMaskArguments args = groupMaskArgs;
                const double whole = static_cast<double>(options.size);
                const PlacementRect r =
                    groupEffectsRect
                        ? *groupEffectsRect
                        : glassBoxAny ? PlacementRect{glassBox[0], glassBox[1],
                                                      glassBox[2] - glassBox[0],
                                                      glassBox[3] - glassBox[1]}
                                      : PlacementRect{0.0, 0.0, whole, whole};
                if (args.kind == OpacityMaskKind::Gradient) {
                    // `[BIN]` THE GRADIENT'S RECT IS THE FRAME ON THE CANVAS
                    // (`0x0000FE04`-`0x0000FE70`): each side of the normalised
                    // frame times the canvas size, with no texture in it -- so
                    // none of the growing below.
                    args.bounds[0] = static_cast<float>(r.x);
                    args.bounds[1] = static_cast<float>(r.y);
                    args.bounds[2] = static_cast<float>(r.width);
                    args.bounds[3] = static_cast<float>(r.height);
                } else {
                    // `[BIN]` The normalised frame is multiplied by the size of
                    // the SDF TEXTURE, and that texture is the canvas plus one
                    // pixel on every side (`adds x8, x28, #2` at `0x1D210`, the
                    // content shifted by `translateByX:1 Y:1` at `0x1D7E8`). So
                    // in the texture's own pixels the rect is `frame * (size +
                    // 2)`, and the canvas pixel `y` is texel `y + 1` -- written
                    // here back in canvas pixels.
                    const double grow = (static_cast<double>(options.size) + 2.0) /
                                        static_cast<double>(options.size);
                    args.bounds[0] = static_cast<float>(r.x * grow - 1.0);
                    args.bounds[1] = static_cast<float>(r.y * grow - 1.0);
                    args.bounds[2] = static_cast<float>(r.width * grow);
                    args.bounds[3] = static_cast<float>(r.height * grow);
                }
                auto mask = surface.opacityMask(*groupField, args);
                if (!mask) return std::unexpected(mask.error());
                note(out.notes, kTranslucencyBoundsNote);

                // THE BLIND SPOT, MEASURED BEFORE THE MASK IS APPLIED. Where
                // the field says "outside", the shader's own `mix(1.0, a, cov)`
                // returns 1.0 and the pixel keeps its opacity -- correct for a
                // pixel that really is outside, and a silent miss for one the
                // art painted anyway. Counted against the image's alpha so the
                // sentence carries a number instead of a worry.
                //
                // ONLY FOR A GROUP OF ONE ELEMENT, where image and field are the
                // same art and a painted pixel outside the field can only be a
                // miss of the generator. In a group of several it is the
                // picture: a plain element beside a glass one is outside the
                // field by construction, and where two glass elements stack
                // the upper one's field says "outside" over a rim of the lower
                // one's art (`kFieldStackNote`). Counting those would bury the
                // sentence that means something.
                const bool reportsHoles = elements == 1;
                const std::size_t gapAt = out.shapeGaps.size();
                out.shapeGaps.push_back(kPendingEntry);
                std::vector<std::string>* gaps = &out.shapeGaps;
                const std::string prefix = soleLabel + ": ";
                // Why a painted pixel can fall outside the field depends on
                // which generator made the field, so the sentence does too.
                const char* const why =
                    soleIsVector
                        ? "flattenSvgToContours funde todo subcaminho pintado num conjunto "
                          "so, assinado por uma regra so, entao subcaminhos sobrepostos de "
                          "orientacao contraria se cancelam sob non-zero (a ressalva de "
                          "DistanceField.h). O mesmo buraco vale para a refracao."
                        : "num raster o campo e assinado pelo contorno alpha >= 0.5, entao "
                          "todo pixel pintado com alpha ABAIXO do limiar fica de fora.";
                auto counted = surface.applyMask(
                    image, *mask,
                    [gaps, gapAt, prefix, why, reportsHoles](std::size_t missed,
                                                             std::size_t painted) {
                        if (!reportsHoles || !(missed > 0 && painted > 0)) {
                            (*gaps)[gapAt] = kDroppedEntry;
                            return;
                        }
                        char buf[512];
                        std::snprintf(
                            buf, sizeof(buf),
                            "translucidez aplicada com buraco: %zu de %zu pixels pintados "
                            "(%.1f%%) caem FORA do campo de distancia e ficaram opacos -- %s",
                            missed, painted,
                            100.0 * static_cast<double>(missed) /
                                static_cast<double>(painted),
                            why);
                        (*gaps)[gapAt] = prefix + buf;
                    });
                if (!counted) return std::unexpected(counted.error());
                ++out.glassTranslucent;
            }

            // ---- THE CONTENT: the image, under the group's opacity and blend
            //
            // `[BIN]` `0x4AF20` -> `0x4B4EC`: `drawShape:(contentFrame)
            // fill:(image) alpha:(float)[+0x38] blendMode:[+0x31]`
            // (`0x4B518`-`0x4BA6C`). The group's opacity and the group's blend
            // apply HERE, against everything already on the icon -- the
            // background, the groups beneath, and this group's own shadow.
            // Na mascara do Clear a imagem passa antes pela matriz do conteudo
            // (`0x4AF20`): `clearContent`.
            //
            // `[BIN]` AND THIS IS THE ONE DRAW `shouldClampPlusLBlending` REACHES
            // (`0x4B530`-`0x4B57C`, and the same lines inline at `0x44614`): when
            // the blend byte is 8, plus-lighter, and the flag (`ctx+0x288`) is
            // set, AND the byte at `ctx+0x528` is 1, the composite is handed to
            // the `clampedPlusL` blend shader instead. The shadow (`0x49ED4`),
            // the glow (`0xF534`) and the highlights (`0x491C0`, `0x475A0`) call
            // `drawShape:...blendMode:` directly and are never clamped.
            // `[OBS]` The second gate is not read -- `BlendFormula.h` says what
            // was found of it --, so it is an option of the render, off unless
            // the caller says otherwise, and the note names it when it bites.
            const bool clampPlusLighter =
                params.shouldClampPlusLBlending && options.drawingContextClampsPlusLighter;
            if (groupBlendMode == BlendMode::PlusLighter && params.shouldClampPlusLBlending &&
                !options.drawingContextClampsPlusLighter) {
                note(out.notes, kPlusLighterClampNote);
            }
            if (auto ok = surface.blendArt(image, static_cast<float>(imageOpacity),
                                           groupBlendMode, clearMask, clampPlusLighter);
                !ok) {
                return std::unexpected(ok.error());
            }

            // ---- the shadow again, OVER the content -----------------------
            //
            // `[BIN]` AFTER THE CONTENT AND BEFORE THE HIGHLIGHTS: the body of
            // `0x4AC84` is the content draw (`0x4ADA4`) followed by this pass
            // (`0x4ADBC`-`0x4AEB4`), still under the translucency clip.
            //
            // `[BIN]` ONLY FOR A GROUP THAT BLENDS NORMALLY, AND ONLY WHEN THE
            // GENERATION DRAWS OVER THE CONTENT. `0x4ADBC`-`0x4ADCC` is one
            // compare chained into another: `Shadow.drawOverContent`
            // (`ctx+0x45B1`) must be 1 AND the byte at `[descriptor+0x31]` -- the
            // group's blend mode (`GlassShadow.h`) -- must be zero. Generation 26
            // clears the flag (`0x77ED0`), so it has no overdraw at all. Until
            // 2026-10-01 the pass ran over every group.
            //
            // `[BIN]` AND ITS CLIP IS THE CONTENT DRAW ITSELF -- `0x4B4EC` again,
            // inside the clip layer -- so it carries what that draw carries: the
            // image's alpha after the mask, and the opacity it is drawn with.
            if (shadow && shadowParams.drawOverContent && groupBlendMode == BlendMode::Normal) {
                const double clip =
                    shadowOverdrawAlpha(groupTranslucency, shadowStyle, sizeClass, shadowParams) *
                    imageOpacity;
                if (auto ok = surface.clipShadowOverdraw(*shadow, image, clip); !ok) {
                    return std::unexpected(ok.error());
                }
                if (shadow->hasOverdraw) {
                    if (auto ok = surface.blendShadow(*shadow, true, shadowCompositeAlpha,
                                                      shadowParams.overdrawBlendMode);
                        !ok) {
                        return std::unexpected(ok.error());
                    }
                    note(out.notes, kShadowOverdrawNote);
                    ++out.glassShadowOverdrawn;
                }
            }

            // ---- THE GLOW, which only generation 26 has ---------------------
            //
            // `[BIN]` `0x48C20`-`0x48DA8`: after the content pass has returned
            // -- the image and the shadow's overdraw -- and before the
            // highlights, and outside the translucency clip. Gated on the
            // Optional (`ctx+0x4500`; nil in generation 27), on the group's SDF
            // not being empty and on its `effectsFrame` not being nil; clipped
            // to that frame outset by one pixel; then `0xF534`: the `glow`
            // shader over the group's field, white, at `innerOpacity x group
            // opacity`, plus-lighter. `GlassGlow.h` carries the fragment and
            // why the depth it sees saturates at the field's reach.
            //
            // `[OBS]` A glass group with no specular has a field with no reach,
            // and what the target draws there was not read: nothing is drawn,
            // and the note says so.
            if (params.glow.has_value() && glassLayers > 0 && !wantsGlow) {
                note(out.notes, kGlowNoReachNote);
            }
            if (wantsGlow && groupField) {
                const bool frameIsNil = groupEffectsRect ? groupEffectsNil : !glassBoxAny;
                if (!frameIsNil) {
                    const PlacementRect r =
                        groupEffectsRect ? *groupEffectsRect
                                         : PlacementRect{glassBox[0], glassBox[1],
                                                         glassBox[2] - glassBox[0],
                                                         glassBox[3] - glassBox[1]};
                    const double frame[4] = {r.x, r.y, r.width, r.height};
                    const GlowArguments glowArgs = glowArguments(
                        *params.glow, groupOpacity,
                        static_cast<double>(options.size) / kCanvasPoints,
                        static_cast<double>(fieldReach), frame);
                    if (glowDraws(glowArgs)) {
                        if (auto ok = surface.glow(*groupField, glowArgs); !ok) {
                            return std::unexpected(ok.error());
                        }
                        note(out.notes, kGlowNote);
                        ++out.glassGlowed;
                    } else if (!(fieldReach > 0.0f)) {
                        note(out.notes, kGlowNoReachNote);
                    }
                }
            }

            // ---- THE HIGHLIGHTS, over the group ---------------------------
            //
            // THEY FILTER THE BACKDROP, SO THEY GO ON AFTER THE GROUP IS IN IT.
            // `[BIN]` `beginLayerWithFlags:1` sets bit 0 of
            // `RB::DisplayList::Layer::Flag`, and `Builder::null_style_draw`
            // (`0x000CE028`) turns that layer's colour matrix into a
            // `BackdropFilterItem` appended to the PARENT -- so the matrix
            // reads the composite, not the group's own image (GlassSpecular.cpp
            // has the addresses). The pass is gated by `hasSpecular` at
            // `0x00049200` and is NOT under the translucency clip.
            //
            // `[BIN]` ONE PASS PER GROUP, over the group's field: the only loop
            // in `0x491C0` is over the highlight slots. And `layerOpacity` is
            // the GROUP's opacity -- `[descriptor+0x38]` (`0x000495F0`) is
            // `FinalizedIcon.Layer.opacity`; an element's own never reaches it.
            if (wantsSpecular && groupField) {
                SpecularArguments groupSpecular = specularArgs;
                groupSpecular.layerOpacity = groupOpacity;
                std::size_t* specularCount = &out.glassSpecular;
                if (auto ok = surface.specular(*groupField, groupSpecular,
                                               [specularCount](std::size_t moved) {
                                                   if (moved > 0) ++*specularCount;
                                               });
                    !ok) {
                    return std::unexpected(ok.error());
                }
                note(out.notes, specularDrawnNote(options.generation));
            }
        }

        // THE GROUP'S `blur-material` IS NOT DRAWN.
        //
        // `[BIN]` WHERE IT WOULD GO was misread until 2026-10-01, here and in
        // `BlurKernel.h`. `0x4A488` is `bl 0x49ED4` with `w1 = 0`, and `0x49ED4`
        // is the SHADOW draw -- it loads `shadowStyle`, `shadowOpacity`,
        // `[+0x38]` and `shadowImage` at `0x49F08`-`0x49F14` -- not the group's
        // content, which is `0x4AC84` and only runs after `0x4A2D4` has returned
        // (`0x48C08`). So the `needs-background` layer of `0x4A48C`-`0x4A5D0`
        // blurs everything beneath the group PLUS the group's shadow, and NOT the
        // group's own art; and `0x4A594` (`bl 0x1135C`) clips it to the inside
        // of the group's SDF. In this loop that is between the shadow and the
        // translucency above.
        //
        // IT IS NOT MADE, because the gabarito refused the only reading of the
        // layer frame this front had -- and that measurement was taken with the
        // old placement and the old clip, so it does not speak for the reading
        // above. `drawBlurMaterial(acc, ..., blurSurface)` with the frame at the
        // unit rect was rendered and measured: the mean channel error against
        // `apple-512.png` goes from `8.95 / 10.03 / 9.89` to
        // `15.58 / 20.09 / 22.49` and the alpha error from `4.95` to `7.09`,
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
        if (blurSurface.draws) note(out.notes, kBlurMaterialFrameNote);
    }

    // ==== THE CHICLET'S HIGHLIGHTS, OVER THE CONTENT ========================
    //
    // `[BIN]` THE ORDER OF THE ROOT PASS IS content, chiclet highlights,
    // outline -- in BOTH generations. `IconRenderer.draw` (`0x42FBC`) branches
    // on `useOS26Compositing` (`ctx+0x3CA`, `0x43140`) into two bodies:
    //
    //   generation 26 (`0x43150`): headroom and colour clamp, `beginLayer`, the
    //     content (`0x469E8`, which reaches `0x435A0`) -- inside a saturation and
    //     duotone layer when the mode is tinted and the appearance dark
    //     (`0x43210`-`0x43334`) --, then `bl 0x475A0` at `0x43338`, then
    //     `bl 0x47AE8` at `0x4333C`, then `drawLayer`.
    //   generation 27 (`0x431B0` -> `0x46FC4` -> `0x47D2C`): the clamp or the
    //     Clear's total matrix, `beginLayer`, `bl 0x435A0` at `0x48268`, then
    //     `bl 0x475A0` at `0x48274` (inside the tint layer instead, at
    //     `0x483E0`, when `darkTintHighlightsBlendWithContent`), `drawLayer`;
    //     and back in `0x431B0`, `bl 0x47AE8` at `0x431C0`.
    //
    // `0x435A0` is the content: it opens with the background (`0x48524`) and
    // runs the groups (`0x48E20`, `0x48B74`); `0x475A0` is the chiclet's
    // highlights, and it is called from nowhere else. So the highlights go on
    // AFTER every group, over whatever the groups drew along the pastille's
    // rim. Until 2026-10-01 this renderer drew them straight after the
    // background, UNDER the groups -- the place the front that added them chose
    // (`Docs/Laudos/2026-09-15-chiclet-realces.md` §5: "logo depois de
    // `clipToChiclet`"), and no reading of the order stood behind it.
    //
    // `[INF]` THE CLIP IS THE ALPHA THE PICTURE HAS HERE (`drawChicletHighlights`).
    // The target clips the highlights to a layer drawn from the chiclet's own
    // shape (`0xD904`, step 3 of `ChicletHighlights.h`), whatever is under it;
    // while the pass ran before the groups the background's alpha WAS that
    // coverage. It still is wherever the background is opaque -- the picture's
    // alpha is then the coverage along the edge and one inside --, and where art
    // overhangs the pastille's edge the alpha it adds inside the one-pixel
    // antialiased band counts too.
    //
    // `[BIN]` THE OUTLINE (`0x47AE8`), third in both bodies, IS NOT DRAWN, and
    // in an ordinary render the target does not draw it either. Its first gate
    // is the byte at `ctx+0x22` (`0x47B48`: `cmp w8, #1; b.ne` past the whole
    // pass) -- `[INF]` the mitigated export's flag: the same byte sends the
    // content wrapper `0x469E8` down the branch that paints the mitigated
    // chiclet (`0x46A28`; `Docs/Laudos/2026-09-30-tinted-export-localizacao.md`)
    // -- followed by the effects byte, a rendering step and the `.color` mode
    // (`0x47B54`-`0x47BB8`). This renderer has no mitigated export.
    // `Outlines.useDynamicOpacity` -- `true` in 27, `false` in 26 (`0x77F54`) --
    // is carried in the block for whoever writes one.
    //
    // `[BIN]` AND WHERE THE TINTED-DARK LAYER CLOSES DEPENDS ON THE GENERATION.
    // The saturation filter and the duotone wrap a layer the content is drawn
    // in. Generation 27 (`0x47D2C`) reads `darkTintHighlightsBlendWithContent`
    // (`ctx+0xB0`, `0x483D4`): set -- its default -- the chiclet's highlights
    // are drawn INSIDE that layer (`0x483E0`) and are recoloured with the
    // content; clear, the layer is closed first (`0x48400`-`0x48418`).
    // Generation 26's body (`0x43150`) has no such test: the layer is closed
    // at `0x4332C` and `0x475A0` is called after it, at `0x43338` -- the
    // highlights are never tinted.
    //
    // So when the highlights are inside, the recolouring commutes with
    // everything this function draws and is left to whoever finishes the
    // rendition, over the finished picture (`applyTintedDark`), as it always
    // was. When they are outside it has to happen HERE, between the content and
    // the highlights: the same arithmetic over the premultiplied target, which
    // is linear with no offset. `RenderedIcon::tintApplied` says it was done.
    const bool tintedDark = options.tint.has_value() && !options.clearMask;
    const bool highlightsInsideTint =
        !params.useOS26Compositing && params.darkTintHighlightsBlendWithContent;
    if (tintedDark && !highlightsInsideTint) {
        if (auto ok = surface.tint(*options.tint); !ok) return std::unexpected(ok.error());
        out.tintApplied = true;
    }
    if (chicletPass) {
        // A nota depende de quantos pixels mudaram: o lugar dela fica guardado
        // ate a contagem chegar (na GPU, no `finish`).
        const std::size_t noteAt = out.notes.size();
        out.notes.push_back(kPendingEntry);
        std::vector<std::string>* notes = &out.notes;
        const DesignGeneration generation = options.generation;
        const ChicletPass pass = *chicletPass;
        if (auto ok = surface.chicletHighlights(
                options.cache, pass.args, platform,
                [notes, noteAt, generation, pass](std::size_t highlighted) {
                    (*notes)[noteAt] =
                        highlighted > 0
                            ? chicletHighlightsNote(generation, pass.set, pass.appearance,
                                                    pass.lum)
                            : std::string(kDroppedEntry);
                });
            !ok) {
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

void tintDark(float* p, std::size_t n, const IconRenderOptions::TintRecolour& tint) {
    const float s = static_cast<float>(std::max(0.0, tint.saturation));
    const float tr = static_cast<float>(tint.r), tg = static_cast<float>(tint.g),
                tb = static_cast<float>(tint.b);
    for (std::size_t i = 0; i < n; ++i, p += 4) {
        const float l = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
        p[0] = (l + s * (p[0] - l)) * tr;
        p[1] = (l + s * (p[1] - l)) * tg;
        p[2] = (l + s * (p[2] - l)) * tb;
    }
}

void applyTintedDark(RenderedIcon& icon, const IconRenderOptions::TintRecolour& tint) {
    tintDark(icon.rgba.data(), icon.rgba.size() / 4, tint);
}

// AS MATRIZES DO MONO, num lugar so: a CPU (`simulatedGlass`, `applyClear`) e
// o shader da tela (Source/app/shaders/stage.frag) leem os mesmos numeros.
//
// `[BIN]` O vidro: VCM claro [0.12, 1.0, 1.2] ou escuro [0.05, 0.3, 0.8]
// (SimulatedGlass.h). O ClearMode (laudo de 20/09 §2): total L/D/H 1.0 / 0.3 /
// 1.0; lighteningVCM [0.9, 2.5, 2.0], highlightsVCM [0.2, 1.35, 1.4], os dois
// com headroom 1.2. `inputClamp = headroom`: cada canal do VCM preso em
// [-0.75, h^(1/2.2)] (AquaKit VibrantColor.h); a tela prende em [0, 1] no fim.
MonoColourMatrices monoColourMatrices() {
    MonoColourMatrices m{};
    const double light[3] = {0.12, 1.0, 1.2}, dark[3] = {0.05, 0.3, 0.8};
    const double lighten[3] = {0.9, 2.5, 2.0}, highlight[3] = {0.2, 1.35, 1.4};
    for (int i = 0; i < 3; ++i) {
        m.glassLight[i] = light[i];
        m.glassDark[i] = dark[i];
        m.clearLighten[i] = lighten[i];
        m.clearHighlight[i] = highlight[i];
    }
    m.clearDarkening = 0.3;
    m.vcmMin = -0.75;
    m.vcmMax = std::pow(1.2, 1.0 / 2.2);
    return m;
}

void applyClear(RenderedIcon& icon, const ClearBackdrop& backdrop, double squareX,
                double squareY, double squareSide, std::uint32_t canvasSize,
                const SimulatedGlass* glass) {
    const MonoColourMatrices m = monoColourMatrices();
    constexpr double kTotalLightening = 1.0, kTotalHighlights = 1.0;
    const double kTotalDarkening = m.clearDarkening;
    const double kVcmMax = m.vcmMax;
    GlyphVCM lighten;
    lighten.lumaFloor = m.clearLighten[0];
    lighten.lumaCeiling = m.clearLighten[1];
    lighten.saturation = m.clearLighten[2];
    GlyphVCM highlight;
    highlight.lumaFloor = m.clearHighlight[0];
    highlight.lumaCeiling = m.clearHighlight[1];
    highlight.saturation = m.clearHighlight[2];
    if (backdrop.width == 0 || backdrop.height == 0 || canvasSize == 0) return;
    parallelRanges(icon.height, static_cast<std::size_t>(icon.width) * icon.height * 80,
                   [&](std::size_t y0, std::size_t y1) {
    for (std::uint32_t y = static_cast<std::uint32_t>(y0); y < y1; ++y) {
        float* p = icon.rgba.data() + static_cast<std::size_t>(y) * icon.width * 4;
        for (std::uint32_t x = 0; x < icon.width; ++x, p += 4) {
            const double u = (icon.originX + x + 0.5) / canvasSize;
            const double v = (icon.originY + y + 0.5) / canvasSize;
            double o[3], raw[3], gc = 0.0;
            backdropWithGlass(backdrop, glass, squareX, squareY, squareSide, u, v, o, gc, raw);
            const double A = std::clamp(static_cast<double>(p[3]), 0.0, 1.0);
            if (A > 0.0) {
                // A matriz total, na cor reta, presa em 0..1 (`addStyle:9`).
                const double mr = std::clamp(kTotalLightening * p[0], 0.0, 1.0);
                const double mg = std::clamp(kTotalDarkening * (1.0 - p[1]), 0.0, 1.0);
                const double mb = std::clamp(kTotalHighlights * p[2], 0.0, 1.0);
                auto vibrant = [&](const GlyphVCM& vcm, double a) {
                    if (a <= 0.0) return;
                    double c[3] = {o[0], o[1], o[2]};
                    applyGlyphVCM(vcm, c);
                    for (int k = 0; k < 3; ++k) o[k] += (std::clamp(c[k], -0.75, kVcmMax) - o[k]) * a;
                };
                vibrant(lighten, mr * A);                                          // L
                for (int k = 0; k < 3; ++k) o[k] = std::max(0.0, o[k] - mg * A);   // D, plusD
                vibrant(highlight, mb * A);                                        // H
            }
            // Cor reta sobre o fundo CRU: compor devolve `o`.
            const double alpha = std::max(A, gc);
            if (alpha <= 0.0) {
                p[0] = p[1] = p[2] = p[3] = 0.0f;
                continue;
            }
            for (int k = 0; k < 3; ++k) {
                const double oc = std::clamp(o[k], 0.0, 1.0);
                p[k] = static_cast<float>(std::clamp((oc - raw[k] * (1.0 - alpha)) / alpha, 0.0, 1.0));
            }
            p[3] = static_cast<float>(alpha);
        }
    }
    });
}

void applyOverGlass(RenderedIcon& icon, const ClearBackdrop& backdrop, double squareX,
                    double squareY, double squareSide, std::uint32_t canvasSize,
                    const SimulatedGlass& glass) {
    if (backdrop.width == 0 || canvasSize == 0) return;
    parallelRanges(icon.height, static_cast<std::size_t>(icon.width) * icon.height * 80,
                   [&](std::size_t y0, std::size_t y1) {
    for (std::uint32_t y = static_cast<std::uint32_t>(y0); y < y1; ++y) {
        float* p = icon.rgba.data() + static_cast<std::size_t>(y) * icon.width * 4;
        for (std::uint32_t x = 0; x < icon.width; ++x, p += 4) {
            const double u = (icon.originX + x + 0.5) / canvasSize;
            const double v = (icon.originY + y + 0.5) / canvasSize;
            double b[3], raw[3], gc = 0.0;
            backdropWithGlass(backdrop, &glass, squareX, squareY, squareSide, u, v, b, gc, raw);
            const double A = std::clamp(static_cast<double>(p[3]), 0.0, 1.0);
            const double alpha = std::max(A, gc);
            if (alpha <= 0.0) continue;
            for (int k = 0; k < 3; ++k) {
                const double o = p[k] * A + b[k] * (1.0 - A);   // o icone sobre o vidro
                p[k] = static_cast<float>(std::clamp((o - raw[k] * (1.0 - alpha)) / alpha, 0.0, 1.0));
            }
            p[3] = static_cast<float>(alpha);
        }
    }
    });
}

}  // namespace rb
