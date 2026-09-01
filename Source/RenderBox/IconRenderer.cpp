#include "Source/RenderBox/IconRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "Source/CoreSVG/Document.h"
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

            if (ext == ".svg") {
                auto svg = icf::svg::SvgDocument::parse(readAll(art));
                if (!svg) {
                    skip("SVG que este leitor nao abre: " + *imageName);
                    continue;
                }
                RenderOptions ro;
                ro.width = ro.height = options.size;
                ro.subdivisions = options.subdivisions;
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
