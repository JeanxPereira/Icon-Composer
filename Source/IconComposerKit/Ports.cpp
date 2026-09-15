#include "Source/IconComposerKit/Ports.h"

#include <cmath>

namespace ick {

std::vector<std::uint8_t> toRgba8(const std::vector<float>& in) {
    std::vector<std::uint8_t> out(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        float v = in[i];
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        out[i] = static_cast<std::uint8_t>(std::lround(v * 255.0f));
    }
    return out;
}

}  // namespace ick
