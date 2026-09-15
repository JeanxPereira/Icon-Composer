#pragma once
// Rasterises a path's exterior pass into a coverage image.
//
// This is where the transcribed stages stop being functions and become a
// picture. The vertex and fragment shaders it runs are the SAME FILES the
// compute probes are gated against, so nothing here can be right while what was
// verified is wrong -- or the other way round.
//
// `[BIN]` The blend is ADDITIVE, and it has to be: each edge contributes a
// signed area, and the sum over the edges crossing a pixel IS the coverage.
// Opposite edges cancel because their `path_value` differs in sign.
#include <cstdint>

#include "Source/RenderBox/Image.h"
#include "Source/RenderBox/PathBuffer.h"
#include "Source/RenderBox/PathVertexOracle.h"

namespace rb {

class CoveragePass {
public:
    static Result<CoveragePass> create(Device& device, VkFormat format = VK_FORMAT_R16G16_SFLOAT);

    CoveragePass() = default;
    ~CoveragePass();
    CoveragePass(CoveragePass&&) noexcept;
    CoveragePass& operator=(CoveragePass&&) noexcept;
    CoveragePass(const CoveragePass&) = delete;
    CoveragePass& operator=(const CoveragePass&) = delete;

    // Clears `target` and draws every edge of `path` into it. Leaves the image in
    // TRANSFER_SRC_OPTIMAL, ready for readBack.
    Result<void> draw(Device& device, Image& target, const PathBuffer& path,
                      const PathGlobals& globals);

private:
    void destroy();

    VkDevice device_ = VK_NULL_HANDLE;
    const DeviceApi* api_ = nullptr;   // this device's dispatch -- see Buffer.h
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    VkShaderModule vertex_ = VK_NULL_HANDLE;
    VkShaderModule fragment_ = VK_NULL_HANDLE;
};

}  // namespace rb
