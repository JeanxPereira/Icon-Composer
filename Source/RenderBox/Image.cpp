#include "Source/RenderBox/Image.h"

#include <cstring>

#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
#include <immintrin.h>
#define RB_HAVE_F16C 1
#endif

namespace rb {

namespace {

// Half to float is EXACT -- every half is a float -- so the vector instruction
// and the scalar `_Float16` cast produce the same bits, and the choice between
// them is speed only. The scalar loop was 0.13 s of the same 1.67 s render.
void halvesToFloatsScalar(const std::uint16_t* src, float* dst, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        _Float16 h = 0;
        std::memcpy(&h, &src[i], sizeof h);
        dst[i] = static_cast<float>(h);
    }
}

#ifdef RB_HAVE_F16C
__attribute__((target("f16c,avx"))) void halvesToFloatsF16c(const std::uint16_t* src,
                                                              float* dst, std::size_t n) {
    std::size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        const __m128i h = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
        _mm256_storeu_ps(dst + i, _mm256_cvtph_ps(h));
    }
    halvesToFloatsScalar(src + i, dst + i, n - i);
}
#endif

void halvesToFloats(const std::uint16_t* src, float* dst, std::size_t n) {
#ifdef RB_HAVE_F16C
    static const bool f16c = __builtin_cpu_supports("f16c") && __builtin_cpu_supports("avx");
    if (f16c) {
        halvesToFloatsF16c(src, dst, n);
        return;
    }
#endif
    halvesToFloatsScalar(src, dst, n);
}

}  // namespace

Result<Image> Image::create(Device& device, std::uint32_t width, std::uint32_t height,
                            VkFormat format) {
    if (!device.valid()) return std::unexpected(std::string("Image::create on an invalid device"));
    if (width == 0 || height == 0) {
        return std::unexpected(std::string("an image of ") + std::to_string(width) + "x" +
                               std::to_string(height) + " has no texels");
    }

    Image img;
    img.device_ = device.handle();
    img.api_ = &device.api();
    const DeviceApi& api = device.api();
    img.format_ = format;
    img.width_ = width;
    img.height_ = height;

    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = format;
    ici.extent = {width, height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (VkResult r = api.vkCreateImage(img.device_, &ici, nullptr, &img.image_); r != VK_SUCCESS) {
        return std::unexpected(std::string("vkCreateImage: ") + describe(r));
    }

    VkMemoryRequirements req{};
    api.vkGetImageMemoryRequirements(img.device_, img.image_, &req);
    auto index = device.memoryTypeIndex(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!index) return std::unexpected(index.error());

    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = *index;
    if (VkResult r = api.vkAllocateMemory(img.device_, &mai, nullptr, &img.memory_); r != VK_SUCCESS) {
        return std::unexpected(std::string("vkAllocateMemory: ") + describe(r));
    }
    if (VkResult r = api.vkBindImageMemory(img.device_, img.image_, img.memory_, 0); r != VK_SUCCESS) {
        return std::unexpected(std::string("vkBindImageMemory: ") + describe(r));
    }

    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = img.image_;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = format;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    if (VkResult r = api.vkCreateImageView(img.device_, &vci, nullptr, &img.view_); r != VK_SUCCESS) {
        return std::unexpected(std::string("vkCreateImageView: ") + describe(r));
    }
    return img;
}

void Image::destroy() {
    if (device_ == VK_NULL_HANDLE) return;
    if (view_ != VK_NULL_HANDLE) api_->vkDestroyImageView(device_, view_, nullptr);
    if (image_ != VK_NULL_HANDLE) api_->vkDestroyImage(device_, image_, nullptr);
    if (memory_ != VK_NULL_HANDLE) api_->vkFreeMemory(device_, memory_, nullptr);
    device_ = VK_NULL_HANDLE;
    api_ = nullptr;
    view_ = VK_NULL_HANDLE;
    image_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;
    width_ = height_ = 0;
}

Image::~Image() { destroy(); }

Image::Image(Image&& o) noexcept
    : device_(o.device_), api_(o.api_), image_(o.image_), memory_(o.memory_), view_(o.view_),
      format_(o.format_), width_(o.width_), height_(o.height_) {
    o.device_ = VK_NULL_HANDLE;
    o.image_ = VK_NULL_HANDLE;
    o.memory_ = VK_NULL_HANDLE;
    o.view_ = VK_NULL_HANDLE;
}

Image& Image::operator=(Image&& o) noexcept {
    if (this != &o) {
        destroy();
        device_ = o.device_;
        api_ = o.api_;
        image_ = o.image_;
        memory_ = o.memory_;
        view_ = o.view_;
        format_ = o.format_;
        width_ = o.width_;
        height_ = o.height_;
        o.device_ = VK_NULL_HANDLE;
        o.image_ = VK_NULL_HANDLE;
        o.memory_ = VK_NULL_HANDLE;
        o.view_ = VK_NULL_HANDLE;
    }
    return *this;
}

Result<std::vector<Texel>> readBack(Device& device, const Image& image) {
    if (image.handle() == VK_NULL_HANDLE) {
        return std::unexpected(std::string("readBack of an image that was never created"));
    }
    if (image.format() != VK_FORMAT_R16G16_SFLOAT) {
        return std::unexpected(std::string("readBack only decodes R16G16_SFLOAT"));
    }
    const std::size_t texels = static_cast<std::size_t>(image.width()) * image.height();
    const VkDeviceSize bytes = texels * 2 * sizeof(std::uint16_t);

    // CACHED FIRST, and it is the whole cost of this function. Memory that is
    // visible and coherent but NOT cached is write-combined: the CPU reads it
    // uncached, and on this machine that was ~220 MB/s -- 0.24 s of a 1.67 s
    // render of the heaviest corpus document spent in one memcpy
    // (`Docs/Laudos/2026-09-29-perfil-do-render.md` §3.1). Coherent AND cached
    // needs no invalidate. A device with no such type falls back to what was
    // asked before, and the bytes read are the same either way.
    auto stage = Buffer::create(device, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                    VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    if (!stage) {
        stage = Buffer::create(device, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }
    if (!stage) return std::unexpected(stage.error());

    VkImage src = image.handle();
    VkBuffer dst = stage->handle();
    const std::uint32_t w = image.width(), h = image.height();
    const DeviceApi* api = &device.api();
    auto ran = device.submitAndWait([api, src, dst, w, h](VkCommandBuffer cmd) {
        // The render pass left the image in TRANSFER_SRC_OPTIMAL, so this is a
        // barrier for VISIBILITY, not a transition: the copy has to see the
        // colour writes, and a queue submission is not a memory dependency.
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = src;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        api->vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &barrier);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {w, h, 1};
        api->vkCmdCopyImageToBuffer(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, 1, &region);
    });
    if (!ran) return std::unexpected(ran.error());

    std::vector<Texel> out(texels);
    static_assert(sizeof(Texel) == 2 * sizeof(float), "Texel is read as a flat float pair");
    halvesToFloats(static_cast<const std::uint16_t*>(stage->mapped()),
                   reinterpret_cast<float*>(out.data()), texels * 2);
    return out;
}

}  // namespace rb
