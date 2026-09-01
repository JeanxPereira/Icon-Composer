#include "Source/RenderBox/PathResolveOracle.h"

#include <cmath>

namespace rb {
namespace {

float saturate(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

}  // namespace

float accumulatorShape(std::uint32_t state, float coverage, const ShapeCurve& shape) {
    const std::uint32_t mode = (state >> kShapeModeShift) & 3u;
    const bool evenOdd = (state & kEvenOdd) != 0u;
    float result = coverage;

    if (mode == 1u) {
        const float a = std::fabs(coverage);
        if (evenOdd) {
            const float f = a - std::floor(a);
            const float whole = a - f;
            const bool even = (static_cast<std::uint32_t>(whole) & 1u) == 0u;
            result = even ? f : (1.0f - f);
        } else {
            result = saturate(a);
        }
    } else if (mode == 2u) {
        const float whole = std::floor(coverage);
        const float f = coverage - whole;
        const float inside = evenOdd
                                 ? static_cast<float>(static_cast<std::uint32_t>(std::fabs(whole)) & 1u)
                                 : static_cast<float>(whole != 0.0f);
        const std::uint32_t sub = (state >> kSubModeShift) & 3u;
        if (sub == 0u)      result = (inside != 0.0f) ? (1.0f - f) : 0.0f;
        else if (sub == 1u) result = (inside != 0.0f) ? 1.0f : f;
        else if (sub == 2u) result = (inside != 0.0f) ? (1.0f - f * 0.5f) : (f * 0.5f);
        else                result = inside;
    }

    constexpr float kShapeEpsilon = 0.0010004043579101562f;
    if ((state & kShapeCurve) != 0u && result >= kShapeEpsilon && result <= shape.x) {
        result = saturate((shape.x * result + shape.y) * result + shape.z);
    }
    return result;
}

}  // namespace rb
