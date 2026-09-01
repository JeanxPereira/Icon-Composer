#pragma once
// The path vertex stage on the CPU, mirroring PathVertex.glsl operation for
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
#include <cstdint>
#include <vector>

#include "Source/RenderBox/PathBuffer.h"

namespace rb {

struct PathGlobals {
    float m0[2]{1.0f, 0.0f};
    float m1[2]{0.0f, 1.0f};
    float m2[2]{0.0f, 0.0f};
    float twoOverSize[2]{2.0f / 256.0f, 2.0f / 256.0f};
    float origin[2]{0.0f, 0.0f};  // in the target's struct; this stage never reads it
    float depth = 0.0f;
    float urx = 0.0f;             // not decoded
    float arg = 0.0f;             // not decoded
};
static_assert(sizeof(PathGlobals) == 52, "the target's PathGlobals is 52 bytes");

struct ClipPosition {
    float x = 0, y = 0, z = 0, w = 0;
    bool operator==(const ClipPosition&) const = default;
};

// One vertex of the edges pass, exactly as PathVertex.glsl computes it.
ClipPosition pathEdgeVertex(const PathBuffer& buffer, const PathGlobals& globals,
                            std::uint32_t vid, std::uint32_t iid);

// How many vertex ids a buffer needs to cover every vertex it declares: 64 ids
// per instance, and an instance carries 32 of them.
std::uint32_t vertexIdCount(const PathBuffer& buffer);

}  // namespace rb
