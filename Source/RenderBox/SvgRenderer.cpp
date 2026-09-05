#include "Source/RenderBox/SvgRenderer.h"

#include "Source/RenderBox/StrokeRender.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Source/RenderBox/PathBuffer.h"
#include "Source/RenderBox/PathCompositeOracle.h"
#include "Source/RenderBox/GradientOracle.h"
#include "Source/RenderBox/PathResolveOracle.h"

namespace rb {
namespace {

// `[BIN]` Mode 1 of `accumulator_shape` is the plain fill: with the even-odd bit
// clear it is `saturate(|coverage|)`, the non-zero rule; with it set it is the
// triangle wave that alternates on each whole winding. Doc 03 §14.
constexpr std::uint32_t kPlainFill = 1u << kShapeModeShift;

std::uint32_t stateFor(icf::svg::FillRule rule) {
    return kPlainFill | (rule == icf::svg::FillRule::EvenOdd ? kEvenOdd : 0u);
}


// The bounding box of a path in its own user space. `objectBoundingBox` -- the
// SVG default, and what 2 of the corpus's 161 gradients use -- measures its
// coordinates as fractions of it.
void pathBounds(const icf::svg::Path& path, double& x0, double& y0, double& x1,
                double& y1) {
    x0 = y0 = 1e300;
    x1 = y1 = -1e300;
    auto see = [&](const icf::svg::Point& p) {
        x0 = std::min(x0, p.x);
        y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x);
        y1 = std::max(y1, p.y);
    };
    for (const icf::svg::Segment& seg : path.segments) {
        switch (seg.kind) {
            case icf::svg::SegmentKind::Move:
            case icf::svg::SegmentKind::Line:
                see(seg.p[0]);
                break;
            case icf::svg::SegmentKind::Cubic:
                // The control points are included. A curve stays inside its own
                // hull, so this box contains the curve -- it is not the tightest
                // box, and SVG does not say the tight one is required.
                see(seg.p[0]);
                see(seg.p[1]);
                see(seg.p[2]);
                break;
            case icf::svg::SegmentKind::Close:
                break;
        }
    }
    if (x1 < x0) {
        x0 = y0 = 0.0;
        x1 = y1 = 1.0;
    }
}

// b after a: the 2x3 product, row-major.
void compose2x3(const double (&a)[6], const double (&b)[6], double (&out)[6]) {
    out[0] = b[0] * a[0] + b[1] * a[3];
    out[1] = b[0] * a[1] + b[1] * a[4];
    out[2] = b[0] * a[2] + b[1] * a[5] + b[2];
    out[3] = b[3] * a[0] + b[4] * a[3];
    out[4] = b[3] * a[1] + b[4] * a[4];
    out[5] = b[3] * a[2] + b[4] * a[5] + b[5];
}

}  // namespace

PathGlobals fitViewBox(const icf::svg::ViewBox& box, std::uint32_t width,
                       std::uint32_t height) {
    PathGlobals g;
    const double w = box.width > 0 ? box.width : 1.0;
    const double h = box.height > 0 ? box.height : 1.0;
    const double s = std::min(static_cast<double>(width) / w,
                              static_cast<double>(height) / h);

    g.m0[0] = static_cast<float>(s);
    g.m0[1] = 0.0f;
    g.m1[0] = 0.0f;
    g.m1[1] = static_cast<float>(s);
    g.m2[0] = static_cast<float>((width - s * w) * 0.5 - s * box.x);
    g.m2[1] = static_cast<float>((height - s * h) * 0.5 - s * box.y);

    g.twoOverSize[0] = 2.0f / static_cast<float>(width);
    g.twoOverSize[1] = 2.0f / static_cast<float>(height);
    // Every pixel to the right of an edge has to be reachable by that edge's
    // quad, because that is how the winding is carried across the row. The
    // bound is the image's own right-hand side.
    g.urx = static_cast<float>(width);
    return g;
}

