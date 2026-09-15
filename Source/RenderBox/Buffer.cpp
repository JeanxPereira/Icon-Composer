#include "Source/RenderBox/Buffer.h"

#include <cstring>

namespace rb {
namespace {

Result<Buffer> staging(Device& device, VkDeviceSize size, VkBufferUsageFlags usage) {
    return Buffer::create(device, size, usage,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

}  // namespace

Result<Buffer> Buffer::create(Device& device, VkDeviceSize size, VkBufferUsageFlags usage,
                              VkMemoryPropertyFlags properties) {
    if (!device.valid()) return std::unexpected(std::string("Buffer::create on an invalid device"));
    // Vulkan makes a zero size undefined behaviour, and validation reports it as a
    // warning that a passing suite scrolls past. Refuse it here, with the word the
    // caller can search for.
    if (size == 0) return std::unexpected(std::string("a buffer size of zero is not allocatable"));

    Buffer b;
    b.device_ = device.handle();
    b.api_ = &device.api();
    b.size_ = size;
    const DeviceApi& api = device.api();

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (VkResult r = api.vkCreateBuffer(b.device_, &bci, nullptr, &b.buffer_); r != VK_SUCCESS) {
        return std::unexpected(std::string("vkCreateBuffer: ") + describe(r));
    }

    VkMemoryRequirements req{};
    api.vkGetBufferMemoryRequirements(b.device_, b.buffer_, &req);
    auto index = device.memoryTypeIndex(req.memoryTypeBits, properties);
    if (!index) return std::unexpected(index.error());

    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = *index;
    if (VkResult r = api.vkAllocateMemory(b.device_, &mai, nullptr, &b.memory_); r != VK_SUCCESS) {
        return std::unexpected(std::string("vkAllocateMemory: ") + describe(r));
    }
    if (VkResult r = api.vkBindBufferMemory(b.device_, b.buffer_, b.memory_, 0); r != VK_SUCCESS) {
        return std::unexpected(std::string("vkBindBufferMemory: ") + describe(r));
    }

    // Mapped once and left mapped. Host-visible memory in this tower is staging:
    // it is written, copied and dropped, and map/unmap around every touch buys
    // nothing but calls.
    if ((properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0) {
        if (VkResult r = api.vkMapMemory(b.device_, b.memory_, 0, VK_WHOLE_SIZE, 0, &b.mapped_);
            r != VK_SUCCESS) {
            return std::unexpected(std::string("vkMapMemory: ") + describe(r));
        }
    }
    return b;
}

void Buffer::destroy() {
    if (device_ == VK_NULL_HANDLE) return;
    if (mapped_ != nullptr) api_->vkUnmapMemory(device_, memory_);
    if (buffer_ != VK_NULL_HANDLE) api_->vkDestroyBuffer(device_, buffer_, nullptr);
    if (memory_ != VK_NULL_HANDLE) api_->vkFreeMemory(device_, memory_, nullptr);
    device_ = VK_NULL_HANDLE;
    api_ = nullptr;
    buffer_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;
    mapped_ = nullptr;
    size_ = 0;
}

Buffer::~Buffer() { destroy(); }

Buffer::Buffer(Buffer&& other) noexcept
    : device_(other.device_), api_(other.api_), buffer_(other.buffer_), memory_(other.memory_),
      size_(other.size_), mapped_(other.mapped_) {
    other.device_ = VK_NULL_HANDLE;
    other.buffer_ = VK_NULL_HANDLE;
    other.memory_ = VK_NULL_HANDLE;
    other.mapped_ = nullptr;
    other.size_ = 0;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        destroy();
        device_ = other.device_;
        api_ = other.api_;
        buffer_ = other.buffer_;
        memory_ = other.memory_;
        size_ = other.size_;
        mapped_ = other.mapped_;
        other.device_ = VK_NULL_HANDLE;
        other.buffer_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.mapped_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

Result<void> upload(Device& device, Buffer& destination, const void* bytes, std::size_t count) {
    if (count == 0) return std::unexpected(std::string("an upload of zero bytes is refused"));
    if (count > destination.size()) {
        return std::unexpected(std::string("upload of ") + std::to_string(count) +
                               " bytes into a buffer of " + std::to_string(destination.size()));
    }
    // Host-visible destination: no queue work is needed, and going through a copy
    // would be a submission that proves nothing.
    if (destination.mapped() != nullptr) {
        std::memcpy(destination.mapped(), bytes, count);
        return {};
    }

    auto stage = staging(device, count, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if (!stage) return std::unexpected(stage.error());
    std::memcpy(stage->mapped(), bytes, count);

    VkBuffer src = stage->handle();
    VkBuffer dst = destination.handle();
    const DeviceApi* api = &device.api();
    return device.submitAndWait([api, src, dst, count](VkCommandBuffer cmd) {
        VkBufferCopy region{};
        region.size = count;
        api->vkCmdCopyBuffer(cmd, src, dst, 1, &region);
    });
}

Result<std::vector<std::uint8_t>> download(Device& device, const Buffer& source) {
    const std::size_t count = static_cast<std::size_t>(source.size());
    if (count == 0) return std::unexpected(std::string("a download of zero bytes is refused"));

    std::vector<std::uint8_t> out(count);
    if (source.mapped() != nullptr) {
        std::memcpy(out.data(), source.mapped(), count);
        return out;
    }

    auto stage = staging(device, source.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    if (!stage) return std::unexpected(stage.error());

    VkBuffer src = source.handle();
    VkBuffer dst = stage->handle();
    const DeviceApi* api = &device.api();
    auto ran = device.submitAndWait([api, src, dst, count](VkCommandBuffer cmd) {
        VkBufferCopy region{};
        region.size = count;
        api->vkCmdCopyBuffer(cmd, src, dst, 1, &region);
    });
    if (!ran) return std::unexpected(ran.error());

    std::memcpy(out.data(), stage->mapped(), count);
    return out;
}

}  // namespace rb
