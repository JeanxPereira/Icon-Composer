#include "Source/RenderBox/Compute.h"

#include <cstring>

namespace rb {

Result<ComputePass> ComputePass::create(Device& device, const std::uint32_t* spirv,
                                        std::size_t byteCount, std::size_t pushConstantBytes) {
    if (!device.valid()) return std::unexpected(std::string("ComputePass on an invalid device"));
    if (byteCount == 0 || byteCount % 4 != 0) {
        return std::unexpected(std::string("a SPIR-V module of ") + std::to_string(byteCount) +
                               " bytes is not a whole number of words");
    }

    ComputePass p;
    p.device_ = device.handle();
    p.api_ = &device.api();
    p.pushBytes_ = pushConstantBytes;
    const DeviceApi& api = device.api();

    VkShaderModuleCreateInfo smi{};
    smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smi.codeSize = byteCount;
    smi.pCode = spirv;
    if (VkResult r = api.vkCreateShaderModule(p.device_, &smi, nullptr, &p.module_); r != VK_SUCCESS) {
        return std::unexpected(std::string("vkCreateShaderModule: ") + describe(r));
    }

    VkDescriptorSetLayoutBinding bindings[2]{};
    for (std::uint32_t i = 0; i < 2; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dsl{};
    dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl.bindingCount = 2;
    dsl.pBindings = bindings;
    if (VkResult r = api.vkCreateDescriptorSetLayout(p.device_, &dsl, nullptr, &p.setLayout_);
        r != VK_SUCCESS) {
        return std::unexpected(std::string("vkCreateDescriptorSetLayout: ") + describe(r));
    }

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push.size = static_cast<std::uint32_t>(pushConstantBytes);
    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &p.setLayout_;
    pli.pushConstantRangeCount = pushConstantBytes > 0 ? 1 : 0;
    pli.pPushConstantRanges = &push;
    if (VkResult r = api.vkCreatePipelineLayout(p.device_, &pli, nullptr, &p.layout_);
        r != VK_SUCCESS) {
        return std::unexpected(std::string("vkCreatePipelineLayout: ") + describe(r));
    }

    VkComputePipelineCreateInfo cpi{};
    cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.module = p.module_;
    cpi.stage.pName = "main";
    cpi.layout = p.layout_;
    if (VkResult r = api.vkCreateComputePipelines(p.device_, VK_NULL_HANDLE, 1, &cpi, nullptr,
                                              &p.pipeline_);
        r != VK_SUCCESS) {
        return std::unexpected(std::string("vkCreateComputePipelines: ") + describe(r));
    }

    VkDescriptorPoolSize size{};
    size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    size.descriptorCount = 2;
    VkDescriptorPoolCreateInfo dpi{};
    dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &size;
    if (VkResult r = api.vkCreateDescriptorPool(p.device_, &dpi, nullptr, &p.pool_); r != VK_SUCCESS) {
        return std::unexpected(std::string("vkCreateDescriptorPool: ") + describe(r));
    }

    VkDescriptorSetAllocateInfo dsa{};
    dsa.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsa.descriptorPool = p.pool_;
    dsa.descriptorSetCount = 1;
    dsa.pSetLayouts = &p.setLayout_;
    if (VkResult r = api.vkAllocateDescriptorSets(p.device_, &dsa, &p.set_); r != VK_SUCCESS) {
        return std::unexpected(std::string("vkAllocateDescriptorSets: ") + describe(r));
    }
    return p;
}

void ComputePass::destroy() {
    if (device_ == VK_NULL_HANDLE) return;
    if (pipeline_ != VK_NULL_HANDLE) api_->vkDestroyPipeline(device_, pipeline_, nullptr);
    if (layout_ != VK_NULL_HANDLE) api_->vkDestroyPipelineLayout(device_, layout_, nullptr);
    if (pool_ != VK_NULL_HANDLE) api_->vkDestroyDescriptorPool(device_, pool_, nullptr);
    if (setLayout_ != VK_NULL_HANDLE) {
        api_->vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);
    }
    if (module_ != VK_NULL_HANDLE) api_->vkDestroyShaderModule(device_, module_, nullptr);
    device_ = VK_NULL_HANDLE;
    api_ = nullptr;
    pipeline_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
    setLayout_ = VK_NULL_HANDLE;
    module_ = VK_NULL_HANDLE;
    set_ = VK_NULL_HANDLE;
}

ComputePass::~ComputePass() { destroy(); }

ComputePass::ComputePass(ComputePass&& o) noexcept
    : device_(o.device_), api_(o.api_), module_(o.module_), setLayout_(o.setLayout_), layout_(o.layout_),
      pipeline_(o.pipeline_), pool_(o.pool_), set_(o.set_), pushBytes_(o.pushBytes_) {
    o.device_ = VK_NULL_HANDLE;
    o.module_ = VK_NULL_HANDLE;
    o.setLayout_ = VK_NULL_HANDLE;
    o.layout_ = VK_NULL_HANDLE;
    o.pipeline_ = VK_NULL_HANDLE;
    o.pool_ = VK_NULL_HANDLE;
    o.set_ = VK_NULL_HANDLE;
}

ComputePass& ComputePass::operator=(ComputePass&& o) noexcept {
    if (this != &o) {
        destroy();
        device_ = o.device_;
        api_ = o.api_;
        module_ = o.module_;
        setLayout_ = o.setLayout_;
        layout_ = o.layout_;
        pipeline_ = o.pipeline_;
        pool_ = o.pool_;
        set_ = o.set_;
        pushBytes_ = o.pushBytes_;
        o.device_ = VK_NULL_HANDLE;
        o.module_ = VK_NULL_HANDLE;
        o.setLayout_ = VK_NULL_HANDLE;
        o.layout_ = VK_NULL_HANDLE;
        o.pipeline_ = VK_NULL_HANDLE;
        o.pool_ = VK_NULL_HANDLE;
        o.set_ = VK_NULL_HANDLE;
    }
    return *this;
}

Result<void> ComputePass::run(Device& device, const Buffer& input, const Buffer& output,
                              const void* constants, std::size_t constantBytes,
                              std::uint32_t invocations, std::uint32_t localSize) {
    if (pipeline_ == VK_NULL_HANDLE) {
        return std::unexpected(std::string("ComputePass::run on an unbuilt pass"));
    }
    if (constantBytes != pushBytes_) {
        return std::unexpected(std::string("the pass was built for ") + std::to_string(pushBytes_) +
                               " push-constant bytes and was given " + std::to_string(constantBytes));
    }
    if (localSize == 0) return std::unexpected(std::string("a local size of zero"));

    VkDescriptorBufferInfo buffers[2]{};
    buffers[0].buffer = input.handle();
    buffers[0].range = VK_WHOLE_SIZE;
    buffers[1].buffer = output.handle();
    buffers[1].range = VK_WHOLE_SIZE;
    VkWriteDescriptorSet writes[2]{};
    for (std::uint32_t i = 0; i < 2; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set_;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &buffers[i];
    }
    const DeviceApi& api = *api_;
    api.vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);

    const std::uint32_t groups = (invocations + localSize - 1) / localSize;
    VkPipeline pipeline = pipeline_;
    VkPipelineLayout layout = layout_;
    VkDescriptorSet set = set_;
    const std::size_t bytes = pushBytes_;

    return device.submitAndWait([&](VkCommandBuffer cmd) {
        api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        api.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0,
                                nullptr);
        if (bytes > 0) {
            api.vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                               static_cast<std::uint32_t>(bytes), constants);
        }
        api.vkCmdDispatch(cmd, groups, 1, 1);
        // The copy that reads the output back is a separate submission, and a
        // queue submission is not an implicit barrier for a HOST read.
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_HOST_READ_BIT;
        api.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                             &barrier, 0, nullptr, 0, nullptr);
    });
}

}  // namespace rb
