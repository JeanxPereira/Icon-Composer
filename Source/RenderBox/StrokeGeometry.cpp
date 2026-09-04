#include "Source/RenderBox/StrokeGeometry.h"

#include <algorithm>
#include <cmath>

namespace rb {
namespace {

// `[BIN]` The constant at `RenderBox 0x161E40`.
constexpr double kRoundCorner = 0.99;

StrokePoint sub(StrokePoint a, StrokePoint b) { return {a.x - b.x, a.y - b.y}; }

StrokePoint normalised(StrokePoint v) {
    const double n = std::sqrt(v.x * v.x + v.y * v.y);
    if (n == 0.0) return {0.0, 0.0};
    return {v.x / n, v.y / n};
}

double dot(StrokePoint a, StrokePoint b) { return a.x * b.x + a.y * b.y; }

bool samePoint(StrokePoint a, StrokePoint b) { return a.x == b.x && a.y == b.y; }

// `[BIN]` The mirrored ghost: `2*P0 - P1`, i.e. the neighbour reflected through
// the end. Not an extrapolation by a fixed distance -- the reflection is what
// makes the discarded window's geometry finite without inventing a direction.
StrokePoint mirror(StrokePoint end, StrokePoint neighbour) {
    return {2.0 * end.x - neighbour.x, 2.0 * end.y - neighbour.y};
}

double smoothstep01(double t) {
    const double c = std::clamp(t, 0.0, 1.0);
    return c * c * (3.0 - 2.0 * c);
}

}  // namespace

LineJoin applyMiterLimit(StrokePoint dirIn, StrokePoint dirOut, double miterLimit) {
    const double c = dot(normalised(dirIn), normalised(dirOut));
    if (c > kRoundCorner) return LineJoin::Round;
    // The binary squares the limit once, up front, and compares against 2 --
    // which is `ml*ml < 2/(1+c)` with no division and no square root.
    if ((1.0 + c) * miterLimit * miterLimit < 2.0) return LineJoin::Bevel;
    return LineJoin::Miter;
}

int joinForCorner(StrokePoint dirIn, StrokePoint dirOut, double radius,
                  const StrokeParams& params) {
    // `[BIN]` `Flattener::lineto` tests the radius FIRST, and the floor is
    // `Flattener[0x2c] = bezier_flatness()/scale`. `recipScale` here IS one
    // pixel measured in point space -- the reciprocal of that scale -- so the
    // floor is `flatness * recipScale`. A stroke thinner than that gets no join
    // at all, and `round` is how the binary spells "no join primitive".
    if (radius <= kBezierFlatness * params.recipScale) return static_cast<int>(LineJoin::Round);
    if (params.join != LineJoin::Miter) return static_cast<int>(params.join);
    return static_cast<int>(applyMiterLimit(dirIn, dirOut, params.miterLimit));
}

std::vector<StrokeLinePoint> strokePointStream(const std::vector<StrokePoint>& raw,
                                               bool closed, const StrokeParams& params) {
    // `[BIN]` `add_point` drops a point whose position equals the previous
    // one's. Doing it here rather than in the caller keeps the instance counts
    // honest: they are derived from the emitted stream, not from the input.
    std::vector<StrokePoint> pts;
    for (const StrokePoint& p : raw) {
        if (pts.empty() || !samePoint(pts.back(), p)) pts.push_back(p);
    }
    if (closed && pts.size() > 1 && samePoint(pts.front(), pts.back())) pts.pop_back();

    std::vector<StrokeLinePoint> out;
    if (pts.size() < 2) return out;

    const double r = params.width * 0.5;
    auto real = [&](StrokePoint p, int join) {
        return StrokeLinePoint{p.x, p.y, r, 1.0, join};
    };
    const std::size_t k = pts.size();

    if (!closed) {
        out.push_back(real(mirror(pts[0], pts[1]), kJoinGhostEnd));
        out.push_back(real(pts[0], static_cast<int>(LineJoin::Round)));
        for (std::size_t i = 1; i + 1 < k; ++i) {
            const StrokePoint in = sub(pts[i], pts[i - 1]);
            const StrokePoint outDir = sub(pts[i + 1], pts[i]);
            out.push_back(real(pts[i], joinForCorner(in, outDir, r, params)));
        }
        out.push_back(real(pts[k - 1], static_cast<int>(LineJoin::Round)));
        out.push_back(real(mirror(pts[k - 1], pts[k - 2]), kJoinGhostEnd));
        return out;
    }

    // `[BIN]` §31.4. The closed form is the open one with the front ghost
    // OVERWRITTEN by the last real point and a copy of the second appended --
    // so the window slides all the way round and every corner, including the
    // seam, is a real join.
    const StrokePoint seamIn = sub(pts[0], pts[k - 1]);
    const StrokePoint seamOut = sub(pts[1], pts[0]);
    const int seam = joinForCorner(seamIn, seamOut, r, params);

    out.push_back(real(pts[k - 1], kJoinWrap));
    out.push_back(real(pts[0], seam));
    for (std::size_t i = 1; i < k; ++i) {
        const StrokePoint in = sub(pts[i], pts[i - 1]);
        const StrokePoint outDir = sub(pts[(i + 1) % k], pts[i]);
        out.push_back(real(pts[i], joinForCorner(in, outDir, r, params)));
    }
    out.push_back(real(pts[0], seam));
    out.push_back(real(pts[1], kJoinWrap));
    return out;
}

std::size_t strokeLineInstanceCount(std::size_t pointCount) {
    return pointCount < 3 ? 0 : pointCount - 3;
}

bool strokeInstanceIsDrawn(const std::vector<StrokeLinePoint>& stream, std::size_t iid) {
    if (iid + 3 >= stream.size()) return false;
    return std::min(stream[iid + 1].join, stream[iid + 2].join) >= 0;
}

double strokeCapDistance(LineCap cap, double ov, double d, double r, double s,
                         bool alongPositive) {
    switch (cap) {
        case LineCap::Round:  return std::sqrt(ov * ov + d * d);
        case LineCap::Square: return std::max(ov, d);
        // `[BIN]` `max(r + ov - s, d)`: the stroke stops at the true end and
        // still leaves the one pixel the antialiasing ramp needs.
        case LineCap::Butt:   return std::max(r + ov - s, d);
        case LineCap::OutwardsTriangle: return d + ov;
        case LineCap::InwardsTriangle:  return std::max(r + ov - d, d);
        case LineCap::ForwardsTriangle:
            return std::max((alongPositive ? d : (r - d)) + ov, d);
        case LineCap::BackwardsTriangle:
            return std::max((alongPositive ? (r - d) : d) + ov, d);
    }
    return std::sqrt(ov * ov + d * d);
}

double strokeCoverageAt(const std::vector<StrokeLinePoint>& stream, std::size_t iid,
                        double px, double py, const StrokeParams& params) {
    if (!strokeInstanceIsDrawn(stream, iid)) return 0.0;
    const StrokeLinePoint& a = stream[iid + 1];
    const StrokeLinePoint& b = stream[iid + 2];

    StrokePoint p0{a.x, a.y}, p1{b.x, b.y};
    const StrokePoint whole{p1.x - p0.x, p1.y - p0.y};
    const double wholeLen = std::sqrt(whole.x * whole.x + whole.y * whole.y);
    if (wholeLen == 0.0) return 0.0;
    const StrokePoint dir{whole.x / wholeLen, whole.y / wholeLen};

    // `[BIN]` THE VERTEX STAGE SHORTENS A BUTT END BY ONE `recip_scale`, with a
    // guard of `len > recip_scale` (`stroke_lines_vertex`, `mod68.ll`). Without
    // it the cap arithmetic below runs from the wrong origin and the stroke
    // ends a pixel and a half long: `butt` computes `rEff + ov - s`, so the
    // signed distance is `ov - s`, and `s` is exactly the amount the vertex
    // stage took off. The two halves only produce the true end together.
    //
    // Left out of the first draft of this file, and found by the test that
    // asserts nothing is covered one unit past the end.
    const bool startCapped = stream[iid].join == kJoinGhostEnd;
    const bool endCapped = iid + 3 < stream.size() && stream[iid + 3].join == kJoinGhostEnd;
    const double s = params.recipScale;
    if (params.cap == LineCap::Butt && wholeLen > s) {
        if (startCapped) { p0.x += s * dir.x; p0.y += s * dir.y; }
        if (endCapped) { p1.x -= s * dir.x; p1.y -= s * dir.y; }
    }

    const StrokePoint seg{p1.x - p0.x, p1.y - p0.y};
    const double L = std::sqrt(seg.x * seg.x + seg.y * seg.y);
    if (L <= 0.0) return 0.0;

    const double u = (px - p0.x) * dir.x + (py - p0.y) * dir.y;
    const double d = std::fabs(-(px - p0.x) * dir.y + (py - p0.y) * dir.x);

    const double t = u / L;
    const double ts = std::clamp(t, 0.0, 1.0);
    const double r = a.radius + (b.radius - a.radius) * ts;

    // `[BIN]` With bit 11 clear the half-width has a floor of half a pixel and
    // the alpha is scaled by `r / rEff` -- a hairline keeps its ink by fading
    // rather than by disappearing between samples.
    const double rEff = params.hardCoverage ? r : std::max(r, s * 0.5);
    double alpha = params.hardCoverage ? 1.0 : (rEff == 0.0 ? 1.0 : r / rEff);
    alpha *= a.alpha + (b.alpha - a.alpha) * ts;

    double dist;
    if (ts != t) {
        const double ov = std::fabs(std::min(u, L - u));
        // `[BIN]` The cap arm runs only at an end FLAGGED as a cap. Here that
        // is the end whose neighbour is the ghost -- an interior corner is
        // covered by the join pass instead.
        const bool startIsEnd = t < 0.0;
        const bool flagged = startIsEnd ? startCapped : endCapped;
        dist = flagged ? strokeCapDistance(params.cap, ov, d, rEff, s, !startIsEnd)
                       : std::sqrt(ov * ov + d * d);
    } else {
        dist = d;
    }

    const double sd = dist - rEff;
    const double half = s * 0.5;
    if (params.hardCoverage) return sd < half ? alpha : 0.0;
    // `[BIN]` A smoothstep exactly one pixel wide, centred on the boundary.
    return alpha * smoothstep01((half - sd) / s);
}

}  // namespace rb
