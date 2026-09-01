#pragma once
// The flat-fill composite on the CPU, mirroring PathComposite.glsl.
//
// `[BIN]` `RB::Shader::composite`, flat path only -- the blend path switches on
// a 56-case field of which six are decoded, and a transcription of it would be
// fifty stubs. Doc 03 section 16.
#include <cstdint>

namespace rb {

// The state bits this function reads, named by what it does with them.
enum CompositeBit : std::uint32_t {
    kWord3Blend = 1u << 0,       // blend instead of the flat premultiply
    kWord3InvertAlpha = 1u << 1,
    kWord2BroadcastAlpha = 1u << 19,
    kWord1CustomBlend = 1u << 30,
};

struct CompositeOut {
    float colour[4]{0, 0, 0, 0};
    float coverage[2]{0, 0};
    float depth = 0;
    bool operator==(const CompositeOut&) const = default;
};

CompositeOut compositeFlat(std::uint32_t word2, std::uint32_t word3, const float (&colour)[4],
                           float shape, float depth);

}  // namespace rb
