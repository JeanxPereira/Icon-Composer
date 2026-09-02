#include "Source/RenderBox/MipPyramid.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "Source/RenderBox/Compute.h"

// The reduction, the LOD and the trilinear fetch all run out of one module, so
// the CPU oracle and the GPU it is gated against cannot be built from different
// sources. The `.inc` is produced by glslc at BUILD time, so a shader that does
// not compile is a build failure and not a test failing for another reason.
static const std::uint32_t kMipSpirv[] =
#include "mip_reduce.comp.inc"
    ;

namespace rb {

namespace {

// The three-stage control block, byte for byte what `mip_reduce.comp` declares.
struct MipControl {
    std::uint32_t count = 0;
    std::uint32_t stage = 0;
    std::uint32_t srcW = 0;
    std::uint32_t srcH = 0;
    std::uint32_t dstW = 0;
    std::uint32_t dstH = 0;
    std::uint32_t levelCount = 0;
    std::uint32_t queryBase = 0;
};

// The level table that heads every input buffer: `uvec4 levels[16]`, 256 bytes.
constexpr std::size_t kLevelTableBytes = kMaxMipLevels * 4 * sizeof(std::uint32_t);

std::uint16_t halfBitsOf(float v) {
    std::uint32_t x = 0;
    std::memcpy(&x, &v, sizeof x);
    const std::uint32_t sign = (x >> 16) & 0x8000u;
    const std::uint32_t rawExp = (x >> 23) & 0xFFu;
    std::uint32_t mantissa = x & 0x7FFFFFu;

    if (rawExp == 0xFFu) {
        // Infinity keeps its sign; a NaN stays a NaN rather than becoming one.
        return static_cast<std::uint16_t>(sign | 0x7C00u |
                                         (mantissa ? (0x200u | (mantissa >> 13)) : 0u));
    }
    std::int32_t exponent = static_cast<std::int32_t>(rawExp) - 127 + 15;
    if (exponent >= 0x1F) return static_cast<std::uint16_t>(sign | 0x7C00u);
    if (exponent <= 0) {
        // Subnormal, or the rounding that lifts one back to the smallest normal.
        if (exponent < -10) return static_cast<std::uint16_t>(sign);
        mantissa |= 0x800000u;  // the implicit bit, now explicit
        const std::uint32_t shift = static_cast<std::uint32_t>(14 - exponent);
        const std::uint32_t kept = mantissa >> shift;
        const std::uint32_t dropped = mantissa & ((1u << shift) - 1u);
        const std::uint32_t halfway = 1u << (shift - 1);
        const bool up = dropped > halfway || (dropped == halfway && (kept & 1u));
        return static_cast<std::uint16_t>(sign | (kept + (up ? 1u : 0u)));
    }
    std::uint32_t kept = mantissa >> 13;
    const std::uint32_t dropped = mantissa & 0x1FFFu;
    if (dropped > 0x1000u || (dropped == 0x1000u && (kept & 1u))) {
        ++kept;
        if (kept == 0x400u) {
            kept = 0;
            ++exponent;
            if (exponent >= 0x1F) return static_cast<std::uint16_t>(sign | 0x7C00u);
        }
    }
    return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(exponent) << 10) | kept);
}

float floatOfHalfBits(std::uint16_t h) {
    const std::uint32_t sign = static_cast<std::uint32_t>(h & 0x8000u) << 16;
    const std::uint32_t exponent = (h >> 10) & 0x1Fu;
    const std::uint32_t mantissa = h & 0x3FFu;
    std::uint32_t bits = 0;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            // A half subnormal is a float NORMAL, so it has to be renormalised
            // rather than shifted into place. Its value is `mantissa * 2^-24`;
            // shifting the leading bit up to 2^10 costs `10 - k` shifts, so the
            // float exponent field lands at `k + 103`.
            std::uint32_t m = mantissa;
            std::int32_t e = 0;
            while ((m & 0x400u) == 0) {
                m <<= 1;
                --e;
            }
            m &= 0x3FFu;
            bits = sign | (static_cast<std::uint32_t>(e + 113) << 23) | (m << 13);
        }
    } else if (exponent == 0x1F) {
        bits = sign | 0x7F800000u | (mantissa << 13);
    } else {
        bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
    }
    float out = 0.0f;
    std::memcpy(&out, &bits, sizeof out);
    return out;
}

}  // namespace

