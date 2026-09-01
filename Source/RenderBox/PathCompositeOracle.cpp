#include "Source/RenderBox/PathCompositeOracle.h"

namespace rb {

CompositeOut compositeFlat(std::uint32_t word2, std::uint32_t word3, const float (&colour)[4],
                           float shape, float depth) {
    CompositeOut o;
    const float alpha = colour[3] * shape;
    // `[BIN]` The coverage channel reports the INVERTED alpha when the bit is set,
    // while the colour keeps the un-inverted one. They are two numbers, not one.
    const float reported = (word3 & kWord3InvertAlpha) != 0u ? (1.0f - alpha) : alpha;

    float premultiplied[4];
    for (int i = 0; i < 4; ++i) premultiplied[i] = shape * colour[i];

    const bool broadcast = (word2 & kWord2BroadcastAlpha) != 0u;
    for (int i = 0; i < 4; ++i) o.colour[i] = broadcast ? premultiplied[3] : premultiplied[i];

    o.coverage[0] = reported;
    o.coverage[1] = 0.0f;

    // `[BIN]` The depth nudge tests the REPORTED alpha, not the raw one.
    constexpr float kCompositeEpsilon = 0.0010004043579101562f;
    const float d = reported < kCompositeEpsilon ? (depth + 1.0f) : depth;
    o.depth = d * 2.3283064365386963e-10f;
    return o;
}

}  // namespace rb
