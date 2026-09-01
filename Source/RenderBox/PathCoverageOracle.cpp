#include "Source/RenderBox/PathCoverageOracle.h"

#include <cmath>
#include <cstring>

#include "Source/RenderBox/PathVertexOracle.h"  // ulpsApart

namespace rb {
namespace {

float saturate(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// The half round-trip the target performs before its `max`. GLSL's
// packHalf2x16 does the same narrowing on the other side.
float narrowToHalf(float v) {
    // Round-to-nearest-even through the 16-bit format, via the compiler's own
    // conversion -- writing the bit twiddling by hand here would be a second
    // implementation to keep in step with the driver's.
    const _Float16 h = static_cast<_Float16>(v);
    return static_cast<float>(h);
}

}  // namespace

Coverage pathCoverage(const CoverageInput& in, CoverageStage stage) {
    Coverage out;
    if (stage == CoverageStage::Interior) {
        out.y = in.extra[1] != 0.0f ? 1.0f : -1.0f;
        return out;
    }

    if (stage == CoverageStage::Exterior) {
        const float px = static_cast<float>(static_cast<int>(in.position[0]));
        const float py = static_cast<float>(static_cast<int>(in.position[1]));

        const float yLow = in.line[0], yHigh = in.line[1];
        const float slope = in.line[2], intercept = in.line[3];

        const float y0 = py > yLow ? py : yLow;
        const float pyPlus = py + 1.0f;
        const float y1 = pyPlus < yHigh ? pyPlus : yHigh;

        const float base = intercept - px;
        const float xAt0 = y0 * slope + base;
        const float xAt1 = y1 * slope + base;
        const float lo = slope < 0.0f ? xAt1 : xAt0;
        const float hi = slope < 0.0f ? xAt0 : xAt1;
        const float heldLo = saturate(lo);
        const float heldHi = saturate(hi);

        const float dx = hi - lo;
        const float cover = saturate(y1 - y0);
        float area = cover * (1.0f - heldHi);

        if (dx != 0.0f) {
            const float mid = heldLo * 0.5f + heldHi * 0.5f - lo;
            const float k = (cover * mid) / dx;
            area = area - heldLo * k + heldHi * k;
        }
        out.y = in.extra[0] * area;
        return out;
    }

    const float px = in.dist[0], py = in.dist[1];
    const float dx = in.dist[2], dy = in.dist[3];
    const float t = saturate((px * dx + py * dy) * in.extra[2]);
    const float vx = dx * -t + px;
    const float vy = dy * -t + py;
    const float d = saturate(std::sqrt(vx * vx + vy * vy));
    const float narrowed = narrowToHalf(d);
    const float floored = narrowed > 0.0015010833740234375f ? narrowed
                                                            : 0.0015010833740234375f;
    // `[BIN]` The subtraction is `fsub half`, not float: the target has already
    // narrowed by this point and stays narrow. Doing it in float here would put
    // the two sides on different grids for a reason that is not a defect.
    out.y = narrowToHalf(1.0f - floored);
    return out;
}

bool matches(const Coverage& a, const Coverage& b, CoverageStage stage,
             std::uint32_t maxHalfUlps) {
    if (std::memcmp(&a.x, &b.x, sizeof(float)) != 0) return false;
    if (stage == CoverageStage::Interior) {
        // A select between two constants. Nothing here can round.
        return std::memcmp(&a.y, &b.y, sizeof(float)) == 0;
    }
    if (stage == CoverageStage::Exterior) {
        // The trapezoid correction DIVIDES by the edge's horizontal extent, and
        // Vulkan allows 2.5 ULP there. Six hand-picked edges agreed bit for bit
        // and a sweep of a thousand did not -- which is what the sweep is for.
        return ulpsApart(a.y, b.y) <= 4;
    }
    // The allowance is ABSOLUTE, and it is derived rather than chosen.
    //
    // The disagreement is one half ULP of `dist`, and `dist` is saturated into
    // [0, 1]. The coarsest half spacing in that range is 2^-11, at [0.5, 1). So
    // one half ULP of `dist` is AT MOST 2^-11 in absolute terms, and that
    // survives `1 - dist` unchanged.
    //
    // Neither ULP count works here, and both were tried: after the result is
    // narrowed the same absolute gap reads as ONE ULP near 0.998 and SIXTEEN
    // near 0.048, because the half grid coarsens with magnitude. The error is
    // absolute; the measure has to be too.
    const float gap = a.y - b.y;
    return (gap < 0 ? -gap : gap) <= static_cast<float>(maxHalfUlps) * 0.00048828125f;
}

}  // namespace rb
