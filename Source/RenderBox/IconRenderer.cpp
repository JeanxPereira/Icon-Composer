#include "Source/RenderBox/IconRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>

#include "Source/CoreSVG/Document.h"
#include "Source/RenderBox/AutomaticGradient.h"
#include "Source/RenderBox/FillResolve.h"
#include "Source/RenderBox/GlassLayer.h"
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
    "o fundo e pintado sobre o quadrado inteiro do canvas: `[OBS]` a geometria do "
    "chiclet -- o raio de canto que o recortaria -- nao foi lida, entao nao ha forma a "
    "que cortar";

const char* const kRasterFillNote =
    "o fill do documento nao alcanca a arte raster desta camada: o override so chega ao "
    "caminho vetorial, e `[OBS]` se o alvo repinta um elemento raster do mesmo jeito que "
    "repinta um vetorial nao foi lido";

const char* const kBackgroundP3Note =
    "fundo com componentes display-p3 desenhado SEM conversao de espaco -- a matriz "
    "nunca foi medida do alvo, e desenha-los como sRGB os desloca em silencio";

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
                out.backgroundPainted = true;
                note(out.notes, kBackgroundShapeNote);
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

    for (std::size_t gi = 0; gi < groups.size(); ++gi) {
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

        for (const icf::Layer& layer : group.layers()) {
            ++out.total;
            const std::string name(layer.name());

            auto skip = [&](const std::string& why) {
                out.skipped.push_back({gi, name, why});
            };

            if (boolOr(layer.resolve("hidden", options.context), false)) {
                continue;  // hidden is an instruction, not a gap
            }
            const bool isGlass = boolOr(layer.resolve("glass", options.context), false);
            if (const icf::json::Value* bm = layer.resolve("blend-mode", options.context)) {
                if (const std::string* s = textOf(bm)) {
                    if (*s != "normal") {
                        skip("mescla '" + *s + "' -- so o caminho chapado esta transcrito");
                        continue;
                    }
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

            if (isGlass && !glassRefractionIsIdentity(refraction)) {
                // `[ART]` 45 of the corpus's 171 glass layers name `.png` art
                // and one names `.heic`. A raster has no path to flatten, so
                // there is no shape to build a field from. This is NOT the same
                // gap as "the glass is not transcribed" and it does not share
                // its sentence: the glass IS transcribed, and what is missing
                // is a field generator that reads a raster's alpha instead of a
                // contour.
                if (!svg) {
                    skip("vidro sobre arte raster: um raster nao tem contorno para achatar e "
                         "este projeto nao tem gerador de campo a partir do alfa (" +
                         *imageName + ")");
                    continue;
                }
                const GlassContours shape = flattenSvgToContours(
                    *svg, placeOnCanvas(svg->viewBox, lp, options.size), options.subdivisions);
                if (shape.mixedRules) {
                    skip("vidro: a arte mistura non-zero e even-odd e o campo assina com uma "
                         "regra so -- escolher uma inverteria o dentro/fora de parte da forma");
                    continue;
                }
                if (shape.contours.empty()) {
                    skip("vidro: a arte nao fecha nenhum contorno pintado (" + *imageName + ")");
                    continue;
                }
                FieldOptions fo;
                fo.rule = shape.rule;
                const FieldImage field =
                    generateField(shape.contours, options.size, options.size, fo);
                glassOver(acc, options.size, options.size,
                          glassDisplacementMap(field, refraction), refraction);
                note(out.notes, glassRulerNote(options.size));
                ++out.glassRefracted;
            }

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
                over(acc, drew->rgba, static_cast<float>(opacity));
                ++out.drawn;
            } else if (ext == ".png") {
                const icf::DecodedPng png = icf::readPng(art.string());
                if (!png.error.empty()) {
                    skip(*imageName + ": " + png.error);
                    continue;
                }
                const std::vector<float> placed = placeRaster(png, lp, options.size);
                over(acc, placed, static_cast<float>(opacity));
                ++out.drawn;
            } else {
                skip("arte com extensao que este leitor nao le: " + *imageName);
            }
        }
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
