#pragma once
// The path vertex stages on the CPU, mirroring path_probe.comp operation for
// operation.
//
// This exists to be the GATE. There is no pixel oracle for this project, so the
// question "does the shader compute what the target computes" cannot be answered
// by looking at a picture. It can be answered by computing the same thing twice,
// independently, and requiring the two to agree exactly.
//
// Two rules keep the mirror honest:
//   * `float` throughout, never `double`. Widening one side would make the two
//     disagree in the last bit for reasons that have nothing to do with a defect.
//   * the SAME ORDER of operations as the GLSL, including the factored Bezier.
//     A rearrangement that is algebraically identical is not bitwise identical,
//     and a gate that has to allow a tolerance stops catching the small errors.
//
// WHERE THE BITWISE COMPARISON STOPS, AND WHY
// -------------------------------------------
// Everything a multiply and an add can reach is compared EXACTLY. Two fields
// cannot be: `path_slope` is `dx / dy`, and `path_intercept` is derived from it.
//
// Vulkan does not require a correctly-rounded 32-bit divide -- the specification
// allows **2.5 ULP** for `OpFDiv`, and drivers spend that budget on a reciprocal
// approximation. Measured here on a Radeon RX 6750 XT: the slope came back one
// ULP from the host's, with every other field bit-identical. `precise` forbids
// contraction; it does not turn an approximate divide into an exact one, and
// there is no SPIR-V control that does.
//
// So the comparison is exact everywhere except downstream of that divide, where
// it is bounded at 4 ULP. That is not a tolerance chosen to make a test pass: it
// is the platform's own documented limit, and it is two orders of magnitude
// tighter than any mutation the sweep applies -- a wrong slope is wrong by
// percent, not by a bit.
#include <cstdint>
#include <vector>

#include "Source/RenderBox/PathBuffer.h"

namespace rb {

struct PathGlobals {
    float m0[2]{1.0f, 0.0f};
    float m1[2]{0.0f, 1.0f};
    float m2[2]{0.0f, 0.0f};
    float twoOverSize[2]{2.0f / 256.0f, 2.0f / 256.0f};
    float origin[2]{0.0f, 0.0f};  // the interior fan's apex
    float depth = 0.0f;
    float urx = 0.0f;             // the exterior quad's right edge
    float arg = 0.0f;             // not decoded
};
static_assert(sizeof(PathGlobals) == 52, "the target's PathGlobals is 52 bytes");

// `[BIN]` The three vertex passes of shader_path.metal, and their packings.
// Doc 03 §8 and §9 carry the measurement.
enum class PathPass : std::uint32_t {
    Edges = 0,     // iid*32 + (vid>>1), vid&1  -- one end of an edge
    Interior = 1,  // iid*21 + (vid>>2), vid&3  -- a triangle fanned from `origin`
    Exterior = 2,  // iid*13 + (vid>>2), vid&3  -- a quad reaching out to `urx`
};

// What one invocation of the probe produces. Only Exterior fills `line` and
// `value`; the other two leave them zero, exactly as the shader does.
struct ProbeVertex {
    float position[4]{0, 0, 0, 0};
    float line[4]{0, 0, 0, 0};   // path_y.x, path_y.y, path_slope, path_intercept
    float extra[4]{0, 0, 0, 0};  // path_value, then padding
    bool operator==(const ProbeVertex&) const = default;
};

// How many representable floats separate two values. NOT a relative epsilon:
// near zero a relative test accepts everything, and at large magnitudes it
// rejects a difference of one bit.
std::uint32_t ulpsApart(float a, float b);

// The comparison the gate uses: bit-identical everywhere except `line[2]` and
// `line[3]`, which are allowed `maxUlps` because they descend from a divide.
bool matches(const ProbeVertex& a, const ProbeVertex& b, std::uint32_t maxUlps = 4);
static_assert(sizeof(ProbeVertex) == 48, "three vec4, as the shader writes them");

// Is the buffer's header consistent with the segments behind it?
//
// THIS EXISTS BECAUSE A MISSING CHECK REBOOTED THE MACHINE. The header carries
// the vertex total as an int through a float slot (convention 1). A buffer that
// writes it as a FLOAT instead -- one of the sweep's own mutations -- makes
// `headerVertexCount` read 8.0f's bits as 1 090 519 040, and everything derived
// from it asks for a hundred gigabytes. The mutation was caught in the sense
// that the suite would have failed; it was not caught before the allocation.
//
// So the total is checked against the segments that produced it: for the last
// drawable segment, `total` must be exactly `count + round(1 / recip_n)`. A
// buffer that fails this is refused, and nothing is sized from it.
bool headerAgreesWithSegments(const PathBuffer& buffer);

// How many vertex ids one instance of a pass carries.
std::uint32_t verticesPerInstance(PathPass pass);
// How many segment indices one instance of a pass covers.
std::uint32_t indicesPerInstance(PathPass pass);
// Enough invocations to cover every vertex the buffer declares.
std::uint32_t invocationCount(const PathBuffer& buffer, PathPass pass);

// One invocation, exactly as path_probe.comp computes it.
ProbeVertex pathProbeVertex(const PathBuffer& buffer, const PathGlobals& globals, PathPass pass,
                            std::uint32_t index);

}  // namespace rb