float quantizeToHalf(float v) { return floatOfHalfBits(halfBitsOf(v)); }

void layerUv(const MultiLevelLayer& layer, float x, float y, float (&uv)[2]) {
    for (int k = 0; k < 2; ++k) {
        const float inner = y * layer.m[1][k] + layer.m[2][k];
        const float t = x * layer.m[0][k] + inner;
        uv[k] = std::min(std::max(t, layer.m[3][k]), layer.m[4][k]);
    }
}

float backdropLod(float radius, float bias, float cap) {
    const float floored = std::max(kBackdropRadiusFloor, quantizeToHalf(radius));
    const float logged = quantizeToHalf(std::log2(floored));
    return std::min(logged - bias, cap);
}

float backdropLod(const MultiLevelLayer& layer, float radius) {
    return backdropLod(radius, layer.m[5][0], layer.m[5][1]);
}

void unpremultiplyBackdrop(const float (&rgba)[4], float (&rgb)[3]) {
    const float a = std::max(rgba[3], kBackdropRadiusFloor);
    for (int k = 0; k < 3; ++k) rgb[k] = rgba[k] / a;
}

std::uint32_t nextMipExtent(std::uint32_t extent) { return extent > 1 ? extent / 2 : 1; }

std::uint32_t mipLevelCount(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) return 0;
    std::uint32_t levels = 1;
    while (width > 1 || height > 1) {
        width = nextMipExtent(width);
        height = nextMipExtent(height);
        ++levels;
    }
    return levels;
}

int mipReduceTaps(std::uint32_t srcExtent, std::uint32_t dstIndex, int (&offset)[3],
                  float (&weight)[3]) {
    const int i = static_cast<int>(dstIndex);
    offset[0] = offset[1] = offset[2] = 0;
    weight[0] = weight[1] = weight[2] = 0.0f;
    if (srcExtent <= 1) {
        weight[0] = 1.0f;
        return 1;
    }
    if ((srcExtent & 1u) == 0u) {
        offset[0] = 2 * i;
        offset[1] = 2 * i + 1;
        weight[0] = weight[1] = 0.5f;
        return 2;
    }
    const float n = static_cast<float>(srcExtent >> 1);
    const float total = static_cast<float>(srcExtent);
    offset[0] = 2 * i;
    offset[1] = 2 * i + 1;
    offset[2] = 2 * i + 2;
    weight[0] = (n - static_cast<float>(i)) / total;
    weight[1] = n / total;
    weight[2] = (static_cast<float>(i) + 1.0f) / total;
    return 3;
}

MipLevel reduceMipLevel(const MipLevel& source) {
    MipLevel out;
    out.width = nextMipExtent(source.width);
    out.height = nextMipExtent(source.height);
    if (source.width == 0 || source.height == 0) return out;
    out.rgba.assign(static_cast<std::size_t>(out.width) * out.height * 4, 0.0f);

    const std::size_t stride = source.width;
    for (std::uint32_t y = 0; y < out.height; ++y) {
        int oy[3];
        float wy[3];
        const int ny = mipReduceTaps(source.height, y, oy, wy);
        for (std::uint32_t x = 0; x < out.width; ++x) {
            int ox[3];
            float wx[3];
            const int nx = mipReduceTaps(source.width, x, ox, wx);

            const std::size_t refAt =
                (static_cast<std::size_t>(oy[0]) * stride + static_cast<std::size_t>(ox[0])) * 4;
            float acc[4] = {0, 0, 0, 0};
            for (int j = 0; j < ny; ++j) {
                for (int i = 0; i < nx; ++i) {
                    const float w = wx[i] * wy[j];
                    const std::size_t at = (static_cast<std::size_t>(oy[j]) * stride +
                                            static_cast<std::size_t>(ox[i])) *
                                           4;
                    for (int c = 0; c < 4; ++c) {
                        acc[c] += w * (source.rgba[at + c] - source.rgba[refAt + c]);
                    }
                }
            }
            const std::size_t at = (static_cast<std::size_t>(y) * out.width + x) * 4;
            for (int c = 0; c < 4; ++c) out.rgba[at + c] = source.rgba[refAt + c] + acc[c];
        }
    }
    return out;
}

