#pragma once
// A one-shot compute dispatch: two storage buffers in, push constants, run, done.
//
// Narrow on purpose. This tower needs compute for one job -- running a shader
// whose output can be READ BACK and checked against a CPU oracle -- and a
// general pipeline cache with descriptor pooling would be machinery nothing has
// asked for. It grows when a second caller needs something else.
#include <cstdint>
#include <vector>

#include "Source/RenderBox/Buffer.h"
#include "Source/RenderBox/Device.h"

namespace rb {

class ComputePass {
public:
    // `spirv` is the compiled module, as words. The .comp files are compiled by
    // CMake with glslc and embedded, so a shader that does not compile is a build
    // failure rather than a test that fails at run time for an unrelated reason.
    static Result<ComputePass> create(Device& device, const std::uint32_t* spirv,
                                      std::size_t byteCount, std::size_t pushConstantBytes);

    ComputePass() = default;
    ~ComputePass();
    ComputePass(ComputePass&&) noexcept;
    ComputePass& operator=(ComputePass&&) noexcept;
    ComputePass(const ComputePass&) = delete;
    ComputePass& operator=(const ComputePass&) = delete;

    // Binds `input` at 0 and `output` at 1, pushes `constants`, and dispatches
    // enough groups of `localSize` to cover `invocations`.
    Result<void> run(Device& device, const Buffer& input, const Buffer& output,
                     const void* constants, std::size_t constantBytes,
                     std::uint32_t invocations, std::uint32_t localSize = 64);

private:
    void destroy();

    VkDevice device_ = VK_NULL_HANDLE;
    const DeviceApi* api_ = nullptr;   // this device's dispatch -- see Buffer.h
    VkShaderModule module_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    std::size_t pushBytes_ = 0;
};

}  // namespace rb
