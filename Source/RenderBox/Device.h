#pragma once
// RenderBox -- the GPU tower, named after the image of the target it answers for.
//
// WHY THIS TOWER IS GPU AND NOT CPU
// ---------------------------------
// Measured in the target's own `default.metallib` (doc 03): RenderBox rasterises
// paths on the GPU, with a distance field. `shader_path.metal` declares
//
//     path_edges_vertex(ShaderState, PathEdgesVertex, PathGlobals,
//                       device const Path::CubicSegment*, ushort, uint)
//
// -- a VERTEX shader that reads cubic segments straight out of a device buffer --
// and `shader_stroke.metal` does the same with `StrokeLinePoint`. The path is
// never flattened on the CPU, anywhere.
//
// So a CPU scanline rasteriser here would not be a stepping stone that gets
// replaced later; it would be a DIFFERENT ALGORITHM, with different antialiasing,
// producing a different pixel. Same class of error already ruled out for the SVG
// filters in doc 04 section 3: being more correct than the target is the wrong
// mistake for a project whose product is reproduction.
//
// HEADLESS, AND WHY THAT IS NOT A COMPROMISE
// ------------------------------------------
// This tower draws into images and reads them back. A window belongs to the app,
// and architecture rule 2 keeps Onyx out of anything a test links -- so there is
// no surface, no swapchain, and no extension that needs one. The suite drives a
// real adapter.
#include <cstdint>
#include <expected>
#include <functional>
#include <string>

#include <vulkan/vulkan.h>

namespace rb {

// A refusal must say what it refused.
//
// The towers below this one answer with `std::optional`: for a parser, "this is
// not an SVG" is the whole story. Here it is not -- an allocation can fail for a
// missing memory type, a lost device, an absent driver or a validation error, and
// "could not create" without the reason is a bug report nobody can act on.
template <class T>
using Result = std::expected<T, std::string>;

// At namespace scope, not nested in Device, and that is a language rule rather
// than a preference: a default MEMBER initializer of a class defined inside
// another class cannot be used by a default ARGUMENT of that enclosing class --
// the enclosing class is still incomplete there.
struct DeviceOptions {
    // On by default. A suite that runs fast because the driver is not checking it
    // is a suite that reports someone else's undefined behaviour as a passing test.
    bool validation = true;
};

class Device {
public:
    using Options = DeviceOptions;

    static Result<Device> create(DeviceOptions options = DeviceOptions{});

    Device() = default;  // an invalid device, so a failed create still has a name
    ~Device();
    Device(Device&& other) noexcept;
    Device& operator=(Device&& other) noexcept;
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    bool valid() const { return device_ != VK_NULL_HANDLE; }
    VkDevice handle() const { return device_; }
    VkPhysicalDevice physical() const { return physical_; }
    VkQueue queue() const { return queue_; }
    std::uint32_t queueFamily() const { return queueFamily_; }

    // The adapter, verbatim from the driver. When a render differs between two
    // machines this is the first question, so it is not decoration.
    const std::string& name() const { return name_; }

    // Records a one-shot command buffer, submits it, and waits on a fence. Every
    // headless operation in this tower is built on this one primitive.
    Result<void> submitAndWait(const std::function<void(VkCommandBuffer)>& record);

    // The index of a memory type satisfying `properties` among `typeBits`, or the
    // reason there is none.
    Result<std::uint32_t> memoryTypeIndex(std::uint32_t typeBits,
                                          VkMemoryPropertyFlags properties) const;

private:
    void destroy();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    std::uint32_t queueFamily_ = 0;
    std::string name_;
};

// The Vulkan result code, spelled the way the header spells it. A number in an
// error message costs the reader a search; the name does not.
const char* describe(VkResult r);

}  // namespace rb
