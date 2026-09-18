#include "Source/RenderBox/CoveragePass.h"

#include <cstring>

static const std::uint32_t kVertexSpirv[] =
#include "path_exterior.vert.inc"
    ;
static const std::uint32_t kFragmentSpirv[] =
#include "path_exterior.frag.inc"
    ;

namespace rb {
namespace {

Result<VkShaderModule> module(const DeviceApi& api, VkDevice device, const std::uint32_t* code,
                             std::size_t bytes) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    if (VkResult r = api.vkCreateShaderModule(device, &ci, nullptr, &m); r != VK_SUCCESS) {
        return std::unexpected(std::string("vkCreateShaderModule: ") + describe(r));
    }
    return m;
}

}  // namespace

Result<CoveragePass> CoveragePass::create(Device& device, VkFormat format) {
    if (!device.valid()) return std::unexpected(std::string("CoveragePass on an invalid device"));
    CoveragePass p;
    p.device_ = device.handle();
    p.api_ = &device.api();
    const DeviceApi& api = device.api();

    // ---- the render pass -------------------------------------------------
    VkAttachmentDescription colour{};
    colour.format = format;
    colour.samples = VK_SAMPLE_COUNT_1_BIT;
    colour.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colour.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colour.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colour.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // Straight to TRANSFER_SRC: the only thing that reads this image is the
    // readback, and a layout the pass has to be transitioned out of afterwards
    // is a transition somebody forgets.
    colour.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference ref{};
    ref.attachment = 0;
    ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;

    VkRenderPassCreateInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 1;
    rpi.pAttachments = &colour;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sub;
    if (VkResult r = api.vkCreateRenderPass(p.device_, &rpi, nullptr, &p.renderPass_);
        r != VK_SUCCESS) {
        return std::unexpected(std::string("vkCreateRenderPass: ") + describe(r));
    }

    // ---- layout ----------------------------------------------------------
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo dsl{};
    dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl.bindingCount = 1;
    dsl.pBindings = &binding;
    if (VkResult r = api.vkCreateDescriptorSetLayout(p.device_, &dsl, nullptr, &p.setLayout_);
        r != VK_SUCCESS) {
        return std::unexpected(std::string("vkCreateDescriptorSetLayout: ") + describe(r));
    }

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push.size = sizeof(CoveragePush);
    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &p.setLayout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &push;
    if (VkResult r = api.vkCreatePipelineLayout(p.device_, &pli, nullptr, &p.layout_);
        r != VK_SUCCESS) {
        return std::unexpected(std::string("vkCreatePipelineLayout: ") + describe(r));
    }

    // ---- the pipeline ----------------------------------------------------
    auto vs = module(api, p.device_, kVertexSpirv, sizeof kVertexSpirv);
    if (!vs) return std::unexpected(vs.error());
    p.vertex_ = *vs;
    auto fs = module(api, p.device_, kFragmentSpirv, sizeof kFragmentSpirv);
    if (!fs) return std::unexpected(fs.error());
    p.fragment_ = *fs;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = p.vertex_;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = p.fragment_;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    // No culling: the quad's winding depends on which way the edge runs, and
    // culling it would drop exactly the edges whose sign makes the sum work.
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // `[BIN]` ADDITIVE, with no alpha involvement: the fragment's output IS a
    // signed contribution, and the pixel's coverage is the sum of them.
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;

    const VkDynamicState dynamics[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynamics;

    VkGraphicsPipelineCreateInfo gpi{};
    gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpi.stageCount = 2;
    gpi.pStages = stages;
    gpi.pVertexInputState = &vi;
    gpi.pInputAssemblyState = &ia;
    gpi.pViewportState = &vp;
    gpi.pRasterizationState = &rs;
    gpi.pMultisampleState = &ms;
    gpi.pColorBlendState = &cb;
    gpi.pDynamicState = &dyn;
    gpi.layout = p.layout_;
    gpi.renderPass = p.renderPass_;
    if (VkResult r = api.vkCreateGraphicsPipelines(p.device_, VK_NULL_HANDLE, 1, &gpi, nullptr,
                                               &p.pipeline_);
        r != VK_SUCCESS) {
        return std::unexpected(std::string("vkCreateGraphicsPipelines: ") + describe(r));
    }

    VkDescriptorPoolSize size{};
    size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    size.descriptorCount = 1;
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

void CoveragePass::destroy() {
    if (device_ == VK_NULL_HANDLE) return;
    if (pipeline_ != VK_NULL_HANDLE) api_->vkDestroyPipeline(device_, pipeline_, nullptr);
    if (layout_ != VK_NULL_HANDLE) api_->vkDestroyPipelineLayout(device_, layout_, nullptr);
    if (pool_ != VK_NULL_HANDLE) api_->vkDestroyDescriptorPool(device_, pool_, nullptr);
    if (setLayout_ != VK_NULL_HANDLE) {
        api_->vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);
    }
    if (renderPass_ != VK_NULL_HANDLE) api_->vkDestroyRenderPass(device_, renderPass_, nullptr);
    if (vertex_ != VK_NULL_HANDLE) api_->vkDestroyShaderModule(device_, vertex_, nullptr);
    if (fragment_ != VK_NULL_HANDLE) api_->vkDestroyShaderModule(device_, fragment_, nullptr);
    device_ = VK_NULL_HANDLE;
    api_ = nullptr;
    pipeline_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
    setLayout_ = VK_NULL_HANDLE;
    renderPass_ = VK_NULL_HANDLE;
    vertex_ = VK_NULL_HANDLE;
    fragment_ = VK_NULL_HANDLE;
    set_ = VK_NULL_HANDLE;
}

CoveragePass::~CoveragePass() { destroy(); }

CoveragePass::CoveragePass(CoveragePass&& o) noexcept
    : device_(o.device_), api_(o.api_), renderPass_(o.renderPass_), setLayout_(o.setLayout_),
      layout_(o.layout_), pipeline_(o.pipeline_), pool_(o.pool_), set_(o.set_),
      vertex_(o.vertex_), fragment_(o.fragment_) {
    o.device_ = VK_NULL_HANDLE;
    o.renderPass_ = VK_NULL_HANDLE;
    o.setLayout_ = VK_NULL_HANDLE;
    o.layout_ = VK_NULL_HANDLE;
    o.pipeline_ = VK_NULL_HANDLE;
    o.pool_ = VK_NULL_HANDLE;
    o.vertex_ = VK_NULL_HANDLE;
    o.fragment_ = VK_NULL_HANDLE;
}

CoveragePass& CoveragePass::operator=(CoveragePass&& o) noexcept {
    if (this != &o) {
        destroy();
        device_ = o.device_;
        api_ = o.api_;
        renderPass_ = o.renderPass_;
        setLayout_ = o.setLayout_;
        layout_ = o.layout_;
        pipeline_ = o.pipeline_;
        pool_ = o.pool_;
        set_ = o.set_;
        vertex_ = o.vertex_;
        fragment_ = o.fragment_;
        o.device_ = VK_NULL_HANDLE;
        o.renderPass_ = VK_NULL_HANDLE;
        o.setLayout_ = VK_NULL_HANDLE;
        o.layout_ = VK_NULL_HANDLE;
        o.pipeline_ = VK_NULL_HANDLE;
        o.pool_ = VK_NULL_HANDLE;
        o.vertex_ = VK_NULL_HANDLE;
        o.fragment_ = VK_NULL_HANDLE;
    }
    return *this;
}

Result<void> CoveragePass::draw(Device& device, Image& target, const PathBuffer& path,
                                const PathGlobals& globals, CoverageViewport vp) {
    if (pipeline_ == VK_NULL_HANDLE) {
        return std::unexpected(std::string("CoveragePass::draw on an unbuilt pass"));
    }
    // Never size a draw from a number the buffer merely claims -- the same guard
    // that stopped a mutated header from asking for a hundred gigabytes.
    if (!headerAgreesWithSegments(path)) {
        return std::unexpected(std::string("the path buffer's header disagrees with its segments"));
    }

    // O VIEWPORT DESLOCADO TEM LIMITE DE APARELHO, e ele e dito e nao
    // contornado. A projecao e o canvas inteiro e a origem e negativa, entao
    // um `size` alto sai da faixa antes de sair da memoria:
    // `maxViewportDimensions` e `viewportBoundsRange` sao os dois numeros, e o
    // minimo que o Vulkan garante (4096 e +-8192) cobre `size <= 4096`.
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device.physical(), &props);
        const VkPhysicalDeviceLimits& lim = props.limits;
        const double vw = vp.width ? vp.width : target.width();
        const double vh = vp.height ? vp.height : target.height();
        if (vw > lim.maxViewportDimensions[0] || vh > lim.maxViewportDimensions[1]) {
            return std::unexpected(std::string("a projecao do desenho passa de "
                                               "maxViewportDimensions deste aparelho"));
        }
        if (-static_cast<double>(vp.originX) < lim.viewportBoundsRange[0] ||
            -static_cast<double>(vp.originY) < lim.viewportBoundsRange[0] ||
            vw - vp.originX > lim.viewportBoundsRange[1] ||
            vh - vp.originY > lim.viewportBoundsRange[1]) {
            return std::unexpected(std::string("o deslocamento do viewport sai de "
                                               "viewportBoundsRange deste aparelho"));
        }
    }

    auto segments = Buffer::create(device, path.entries.size() * sizeof(CubicSegment),
                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!segments) return std::unexpected(segments.error());
    std::memcpy(segments->mapped(), path.entries.data(),
                path.entries.size() * sizeof(CubicSegment));

    VkDescriptorBufferInfo bi{};
    bi.buffer = segments->handle();
    bi.range = VK_WHOLE_SIZE;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &bi;
    const DeviceApi& api = *api_;
    api.vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    VkImageView view = target.view();
    VkFramebufferCreateInfo fbi{};
    fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbi.renderPass = renderPass_;
    fbi.attachmentCount = 1;
    fbi.pAttachments = &view;
    fbi.width = target.width();
    fbi.height = target.height();
    fbi.layers = 1;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    if (VkResult r = api.vkCreateFramebuffer(device_, &fbi, nullptr, &framebuffer);
        r != VK_SUCCESS) {
        return std::unexpected(std::string("vkCreateFramebuffer: ") + describe(r));
    }

    const std::uint32_t indices = indicesPerInstance(PathPass::Exterior);
    const std::int32_t total = path.vertexCount();
    const std::uint32_t instances =
        (static_cast<std::uint32_t>(total) + indices - 1) / indices;

    VkRenderPass rp = renderPass_;
    const CoveragePush push{globals, vp.originX, vp.originY};
    VkPipeline pipeline = pipeline_;
    VkPipelineLayout layout = layout_;
    VkDescriptorSet set = set_;
    const std::uint32_t w = target.width(), h = target.height();

    auto ran = device.submitAndWait([&, vp, push](VkCommandBuffer cmd) {
        VkClearValue clear{};
        VkRenderPassBeginInfo rbi{};
        rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rbi.renderPass = rp;
        rbi.framebuffer = framebuffer;
        rbi.renderArea.extent = {w, h};
        rbi.clearValueCount = 1;
        rbi.pClearValues = &clear;
        api.vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x = static_cast<float>(-vp.originX);
        viewport.y = static_cast<float>(-vp.originY);
        viewport.width = static_cast<float>(vp.width ? vp.width : w);
        viewport.height = static_cast<float>(vp.height ? vp.height : h);
        viewport.maxDepth = 1.0f;
        api.vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor{};
        scissor.extent = {w, h};
        api.vkCmdSetScissor(cmd, 0, 1, &scissor);

        api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        api.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0,
                                nullptr);
        api.vkCmdPushConstants(cmd, layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(CoveragePush), &push);
        api.vkCmdDraw(cmd, indices * 6, instances, 0, 0);
        api.vkCmdEndRenderPass(cmd);
    });
    api.vkDestroyFramebuffer(device_, framebuffer, nullptr);
    if (!ran) return std::unexpected(ran.error());
    return {};
}

}  // namespace rb
