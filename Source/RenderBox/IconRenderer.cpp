#include "Source/RenderBox/IconRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "Source/CoreSVG/Document.h"
#include "Source/RenderBox/AutomaticGradient.h"
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


// The layer's own fill, turned into what the renderer paints with.
//
// `[INF]` The art gives the shape and the fill gives the colour -- see the note
// on `FillOverride`. `why` is filled when a fill kind is recognised but cannot
// be honoured, so the layer is NAMED rather than drawn in the art's colours,
// which would look like a render and be a different picture.
FillOverride overrideFor(const icf::Fill& fill, const LayerPlacement& placement,
                         std::uint32_t size, std::string& why) {
    FillOverride out;
    auto asColour = [](const icf::Color& c, float (&rgba)[4]) {
        const bool grey = c.count < 3;
        rgba[0] = static_cast<float>(c.components[0]);
        rgba[1] = static_cast<float>(grey ? c.components[0] : c.components[1]);
        rgba[2] = static_cast<float>(grey ? c.components[0] : c.components[2]);
        rgba[3] = static_cast<float>(grey ? c.components[1] : c.components[3]);
    };

    switch (fill.kind) {
        case icf::FillKind::None:
            return out;

        case icf::FillKind::Solid:
            if (fill.colors.empty()) return out;
            out.kind = FillOverride::Kind::Solid;
            asColour(fill.colors.front(), out.colour);
            return out;

        case icf::FillKind::LinearGradient: {
            // `[ART]` The document's linear gradient carries EXACTLY two stops --
            // 48 of 48 in the corpus -- plus an `orientation` whose start and
            // stop are points in the unit square. Which is the target's ramp
            // kind 0, the two-colour mix (doc 03 §23.4).
            if (fill.colors.size() < 2 || !fill.orientation) {
                why = "linear-gradient sem duas cores ou sem orientacao";
                return out;
            }
            out.kind = FillOverride::Kind::Ramp;
            out.stops.resize(2);
            out.stops[0].location = 0.0f;
            out.stops[1].location = 1.0f;
            asColour(fill.colors[0], out.stops[0].rgba);
            asColour(fill.colors[1], out.stops[1].rgba);

            // The unit square is the CANVAS, and the layer's placement does not
            // move it: a layer's fill covers the layer, and the orientation is
            // given in the same fractions for every layer. `[OBS]` That the unit
            // square is the canvas rather than the layer's own box is NOT
            // measured -- the corpus cannot tell them apart while every
            // orientation runs corner to corner.
            (void)placement;
            const double sx = fill.orientation->stop.x - fill.orientation->start.x;
            const double sy = fill.orientation->stop.y - fill.orientation->start.y;
            const double len2 = sx * sx + sy * sy;
            if (len2 == 0.0) {
                why = "linear-gradient com orientacao de comprimento zero";
                out.kind = FillOverride::Kind::None;
                return out;
            }
            const double k = 1.0 / static_cast<double>(size);   // pixels -> unit square
            out.m[0] = sx / len2 * k;
            out.m[1] = sy / len2 * k;
            out.m[2] = -(fill.orientation->start.x * sx + fill.orientation->start.y * sy) / len2;
            return out;
        }

        case icf::FillKind::AutomaticGradient: {
            if (fill.colors.empty()) {
                why = "automatic-gradient sem cor base";
                return out;
            }
            // The STOPS are derivable -- doc 03 §24 read the rule. Where the
            // AXIS goes is not: that function returns stops, not a placement,
            // and §24.4 records the geometry as untraced. Deriving the colours
            // and then inventing an axis would put real colours in the wrong
            // places, so the layer is named instead.
            why = "automatic-gradient: as paradas sao derivaveis (doc 03 §24) mas o "
                  "EIXO nao foi medido -- inventa-lo poria cores certas em lugar errado";
            return out;
        }

        case icf::FillKind::Automatic:
        case icf::FillKind::SystemLight:
        case icf::FillKind::SystemDark:
            why = "fill de sistema: escolhe entre duas rampas enlatadas dos parametros "
                  "de render, cujos valores nao foram lidos (doc 03 §24.3)";
            return out;

        default:
            why = "fill que este renderizador nao le";
            return out;
    }
}

}  // namespace

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

    for (std::size_t gi = 0; gi < groups.size(); ++gi) {
        const icf::Group& group = groups[gi];
        const LayerPlacement gp = placementOf(group.resolve("position", options.context));

        for (const icf::Layer& layer : group.layers()) {
            ++out.total;
            const std::string name(layer.name());

            auto skip = [&](const std::string& why) {
                out.skipped.push_back({gi, name, why});
            };

            if (boolOr(layer.resolve("hidden", options.context), false)) {
                continue;  // hidden is an instruction, not a gap
            }
            if (boolOr(layer.resolve("glass", options.context), false)) {
                skip("camada de vidro -- o efeito nao foi transcrito");
                continue;
            }
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

            // The layer's own fill, if it has one. A fill this renderer knows
            // by name but cannot honour NAMES the layer -- drawing the art in
            // its own colours instead would be a different picture wearing the
            // look of a finished one.
            FillOverride paint;
            if (const icf::json::Value* f = layer.resolve("fill", options.context)) {
                if (auto parsed = icf::fillFrom(*f)) {
                    std::string why;
                    paint = overrideFor(*parsed, lp, options.size, why);
                    if (!why.empty()) {
                        skip(why);
                        continue;
                    }
                }
            }

            if (ext == ".svg") {
                auto svg = icf::svg::SvgDocument::parse(readAll(art));
                if (!svg) {
                    skip("SVG que este leitor nao abre: " + *imageName);
                    continue;
                }
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
