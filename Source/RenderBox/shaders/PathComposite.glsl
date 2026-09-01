// Colour: the shape's alpha becomes a painted pixel.
//
// `[BIN]` `RB::Shader::composite(ShaderState, half4 colour, half shape, float depth,
// half4 dst, half2 coverage, constant float*)`, in `shader_accumulator.metal`.
//
// WHAT IS HERE AND WHAT IS NOT
// ----------------------------
// The function has two paths, chosen by WORD 3 BIT 0:
//
//   clear → premultiply the colour by the shape and write it. That is the flat
//           fill, and it is transcribed below.
//   set   → run the blend, which switches on the 56-case mode field. Six of
//           those 56 are decoded (doc 03 section 15), so transcribing it would
//           be fifty stubs wearing a function's name. It is NOT here.
//
// `[BIN]` Four state bits are named by what this function does with them:
//
//     word 3 bit 0   blend, rather than the flat premultiply
//     word 3 bit 1   invert the alpha: 1 - a
//     word 2 bit 19  broadcast alpha across rgb, instead of keeping the colour
//     word 1 bit 30  use `custom_blend`, a STITCHABLE function -- the same
//                    mechanism `glassBackground_v1` is exposed through

struct CompositeOut {
    vec4 colour;
    vec2 coverage;
    float depth;
};

// `[BIN]` The depth nudge: a shape below the epsilon is pushed one step back, so
// a fragment that paints nothing does not win the depth test against one that
// does. The same 2^-32 scaling as the vertex stage.
const float kCompositeEpsilon = 0.0010004043579101562;  // 0xH1419

// `word1` is not a parameter: the flat path never reads it. It matters only on
// the blend path, which is not transcribed here.
CompositeOut rbCompositeFlat(uint word2, uint word3, vec4 colour, float shape, float depth) {
    CompositeOut o;
    precise float alpha = colour.a * shape;
    // word 3 bit 1: the alpha the COVERAGE channel reports is inverted, while the
    // colour keeps the un-inverted one. They are not the same number.
    float reported = ((word3 & 2u) != 0u) ? (1.0 - alpha) : alpha;

    precise vec4 premultiplied = vec4(shape) * colour;
    // word 2 bit 19: the target takes alpha in every channel rather than colour.
    o.colour = ((word2 & 524288u) != 0u) ? vec4(premultiplied.a) : premultiplied;
    o.coverage = vec2(reported, 0.0);

    float d = (reported < kCompositeEpsilon) ? (depth + 1.0) : depth;
    o.depth = d * 2.3283064365386963e-10;
    return o;
}