MipPyramid MipPyramid::build(const float* premultipliedRgba, std::uint32_t width,
                             std::uint32_t height) {
    MipPyramid pyramid;
    if (!premultipliedRgba || width == 0 || height == 0) return pyramid;

    MipLevel base;
    base.width = width;
    base.height = height;
    base.rgba.assign(premultipliedRgba,
                     premultipliedRgba + static_cast<std::size_t>(width) * height * 4);
    pyramid.levels_.push_back(std::move(base));

    while (pyramid.levels_.back().width > 1 || pyramid.levels_.back().height > 1) {
        pyramid.levels_.push_back(reduceMipLevel(pyramid.levels_.back()));
        if (pyramid.levels_.size() >= kMaxMipLevels) break;
    }
    return pyramid;
}

const MipLevel& MipPyramid::level(std::size_t index) const {
    static const MipLevel kEmpty;
    if (index >= levels_.size()) return kEmpty;
    return levels_[index];
}

void MipPyramid::sampleLevel(std::size_t index, float u, float v, float (&out)[4]) const {
    for (int c = 0; c < 4; ++c) out[c] = 0.0f;
    if (index >= levels_.size()) return;
    const MipLevel& l = levels_[index];
    if (l.width == 0 || l.height == 0) return;

    const float fx = u * static_cast<float>(l.width) - 0.5f;
    const float fy = v * static_cast<float>(l.height) - 0.5f;
    const float bx = std::floor(fx);
    const float by = std::floor(fy);
    const float tx = fx - bx;
    const float ty = fy - by;

    const int hi = static_cast<int>(l.width) - 1;
    const int vi = static_cast<int>(l.height) - 1;
    const int x0 = std::min(std::max(static_cast<int>(bx), 0), hi);
    const int x1 = std::min(std::max(static_cast<int>(bx) + 1, 0), hi);
    const int y0 = std::min(std::max(static_cast<int>(by), 0), vi);
    const int y1 = std::min(std::max(static_cast<int>(by) + 1, 0), vi);

    const std::size_t w = l.width;
    const std::size_t a00 = (static_cast<std::size_t>(y0) * w + x0) * 4;
    const std::size_t a10 = (static_cast<std::size_t>(y0) * w + x1) * 4;
    const std::size_t a01 = (static_cast<std::size_t>(y1) * w + x0) * 4;
    const std::size_t a11 = (static_cast<std::size_t>(y1) * w + x1) * 4;
    for (int c = 0; c < 4; ++c) {
        const float top = l.rgba[a00 + c] + (l.rgba[a10 + c] - l.rgba[a00 + c]) * tx;
        const float bottom = l.rgba[a01 + c] + (l.rgba[a11 + c] - l.rgba[a01 + c]) * tx;
        out[c] = top + (bottom - top) * ty;
    }
}

void MipPyramid::sample(float lod, float u, float v, float (&out)[4]) const {
    for (int c = 0; c < 4; ++c) out[c] = 0.0f;
    if (levels_.empty()) return;
    const std::size_t last = levels_.size() - 1;
    const float clamped = std::min(std::max(lod, 0.0f), static_cast<float>(last));
    const float lo = std::floor(clamped);
    const float f = clamped - lo;
    const std::size_t i0 = static_cast<std::size_t>(lo);
    const std::size_t i1 = std::min(i0 + 1, last);

    float a[4], b[4];
    sampleLevel(i0, u, v, a);
    sampleLevel(i1, u, v, b);
    for (int c = 0; c < 4; ++c) out[c] = a[c] + (b[c] - a[c]) * f;
}

