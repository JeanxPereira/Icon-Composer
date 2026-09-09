#include "Source/RenderBox/SvgRenderer.h"

#include "Source/RenderBox/StrokeRender.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>

#include "Source/RenderBox/PathBuffer.h"
#include "Source/RenderBox/PathCompositeOracle.h"
#include "Source/RenderBox/GradientOracle.h"
#include "Source/RenderBox/SvgFilter.h"
#include "Source/RenderBox/SvgPattern.h"
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

    // THE CLIP MASKS, BUILT ONCE EACH AND ON DEMAND.
    //
    // A clip is a per-pixel INTERSECTION, so the mask is the same for every
    // shape that references it and is worth caching: the corpus's one clip is
    // used once, but a file that clipped forty shapes to one rect would
    // otherwise pay forty coverage passes for one answer.
    //
    // The region is the UNION of the `<clipPath>`'s children. `max` is the
    // union taken on the coverage itself -- exact where they do not overlap,
    // and never brighter than 1 where they do. `[ART]` The corpus has a single
    // child, so the multi-child case is built and not exercised.
    std::map<std::string, std::vector<float>> clipMasks;
    bool clipFailed = false;
    std::string clipError;
    auto clipFor = [&](const std::string& id) -> const std::vector<float>* {
        auto it = clipMasks.find(id);
        if (it != clipMasks.end()) return &it->second;
        auto def = doc.clipPaths.find(id);
        if (def == doc.clipPaths.end()) return nullptr;
        std::vector<float> mask(texels, 0.0f);
        for (const icf::svg::Path& path : def->second) {
            if (path.segments.empty()) continue;
            auto buffer = buildPathBuffer(path, BuildOptions{options.subdivisions});
            if (!buffer) { clipFailed = true; clipError = buffer.error(); return nullptr; }
            auto drew = pass->draw(device, *image, *buffer, globals);
            if (!drew) { clipFailed = true; clipError = drew.error(); return nullptr; }
            auto cov = readBack(device, *image);
            if (!cov) { clipFailed = true; clipError = cov.error(); return nullptr; }
            if (cov->size() != texels) { clipFailed = true; clipError = "clip coverage size"; return nullptr; }
            // A clip's children use their own fill rule; non-zero is SVG's
            // initial value and no corpus clip names another.
            const std::uint32_t st = stateFor(icf::svg::FillRule::NonZero);
            for (std::size_t t = 0; t < texels; ++t) {
                mask[t] = std::fmax(mask[t], accumulatorShape(st, (*cov)[t].y, ShapeCurve{}));
            }
        }
        return &clipMasks.emplace(id, std::move(mask)).first->second;
    };

    // THE LUMINANCE MASKS, built once each and on demand.
    //
    // A mask is not a clip. A clip is geometry, so its coverage is the answer; a
    // mask carries PAINT, and its value at a pixel is the LUMINANCE of what it
    // draws times its alpha -- a black rect and a white one are the same clip
    // and opposite masks.
    //
    // So the mask's children are rendered as a document of their own, through
    // this very function, at the same placement. That reuse is the point: a mask
    // filled with a gradient, or carrying its own clip or opacity, works because
    // nothing here is a second implementation of the first.
    //
    // `[OBS]` THE COLOUR SPACE IS NOT SETTLED. SVG 1.1 says the luminance is
    // taken in linearRGB and browsers take it in sRGB, and the two disagree on
    // every colour that is not black or white. `[ART]` All three corpus masks
    // are pure white and pure black, whose luminance is 1 and 0 in either -- so
    // the corpus cannot decide it, and this uses the sRGB coefficients without
    // claiming they are the target's.
    // THE DECODED RASTERS, one per `<image>` id and decoded ON DEMAND.
    //
    // `[ART]` Each of Delta's three files carries a 1024x1024 RGBA PNG as 2,4 MB
    // of base64, and each is named by five patterns. Decoding per SHAPE would
    // inflate five times over for one picture, so the id is the key -- the same
    // reason the clip masks are cached above.
    std::map<std::string, icf::DecodedPng> decodedImages;
    auto imageFor = [&](const std::string& id) -> const icf::DecodedPng* {
        auto have = decodedImages.find(id);
        if (have != decodedImages.end()) {
            return have->second.error.empty() ? &have->second : nullptr;
        }
        auto def = doc.images.find(id);
        if (def == doc.images.end()) return nullptr;
        icf::DecodedPng got;
        if (def->second.mediaType != "image/png") {
            // The reader keeps whatever the `data:` URI declared; only PNG has a
            // decoder here, and naming the type is better than handing arbitrary
            // bytes to a PNG parser and reporting its complaint instead of ours.
            got.error = "media type " + def->second.mediaType + " nao e lido";
        } else {
            got = icf::decodePng(def->second.bytes.data(), def->second.bytes.size());
        }
        auto& slot = decodedImages.emplace(id, std::move(got)).first->second;
        return slot.error.empty() ? &slot : nullptr;
    };

    std::map<std::string, std::vector<float>> maskValues;
    auto luminanceFor = [&](const std::string& id) -> const std::vector<float>* {
        auto it = maskValues.find(id);
        if (it != maskValues.end()) return &it->second;
        auto def = doc.masks.find(id);
        if (def == doc.masks.end()) return nullptr;

        icf::svg::SvgDocument sub;
        sub.viewBox = doc.viewBox;
        sub.gradients = doc.gradients;
        sub.clipPaths = doc.clipPaths;
        sub.shapes = def->second.shapes;
        RenderOptions subOptions;
        subOptions.width = options.width;
        subOptions.height = options.height;
        subOptions.subdivisions = options.subdivisions;
        auto drawn = renderSvgPlaced(device, sub, placement, subOptions);
        if (!drawn) { clipFailed = true; clipError = drawn.error(); return nullptr; }

        std::vector<float> value(texels, 0.0f);
        for (std::size_t t = 0; t < texels; ++t) {
            const float* px = &drawn->rgba[t * 4];
            // The coefficients SVG names for `luminanceToAlpha`.
            const float lum = 0.2125f * px[0] + 0.7154f * px[1] + 0.0721f * px[2];
            value[t] = lum * px[3];
        }
        // Content outside the mask's REGION does not mask. `[ART]` Both corpus
        // regions cover their own content exactly, so this cuts nothing there --
        // it is here because a mask whose region is smaller than its art would
        // otherwise mask with paint the author fenced off.
        if (def->second.hasRegion) {
            const double sx = placement.m0[0], sy = placement.m1[1];
            const double ox = placement.m2[0], oy = placement.m2[1];
            const double x0 = def->second.x * sx + ox, y0 = def->second.y * sy + oy;
            const double x1 = (def->second.x + def->second.width) * sx + ox;
            const double y1 = (def->second.y + def->second.height) * sy + oy;
            for (std::uint32_t py = 0; py < options.height; ++py) {
                for (std::uint32_t px2 = 0; px2 < options.width; ++px2) {
                    const double cx = px2 + 0.5, cy = py + 0.5;
                    if (cx < x0 || cx > x1 || cy < y0 || cy > y1) {
                        value[static_cast<std::size_t>(py) * options.width + px2] = 0.0f;
                    }
                }
            }
        }
        return &maskValues.emplace(id, std::move(value)).first->second;
    };

    for (std::size_t i = 0; i < doc.shapes.size(); ++i) {
        const icf::svg::Shape& shape = doc.shapes[i];

        // ---- A FILTERED GROUP, WHICH IS NOT A SHAPE ------------------------
        //
        // A blur of the sum is not the sum of the blurs, so unlike a clip or a
        // mask this cannot be folded into the shapes one at a time. The run of
        // shapes carrying one `filterInstance` is composed into a target of its
        // own -- through THIS function, the way a mask's children are -- and the
        // chain runs over that.
        //
        // The run is contiguous because the shapes come out of the reader in
        // paint order and a group's children are adjacent. `filterInstance`
        // rather than `filterId` is what bounds it: two sibling groups naming
        // one filter are two targets, not one.
        if (shape.filterInstance != 0) {
            std::size_t end = i;
            while (end < doc.shapes.size() &&
                   doc.shapes[end].filterInstance == shape.filterInstance) {
                ++end;
            }
            auto def = doc.filters.find(shape.filterId);
            if (def == doc.filters.end()) {
                // The reader names a dangling reference and never marks the
                // shape, so this cannot be reached from a parsed document. It is
                // here because `renderSvgPlaced` is public and takes a document
                // anyone can build.
                for (std::size_t k = i; k < end; ++k) {
                    out.skipped.push_back({k, doc.shapes[k].element,
                                           "filter url(#" + shape.filterId +
                                               ") nao resolve para nenhum filter do documento"});
                }
                i = end - 1;
                continue;
            }

            icf::svg::SvgDocument sub;
            sub.viewBox = doc.viewBox;
            sub.gradients = doc.gradients;
            sub.clipPaths = doc.clipPaths;
            sub.masks = doc.masks;
            for (std::size_t k = i; k < end; ++k) {
                icf::svg::Shape copy = doc.shapes[k];
                // WITHOUT the mark, or this recurses forever.
                copy.filterId.clear();
                copy.filterInstance = 0;
                sub.shapes.push_back(std::move(copy));
            }
            RenderOptions subOptions;
            subOptions.width = options.width;
            subOptions.height = options.height;
            subOptions.subdivisions = options.subdivisions;
            subOptions.override = options.override;
            auto drawn = renderSvgPlaced(device, sub, placement, subOptions);
            if (!drawn) return std::unexpected(drawn.error());

            const FilteredGroup filtered =
                applySvgFilter(def->second, drawn->rgba, options.width, options.height, globals);
            if (!filtered.ok) {
                for (std::size_t k = i; k < end; ++k) {
                    out.skipped.push_back({k, doc.shapes[k].element, filtered.why});
                }
                i = end - 1;
                continue;
            }
            for (const auto& n : filtered.notes) out.filterNotes.push_back(n);
            // What the sub-render could not do is the outer render's report too,
            // with its indices carried back so a gap keeps its address.
            for (const auto& sk : drawn->skipped) {
                out.skipped.push_back({i + sk.index, sk.element, sk.why});
            }
            for (std::size_t idx : drawn->unconvertedP3) out.unconvertedP3.push_back(i + idx);

            // Straight in, premultiplied over the accumulator.
            for (std::size_t t = 0; t < texels; ++t) {
                const float sa = filtered.rgba[t * 4 + 3];
                if (sa <= 0.0f) continue;
                const float inv = 1.0f - sa;
                float* dst = &acc[t * 4];
                for (int k = 0; k < 3; ++k) dst[k] = filtered.rgba[t * 4 + k] * sa + dst[k] * inv;
                dst[3] = sa + dst[3] * inv;
            }
            out.drawn += drawn->drawn;
            out.strokesDrawn += drawn->strokesDrawn;
            i = end - 1;
            continue;
        }

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
        ResolvedPattern pattern;
        const bool overridden = options.override.kind != FillOverride::Kind::None;
        if (overridden) {
            // The layer's fill replaces the art's own, so a reference that
            // cannot be resolved is no longer a reason to skip the shape.
        } else if (shape.fill.kind == icf::svg::PaintKind::Reference) {
            double bx0, by0, bx1, by1;
            pathBounds(shape.path, bx0, by0, bx1, by1);
            // A PAINT REFERENCE IS NOT ALWAYS A GRADIENT, and until 2026-09-09
            // the refusal said it was: a shape filled with `url(#pattern0)` came
            // back "nao resolve para nenhum gradiente do documento", sending
            // whoever read the report to look for a `<linearGradient>` that never
            // existed. `[ART]` Ten shapes across two of Delta's files.
            //
            // The document is asked which kind the id NAMES, and each answers for
            // itself.
            if (doc.patterns.count(shape.fill.reference)) {
                auto def = doc.patterns.find(shape.fill.reference);
                pattern = resolvePattern(doc, shape.fill.reference, bx0, by0, bx1, by1,
                                         imageFor(def->second.imageId));
                if (!pattern.ok) {
                    out.skipped.push_back({i, shape.element, pattern.why});
                    continue;
                }
            } else {
                ramp = resolveGradient(doc, shape.fill.reference, bx0, by0, bx1, by1);
                if (!ramp.ok) {
                    out.skipped.push_back({i, shape.element, ramp.why});
                    continue;
                }
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

        // THE CLIPS IN FORCE ON THIS SHAPE, intersected into one factor.
        //
        // Order matters here only for cost: the masks are built before the
        // shape's own coverage is drawn, because both use the same target and
        // the shape's coverage is read back immediately after its draw.
        //
        // A `clip-path` that names no definition is a GAP and not a silent pass
        // at full coverage. Drawing unclipped would put the whole shape on the
        // canvas where the author asked for a sliver of it -- a plausible
        // picture, and the wrong one.
        std::vector<const std::vector<float>*> masks;
        std::string missingClip;
        for (const std::string& id : shape.clipPaths) {
            const std::vector<float>* m = clipFor(id);
            if (clipFailed) return std::unexpected(clipError);
            if (!m) { missingClip = id; break; }
            masks.push_back(m);
        }
        if (!missingClip.empty()) {
            out.skipped.push_back({i, shape.element,
                                   "clip-path url(#" + missingClip +
                                       ") nao resolve para nenhum clipPath do documento"});
            continue;
        }
        // The masks in force, resolved the same way and for the same reason: a
        // reference that names nothing is a GAP. Drawing unmasked would put the
        // whole shape where the author asked for a piece of it.
        std::vector<const std::vector<float>*> lum;
        std::string missingMask;
        for (const std::string& id : shape.masks) {
            const std::vector<float>* m = luminanceFor(id);
            if (clipFailed) return std::unexpected(clipError);
            if (!m) { missingMask = id; break; }
            lum.push_back(m);
        }
        if (!missingMask.empty()) {
            out.skipped.push_back({i, shape.element,
                                   "mask url(#" + missingMask +
                                       ") nao resolve para nenhuma mask do documento"});
            continue;
        }
        auto clipAt = [&masks, &lum](std::size_t t) {
            float f = 1.0f;
            for (const std::vector<float>* m : masks) f *= (*m)[t];
            for (const std::vector<float>* m : lum) f *= (*m)[t];
            return f;
        };

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
            } else if (pattern.ok) {
                // The pixel CENTRE, back through the placement into user space,
                // and from there into the tile. A point the tile does not cover
                // is NOT painted -- and it cannot be painted with the shape's
                // own fill either, which is black by SVG's initial value.
                const double px = static_cast<double>(t % options.width) + 0.5;
                const double py = static_cast<double>(t / options.width) + 0.5;
                const double ux = (px - globals.m2[0]) * sx;
                const double uy = (py - globals.m2[1]) * sy;
                if (!samplePattern(pattern, ux, uy, colour)) continue;
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
            const float srcA = c.coverage[0] * static_cast<float>(shape.opacity) * clipAt(t);
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
                        const float a = cov[t] * sa * static_cast<float>(shape.opacity) * clipAt(t);
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
