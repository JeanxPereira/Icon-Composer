#include "Source/RenderBox/PathBuffer.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace rb {
namespace {

std::uint32_t bits(float f) {
    std::uint32_t u = 0;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

float fromBits(std::uint32_t u) {
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof f);
    return f;
}

void setPoint(float (&slot)[2], const icf::svg::Point& p) {
    slot[0] = static_cast<float>(p.x);
    slot[1] = static_cast<float>(p.y);
}

}  // namespace

bool isSubpathBreak(const CubicSegment& s) {
    // The target's own test, transcribed: bitcast p1.x, mask the exponent, and
    // compare against all-ones. Using std::isfinite instead would agree today and
    // is not what the shader does -- and this predicate exists so the CPU writer
    // and the GPU reader cannot disagree.
    return (bits(s.p1[0]) & 0x7F800000u) == 0x7F800000u;
}

void markSubpathBreak(CubicSegment& s) {
    s.p1[0] = std::numeric_limits<float>::quiet_NaN();
}

std::int32_t headerVertexCount(const CubicSegment& header) {
    std::int32_t n = 0;
    std::memcpy(&n, &header.recip_n, sizeof n);
    return n;
}

Result<PathBuffer> buildPathBuffer(const icf::svg::Path& path, BuildOptions options) {
    if (options.subdivisions < 1) {
        return std::unexpected(std::string("subdivisions must be at least 1, got ") +
                               std::to_string(options.subdivisions));
    }
    if (path.segments.empty()) {
        return std::unexpected(std::string("an empty path has no buffer"));
    }

    PathBuffer out;
    out.entries.emplace_back();  // the header, filled in at the end

    icf::svg::Point current{};
    icf::svg::Point subpathStart{};
    bool started = false;
    std::int32_t running = 0;

    // A line is written as a cubic with its controls on the chord, at a third and
    // two thirds. That is exact -- a cubic with collinear, evenly spaced controls
    // IS the segment -- and it keeps one entry shape in the buffer, which is what
    // the target's single `CubicSegment` type already tells us it wants.
    auto emitCubic = [&](const icf::svg::Point& c1, const icf::svg::Point& c2,
                         const icf::svg::Point& end) {
        CubicSegment s;
        setPoint(s.p1, c1);
        setPoint(s.p2, c2);
        setPoint(s.p3, end);
        // EXCLUSIVE, and the difference is not cosmetic: the shader computes
        // `local = vertexIndex - count` and feeds it to `t = recip_n * local`.
        // An inclusive sum makes `local` negative and every t wrong.
        s.count = running;                                          // convention 2
        running += options.subdivisions;
        s.recip_n = 1.0f / static_cast<float>(options.subdivisions);
        // A curve whose control happens to be non-finite would read as a subpath
        // break and silently stop being drawn. Refuse it at the writer instead.
        if (isSubpathBreak(s)) return false;
        out.entries.push_back(s);
        current = end;
        return true;
    };

    auto lineTo = [&](const icf::svg::Point& end) {
        const icf::svg::Point c1{current.x + (end.x - current.x) / 3.0,
                                 current.y + (end.y - current.y) / 3.0};
        const icf::svg::Point c2{current.x + 2.0 * (end.x - current.x) / 3.0,
                                 current.y + 2.0 * (end.y - current.y) / 3.0};
        return emitCubic(c1, c2, end);
    };

    // The edge an open subpath is missing, written where the subpath ends: at
    // the next move and at the end of the path.
    auto closeOpen = [&] {
        if (!options.closeOpenSubpaths || !started) return true;
        if (current.x == subpathStart.x && current.y == subpathStart.y) return true;
        return lineTo(subpathStart);
    };

    for (const auto& seg : path.segments) {
        switch (seg.kind) {
            case icf::svg::SegmentKind::Move: {
                if (!closeOpen()) {
                    return std::unexpected(std::string("a non-finite coordinate would read "
                                                       "as a subpath break"));
                }
                CubicSegment s;
                markSubpathBreak(s);                                 // convention 4
                setPoint(s.p3, seg.p[0]);                            // convention 3
                s.count = running;   // a break contributes no vertices, so the
                s.recip_n = 0.0f;    // prefix sum does not advance
                out.entries.push_back(s);
                current = seg.p[0];
                subpathStart = seg.p[0];
                started = true;
                break;
            }
            case icf::svg::SegmentKind::Line:
                if (!started) return std::unexpected(std::string("a line before any move"));
                if (!lineTo(seg.p[0])) {
                    return std::unexpected(std::string("a non-finite coordinate would read "
                                                       "as a subpath break"));
                }
                break;
            case icf::svg::SegmentKind::Cubic:
                if (!started) return std::unexpected(std::string("a curve before any move"));
                if (!emitCubic(seg.p[0], seg.p[1], seg.p[2])) {
                    return std::unexpected(std::string("a non-finite control point would read "
                                                       "as a subpath break"));
                }
                break;
            case icf::svg::SegmentKind::Close:
                if (!started) return std::unexpected(std::string("a close before any move"));
                // Only when it would move: a close on the point it started from is
                // a zero-length curve, and the subdivision vertices it would add
                // are vertices that draw nothing.
                if (current.x != subpathStart.x || current.y != subpathStart.y) {
                    if (!lineTo(subpathStart)) {
                        return std::unexpected(std::string("a non-finite coordinate would read "
                                                           "as a subpath break"));
                    }
                }
                break;
        }
    }
    if (!closeOpen()) {
        return std::unexpected(std::string("a non-finite coordinate would read "
                                           "as a subpath break"));
    }

    // Zero VERTICES, not zero entries: `M0 0 M5 5` writes two breaks and no
    // curve, so counting entries would call it drawable. A buffer whose header
    // says zero vertices makes the shader draw nothing, silently.
    if (running == 0) {
        return std::unexpected(std::string("the path has no drawable segment"));
    }

    // The header, per convention 1. `recip_n` carries an int through a float slot
    // because that is what the shader loads out of it.
    CubicSegment& header = out.entries.front();
    header.count = static_cast<std::int32_t>(out.entries.size() - 1);
    std::memcpy(&header.recip_n, &running, sizeof running);
    // The header's own p3 is where the path starts -- convention 3 -- so segment
    // one's p0 resolves without a special case in the shader.
    setPoint(header.p3, path.segments.front().p[0]);
    // ... and its p1 must NOT read as a break, or entry 1 inherits one.
    header.p1[0] = 0.0f;
    header.p1[1] = 0.0f;
    (void)fromBits;
    return out;
}

}  // namespace rb
