#pragma once
// A buffer and its memory, as one object.
//
// Vulkan separates the two, and every place this tower needs a buffer it needs
// exactly one allocation bound to exactly one buffer. Keeping them apart would
// buy sub-allocation nobody has asked for and cost a lifetime rule to get wrong.
#include <cstdint>
#include <vector>

#include "Source/RenderBox/Device.h"

namespace rb {

class Buffer {
public:
    static Result<Buffer> create(Device& device, VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkMemoryPropertyFlags properties);

    Buffer() = default;
    ~Buffer();
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    VkBuffer handle() const { return buffer_; }
    VkDeviceSize size() const { return size_; }

    // The host pointer, or null when the memory is not host-visible. Null is the
    // honest answer for device-local memory: the CPU cannot address it, and
    // pretending otherwise is how a copy silently reads garbage.
    void* mapped() const { return mapped_; }

private:
    void destroy();

    VkDevice device_ = VK_NULL_HANDLE;
    // The dispatch of the device above, so that destroy() can free without being
    // handed the Device again. It points into the Device, which outlives every
    // buffer made from it -- the same lifetime rule `device_` already relies on.
    const DeviceApi* api_ = nullptr;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    void* mapped_ = nullptr;
};

// Writes `bytes` into a buffer the CPU may not be able to address, through a
// staging buffer and a copy on the queue.
Result<void> upload(Device& device, Buffer& destination, const void* bytes, std::size_t count);

// The reverse. Returns the buffer's whole contents.
Result<std::vector<std::uint8_t>> download(Device& device, const Buffer& source);

}  // namespace rb
