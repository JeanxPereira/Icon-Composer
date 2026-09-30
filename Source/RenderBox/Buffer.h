#pragma once
// A buffer and its memory, as one object.
//
// Vulkan separates the two, and almost every place this tower needs a buffer it
// needs exactly one allocation bound to exactly one buffer. The exception is the
// resident chain, whose buffers are ranges of one big allocation (`createPlaced`,
// GpuResident.h): there the allocation outlives the buffer and is not its to free.
#include <cstdint>
#include <vector>

#include "Source/RenderBox/Device.h"

namespace rb {

class Buffer {
public:
    static Result<Buffer> create(Device& device, VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkMemoryPropertyFlags properties);

    // A buffer bound to memory somebody else owns -- a range of a bigger
    // allocation (the resident heap, GpuResident.h). `place` gets the buffer's
    // requirements and answers the memory and the offset to bind at; the buffer
    // never frees that memory. `vkAllocateMemory` is the expensive call (~7 ms
    // for 16 MB on the RX 6750 XT); `vkCreateBuffer` + bind is microseconds.
    struct Placement {
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
    };
    template <class Place>
    static Result<Buffer> createPlaced(Device& device, VkDeviceSize size, VkBufferUsageFlags usage,
                                       Place&& place) {
        auto b = createUnbound(device, size, usage);
        if (!b) return b;
        const Result<Placement> at = place(b->requirements());
        if (!at) return std::unexpected(at.error());
        if (auto ok = b->bindTo(at->memory, at->offset); !ok) return std::unexpected(ok.error());
        return b;
    }

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
    static Result<Buffer> createUnbound(Device& device, VkDeviceSize size,
                                        VkBufferUsageFlags usage);
    VkMemoryRequirements requirements() const;
    Result<void> bindTo(VkDeviceMemory memory, VkDeviceSize offset);

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
