#pragma once
// A device image, its memory, and a view -- as one object, for the same reason
// Buffer keeps its allocation: every image this tower makes is exactly one
// allocation bound to exactly one image.
#include <cstdint>
#include <vector>

#include "Source/RenderBox/Buffer.h"
#include "Source/RenderBox/Device.h"

namespace rb {

// THE COVERAGE TARGET IS WIDER THAN THE TARGET'S, ON PURPOSE.
//
// `[BIN]` The coverage target the path fragments write is a `half2`, and until
// 2026-10-01 this was `VK_FORMAT_R16G16_SFLOAT` for that reason.
//
// `[ART]` The pass is additive: every edge adds its signed vertical extent to
// every pixel on its right, all the way to the target's right edge, and to the
// right of a closed path those contributions cancel to exactly zero -- in exact
// arithmetic. In a half each add is rounded, and how is the DEVICE's business:
// on the Radeon this was measured on the blend truncates toward zero, and a row
// of 26 edges ends at `7 x 2^-11` instead of zero. The resolve paints anything
// that is not exactly zero, so each layer left a smear of its fill from the
// art's right edge to the canvas edge -- alpha 1-2 of 255 where the chiclet's
// corner leaves the canvas transparent, a one-level tint inside it. Modelled
// row by row: truncation matches 412 of 412 rows of an isolated layer at 412 px,
// round-to-nearest 296.
//
// In a `float` the same sums are exact: each contribution is a difference of two
// `float` ordinates clipped to one pixel row, the partial sums stay below one in
// magnitude, and 24 bits hold them under any rounding mode. So the wide target
// is what makes the result the same on every device, which a half cannot be.
// The fragment's own arithmetic keeps its half narrowing (`PathFragment.glsl`),
// which is deterministic.
inline constexpr VkFormat kCoverageFormat = VK_FORMAT_R32G32_SFLOAT;

class Image {
public:
    // Nothing here forces the format -- it is a parameter, and the default is
    // the coverage target's.
    static Result<Image> create(Device& device, std::uint32_t width, std::uint32_t height,
                                VkFormat format = kCoverageFormat);

    Image() = default;
    ~Image();
    Image(Image&&) noexcept;
    Image& operator=(Image&&) noexcept;
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    VkImage handle() const { return image_; }
    VkImageView view() const { return view_; }
    VkFormat format() const { return format_; }
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }

private:
    void destroy();

    VkDevice device_ = VK_NULL_HANDLE;
    const DeviceApi* api_ = nullptr;   // this device's dispatch -- see Buffer.h
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
};

// One texel of the coverage target, decoded from half.
struct Texel {
    float x = 0;
    float y = 0;
};

// Copies the image back through a staging buffer and decodes the halves. Row
// major, width * height entries.
Result<std::vector<Texel>> readBack(Device& device, const Image& image);

}  // namespace rb
