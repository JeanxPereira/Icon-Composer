#include "Source/RenderBox/StrokeRender.h"

#include <algorithm>
#include <cmath>

namespace rb {
namespace {

StrokePoint cubicAt(const icf::svg::Point& p0, const icf::svg::Point& c0,
                    const icf::svg::Point& c1, const icf::svg::Point& p1, double t) {
    const double u = 1.0 - t;
    const double a = u * u * u, b = 3.0 * u * u * t, c = 3.0 * u * t * t, d = t * t * t;
    return {a * p0.x + b * c0.x + c * c1.x + d * p1.x,
            a * p0.y + b * c0.y + c * c1.y + d * p1.y};
}

}  // namespace

std::vector<FlatSubpath> flattenForStroke(const icf::svg::Path& path, int subdivisions) {
    const int n = std::max(1, subdivisions);
    std::vector<FlatSubpath> out;
    icf::svg::Point cur{0.0, 0.0};
    icf::svg::Point start{0.0, 0.0};
    bool open = false;

    auto push = [&](StrokePoint p) {
        if (!open) {
            out.push_back({});
            open = true;
        }
        out.back().points.push_back(p);
    };

    for (const icf::svg::Segment& s : path.segments) {
        switch (s.kind) {
            case icf::svg::SegmentKind::Move:
                // A `Move` ENDS the current subpath without closing it. The two
                // are different: an unclosed subpath gets caps, a closed one
                // gets a join at the seam.
                open = false;
                cur = start = s.p[0];
                push({cur.x, cur.y});
                break;
            case icf::svg::SegmentKind::Line:
                push({s.p[0].x, s.p[0].y});
                cur = s.p[0];
                break;
            case icf::svg::SegmentKind::Cubic:
                for (int i = 1; i <= n; ++i) {
                    const StrokePoint p =
                        cubicAt(cur, s.p[0], s.p[1], s.p[2], double(i) / n);
                    push(p);
                }
                cur = s.p[2];
                break;
            case icf::svg::SegmentKind::Close:
                if (open && !out.empty()) out.back().closed = true;
                open = false;
                cur = start;
                break;
        }
    }
    // A subpath of fewer than two points draws nothing, and carrying it would
    // make the instance counts lie.
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](const FlatSubpath& f) { return f.points.size() < 2; }),
              out.end());
    return out;
}

std::vector<float> rasteriseStroke(const icf::svg::Shape& shape,
                                   const StrokePlacement& placement,
                                   std::uint32_t width, std::uint32_t height,
                                   int subdivisions, const StrokeParams& base,
                                   std::int32_t originX, std::int32_t originY) {
    if (shape.stroke.kind == icf::svg::PaintKind::None) return {};
    if (shape.strokeWidth <= 0.0) return {};

    StrokeParams params = base;
    // The coverage is computed in PIXELS, so one pixel is one unit and the
    // width crosses over with the map's scale.
    params.recipScale = 1.0;
    params.width = shape.strokeWidth * placement.scale;
    if (params.width <= 0.0) return {};

    const auto subpaths = flattenForStroke(shape.path, subdivisions);
    if (subpaths.empty()) return {};

    std::vector<float> cov(static_cast<std::size_t>(width) * height, 0.0f);
    const double half = params.width * 0.5 + 1.0;  // the ramp needs a pixel

    for (const FlatSubpath& sp : subpaths) {
        std::vector<StrokePoint> mapped;
        mapped.reserve(sp.points.size());
        for (const StrokePoint& p : sp.points) {
            mapped.push_back({placement.m0[0] * p.x + placement.m1[0] * p.y + placement.m2[0],
                              placement.m0[1] * p.x + placement.m1[1] * p.y + placement.m2[1]});
        }
        const auto stream = strokePointStream(mapped, sp.closed, params);
        const std::size_t instances = strokeLineInstanceCount(stream.size());

        for (std::size_t iid = 0; iid < instances; ++iid) {
            if (!strokeInstanceIsDrawn(stream, iid)) continue;
            const StrokeLinePoint& a = stream[iid + 1];
            const StrokeLinePoint& b = stream[iid + 2];

            // Only the box this segment can possibly touch. Scanning the whole
            // target per segment is what makes the naive version unusable on a
            // path with two thousand of them.
            //
            // A caixa e o BUFFER, e a amostragem e ABSOLUTA: o grampo abaixo so
            // corta o retangulo, e o pixel que esta dentro do buffer recebe
            // exatamente a mesma cobertura do render cheio.
            const double x0 = std::min(a.x, b.x) - half, x1 = std::max(a.x, b.x) + half;
            const double y0 = std::min(a.y, b.y) - half, y1 = std::max(a.y, b.y) + half;
            const long bx0 = originX, by0 = originY;
            const long bx1 = originX + static_cast<long>(width) - 1;
            const long by1 = originY + static_cast<long>(height) - 1;
            const long px0 = std::max<long>(bx0, static_cast<long>(std::floor(x0)));
            const long py0 = std::max<long>(by0, static_cast<long>(std::floor(y0)));
            const long px1 = std::min<long>(bx1, static_cast<long>(std::ceil(x1)));
            const long py1 = std::min<long>(by1, static_cast<long>(std::ceil(y1)));

            for (long y = py0; y <= py1; ++y) {
                for (long x = px0; x <= px1; ++x) {
                    const double c = strokeCoverageAt(stream, iid, x + 0.5, y + 0.5, params);
                    if (c <= 0.0) continue;
                    float& dst =
                        cov[static_cast<std::size_t>(y - by0) * width + (x - bx0)];
                    // MAX, not sum. Two segments of one stroke overlap at every
                    // join, and adding would show the seam as a bright line --
                    // the same reason the target draws joins as their own
                    // primitive rather than letting the quads accumulate.
                    dst = std::max(dst, static_cast<float>(c));
                }
            }
        }
    }
    return cov;
}

}  // namespace rb
