#version 450
// The exterior pass, as a real vertex shader.
//
// The arithmetic is the SAME FILE the compute probe runs -- PathVertex.glsl --
// so the stage that draws and the stage that is gated against the CPU oracle
// cannot drift apart. This shader is the plumbing around it and nothing else.
#include "PathVertex.glsl"

layout(std430, binding = 0) readonly buffer Segments {
    CubicSegment segments[];
};

layout(push_constant) uniform Globals {
    vec2 m0;
    vec2 m1;
    vec2 m2;
    vec2 twoOverSize;
    vec2 origin;
    float depth;
    float urx;
    float arg;
    // A origem do buffer na grade do canvas. O estagio de vertice NAO a le --
    // ele projeta no canvas, como o render cheio -- mas o bloco de push e um
    // so, e os dois estagios tem que declara-lo igual.
    int gridOriginX;
    int gridOriginY;
} g;

layout(location = 0) out vec2 vPathY;
layout(location = 1) out float vSlope;
layout(location = 2) out float vIntercept;
layout(location = 3) out float vValue;

// `[BIN]` The target packs four vertices per index and lets the draw call turn
// them into a quad. Here they are drawn as a TRIANGLE LIST -- six vertices per
// index, mapped back onto the target's four corners -- because a strip of 13
// consecutive quads would stitch them into each other. The topology is ours; the
// corner each vertex IS is the target's.
// The target's four corners are in RING order -- 0 lower-left, 1 lower-right,
// 2 upper-right, 3 upper-left -- so a triangle LIST splits them on the 0-2
// diagonal: (0,1,2) and (0,2,3).
//
// (0,1,2) and (1,2,3) is the TRIANGLE STRIP convention, and it is wrong here:
// measured, the two halves OVERLAP and leave the other half of the quad
// unrasterised. The varyings were correct and the coverage arithmetic was
// correct; half the quad simply never had a fragment.
const int kCorner[6] = int[6](0, 1, 2, 0, 2, 3);

void main() {
    int local = gl_VertexIndex % 6;
    int index = gl_VertexIndex / 6;
    uint corner = uint(kCorner[local]);
    // The transcribed function reads (vid >> 2) for the index and (vid & 3) for
    // the corner, so the vid handed to it is rebuilt to say exactly that.
    uint vid = uint(index) * 4u + corner;

    int vertexIndex = int(gl_InstanceIndex) * 13 + index;

    gl_Position = kClippedAway;
    vPathY = vec2(0.0);
    vSlope = 0.0;
    vIntercept = 0.0;
    vValue = 0.0;

    int total = floatBitsToInt(segments[0].recip_n);
    if (total <= vertexIndex) return;

    int base = 1;
    int width = segments[0].count;
    while (width > 1) {
        int halfWidth = width >> 1;
        int mid = base + halfWidth;
        bool goLeft = segments[mid].count > vertexIndex;
        width = goLeft ? halfWidth : (width - halfWidth);
        base = goLeft ? base : mid;
    }
    CubicSegment seg = segments[base];
    if (rbIsSubpathBreak(seg.p1)) return;

    vec2 p0 = segments[base - 1].p3;
    int step = vertexIndex - seg.count;
    float t0 = seg.recip_n * float(step);
    float t1 = seg.recip_n * float(step + 1);
    vec2 pa = rbCubicAt(t0, p0, seg.p1, seg.p2, seg.p3);
    vec2 pb = rbCubicAt(t1, p0, seg.p1, seg.p2, seg.p3);

    vec2 a = rbToWorld(pa, g.m0, g.m1, g.m2);
    vec2 b = rbToWorld(pb, g.m0, g.m1, g.m2);
    vec2 d = b - a;
    if (!(abs(d.y) > 1.0e-4)) return;

    precise float slope = d.x / d.y;
    precise float intercept = a.y * (-slope) + a.x;
    bool upward = d.y > 0.0;
    vec2 pathY = upward ? vec2(a.y, b.y) : vec2(b.y, a.y);

    precise float x = (corner == 0u || corner == 3u) ? (min(a.x, b.x) - 0.5) : (g.urx + 0.5);
    precise float y = (corner < 2u) ? (pathY.x - 0.5) : (pathY.y + 0.5);

    // THE Y TERM IS NOT THE TARGET'S, AND IT MUST NOT BE.
    //
    // `rbToClip` transcribes `y * -two_over_size.y + 1`, which is correct for
    // METAL: its NDC y points UP, so the flip converts a y-down world into it,
    // and Metal's top-left fragment origin then puts world y back in agreement
    // with `position.y` -- which is what the fragment stage compares `path_y`
    // against.
    //
    // VULKAN'S NDC Y POINTS DOWN. The world is already y-down, so the same flip
    // inverts it: measured, world y 8..20 landed on screen rows 24..12 while the
    // fragment clipped against path_y 8..20, and the render came out sheared and
    // displaced. The coverage VALUES were right everywhere the quad landed --
    // the quad was in the wrong place.
    //
    // So the transcription keeps the target's arithmetic, gated bit for bit
    // against the CPU oracle, and the adaptation to the other API lives here,
    // where the API is.
    gl_Position = vec4(x * g.twoOverSize.x + -1.0, y * g.twoOverSize.y - 1.0,
                       g.depth * 2.3283064365386963e-10, 1.0);
    vPathY = pathY;
    vSlope = slope;
    vIntercept = intercept;
    vValue = upward ? 1.0 : -1.0;
}
