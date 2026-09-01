#include "Source/RenderBox/PathVertexOracle.h"

#include <cmath>
#include <cstring>

namespace rb {
namespace {

struct Vec2 {
    float x = 0, y = 0;
};

// `std::fma` is NOT used here, and that is deliberate. glslc lowers GLSL's `fma`
// to an OpFma the driver may or may not contract, while `std::fma` on the host is
// always the fused, single-rounding form. Both sides are spelled as a plain
// multiply and add, and the GLSL side is `precise` so the driver may not fuse it.
Vec2 mad(const Vec2& a, const Vec2& b, const Vec2& c) {
    return {a.x * b.x + c.x, a.y * b.y + c.y};
}

Vec2 point(const float (&p)[2]) { return {p[0], p[1]}; }

bool isBreak(const float (&p1)[2]) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &p1[0], sizeof bits);
    return (bits & 0x7F800000u) == 0x7F800000u;
}

Vec2 cubicAt(float t, const Vec2& p0, const Vec2& p1, const Vec2& p2, const Vec2& p3) {
    const float u = 1.0f - t;
    const float u2 = u * u;
    const float u3 = u * u2;
    const float k = (t * 3.0f) * u;
    const Vec2 inner = mad({u, u}, p1, {p2.x * t, p2.y * t});
    const Vec2 acc = mad({u3, u3}, p0, {k * inner.x, k * inner.y});
    const float t3 = t * (t * t);
    return {t3 * p3.x + acc.x, t3 * p3.y + acc.y};
}

Vec2 toWorld(const Vec2& p, const PathGlobals& g) {
    const Vec2 inner = mad({p.y, p.y}, point(g.m1), point(g.m2));
    return mad({p.x, p.x}, point(g.m0), inner);
}

void toClip(float (&out)[4], const Vec2& p, const PathGlobals& g) {
    const Vec2 world = toWorld(p, g);
    out[0] = world.x * g.twoOverSize[0] + -1.0f;
    out[1] = world.y * -g.twoOverSize[1] + 1.0f;
    out[2] = g.depth * 2.3283064365386963e-10f;
    out[3] = 1.0f;
}

ProbeVertex clippedAway() {
    ProbeVertex v;
    v.position[0] = -2.0f;
    v.position[1] = -2.0f;
    v.position[2] = 0.0f;
    v.position[3] = 1.0f;
    return v;
}

// -1 when the vertex belongs to no drawable segment.
int findSegment(const PathBuffer& buffer, int vertexIndex) {
    if (buffer.entries.empty()) return -1;
    if (headerVertexCount(buffer.entries.front()) <= vertexIndex) return -1;
    int base = 1;
    int width = buffer.entries.front().count;
    while (width > 1) {
        const int halfWidth = width >> 1;
        const int mid = base + halfWidth;
        const bool goLeft = buffer.entries[static_cast<std::size_t>(mid)].count > vertexIndex;
        width = goLeft ? halfWidth : (width - halfWidth);
        base = goLeft ? base : mid;
    }
    if (isBreak(buffer.entries[static_cast<std::size_t>(base)].p1)) return -1;
    return base;
}

}  // namespace

std::uint32_t ulpsApart(float a, float b) {
    if (a == b) return 0;
    std::int32_t ia = 0, ib = 0;
    std::memcpy(&ia, &a, sizeof ia);
    std::memcpy(&ib, &b, sizeof ib);
    // Map the sign-magnitude encoding onto a monotonic integer, so that adjacent
    // floats are adjacent integers across zero too.
    if (ia < 0) ia = static_cast<std::int32_t>(0x80000000u) - ia;
    if (ib < 0) ib = static_cast<std::int32_t>(0x80000000u) - ib;
    const std::int64_t d = static_cast<std::int64_t>(ia) - static_cast<std::int64_t>(ib);
    const std::int64_t abs = d < 0 ? -d : d;
    return abs > 0xFFFFFFFFll ? 0xFFFFFFFFu : static_cast<std::uint32_t>(abs);
}

bool matches(const ProbeVertex& a, const ProbeVertex& b, std::uint32_t maxUlps) {
    for (int i = 0; i < 4; ++i) {
        if (std::memcmp(&a.position[i], &b.position[i], sizeof(float)) != 0) return false;
        if (std::memcmp(&a.extra[i], &b.extra[i], sizeof(float)) != 0) return false;
    }
    // path_y is not derived from the divide, so it is exact too.
    if (std::memcmp(&a.line[0], &b.line[0], sizeof(float)) != 0) return false;
    if (std::memcmp(&a.line[1], &b.line[1], sizeof(float)) != 0) return false;
    return ulpsApart(a.line[2], b.line[2]) <= maxUlps &&
           ulpsApart(a.line[3], b.line[3]) <= maxUlps;
}

std::uint32_t verticesPerInstance(PathPass pass) {
    switch (pass) {
        case PathPass::Edges: return 64;     // 32 edges, two ends each
        case PathPass::Interior: return 84;  // 21 indices, four vertices each
        case PathPass::Exterior: return 52;  // 13 indices, four vertices each
    }
    return 0;
}

