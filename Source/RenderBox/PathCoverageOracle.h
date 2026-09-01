#pragma once
// The three path fragment stages on the CPU, mirroring PathFragment.glsl.
//
// Same contract as PathVertexOracle: `float` throughout, the same order of
// operations, and bit-exact agreement everywhere a multiply and an add can
// reach. `rbExteriorShape` contains a DIVIDE, so the same 4-ULP allowance
// applies to whatever the divide feeds -- for the reason measured there, not as
// a convenience.
#include <cstdint>

namespace rb {

// What one fragment of the coverage pass is given. The names are the target's.
struct CoverageInput {
    float position[4]{0, 0, 0, 1};
    float line[4]{0, 0, 0, 0};   // path_y.x, path_y.y, path_slope, path_intercept
    float extra[4]{0, 0, 0, 0};  // path_value, frontFacing, scale, unused
    float dist[4]{0, 0, 0, 0};   // path_p.xy, path_p1p2.xy
};
static_assert(sizeof(CoverageInput) == 64, "four vec4, as the shader reads them");

enum class CoverageStage : std::uint32_t {
    Interior = 0,
    Exterior = 1,
    Distance = 2,
};

// The `half2` the target writes into its `coverage` render target. All three
// stages leave `.x` at zero.
struct Coverage {
    float x = 0;
    float y = 0;
    bool operator==(const Coverage&) const = default;
};

Coverage pathCoverage(const CoverageInput& in, CoverageStage stage);

// Interior is required to agree BIT FOR BIT: it is a select between two
// constants and nothing in it can round.
//
// Exterior gets 4 float ULP, because its trapezoid correction DIVIDES. Six
// hand-picked edges agreed bit for bit and a sweep of a thousand did not -- the
// claim was mine and the sweep is what corrected it.
//
// Distance gets an ABSOLUTE allowance of 2^-11, and it is DERIVED.
//
// Vulkan permits 3 ULP on `sqrt`. The stage narrows to half before its `max`,
// so that sub-ULP float disagreement is quantised into one half ULP of `dist` --
// and `dist` is saturated into [0, 1], whose coarsest half spacing is 2^-11.
// One half ULP of `dist` is therefore at most 2^-11 absolute, and `1 - dist`
// carries it through unchanged.
//
// NEITHER ulp count works, and both were tried against the sweep: after the
// result is narrowed the same absolute gap reads as one ULP near 0.998 and
// SIXTEEN near 0.048, because the half grid coarsens with magnitude. The error
// is absolute, so the measure is.
bool matches(const Coverage& a, const Coverage& b, CoverageStage stage,
             std::uint32_t maxHalfUlps = 1);

}  // namespace rb