// ---- the GPU side --------------------------------------------------------

namespace {

// A host-visible storage buffer holding `bytes`, zero-filled first so the level
// table a stage ignores is still defined rather than whatever was in memory.
Result<Buffer> stagingBuffer(Device& device, std::size_t bytes) {
    auto buffer = Buffer::create(device, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!buffer) return std::unexpected(buffer.error());
    if (!buffer->mapped()) return std::unexpected(std::string("the storage buffer is not mapped"));
    std::memset(buffer->mapped(), 0, bytes);
    return buffer;
}

}  // namespace

Result<MipPyramid> MipPyramid::buildOnGpu(Device& device, const float* premultipliedRgba,
                                          std::uint32_t width, std::uint32_t height) {
    if (!device.valid()) return std::unexpected(std::string("no device"));
    if (!premultipliedRgba || width == 0 || height == 0) {
        return std::unexpected(std::string("an empty image has no pyramid"));
    }

    auto pass = ComputePass::create(device, kMipSpirv, sizeof kMipSpirv, sizeof(MipControl));
    if (!pass) return std::unexpected(pass.error());

    MipPyramid pyramid;
    MipLevel base;
    base.width = width;
    base.height = height;
    base.rgba.assign(premultipliedRgba,
                     premultipliedRgba + static_cast<std::size_t>(width) * height * 4);
    pyramid.levels_.push_back(std::move(base));

    while (pyramid.levels_.back().width > 1 || pyramid.levels_.back().height > 1) {
        if (pyramid.levels_.size() >= kMaxMipLevels) break;
        const MipLevel& source = pyramid.levels_.back();

        MipLevel out;
        out.width = nextMipExtent(source.width);
        out.height = nextMipExtent(source.height);
        const std::size_t dstTexels = static_cast<std::size_t>(out.width) * out.height;

        const std::size_t inBytes = kLevelTableBytes + source.rgba.size() * sizeof(float);
        auto input = stagingBuffer(device, inBytes);
        if (!input) return std::unexpected(input.error());
        std::memcpy(static_cast<std::uint8_t*>(input->mapped()) + kLevelTableBytes,
                    source.rgba.data(), source.rgba.size() * sizeof(float));

        auto output = stagingBuffer(device, dstTexels * 4 * sizeof(float));
        if (!output) return std::unexpected(output.error());

        MipControl control;
        control.count = static_cast<std::uint32_t>(dstTexels);
        control.stage = 0;
        control.srcW = source.width;
        control.srcH = source.height;
        control.dstW = out.width;
        control.dstH = out.height;
        control.levelCount = 1;
        auto ran = pass->run(device, *input, *output, &control, sizeof control, control.count);
        if (!ran) return std::unexpected(ran.error());

        out.rgba.assign(dstTexels * 4, 0.0f);
        std::memcpy(out.rgba.data(), output->mapped(), out.rgba.size() * sizeof(float));
        pyramid.levels_.push_back(std::move(out));
    }
    return pyramid;
}

Result<std::vector<float>> backdropLodOnGpu(Device& device,
                                            const std::vector<std::array<float, 3>>& queries) {
    if (!device.valid()) return std::unexpected(std::string("no device"));
    if (queries.empty()) return std::vector<float>{};

    auto pass = ComputePass::create(device, kMipSpirv, sizeof kMipSpirv, sizeof(MipControl));
    if (!pass) return std::unexpected(pass.error());

    const std::size_t inBytes = kLevelTableBytes + queries.size() * 4 * sizeof(float);
    auto input = stagingBuffer(device, inBytes);
    if (!input) return std::unexpected(input.error());
    auto* payload = reinterpret_cast<float*>(static_cast<std::uint8_t*>(input->mapped()) +
                                             kLevelTableBytes);
    for (std::size_t i = 0; i < queries.size(); ++i) {
        payload[i * 4 + 0] = queries[i][0];
        payload[i * 4 + 1] = queries[i][1];
        payload[i * 4 + 2] = queries[i][2];
        payload[i * 4 + 3] = 0.0f;
    }

    auto output = stagingBuffer(device, queries.size() * 4 * sizeof(float));
    if (!output) return std::unexpected(output.error());

    MipControl control;
    control.count = static_cast<std::uint32_t>(queries.size());
    control.stage = 1;
    auto ran = pass->run(device, *input, *output, &control, sizeof control, control.count);
    if (!ran) return std::unexpected(ran.error());

    std::vector<float> raw(queries.size() * 4, 0.0f);
    std::memcpy(raw.data(), output->mapped(), raw.size() * sizeof(float));
    std::vector<float> out(queries.size(), 0.0f);
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = raw[i * 4];
    return out;
}

