#include "Source/RenderBox/SvgPattern.h"

#include <cmath>

namespace rb {
namespace {

// The inverse of a 2x3 affine, in the layout `icf::svg::Transform` keeps:
//
//     | a c e |
//     | b d f |
//
// A degenerate linear part has no inverse, and a pattern whose content collapses
// to a line or a point cannot be sampled -- so this reports rather than dividing
// by a zero determinant.
bool invert(const icf::svg::Transform& t, double out[6]) {
    const double det = t.a * t.d - t.c * t.b;
    if (std::abs(det) < 1e-12) return false;
    const double ia = t.d / det, ic = -t.c / det;
    const double ib = -t.b / det, id = t.a / det;
    out[0] = ia;
    out[1] = ic;
    out[2] = -(ia * t.e + ic * t.f);
    out[3] = ib;
    out[4] = id;
    out[5] = -(ib * t.e + id * t.f);
    return true;
}

// `lhs` after `rhs`, both row-major 2x3.
void compose(const double lhs[6], const double rhs[6], double out[6]) {
    out[0] = lhs[0] * rhs[0] + lhs[1] * rhs[3];
    out[1] = lhs[0] * rhs[1] + lhs[1] * rhs[4];
    out[2] = lhs[0] * rhs[2] + lhs[1] * rhs[5] + lhs[2];
    out[3] = lhs[3] * rhs[0] + lhs[4] * rhs[3];
    out[4] = lhs[3] * rhs[1] + lhs[4] * rhs[4];
    out[5] = lhs[3] * rhs[2] + lhs[4] * rhs[5] + lhs[5];
}

}  // namespace

ResolvedPattern resolvePattern(const icf::svg::SvgDocument& doc, const std::string& id,
                               double bx0, double by0, double bx1, double by1,
                               const icf::DecodedPng* image) {
    ResolvedPattern out;
    auto it = doc.patterns.find(id);
    if (it == doc.patterns.end()) {
        out.why = "url(#" + id + ") nao resolve para nenhum pattern do documento";
        return out;
    }
    const icf::svg::SvgDocument::Pattern& p = it->second;

    auto img = doc.images.find(p.imageId);
    if (img == doc.images.end()) {
        out.why = "o <use> do pattern nomeia #" + p.imageId +
                  ", que nao e uma <image> legivel deste documento";
        return out;
    }
    if (!image) {
        out.why = "a <image> #" + p.imageId + " nao foi decodada";
        return out;
    }
    if (image->width == 0 || image->height == 0) {
        out.why = "a <image> #" + p.imageId + " decodou vazia";
        return out;
    }

    const double bw = bx1 - bx0, bh = by1 - by0;
    if (!(bw > 0.0) || !(bh > 0.0)) {
        out.why = "a forma preenchida pelo pattern nao tem caixa";
        return out;
    }

    // `patternUnits` is `objectBoundingBox` here and nowhere else: the reader
    // refuses `userSpaceOnUse` by name (no corpus pattern uses it, and it is a
    // different mapping rather than a different default), so this does not have
    // to carry a branch no input can reach.
    out.tileX = bx0 + p.x * bw;
    out.tileY = by0 + p.y * bh;
    out.tileW = p.width * bw;
    out.tileH = p.height * bh;
    if (!(out.tileW > 0.0) || !(out.tileH > 0.0)) {
        out.why = "o ladrilho do pattern tem area zero";
        return out;
    }

    // THE CHAIN, right to left: an offset inside the tile becomes a CONTENT
    // coordinate, the `<use>`'s transform is undone to reach the image's own
    // coordinates, and those are scaled by what the `<image>` element says its
    // size is against what the file turned out to hold.
    //
    //   S1   content units. Under `objectBoundingBox` one unit is the box, so a
    //        user offset divides by it; under `userSpaceOnUse` the offset already
    //        IS the content coordinate. `[ART]` All 11 corpus patterns are the
    //        first; the second is built and has no witness.
    //   Tinv the `<use>`'s transform, undone.
    //   S2   `[OBS]` the element's `width`/`height` against the decoded file's.
    //        The corpus's three images say 1024 and decode to 1024, so this is
    //        an identity there and is written for the case where it is not --
    //        `preserveAspectRatio="none"`, which all three name, is exactly the
    //        instruction to stretch rather than fit.
    double s1[6] = {1, 0, 0, 0, 1, 0};
    if (!p.contentUserSpace) {
        s1[0] = 1.0 / bw;
        s1[4] = 1.0 / bh;
    }
    double tinv[6];
    if (!invert(p.contentTransform, tinv)) {
        out.why = "o transform do <use> do pattern nao tem inversa";
        return out;
    }
    // The `<image>` ELEMENT's declared size, which is what the pattern's geometry
    // is written against. Zero means it said nothing, and then the file's own
    // size stands.
    const double ew = img->second.width;
    const double eh = img->second.height;
    double s2[6] = {1, 0, 0, 0, 1, 0};
    if (ew > 0.0) s2[0] = static_cast<double>(image->width) / ew;
    if (eh > 0.0) s2[4] = static_cast<double>(image->height) / eh;

    double tmp[6];
    compose(tinv, s1, tmp);
    compose(s2, tmp, out.m);

    out.image = image;
    out.ok = true;
    return out;
}

bool samplePattern(const ResolvedPattern& p, double ux, double uy, float out[4]) {
    if (!p.ok || !p.image) return false;

    // Into the tile. `fmod` can come back negative for a point left of or above
    // the tile's origin, and a pattern repeats in BOTH directions -- so the
    // remainder is brought back into range rather than clamped.
    double lx = std::fmod(ux - p.tileX, p.tileW);
    double ly = std::fmod(uy - p.tileY, p.tileH);
    if (lx < 0.0) lx += p.tileW;
    if (ly < 0.0) ly += p.tileH;

    const double px = p.m[0] * lx + p.m[1] * ly + p.m[2];
    const double py = p.m[3] * lx + p.m[4] * ly + p.m[5];

    const icf::DecodedPng& img = *p.image;
    // Half a texel back, because a pixel's value belongs at its CENTRE.
    const double cx = px - 0.5, cy = py - 0.5;
    if (cx < -1.0 || cy < -1.0 || cx > img.width || cy > img.height) return false;

    // `[INF]` Bilinear, and PREMULTIPLIED while weighting -- the same choice, for
    // the same reason, as `placeRaster` in `IconRenderer.cpp`: what the target
    // resamples with was never decoded, and averaging straight colour drags the
    // colour of transparent texels into their opaque neighbours.
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
            const std::size_t s = (static_cast<std::size_t>(sy) * img.width + sx) * 4;
            const float sa = img.rgba[s + 3];
            for (int c = 0; c < 3; ++c) acc[c] += static_cast<float>(wgt) * img.rgba[s + c] * sa;
            acc[3] += static_cast<float>(wgt) * sa;
        }
    }
    out[3] = acc[3];
    for (int c = 0; c < 3; ++c) out[c] = acc[3] > 0.0f ? acc[c] / acc[3] : 0.0f;
    return true;
}

}  // namespace rb
