#pragma once
// The fill rule on the CPU, mirroring PathResolve.glsl.
//
// `[BIN]` `RB::Shader::accumulator_shape`. Doc 03 section 14 carries the
// measurement, including the four state fields it names.
#include <cstdint>

namespace rb {

// The named bits, so a caller writes intent rather than a magic number.
enum StateBit : std::uint32_t {
    kShapeModeShift = 6,   // bits 6-7
    kSubModeShift = 8,     // bits 8-9
    kEvenOdd = 1u << 10,   // clear = non-zero
    kShapeCurve = 1u << 11,
};

// `[BIN]` `shape` is the half4 the target passes alongside: x is both the
// threshold and the quadratic's leading coefficient, y and z the rest of it.
struct ShapeCurve {
    float x = 0, y = 0, z = 0, w = 0;
};

float accumulatorShape(std::uint32_t state, float coverage, const ShapeCurve& shape);

}  // namespace rb
