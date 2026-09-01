// The target's path vertex stage, transcribed from the IR.
//
// `[BIN]` Every line below corresponds to instructions in
// `RenderBox.framework/…/default.metallib` mod58, `path_edges_vertex`. Doc 03 §7
// carries the measurement; this file carries the arithmetic, in the ORDER the
// target performs it -- a Bezier evaluated by the textbook formula and one
// evaluated the way the target factors it do not round identically, and the
// point of transcribing is to be the same, not to be equivalent.
//
// ON `precise`
// ------------
// Every result that the CPU oracle compares against is `precise`, which forbids
// the driver from contracting a multiply and an add into a fused one. Without
// it the two sides differ in the last bit on some drivers and not on others,
// and the gate would have to accept a tolerance -- which is exactly how a small
// real error hides. The target's own IR uses explicit `fma` calls; matching
// THAT would mean fusing on both sides, and the host and the driver do not agree
// on when they may. Forbidding fusion on both sides is the only version of this
// that is checkable.

struct CubicSegment {
    int count;        // the vertex index this segment STARTS at (exclusive prefix sum)
    float recip_n;    // 1/n for a segment; the header carries an int here instead
    vec2 p1;
    vec2 p2;
    vec2 p3;
};

// `[BIN]` A vertex that must not be drawn is placed OUTSIDE the clip volume
// rather than discarded: the whole primitive is clipped away, with no branch in
// the fragment stage and no discard anywhere.
const vec4 kClippedAway = vec4(-2.0, -2.0, 0.0, 1.0);

// `[BIN]` The subpath-break test, bit for bit: bitcast p1.x, mask the exponent,
// compare against all-ones. Inf or NaN both qualify.
bool rbIsSubpathBreak(vec2 p1) {
    return (floatBitsToInt(p1.x) & 0x7F800000) == 0x7F800000;
}

// `[BIN]` `%13 = shl iid, 5` then `%16 = %13 + (vid >> 1)`: THIRTY-TWO vertices
// per instance, and the pair (vid>>1, vid&1) is the two ends of one edge. An
// instance therefore spans several segments, which is exactly why the segment a
// vertex belongs to has to be searched for.
int rbVertexIndex(uint vid, uint iid) {
    return int(iid) * 32 + int(vid >> 1);
}

// The evaluation, factored the way the target factors it:
//
//   u3*p0 + (3*t*u)*(u*p1 + t*p2) + t3*p3
//
// and NOT u3*p0 + 3u2t*p1 + 3ut2*p2 + t3*p3, which is the same curve and a
// different float.
vec2 rbCubicAt(float t, vec2 p0, vec2 p1, vec2 p2, vec2 p3) {
    precise float u = 1.0 - t;
    precise float u2 = u * u;
    precise float u3 = u * u2;
    precise float k = (t * 3.0) * u;
    precise vec2 inner = vec2(u) * p1 + p2 * t;
    precise vec2 acc = vec2(u3) * p0 + vec2(k) * inner;
    precise float t3 = t * (t * t);
    precise vec2 result = vec2(t3) * p3 + acc;
    return result;
}

// `[BIN]` Path space to clip space:
//     world = m0*p.x + m1*p.y + m2          (path_matrix, a 2x3 affine)
//     ndc.x = world.x *  twoOverSize.x - 1.0
//     ndc.y = world.y * -twoOverSize.y + 1.0      -- Y is flipped here
//     ndc.z = depth * 2^-32                        (0x3DF0000000000000)
//
// `origin` sits in PathGlobals at offset 32 and this stage never reads it.
vec2 rbToWorld(vec2 p, vec2 m0, vec2 m1, vec2 m2) {
    precise vec2 inner = vec2(p.y) * m1 + m2;
    precise vec2 world = vec2(p.x) * m0 + inner;
    return world;
}

vec4 rbToClip(vec2 p, vec2 m0, vec2 m1, vec2 m2, vec2 twoOverSize, float depth) {
    precise vec2 world = rbToWorld(p, m0, m1, m2);
    precise float x = world.x * twoOverSize.x + -1.0;
    precise float y = world.y * -twoOverSize.y + 1.0;
    return vec4(x, y, depth * 2.3283064365386963e-10, 1.0);
}
