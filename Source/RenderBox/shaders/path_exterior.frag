#version 450
// The exterior fragment, as a real fragment shader. The arithmetic is the same
// file the coverage probe runs -- PathFragment.glsl -- so what draws and what is
// gated cannot drift apart.
#include "PathFragment.glsl"

layout(location = 0) in vec2 vPathY;
layout(location = 1) in float vSlope;
layout(location = 2) in float vIntercept;
layout(location = 3) in float vValue;

// `[BIN]` The target writes `coverage`, a half2 at location 1, and leaves .x at
// zero. Here it is location 0 because it is the only attachment; the CHANNELS
// are the target's.
layout(location = 0) out vec2 coverage;

void main() {
    coverage = vec2(0.0, rbExteriorShape(gl_FragCoord, vPathY, vSlope, vIntercept, vValue));
}