Result<std::vector<std::array<float, 4>>> sampleOnGpu(
    Device& device, const MipPyramid& pyramid,
    const std::vector<std::array<float, 3>>& queries) {
    if (!device.valid()) return std::unexpected(std::string("no device"));
    if (pyramid.empty()) return std::unexpected(std::string("an empty pyramid"));
    if (pyramid.levelCount() > kMaxMipLevels) {
        return std::unexpected(std::string("the level table holds ") +
                               std::to_string(kMaxMipLevels) + " levels and the pyramid has " +
                               std::to_string(pyramid.levelCount()));
    }
    if (queries.empty()) return std::vector<std::array<float, 4>>{};

    auto pass = ComputePass::create(device, kMipSpirv, sizeof kMipSpirv, sizeof(MipControl));
    if (!pass) return std::unexpected(pass.error());

    // The table first, then every level's texels back to back, then the queries.
    std::vector<std::uint32_t> table(kMaxMipLevels * 4, 0);
    std::size_t texelBase = 0;  // in vec4 units, from the start of data[]
    for (std::size_t i = 0; i < pyramid.levelCount(); ++i) {
        const MipLevel& l = pyramid.level(i);
        table[i * 4 + 0] = l.width;
        table[i * 4 + 1] = l.height;
        table[i * 4 + 2] = static_cast<std::uint32_t>(texelBase);
        texelBase += static_cast<std::size_t>(l.width) * l.height;
    }
    const std::size_t queryBase = texelBase;
    const std::size_t dataVec4s = texelBase + queries.size();
    const std::size_t inBytes = kLevelTableBytes + dataVec4s * 4 * sizeof(float);

    auto input = stagingBuffer(device, inBytes);
    if (!input) return std::unexpected(input.error());
    auto* bytes = static_cast<std::uint8_t*>(input->mapped());
    std::memcpy(bytes, table.data(), kLevelTableBytes);
    auto* payload = reinterpret_cast<float*>(bytes + kLevelTableBytes);
    std::size_t at = 0;
    for (std::size_t i = 0; i < pyramid.levelCount(); ++i) {
        const MipLevel& l = pyramid.level(i);
        std::memcpy(payload + at * 4, l.rgba.data(), l.rgba.size() * sizeof(float));
        at += static_cast<std::size_t>(l.width) * l.height;
    }
    for (std::size_t i = 0; i < queries.size(); ++i) {
        payload[(queryBase + i) * 4 + 0] = queries[i][0];
        payload[(queryBase + i) * 4 + 1] = queries[i][1];
        payload[(queryBase + i) * 4 + 2] = queries[i][2];
        payload[(queryBase + i) * 4 + 3] = 0.0f;
    }

    auto output = stagingBuffer(device, queries.size() * 4 * sizeof(float));
    if (!output) return std::unexpected(output.error());

    MipControl control;
    control.count = static_cast<std::uint32_t>(queries.size());
    control.stage = 2;
    control.levelCount = static_cast<std::uint32_t>(pyramid.levelCount());
    control.queryBase = static_cast<std::uint32_t>(queryBase);
    auto ran = pass->run(device, *input, *output, &control, sizeof control, control.count);
    if (!ran) return std::unexpected(ran.error());

    std::vector<std::array<float, 4>> out(queries.size());
    std::memcpy(out.data(), output->mapped(), out.size() * 4 * sizeof(float));
    return out;
}

}  // namespace rb