Result<RenderedImage> renderSvg(Device& device, const icf::svg::SvgDocument& doc,
                                RenderOptions options) {
    return renderSvgPlaced(device, doc,
                           fitViewBox(doc.viewBox, options.width, options.height), options);
}

Result<RenderedImage> renderSvgPlaced(Device& device, const icf::svg::SvgDocument& doc,
                                      const PathGlobals& placement, RenderOptions options) {
    if (options.width == 0 || options.height == 0) {
        return std::unexpected("a render target of zero area was asked for");
    }

    RenderedImage out;
    out.width = options.width;
    out.height = options.height;
    const std::size_t texels = static_cast<std::size_t>(options.width) * options.height;

    // The accumulator is PREMULTIPLIED while compositing -- `over` is only
    // associative in premultiplied form -- and is un-multiplied once at the end.
    std::vector<float> acc(texels * 4, 0.0f);

    auto image = Image::create(device, options.width, options.height);
    if (!image) return std::unexpected(image.error());
    auto pass = CoveragePass::create(device);
    if (!pass) return std::unexpected(pass.error());

    const PathGlobals globals = placement;

    for (std::size_t i = 0; i < doc.shapes.size(); ++i) {
        const icf::svg::Shape& shape = doc.shapes[i];

        // `none` is not an error and not a gap: the shape is deliberately
        // unpainted, and reporting it as skipped would bury the real gaps in
        // noise. It is simply not drawn.
        //
        // A STROKE STILL COUNTS AS PAINT. `fill="none"` with a stroke is the
        // normal way to draw a line, and this early exit dropped every such
        // shape before the stroke code below could see it -- which is how the
        // end-to-end test came back with a blank canvas and `drawn == 1`.
        const bool strokePaints = shape.stroke.kind != icf::svg::PaintKind::None &&
                                  shape.strokeWidth > 0.0;
        if (shape.fill.kind == icf::svg::PaintKind::None &&
            options.override.kind == FillOverride::Kind::None && !strokePaints) {
            continue;
        }
        ResolvedGradient ramp;
        const bool overridden = options.override.kind != FillOverride::Kind::None;
        if (overridden) {
            // The layer's fill replaces the art's own, so a reference that
            // cannot be resolved is no longer a reason to skip the shape.
        } else if (shape.fill.kind == icf::svg::PaintKind::Reference) {
            double bx0, by0, bx1, by1;
            pathBounds(shape.path, bx0, by0, bx1, by1);
            ramp = resolveGradient(doc, shape.fill.reference, bx0, by0, bx1, by1);
            if (!ramp.ok) {
                out.skipped.push_back({i, shape.element, ramp.why});
                continue;
            }
        }
        // There is NO branch here for a fill value the reader cannot read, and
        // the mutation sweep is why. `SvgDocument::resolve` never hands one
        // out: a value it does not know goes into `unsupported()` -- named at
        // the DOCUMENT, which is where it belongs -- and the shape keeps the
        // inherited paint. So a branch here could not be reached by any input,
        // and a guard nothing can reach is dead weight that makes the sweep
        // report a defect nobody can fix.
        if (shape.path.segments.empty()) continue;

        auto buffer = buildPathBuffer(shape.path, BuildOptions{options.subdivisions});
        if (!buffer) {
            out.skipped.push_back({i, shape.element, buffer.error()});
            continue;
        }
        auto drew = pass->draw(device, *image, *buffer, globals);
        if (!drew) return std::unexpected(drew.error());
        auto coverage = readBack(device, *image);
        if (!coverage) return std::unexpected(coverage.error());
        if (coverage->size() != texels) {
            return std::unexpected("the coverage read back is not the target's size");
        }

        if (!overridden && shape.fill.color.displayP3) out.unconvertedP3.push_back(i);
        float colour[4] = {
            static_cast<float>(shape.fill.color.r), static_cast<float>(shape.fill.color.g),
            static_cast<float>(shape.fill.color.b), static_cast<float>(shape.fill.color.a)};
        if (options.override.kind == FillOverride::Kind::Solid) {
            for (int k = 0; k < 4; ++k) colour[k] = options.override.colour[k];
        }
        const std::uint32_t state = stateFor(shape.fillRule);

        // For a gradient the colour is per pixel, so the device pixel has to
        // come back to the SVG's user space first. The placement is a uniform
        // scale and a translation with no rotation, which is why undoing it is
        // two subtractions and a divide rather than a general inverse.
        const double sx = globals.m0[0] != 0.0f ? 1.0 / globals.m0[0] : 0.0;
        const double sy = globals.m1[1] != 0.0f ? 1.0 / globals.m1[1] : 0.0;

        const bool fillPaints = shape.fill.kind != icf::svg::PaintKind::None ||
                                options.override.kind != FillOverride::Kind::None;
        for (std::size_t t = 0; fillPaints && t < texels; ++t) {
            // `[BIN]` The signed area the edges accumulate lands in the SECOND
            // channel of the half2 target; the first carries the other half of
            // the pair. Doc 03 §12 and §13.
            const float alpha = accumulatorShape(state, (*coverage)[t].y, ShapeCurve{});
            if (alpha == 0.0f) continue;

            if (options.override.kind == FillOverride::Kind::Ramp) {
                // The override's map is already in CANVAS pixels: the compositor
                // built it, and a layer's fill is placed on the canvas rather
                // than in the art's own space.
                const double px = static_cast<double>(t % options.width) + 0.5;
                const double py = static_cast<double>(t / options.width) + 0.5;
                const double gx = options.override.m[0] * px + options.override.m[1] * py +
                                  options.override.m[2];
                rampAtPositions(options.override.stops, static_cast<float>(gx), colour);
            } else if (ramp.ok) {
                // The pixel CENTRE, back through the placement into user space,
                // and from there into the gradient's own space.
                const double px = static_cast<double>(t % options.width) + 0.5;
                const double py = static_cast<double>(t / options.width) + 0.5;
                const double ux = (px - globals.m2[0]) * sx;
                const double uy = (py - globals.m2[1]) * sy;
                const double gx = ramp.m[0] * ux + ramp.m[1] * uy + ramp.m[2];
                const double gy = ramp.m[3] * ux + ramp.m[4] * uy + ramp.m[5];
                bool covered = false;
                const float raw = gradientValue(ramp.state, static_cast<float>(gx),
                                                static_cast<float>(gy), 1.0f, 0.0f, &covered);
                if (!covered) continue;
                rampAtPositions(ramp.stops, foldValue(ramp.state, raw), colour);
            }
            const CompositeOut c = compositeFlat(0u, 0u, colour, alpha, 0.0f);
            // `[BIN]` `c.coverage[0]` is the alpha the target REPORTS: the
            // paint's own alpha times the coverage, and the channel the invert
            // bit would act on. That number is measured.
            //
            // `[INF]` Stacking one shape on the next is a DIFFERENT operation
            // from `composite`, and it is not decoded: the target does it with a
            // framebuffer blend whose configuration lives in `RenderState` bits
            // that have no semantics yet (doc 03 §11). So the `over` below is
            // the standard premultiplied one and it is THIS renderer's choice,
            // not a transcription -- which is also why `c.colour` is not used
            // here: it is premultiplied by the coverage alone, not by the
            // paint's alpha, and it is the right value for the target's
            // attachment rather than for an accumulator.
            // `opacity` composites the shape against the backdrop at this
            // alpha. It is a SEPARATE multiplier from the paint's own alpha and
            // from `fill-opacity`, and all three apply -- SVG 1.1 §14.5 stacks
            // them rather than choosing one.
            const float srcA = c.coverage[0] * static_cast<float>(shape.opacity);
            const float inv = 1.0f - srcA;
            float* dst = &acc[t * 4];
            for (int k = 0; k < 3; ++k) dst[k] = colour[k] * srcA + dst[k] * inv;
            dst[3] = srcA + dst[3] * inv;
        }

        // THE STROKE, after the fill and over it, which is the order SVG
        // defines. Until 2026-09-04 this loop drew the fill and dropped the
        // stroke SILENTLY -- a plausible picture with a clean report, the same
        // defect the group blend carried. `[ART]` It is the corpus's largest
        // single blocker: 31 layers over 10 documents.
        if (shape.stroke.kind != icf::svg::PaintKind::None && shape.strokeWidth > 0.0) {
            if (shape.stroke.kind == icf::svg::PaintKind::Reference) {
                // `[ART]` No corpus stroke paints with a gradient -- all 35 are
                // flat, 31 hex and 4 `white`. Named rather than approximated
                // with the first stop.
                out.skipped.push_back({i, shape.element,
                                       "o traco pinta com url(#...) e so traco chapado"
                                       " esta transcrito"});
            } else {
                StrokePlacement sp;
                sp.m0[0] = globals.m0[0];
                sp.m0[1] = globals.m0[1];
                sp.m1[0] = globals.m1[0];
                sp.m1[1] = globals.m1[1];
                sp.m2[0] = globals.m2[0];
                sp.m2[1] = globals.m2[1];
                sp.scale = std::sqrt(static_cast<double>(globals.m0[0]) * globals.m0[0] +
                                     static_cast<double>(globals.m0[1]) * globals.m0[1]);

                // `[ART]` The corpus names no `stroke-linecap` and no
                // `stroke-linejoin` in any of its 35 stroked SVGs, so these are
                // the SVG defaults and not a choice this renderer is making.
                // `stroke-miterlimit` IS named -- 22.9256, in eleven places --
                // and the reader does not carry it yet, so the SVG default of 4
                // stands and the difference only shows on a corner sharper than
                // about 29 degrees.
                StrokeParams params;
                params.cap = LineCap::Butt;
                params.join = LineJoin::Miter;
                params.miterLimit = 4.0;

                const std::vector<float> cov =
                    rasteriseStroke(shape, sp, options.width, options.height,
                                    options.subdivisions, params);
                if (!cov.empty()) {
                    const float sr = static_cast<float>(shape.stroke.color.r);
                    const float sg = static_cast<float>(shape.stroke.color.g);
                    const float sb = static_cast<float>(shape.stroke.color.b);
                    const float sa = static_cast<float>(shape.stroke.color.a);
                    if (shape.stroke.color.displayP3) out.unconvertedP3.push_back(i);
                    for (std::size_t t = 0; t < texels; ++t) {
                        const float a = cov[t] * sa * static_cast<float>(shape.opacity);
                        if (a <= 0.0f) continue;
                        const float inv2 = 1.0f - a;
                        float* d2 = &acc[t * 4];
                        d2[0] = sr * a + d2[0] * inv2;
                        d2[1] = sg * a + d2[1] * inv2;
                        d2[2] = sb * a + d2[2] * inv2;
                        d2[3] = a + d2[3] * inv2;
                    }
                    ++out.strokesDrawn;
                }
            }
        }

        ++out.drawn;
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


ResolvedGradient resolveGradient(const icf::svg::SvgDocument& doc, const std::string& id,
                                 double bx0, double by0, double bx1, double by1) {
    ResolvedGradient out;
    const auto it = doc.gradients.find(id);
    if (it == doc.gradients.end()) {
        out.why = "url(#" + id + ") nao resolve para nenhum gradiente do documento";
        return out;
    }
    const icf::svg::Gradient& g = it->second;
    if (g.stops.empty()) {
        out.why = "o gradiente #" + id + " nao tem paradas";
        return out;
    }

    for (const icf::svg::GradientStop& st : g.stops) {
        RampPoint p;
        p.location = static_cast<float>(st.offset);
        p.rgba[0] = static_cast<float>(st.color.r);
        p.rgba[1] = static_cast<float>(st.color.g);
        p.rgba[2] = static_cast<float>(st.color.b);
        p.rgba[3] = static_cast<float>(st.color.a);
        out.stops.push_back(p);
    }
    std::stable_sort(out.stops.begin(), out.stops.end(),
                     [](const RampPoint& a, const RampPoint& b) {
                         return a.location < b.location;
                     });

    // `objectBoundingBox` is the SVG default and the coordinates are fractions
    // of the shape's box, so that box becomes part of the map. `userSpaceOnUse`
    // -- 159 of the corpus's 161 -- leaves the coordinates alone.
    double toUser[6] = {1, 0, 0, 0, 1, 0};
    if (!g.userSpace) {
        const double w = bx1 - bx0, h = by1 - by0;
        toUser[0] = w != 0.0 ? w : 1.0;
        toUser[2] = bx0;
        toUser[4] = h != 0.0 ? h : 1.0;
        toUser[5] = by0;
    }
    // `gradientTransform` -- 13 in the corpus -- applies in that same space.
    double full[6];
    const double gt[6] = {g.transform.a, g.transform.c, g.transform.e,
                          g.transform.b, g.transform.d, g.transform.f};
    compose2x3(toUser, gt, full);

    // Now the inverse: user space -> gradient space. `[BIN]` For the linear
    // geometry the target's `Gradient::value` is literally `p.x`, so this map
    // has to put the axis start at x = 0 and its end at x = 1. For the radial
    // one it is `|p| * a + b` with a = 1 and b = 0, so the map has to put the
    // centre at the origin and the radius at 1.
    double raw[6];
    if (g.kind == icf::svg::GradientKind::Linear) {
        const double dx = g.x2 - g.x1, dy = g.y2 - g.y1;
        const double len2 = dx * dx + dy * dy;
        if (len2 == 0.0) {
            out.why = "o gradiente #" + id + " tem eixo de comprimento zero";
            return out;
        }
        // The projection onto the axis, normalised: t = ((p - p1) . d) / |d|^2.
        raw[0] = dx / len2;
        raw[1] = dy / len2;
        raw[2] = -(g.x1 * dx + g.y1 * dy) / len2;
        raw[3] = 0.0;
        raw[4] = 0.0;
        raw[5] = 0.0;
        out.state = 0u << kGradientKindShift;   // linear, pad
    } else {
        if (g.radius == 0.0) {
            out.why = "o gradiente #" + id + " tem raio zero";
            return out;
        }
        raw[0] = 1.0 / g.radius;
        raw[1] = 0.0;
        raw[2] = -g.cx / g.radius;
        raw[3] = 0.0;
        raw[4] = 1.0 / g.radius;
        raw[5] = -g.cy / g.radius;
        out.state = 3u << kGradientKindShift;   // radial, pad
    }

    // `full` carries user -> the gradient's declared space; `raw` carries that
    // space -> the parameter. They compose in that order, and the inverse of
    // `full` is what a point actually needs -- so invert it here rather than
    // per pixel.
    const double det = full[0] * full[4] - full[1] * full[3];
    if (det == 0.0) {
        out.why = "o gradientTransform do #" + id + " nao e invertivel";
        return out;
    }
    double inv[6];
    inv[0] = full[4] / det;
    inv[1] = -full[1] / det;
    inv[2] = (full[1] * full[5] - full[4] * full[2]) / det;
    inv[3] = -full[3] / det;
    inv[4] = full[0] / det;
    inv[5] = (full[3] * full[2] - full[0] * full[5]) / det;
    compose2x3(inv, raw, out.m);

    // `[ART]` `spreadMethod` occurs ZERO times in the corpus's 161 gradients, so
    // the spread is `pad` and that is a scope decision with a number behind it,
    // not an omission (spec §3.1).
    out.ok = true;
    return out;
}

}  // namespace rb
