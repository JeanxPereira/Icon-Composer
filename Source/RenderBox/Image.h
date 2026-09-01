#pragma once
// A device image, its memory, and a view -- as one object, for the same reason
// Buffer keeps its allocation: every image this tower makes is exactly one
// allocation bound to exactly one image.
#include <cstdint>
#include <vector>

#include "Source/RenderBox/Buffer.h"
#include "Source/RenderBox/Device.h"

namespace rb {

class Image {
public:
    // `[BIN]` The coverage target the path fragments write is a `half2`, so the
    // default is the two-channel 16-bit float format. Nothing here forces it --
    // the format is a parameter, and the default is the target's.
    static Result<Image> create(Device& device, std::uint32_t width, std::uint32_t height,
                                VkFormat format = VK_FORMAT_R16G16_SFLOAT);

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
