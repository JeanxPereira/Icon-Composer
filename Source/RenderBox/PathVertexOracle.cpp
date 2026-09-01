#include "Source/RenderBox/PathVertexOracle.h"

#include <cstring>

namespace rb {
namespace {

struct Vec2 {
    float x = 0, y = 0;
};

Vec2 operator*(const Vec2& a, float s) { return {a.x * s, a.y * s}; }

// `std::fma` is NOT used here, and that is deliberate. glslc lowers GLSL's `fma`
// to an OpFma the driver may or may not contract, while `std::fma` on the host is
// always the fused, single-rounding form. Spelling both sides as a plain multiply
// and add is the only way to make them agree without guessing what the driver
// did -- and the GLSL side is compiled with contraction off for the same reason.
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
    const Vec2 inner = mad({u, u}, p1, p2 * t);
    const Vec2 acc = mad({u3, u3}, p0, Vec2{k * inner.x, k * inner.y});
    const float t3 = t * (t * t);
    return {t3 * p3.x + acc.x, t3 * p3.y + acc.y};
}

}  // namespace

std::uint32_t vertexIdCount(const PathBuffer& buffer) {
    const std::int32_t total = buffer.vertexCount();
    if (total <= 0) return 0;
    const std::uint32_t instances = (static_cast<std::uint32_t>(total) + 31u) / 32u;
    return instances * 64u;
}

ClipPosition pathEdgeVertex(const PathBuffer& buffer, const PathGlobals& g,
                            std::uint32_t vid, std::uint32_t iid) {
    // The clipped-away position, which is what an undrawable vertex gets.
    ClipPosition clipped{-2.0f, -2.0f, 0.0f, 1.0f};
    if (buffer.entries.empty()) return clipped;

    const int vertexIndex = static_cast<int>(iid) * 32 + static_cast<int>(vid >> 1);
    const std::uint32_t side = vid & 1u;

    const std::int32_t total = headerVertexCount(buffer.entries.front());
    if (total <= vertexIndex) return clipped;

    int base = 1;
    int width = buffer.entries.front().count;
    while (width > 1) {
        const int half = width >> 1;
        const int mid = base + half;
        const bool goLeft = buffer.entries[static_cast<std::size_t>(mid)].count > vertexIndex;
        width = goLeft ? half : (width - half);
        base = goLeft ? base : mid;
    }

    const CubicSegment& seg = buffer.entries[static_cast<std::size_t>(base)];
    if (isBreak(seg.p1)) return clipped;

    const Vec2 p0 = point(buffer.entries[static_cast<std::size_t>(base - 1)].p3);
    const int local = vertexIndex - seg.count;
    const float t = seg.recip_n * static_cast<float>(local + static_cast<int>(side));
    const Vec2 p = cubicAt(t, p0, point(seg.p1), point(seg.p2), point(seg.p3));

    const Vec2 m0 = point(g.m0), m1 = point(g.m1), m2 = point(g.m2);
    const Vec2 inner = mad({p.y, p.y}, m1, m2);
    const Vec2 world = mad({p.x, p.x}, m0, inner);

    ClipPosition out;
    out.x = world.x * g.twoOverSize[0] + -1.0f;
    out.y = world.y * -g.twoOverSize[1] + 1.0f;
    out.z = g.depth * 2.3283064365386963e-10f;
    out.w = 1.0f;
    return out;
}

}  // namespace rb