std::uint32_t indicesPerInstance(PathPass pass) {
    switch (pass) {
        case PathPass::Edges: return 32;
        case PathPass::Interior: return 21;
        case PathPass::Exterior: return 13;
    }
    return 0;
}

bool headerAgreesWithSegments(const PathBuffer& buffer) {
    if (buffer.entries.size() < 2) return false;
    const std::int32_t total = headerVertexCount(buffer.entries.front());
    if (total <= 0) return false;
    if (buffer.entries.front().count != static_cast<std::int32_t>(buffer.entries.size() - 1)) {
        return false;
    }
    // The last DRAWABLE entry: a trailing break contributes nothing and starts
    // where the total already is.
    for (std::size_t i = buffer.entries.size(); i-- > 1;) {
        const CubicSegment& s = buffer.entries[i];
        if (isSubpathBreak(s)) continue;
        if (!(s.recip_n > 0.0f) || s.recip_n > 1.0f) return false;
        const float n = 1.0f / s.recip_n;
        const std::int32_t rounded = static_cast<std::int32_t>(n + 0.5f);
        return s.count + rounded == total;
    }
    return false;
}

std::uint32_t invocationCount(const PathBuffer& buffer, PathPass pass) {
    // Never size an allocation from a number the buffer merely claims.
    if (!headerAgreesWithSegments(buffer)) return 0;
    const std::int32_t total = buffer.vertexCount();
    if (total <= 0) return 0;
    const std::uint32_t stride = indicesPerInstance(pass);
    const std::uint32_t instances = (static_cast<std::uint32_t>(total) + stride - 1) / stride;
    return instances * verticesPerInstance(pass);
}

ProbeVertex pathProbeVertex(const PathBuffer& buffer, const PathGlobals& g, PathPass pass,
                            std::uint32_t index) {
    ProbeVertex out = clippedAway();
    if (buffer.entries.empty()) return out;

    const std::uint32_t perInstance = verticesPerInstance(pass);
    const std::uint32_t stride = indicesPerInstance(pass);
    const std::uint32_t vid = index % perInstance;
    const std::uint32_t iid = index / perInstance;

    const int vertexIndex = static_cast<int>(iid) * static_cast<int>(stride) +
                            static_cast<int>(pass == PathPass::Edges ? (vid >> 1) : (vid >> 2));
    const std::uint32_t corner = (pass == PathPass::Edges) ? (vid & 1u) : (vid & 3u);

    // The fan apex exists whether or not the index is in range.
    if (pass == PathPass::Interior && corner == 0u) {
        toClip(out.position, point(g.origin), g);
        return out;
    }

    const int base = findSegment(buffer, vertexIndex);
    if (base < 0) return out;

    const CubicSegment& seg = buffer.entries[static_cast<std::size_t>(base)];
    const Vec2 p0 = point(buffer.entries[static_cast<std::size_t>(base - 1)].p3);
    const int local = vertexIndex - seg.count;
    const float t0 = seg.recip_n * static_cast<float>(local);
    const float t1 = seg.recip_n * static_cast<float>(local + 1);

    if (pass == PathPass::Edges) {
        const Vec2 p = cubicAt(corner == 0u ? t0 : t1, p0, point(seg.p1), point(seg.p2),
                               point(seg.p3));
        toClip(out.position, p, g);
        return out;
    }

    const Vec2 pa = cubicAt(t0, p0, point(seg.p1), point(seg.p2), point(seg.p3));
    const Vec2 pb = cubicAt(t1, p0, point(seg.p1), point(seg.p2), point(seg.p3));

    if (pass == PathPass::Interior) {
        toClip(out.position, corner == 1u ? pa : pb, g);
        return out;
    }

    const Vec2 a = toWorld(pa, g);
    const Vec2 b = toWorld(pb, g);
    const Vec2 d{b.x - a.x, b.y - a.y};
    if (!(std::fabs(d.y) > 1.0e-4f)) return out;

    const float slope = d.x / d.y;
    const float intercept = a.y * (-slope) + a.x;
    const bool upward = d.y > 0.0f;
    const float pathY0 = upward ? a.y : b.y;
    const float pathY1 = upward ? b.y : a.y;

    const float x = (corner == 0u || corner == 3u) ? ((a.x < b.x ? a.x : b.x) - 0.5f)
                                                   : (g.urx + 0.5f);
    const float y = (corner < 2u) ? (pathY0 - 0.5f) : (pathY1 + 0.5f);

    out.position[0] = x * g.twoOverSize[0] + -1.0f;
    out.position[1] = y * -g.twoOverSize[1] + 1.0f;
    out.position[2] = g.depth * 2.3283064365386963e-10f;
    out.position[3] = 1.0f;
    out.line[0] = pathY0;
    out.line[1] = pathY1;
    out.line[2] = slope;
    out.line[3] = intercept;
    out.extra[0] = upward ? 1.0f : -1.0f;
    return out;
}

}  // namespace rb
