#include "Source/RenderBox/ChicletShape.h"

#include <algorithm>
#include <cmath>

namespace rb {
namespace {

using icf::svg::Path;
using icf::svg::Point;
using icf::svg::Segment;
using icf::svg::SegmentKind;

// `[BIN]` `0x15ECB8` lane 0, read at `0x7F688`. It is `extent - 1.0`: the slack
// test asks whether the edge has room for what the continuous corner wants
// BEYOND what a circular one would have taken.
constexpr float kSlack = 0.528664947f;

// `[BIN]` The blend, `0x7F6DC`-`0x7F6F0`. Pairs of floats at `0x15EC80` and
// `0x15EC88`, then two scalars at `0x15ECBC` and `0x15ECC0`.
constexpr float kBlendExtentBase = 1.0f;          // `0x15EC88` lane 0
constexpr float kBlendExtentSlope = 0.528664947f; // `0x15EC80` lane 0
constexpr float kBlendControlBase = 0.959999979f; // `0x15EC88` lane 1
constexpr float kBlendControlSlope = 0.128490031f;// `0x15EC80` lane 1
constexpr float kBlendShoulderBase = 0.819999993f;   // `0x15ECBC`
constexpr float kBlendShoulderSlope = 0.0484070182f; // `0x15ECC0`

// `[BIN]` The middle cubic's four control coordinates, `0x15EBD0`-`0x15EC00`,
// read at `0x7F758`, `0x7F774`, `0x7F78C` and `0x7F7A4`. They are symmetric
// about the corner's diagonal, which is why only two distinct numbers appear.
constexpr double kNear = 0.074911400675773621;
constexpr double kMid1 = 0.16906000673770905;
constexpr double kMid2 = 0.37282401323318481;
constexpr double kFar = 0.63149398565292358;

struct Vec {
    double x = 0;
    double y = 0;
};

Point at(Vec c, Vec e, double se, Vec f, double sf) {
    return Point{c.x + e.x * se + f.x * sf, c.y + e.y * se + f.y * sf};
}

void lineTo(Path& p, Point q) { p.segments.push_back(Segment{SegmentKind::Line, {q, {}, {}}}); }

void cubicTo(Path& p, Point a, Point b, Point c) {
    p.segments.push_back(Segment{SegmentKind::Cubic, {a, b, c}});
}

// One corner: entered along the edge whose inward direction from the corner is
// `e` (radius `re`, params `pe`), left along the edge `f` (radius `rf`, params
// `pf`). Transcribed from the first corner block, `0x7F704`-`0x7F860`; the three
// blocks that follow it differ only in the sign of the constant pool they read
// (`0x15EC10`-`0x15EC48` is `0x15EBD0`-`0x15EC00` negated), which is this
// function's `e` and `f`.
void emitCorner(Path& p, Vec c, Vec e, double re, const ContinuousCornerParams& pe, Vec f,
                double rf, const ContinuousCornerParams& pf) {
    // The line up to where the corner begins. The subpath's own start is handled
    // by the caller, so this is a lineto in every case -- four of them, which is
    // the count the target emits.
    lineTo(p, at(c, e, pe.extent * re, f, 0.0));

    cubicTo(p, at(c, e, pe.control * re, f, 0.0), at(c, e, pe.shoulder * re, f, 0.0),
            at(c, e, kFar * re, f, kNear * rf));

    cubicTo(p, at(c, e, kMid2 * re, f, kMid1 * rf), at(c, e, kMid1 * re, f, kMid2 * rf),
            at(c, e, kNear * re, f, kFar * rf));

    cubicTo(p, at(c, e, 0.0, f, pf.shoulder * rf), at(c, e, 0.0, f, pf.control * rf),
            at(c, e, 0.0, f, pf.extent * rf));
}

Point cubicAt(Point p0, const Segment& s, double t) {
    const double u = 1.0 - t;
    const double a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
    return Point{a * p0.x + b * s.p[0].x + c * s.p[1].x + d * s.p[2].x,
                 a * p0.y + b * s.p[0].y + c * s.p[1].y + d * s.p[2].y};
}

// The outline as a closed polygon. The chiclet is one convex loop, so flattening
// it costs a fixed number of points and needs no adaptive test.
std::vector<Point> flatten(const Path& path, int perCubic) {
    std::vector<Point> out;
    Point cur{};
    for (const Segment& s : path.segments) {
        switch (s.kind) {
            case SegmentKind::Move:
                cur = s.p[0];
                out.push_back(cur);
                break;
            case SegmentKind::Line:
                cur = s.p[0];
                out.push_back(cur);
                break;
            case SegmentKind::Cubic:
                for (int i = 1; i <= perCubic; ++i) {
                    out.push_back(cubicAt(cur, s, static_cast<double>(i) / perCubic));
                }
                cur = s.p[2];
                break;
            case SegmentKind::Close:
                break;
        }
    }
    return out;
}

}  // namespace

ContinuousCornerParams continuousCornerParams(double edge, double rNear, double rFar) {
    const float sum = static_cast<float>(rNear) + static_cast<float>(rFar);
    const float room = std::fabs(static_cast<float>(edge)) - sum;
    const float t = room / (sum * kSlack);

    ContinuousCornerParams p;   // the canonical triple is the default
    if (t >= 1.0f) return p;    // `0x7F6C4`: `b.pl` takes the canonical branch

    // `0x7F6C8`-`0x7F6D8`: a NaN survives neither comparison and lands on the
    // canonical clamp of 1 in the target too, which `std::fmin`/`fmax` would not
    // reproduce -- so the order of these two tests is the target's.
    float u = (t < 0.0f) ? 0.0f : t;
    if (u > 1.0f) u = 1.0f;

    p.extent = static_cast<double>(kBlendExtentBase + kBlendExtentSlope * u);
    p.control = static_cast<double>(kBlendControlBase + kBlendControlSlope * u);
    p.shoulder = static_cast<double>(kBlendShoulderBase + kBlendShoulderSlope * u);
    return p;
}

icf::svg::Path continuousRoundedRect(double x, double y, double w, double h, double rx,
                                     double ry) {
    // `[BIN]` `RB::clamp_corner_radii` runs before `add_rounded_rect`
    // (`0x80DEC`): a radius never eats more than half its edge. The chiclet is
    // nowhere near that, but a caller with a smaller canvas can be.
    rx = std::clamp(rx, 0.0, std::fabs(w) * 0.5);
    ry = std::clamp(ry, 0.0, std::fabs(h) * 0.5);

    const double x0 = std::min(x, x + w), x1 = std::max(x, x + w);
    const double y0 = std::min(y, y + h), y1 = std::max(y, y + h);

    // Each EDGE has its own slack, and the corner reads the one belonging to the
    // edge it is leaving along -- `0x7F7BC`-`0x7F824` recomputes it between the
    // second and the third cubic of the same corner.
    const ContinuousCornerParams vert = continuousCornerParams(y1 - y0, ry, ry);
    const ContinuousCornerParams horz = continuousCornerParams(x1 - x0, rx, rx);

    const Vec up{0, -1}, down{0, 1}, left{-1, 0}, right{1, 0};

    Path p;
    // `0x7F634`: the seam sits halfway between the two points where the right
    // edge's corners begin -- the plain radii, not the extents.
    const double seam = 0.5 * ((y0 + ry) + (y1 - ry));
    p.segments.push_back(Segment{SegmentKind::Move, {Point{x1, seam}, {}, {}}});

    emitCorner(p, Vec{x1, y1}, up, ry, vert, left, rx, horz);     // bottom right
    emitCorner(p, Vec{x0, y1}, right, rx, horz, up, ry, vert);    // bottom left
    emitCorner(p, Vec{x0, y0}, down, ry, vert, right, rx, horz);  // top left
    emitCorner(p, Vec{x1, y0}, left, rx, horz, down, ry, vert);   // top right

    lineTo(p, Point{x1, seam});
    p.segments.push_back(Segment{SegmentKind::Close, {}});
    return p;
}

double chicletCornerRadius(std::uint32_t size) {
    return 266.24 * static_cast<double>(size) / 1024.0;
}

std::vector<float> chicletCoverage(std::uint32_t size) {
    std::vector<float> cov(static_cast<std::size_t>(size) * size, 0.0f);
    if (size == 0) return cov;

    const double r = chicletCornerRadius(size);
    const Path outline = continuousRoundedRect(0.0, 0.0, size, size, r, r);

    const int perCubic = std::clamp(static_cast<int>(std::ceil(r * 0.5)), 8, 96);
    const std::vector<Point> poly = flatten(outline, perCubic);
    if (poly.size() < 3) return cov;

    // A scanline sweep with four sub-rows and EXACT horizontal coverage: the
    // vertical error is bounded by a quarter pixel on a curve that is nearly
    // horizontal only at the top and bottom of the shape, and the horizontal
    // term -- where the corner actually is steep -- carries no error at all.
    constexpr int kSubRows = 4;
    const double w = 1.0 / kSubRows;

    std::vector<double> xs;
    for (std::uint32_t py = 0; py < size; ++py) {
        float* row = cov.data() + static_cast<std::size_t>(py) * size;
        for (int s = 0; s < kSubRows; ++s) {
            const double sy = py + (s + 0.5) * w;
            xs.clear();
            for (std::size_t i = 0, n = poly.size(); i < n; ++i) {
                const Point& a = poly[i];
                const Point& b = poly[(i + 1) % n];
                if ((a.y <= sy) == (b.y <= sy)) continue;
                xs.push_back(a.x + (sy - a.y) * (b.x - a.x) / (b.y - a.y));
            }
            if (xs.size() < 2) continue;
            std::sort(xs.begin(), xs.end());
            for (std::size_t k = 0; k + 1 < xs.size(); k += 2) {
                const double lo = std::max(xs[k], 0.0);
                const double hi = std::min(xs[k + 1], static_cast<double>(size));
                if (hi <= lo) continue;
                const auto first = static_cast<std::uint32_t>(lo);
                const auto last = static_cast<std::uint32_t>(
                    std::min(std::ceil(hi) - 1.0, static_cast<double>(size) - 1.0));
                for (std::uint32_t px = first; px <= last && px < size; ++px) {
                    const double overlap =
                        std::min(hi, px + 1.0) - std::max(lo, static_cast<double>(px));
                    if (overlap > 0.0) row[px] += static_cast<float>(overlap * w);
                }
            }
        }
    }
    for (float& c : cov) c = std::clamp(c, 0.0f, 1.0f);
    return cov;
}

void clipToChiclet(std::vector<float>& acc, std::uint32_t size) {
    const std::size_t texels = static_cast<std::size_t>(size) * size;
    if (acc.size() < texels * 4) return;
    const std::vector<float> cov = chicletCoverage(size);
    for (std::size_t i = 0; i < texels; ++i) {
        const float c = cov[i];
        if (c >= 1.0f) continue;
        for (int k = 0; k < 4; ++k) acc[i * 4 + k] *= c;
    }
}

}  // namespace rb
