#include "Source/RenderBox/GlassLayer.h"

#include <cmath>
#include <cstdio>

namespace rb {
namespace {

// The affine `PathGlobals` carries, spelled the way `PathVertex.glsl` spells it
// (`world = m0*p.x + m1*p.y + m2`) so the glass shape lands on exactly the
// pixels the coverage rasteriser fills for the same art.
void toWorld(const PathGlobals& g, double x, double y, float& ox, float& oy) {
    ox = static_cast<float>(g.m0[0] * x + g.m1[0] * y + g.m2[0]);
    oy = static_cast<float>(g.m0[1] * x + g.m1[1] * y + g.m2[1]);
}

// A point appended unless it repeats the one before it. Two identical points
// make a zero-length segment, and a zero-length segment is a division by zero
// waiting in the projection `FieldShape` does.
void push(std::vector<float>& xy, float x, float y) {
    const std::size_t n = xy.size();
    if (n >= 2 && xy[n - 2] == x && xy[n - 1] == y) return;
    xy.push_back(x);
    xy.push_back(y);
}

// The contour ends. The closing segment is implied by `FieldContour`, so a
// trailing point equal to the first is dropped rather than left to become a
// zero-length closing segment -- `M0 0 L1 0 L1 1 L0 0 Z` is common and would
// otherwise close onto itself.
void flush(std::vector<float>& xy, std::vector<FieldContour>& out) {
    if (xy.size() >= 4 && xy[xy.size() - 2] == xy[0] && xy[xy.size() - 1] == xy[1]) {
        xy.resize(xy.size() - 2);
    }
    // Fewer than three points bound no area. Keeping such a contour would add a
    // crease to the field where the art has nothing at all.
    if (xy.size() >= 6) out.push_back(FieldContour{xy});
    xy.clear();
}

}  // namespace

GlassContours flattenSvgToContours(const icf::svg::SvgDocument& doc, const PathGlobals& globals,
                                   int subdivisions) {
    GlassContours out;
    const int n = subdivisions > 0 ? subdivisions : 1;

    bool sawRule = false;
    for (const icf::svg::Shape& shape : doc.shapes) {
        // A shape painted with `none` covers nothing, so it is not part of the
        // glass's shape either. `PaintKind::None` is 148 of the corpus's paint
        // values and is "not painted", which the reader states is not an error.
        if (shape.fill.kind == icf::svg::PaintKind::None) {
            ++out.unpaintedShapes;
            continue;
        }

        const FieldRule rule = shape.fillRule == icf::svg::FillRule::EvenOdd ? FieldRule::EvenOdd
                                                                            : FieldRule::NonZero;
        if (!sawRule) {
            out.rule = rule;
            sawRule = true;
        } else if (rule != out.rule) {
            out.mixedRules = true;
        }

        std::vector<float> xy;
        double cx = 0.0, cy = 0.0;      // the pen, in the SVG's user space
        double sx = 0.0, sy = 0.0;      // where the current subpath started
        float px = 0.0f, py = 0.0f;

        for (const icf::svg::Segment& s : shape.path.segments) {
            switch (s.kind) {
                case icf::svg::SegmentKind::Move:
                    flush(xy, out.contours);
                    cx = sx = s.p[0].x;
                    cy = sy = s.p[0].y;
                    toWorld(globals, cx, cy, px, py);
                    push(xy, px, py);
                    break;

                case icf::svg::SegmentKind::Line:
                    cx = s.p[0].x;
                    cy = s.p[0].y;
                    toWorld(globals, cx, cy, px, py);
                    push(xy, px, py);
                    break;

                case icf::svg::SegmentKind::Cubic: {
                    // Uniform in the parameter, not adaptive. Uniform is what
                    // makes the sagitta bound above quotable: `subdivisions`
                    // chords on one cubic, the same count the coverage pass
                    // uses, so the two halves of a layer agree by construction
                    // instead of by luck.
                    const double x0 = cx, y0 = cy;
                    const double x1 = s.p[0].x, y1 = s.p[0].y;
                    const double x2 = s.p[1].x, y2 = s.p[1].y;
                    const double x3 = s.p[2].x, y3 = s.p[2].y;
                    for (int i = 1; i <= n; ++i) {
                        const double t = static_cast<double>(i) / n;
                        const double u = 1.0 - t;
                        const double a = u * u * u;
                        const double b = 3.0 * u * u * t;
                        const double c = 3.0 * u * t * t;
                        const double d = t * t * t;
                        toWorld(globals, a * x0 + b * x1 + c * x2 + d * x3,
                                a * y0 + b * y1 + c * y2 + d * y3, px, py);
                        push(xy, px, py);
                    }
                    cx = x3;
                    cy = y3;
                    break;
                }

                case icf::svg::SegmentKind::Close:
                    flush(xy, out.contours);
                    cx = sx;
                    cy = sy;
                    break;
            }
        }
        // A subpath the author left open is closed anyway: a distance field has
        // no open region, and leaving it open would give the shape an inside
        // that leaks out through the gap.
        flush(xy, out.contours);
    }
    return out;
}

std::string glassRulerNote(std::uint32_t size) {
    if (static_cast<double>(size) == kCanvasPoints) return std::string();
    char buf[320];
    std::snprintf(buf, sizeof(buf),
                  "vidro em %u px: a altura de refracao esta em PONTOS e foi multiplicada por "
                  "%u/1024 -- fator [INF], nao lido do alvo. O corte de banda -5.0 de "
                  "sdf_glass_displacement e uma constante do alvo em unidades de CAMPO e nao "
                  "foi reescalado, entao a borda da banda cai na distancia errada fora de 1024.",
                  size, size);
    return std::string(buf);
}

GlassRefraction glassRefractionFor(const DenormalisedGlass& glass, std::uint32_t size) {
    const double k = glassPointsToPixels(size);
    GlassRefraction r;
    r.heightPixels = static_cast<float>(glass.refractionHeightPoints * k);
    r.scalePixels = static_cast<float>(glass.displacementShaderArgument * k);
    r.variant = static_cast<std::uint32_t>(glass.refractionSupersampling);
    return r;
}

bool glassRefractionIsIdentity(const GlassRefraction& r) {
    // `fma(disp, 2s, -s)` at `s == 0` is zero for every `disp`, and `-0.0`
    // multiplies to zero just as well -- so the test is on the value, not on
    // its sign bit.
    return r.scalePixels == 0.0f;
}

float glassFilterWidth(const FieldSample& sample) {
    return std::fabs(sample.gx) + std::fabs(sample.gy);
}

DisplacementImage glassDisplacementMap(const FieldImage& field, const GlassRefraction& r) {
    DisplacementImage out;
    out.width = field.width;
    out.height = field.height;
    out.rgba.assign(static_cast<std::size_t>(field.width) * field.height * 4, 0.0f);

    sdfdisp::Params params;
    params.height = r.heightPixels;
    params.curvature = r.curvature;
    params.offset = r.offset;
    params.maskOffset = r.maskOffset;
    sdfdisp::Rot rot;
    rot.cos = r.angleCos;
    rot.sin = r.angleSin;

    for (std::uint32_t y = 0; y < field.height; ++y) {
        for (std::uint32_t x = 0; x < field.width; ++x) {
            const float* f = field.at(x, y);
            sdfdisp::Field in;
            in.d = f[0];
            in.gx = f[1];
            in.gy = f[2];
            in.coverage = f[3];

            FieldSample s;
            s.distance = f[0];
            s.gx = f[1];
            s.gy = f[2];
            s.coverage = f[3];
            const sdfdisp::Result d =
                sdfdisp::glassDisplacement(in, params, rot, glassFilterWidth(s));

            const std::size_t i = (static_cast<std::size_t>(y) * field.width + x) * 4;
            out.rgba[i + 0] = 0.5f + 0.5f * d.x;
            out.rgba[i + 1] = 0.5f + 0.5f * d.y;
            // The per-tap weight, not the mask: the shader already divides by
            // the tap count, so anything but 1 here darkens the backdrop.
            out.rgba[i + 2] = 1.0f;
            out.rgba[i + 3] = d.alpha;
        }
    }
    return out;
}

void glassOver(std::vector<float>& acc, std::uint32_t width, std::uint32_t height,
               const DisplacementImage& map, const GlassRefraction& r) {
    if (map.width != width || map.height != height) return;
    if (acc.size() < static_cast<std::size_t>(width) * height * 4) return;

    // The backdrop is read from a SNAPSHOT. Refracting in place would feed
    // already-refracted pixels back into later taps, which is a smear that
    // depends on the scan order and would still look like glass.
    const std::vector<float> backdrop = acc;

    DisplacementParams p;
    p.scale = r.scalePixels;
    // Both layers are the same map: a point in pixels onto the whole image in
    // normalised UV, clamped to it. `[BIN]` The transform's shape (an affine
    // 2x3 plus a clamp rect) is the target's `RB::Layer`; the VALUES are this
    // renderer's, because this renderer owns the two buffers.
    const float ix = 1.0f / static_cast<float>(width);
    const float iy = 1.0f / static_cast<float>(height);
    for (DisplacementLayer* layer : {&p.source, &p.map}) {
        layer->m[0][0] = ix;
        layer->m[0][1] = 0.0f;
        layer->m[1][0] = 0.0f;
        layer->m[1][1] = iy;
        layer->m[2][0] = 0.0f;
        layer->m[2][1] = 0.0f;
        layer->m[3][0] = 0.0f;
        layer->m[3][1] = 0.0f;
        layer->m[4][0] = 1.0f;
        layer->m[4][1] = 1.0f;
    }

    SampledImage source;
    source.width = static_cast<int>(width);
    source.height = static_cast<int>(height);
    source.rgba = backdrop.data();
    SampledImage mapped;
    mapped.width = static_cast<int>(width);
    mapped.height = static_cast<int>(height);
    mapped.rgba = map.rgba.data();

    // `p`'s screen derivatives: one pixel per pixel, and axis aligned, because
    // `p` IS the pixel here. The tap jitter rides on these, which is why they
    // are passed rather than assumed inside the transcription.
    const float dpdx[2] = {1.0f, 0.0f};
    const float dpdy[2] = {0.0f, 1.0f};

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
            const float mask = map.rgba[i + 3];
            if (mask <= 0.0f) continue;   // outside the shape: untouched, bit for bit

            float refracted[4];
            displacementMap(p, r.variant, static_cast<float>(x) + 0.5f,
                            static_cast<float>(y) + 0.5f, dpdx, dpdy, source, mapped, refracted);
            for (int k = 0; k < 4; ++k) {
                acc[i + k] = backdrop[i + k] + (refracted[k] - backdrop[i + k]) * mask;
            }
        }
    }
}

}  // namespace rb
