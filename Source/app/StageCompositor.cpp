#include "Source/app/StageCompositor.h"

#include "Source/RenderBox/ChicletShape.h"

#include "imgui_impl_vulkan.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace icapp {
namespace {

const std::uint32_t kVertSpirv[] =
#include "stage.vert.inc"
    ;
const std::uint32_t kFragSpirv[] =
#include "stage.frag.inc"
    ;

// O bloco de push constants de stage.vert / stage.frag, campo a campo.
struct Push {
    float quad[4];
    float quadUV[4];
    float square[4];
    float stage[4];
    float cover[4];
    float solid[4];
    float lens[4];
    float fb[2];
    float pad[2];
};
static_assert(sizeof(Push) == 128, "o minimo garantido de maxPushConstantsSize");

// O que vai dentro da draw list com cada desenho (`AddCallback` copia).
struct DrawData {
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkDescriptorSet set;
    Push push;
};

constexpr std::size_t kSetRing = 32;

// AS MATRIZES QUE O SHADER TEM ESCRITAS (stage.frag). Conferidas contra
// `rb::monoColourMatrices` na partida: se a RenderBox mudar um numero e o
// shader nao, o compositor nao sobe e o vidro volta para a CPU, em vez de a
// tela e as miniaturas passarem a discordar caladas.
bool shaderMatchesCpu() {
    const rb::MonoColourMatrices m = rb::monoColourMatrices();
    const double light[3] = {0.12, 1.0, 1.2}, dark[3] = {0.05, 0.3, 0.8};
    const double lighten[3] = {0.9, 2.5, 2.0}, highlight[3] = {0.2, 1.35, 1.4};
    for (int i = 0; i < 3; ++i) {
        if (m.glassLight[i] != light[i] || m.glassDark[i] != dark[i] || m.clearLighten[i] != lighten[i] ||
            m.clearHighlight[i] != highlight[i])
            return false;
    }
    return m.clearDarkening == 0.3 && m.vcmMin == -0.75 && std::fabs(m.vcmMax - 1.0864043) < 1e-6;
}

// IEEE 754 binary32 -> binary16, arredondando para o mais proximo.
std::uint16_t toHalf(float f) {
    std::uint32_t x;
    std::memcpy(&x, &f, sizeof x);
    const std::uint32_t sign = (x >> 16) & 0x8000u;
    const std::int32_t exponent = static_cast<std::int32_t>((x >> 23) & 0xFFu) - 127 + 15;
    std::uint32_t mantissa = x & 0x7FFFFFu;
    if (exponent <= 0) {
        if (exponent < -10) return static_cast<std::uint16_t>(sign);
        mantissa = (mantissa | 0x800000u) >> (1 - exponent);
        return static_cast<std::uint16_t>(sign | ((mantissa + 0x1000u) >> 13));
    }
    if (exponent >= 31) return static_cast<std::uint16_t>(sign | 0x7C00u);
    // SOMA, e nao OU: o arredondamento de uma mantissa cheia vai a 0x400 e tem
    // de SUBIR o expoente. Com OU, 0,49999 virava 0,25 -- e o deslocamento
    // "quase zero" da lente (0,5 e zero) virava um degrau de 14% do lado.
    return static_cast<std::uint16_t>(sign | ((static_cast<std::uint32_t>(exponent) << 10) +
                                              ((mantissa + 0x1000u) >> 13)));
}

void drawCallback(const ImDrawList*, const ImDrawCmd* cmd) {
    const auto* d = static_cast<const DrawData*>(cmd->UserCallbackData);
    const auto* state = static_cast<ImGui_ImplVulkan_RenderState*>(ImGui::GetPlatformIO().Renderer_RenderState);
    const ImDrawData* dd = ImGui::GetDrawData();
    if (!d || !state || !dd) return;
    VkCommandBuffer cb = state->CommandBuffer;
    const float fbW = dd->DisplaySize.x * dd->FramebufferScale.x;
    const float fbH = dd->DisplaySize.y * dd->FramebufferScale.y;

    // O recorte do comando, em pixels do framebuffer: o backend nao o aplica a
    // um callback, e sem ele o palco pintaria por cima da barra.
    const float x0 = std::clamp((cmd->ClipRect.x - dd->DisplayPos.x) * dd->FramebufferScale.x, 0.0f, fbW);
    const float y0 = std::clamp((cmd->ClipRect.y - dd->DisplayPos.y) * dd->FramebufferScale.y, 0.0f, fbH);
    const float x1 = std::clamp((cmd->ClipRect.z - dd->DisplayPos.x) * dd->FramebufferScale.x, 0.0f, fbW);
    const float y1 = std::clamp((cmd->ClipRect.w - dd->DisplayPos.y) * dd->FramebufferScale.y, 0.0f, fbH);
    if (x1 <= x0 || y1 <= y0) return;
    VkRect2D scissor;
    scissor.offset = {static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0)};
    scissor.extent = {static_cast<std::uint32_t>(x1 - x0), static_cast<std::uint32_t>(y1 - y0)};
    VkViewport viewport{0.0f, 0.0f, fbW, fbH, 0.0f, 1.0f};

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, d->pipeline);
    vkCmdSetViewport(cb, 0, 1, &viewport);
    vkCmdSetScissor(cb, 0, 1, &scissor);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, d->layout, 0, 1, &d->set, 0, nullptr);
    vkCmdPushConstants(cb, d->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push),
                       &d->push);
    vkCmdDraw(cb, 4, 1, 0, 0);
}

}  // namespace

StageCompositor::StageCompositor(const Gpu& gpu, TexturePool& pool) : gpu_(gpu), pool_(pool) {
    if (!build()) {
        // Meio montado nao serve: `ready()` le o pipeline.
        if (pipeline_) vkDestroyPipeline(gpu_.device, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
}

StageCompositor::~StageCompositor() {
    if (!gpu_.device) return;
    vkDeviceWaitIdle(gpu_.device);
    destroyTexture(white_);
    for (auto& [index, t] : backdrops_) destroyTexture(t);
    for (Lens& l : lens_) destroyTexture(l.texture);
    if (pipeline_) vkDestroyPipeline(gpu_.device, pipeline_, nullptr);
    if (layout_) vkDestroyPipelineLayout(gpu_.device, layout_, nullptr);
    if (descriptors_) vkDestroyDescriptorPool(gpu_.device, descriptors_, nullptr);
    if (setLayout_) vkDestroyDescriptorSetLayout(gpu_.device, setLayout_, nullptr);
    if (linear_) vkDestroySampler(gpu_.device, linear_, nullptr);
    if (trilinear_) vkDestroySampler(gpu_.device, trilinear_, nullptr);
    if (fence_) vkDestroyFence(gpu_.device, fence_, nullptr);
    if (commands_) vkDestroyCommandPool(gpu_.device, commands_, nullptr);
}

std::uint32_t StageCompositor::memoryType(std::uint32_t bits, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(gpu_.physical, &mp);
    for (std::uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    return UINT32_MAX;
}

bool StageCompositor::build() {
    auto fail = [](const char* what) {
        std::fprintf(stderr, "IconComposer: stage compositor: %s; the Mono glass stays on the CPU\n", what);
        return false;
    };
    if (!gpu_.device || !gpu_.renderPass) return fail("the shell gave no render pass");
    if (!shaderMatchesCpu()) return fail("stage.frag and rb::monoColourMatrices disagree");

    VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    cpi.queueFamilyIndex = gpu_.queueFamily;
    if (vkCreateCommandPool(gpu_.device, &cpi, nullptr, &commands_) != VK_SUCCESS) return fail("no command pool");
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence(gpu_.device, &fi, nullptr, &fence_) != VK_SUCCESS) return fail("no fence");

    // Os dois amostradores: bilinear preso na borda (o icone e a lente), e o
    // mesmo com mips (o fundo, que o desfoque le em niveis).
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.maxLod = 0.0f;
    if (vkCreateSampler(gpu_.device, &si, nullptr, &linear_) != VK_SUCCESS) return fail("no sampler");
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.maxLod = VK_LOD_CLAMP_NONE;
    if (vkCreateSampler(gpu_.device, &si, nullptr, &trilinear_) != VK_SUCCESS) return fail("no sampler");

    VkDescriptorSetLayoutBinding bindings[3]{};
    for (std::uint32_t i = 0; i < 3; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 3;
    li.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(gpu_.device, &li, nullptr, &setLayout_) != VK_SUCCESS)
        return fail("no descriptor set layout");

    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<std::uint32_t>(kSetRing * 3)};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = static_cast<std::uint32_t>(kSetRing);
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &size;
    if (vkCreateDescriptorPool(gpu_.device, &dpi, nullptr, &descriptors_) != VK_SUCCESS)
        return fail("no descriptor pool");
    std::vector<VkDescriptorSetLayout> layouts(kSetRing, setLayout_);
    sets_.resize(kSetRing);
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = descriptors_;
    dai.descriptorSetCount = static_cast<std::uint32_t>(kSetRing);
    dai.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(gpu_.device, &dai, sets_.data()) != VK_SUCCESS) {
        sets_.clear();
        return fail("no descriptor sets");
    }

    VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &setLayout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &range;
    if (vkCreatePipelineLayout(gpu_.device, &pli, nullptr, &layout_) != VK_SUCCESS) return fail("no pipeline layout");

    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smi.codeSize = sizeof kVertSpirv;
    smi.pCode = kVertSpirv;
    if (vkCreateShaderModule(gpu_.device, &smi, nullptr, &vert) != VK_SUCCESS) return fail("stage.vert");
    smi.codeSize = sizeof kFragSpirv;
    smi.pCode = kFragSpirv;
    if (vkCreateShaderModule(gpu_.device, &smi, nullptr, &frag) != VK_SUCCESS) {
        vkDestroyShaderModule(gpu_.device, vert, nullptr);
        return fail("stage.frag");
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    // A mesma mescla do ImGui: a cor reta por cima do que ja esta no quadro.
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blending.attachmentCount = 1;
    blending.pAttachments = &blend;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    const VkDynamicState dynamics[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamics;

    VkGraphicsPipelineCreateInfo gpi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gpi.stageCount = 2;
    gpi.pStages = stages;
    gpi.pVertexInputState = &vertexInput;
    gpi.pInputAssemblyState = &assembly;
    gpi.pViewportState = &viewport;
    gpi.pRasterizationState = &raster;
    gpi.pMultisampleState = &multisample;
    gpi.pDepthStencilState = &depth;
    gpi.pColorBlendState = &blending;
    gpi.pDynamicState = &dynamic;
    gpi.layout = layout_;
    gpi.renderPass = gpu_.renderPass;
    gpi.subpass = 0;
    const VkResult made = vkCreateGraphicsPipelines(gpu_.device, VK_NULL_HANDLE, 1, &gpi, nullptr, &pipeline_);
    vkDestroyShaderModule(gpu_.device, vert, nullptr);
    vkDestroyShaderModule(gpu_.device, frag, nullptr);
    if (made != VK_SUCCESS) {
        pipeline_ = VK_NULL_HANDLE;
        return fail("the pipeline could not be created");
    }

    // O fundo de quem nao tem imagem: um texel, para o conjunto estar completo.
    if (!createTexture(white_, VK_FORMAT_R8G8B8A8_UNORM, 1, 1, {{255, 255, 255, 255}}, 4)) return fail("no texture");
    return true;
}

bool StageCompositor::createTexture(Texture& t, VkFormat format, std::uint32_t w, std::uint32_t h,
                                    const std::vector<std::vector<std::uint8_t>>& levels,
                                    std::uint32_t bytesPerTexel) {
    if (w == 0 || h == 0 || levels.empty()) return false;
    t.width = w;
    t.height = h;
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent = {w, h, 1};
    ii.mipLevels = static_cast<std::uint32_t>(levels.size());
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(gpu_.device, &ii, nullptr, &t.image) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(gpu_.device, t.image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(gpu_.device, &ai, nullptr, &t.memory) != VK_SUCCESS) {
        destroyTexture(t);
        return false;
    }
    vkBindImageMemory(gpu_.device, t.image, t.memory, 0);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = t.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, ii.mipLevels, 0, 1};
    if (vkCreateImageView(gpu_.device, &vi, nullptr, &t.view) != VK_SUCCESS) {
        destroyTexture(t);
        return false;
    }

    // Todos os niveis num buffer so, um depois do outro.
    VkDeviceSize total = 0;
    for (const auto& level : levels) total += level.size();
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = total;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (vkCreateBuffer(gpu_.device, &bi, nullptr, &staging) != VK_SUCCESS) {
        destroyTexture(t);
        return false;
    }
    vkGetBufferMemoryRequirements(gpu_.device, staging, &req);
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memoryType(req.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(gpu_.device, &ai, nullptr, &stagingMemory) != VK_SUCCESS) {
        vkDestroyBuffer(gpu_.device, staging, nullptr);
        destroyTexture(t);
        return false;
    }
    vkBindBufferMemory(gpu_.device, staging, stagingMemory, 0);
    void* mapped = nullptr;
    vkMapMemory(gpu_.device, stagingMemory, 0, total, 0, &mapped);
    std::vector<VkBufferImageCopy> regions;
    VkDeviceSize offset = 0;
    std::uint32_t lw = w, lh = h;
    for (std::size_t i = 0; i < levels.size(); ++i) {
        // Um nivel menor que o esperado seria uma copia para fora do buffer.
        if (levels[i].size() != static_cast<std::size_t>(lw) * lh * bytesPerTexel) {
            vkUnmapMemory(gpu_.device, stagingMemory);
            vkDestroyBuffer(gpu_.device, staging, nullptr);
            vkFreeMemory(gpu_.device, stagingMemory, nullptr);
            destroyTexture(t);
            return false;
        }
        std::memcpy(static_cast<std::uint8_t*>(mapped) + offset, levels[i].data(), levels[i].size());
        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<std::uint32_t>(i), 0, 1};
        region.imageExtent = {lw, lh, 1};
        regions.push_back(region);
        offset += levels[i].size();
        lw = std::max(1u, lw / 2);
        lh = std::max(1u, lh / 2);
    }
    vkUnmapMemory(gpu_.device, stagingMemory);

    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = commands_;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(gpu_.device, &cai, &cb);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &begin);
    VkImageMemoryBarrier toDst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = t.image;
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, ii.mipLevels, 0, 1};
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toDst);
    vkCmdCopyBufferToImage(cb, staging, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<std::uint32_t>(regions.size()), regions.data());
    VkImageMemoryBarrier toRead = toDst;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &toRead);
    vkEndCommandBuffer(cb);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cb;
    vkResetFences(gpu_.device, 1, &fence_);
    const VkResult sent = vkQueueSubmit(gpu_.queue, 1, &submit, fence_);
    if (sent == VK_SUCCESS) vkWaitForFences(gpu_.device, 1, &fence_, VK_TRUE, UINT64_MAX);
    vkFreeCommandBuffers(gpu_.device, commands_, 1, &cb);
    vkDestroyBuffer(gpu_.device, staging, nullptr);
    vkFreeMemory(gpu_.device, stagingMemory, nullptr);
    if (sent != VK_SUCCESS) {
        destroyTexture(t);
        return false;
    }
    return true;
}

void StageCompositor::destroyTexture(Texture& t) {
    if (t.view) vkDestroyImageView(gpu_.device, t.view, nullptr);
    if (t.image) vkDestroyImage(gpu_.device, t.image, nullptr);
    if (t.memory) vkFreeMemory(gpu_.device, t.memory, nullptr);
    t = Texture{};
}

void StageCompositor::setBackdrop(int index, const std::shared_ptr<const ick::StagePixels>& pixels) {
    if (!ready() || !pixels || pixels->width == 0 || pixels->height == 0 || backdrops_.count(index)) return;
    // A cadeia de mips por caixa de 2x2, na CPU: o desfoque do shader le o
    // nivel cuja pegada e o passo entre as amostras.
    std::vector<std::vector<std::uint8_t>> levels;
    levels.push_back(pixels->rgba);
    std::uint32_t w = pixels->width, h = pixels->height;
    while (w > 1 || h > 1) {
        const std::uint32_t nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
        const std::vector<std::uint8_t>& src = levels.back();
        std::vector<std::uint8_t> dst(static_cast<std::size_t>(nw) * nh * 4);
        for (std::uint32_t y = 0; y < nh; ++y) {
            const std::uint32_t y0 = std::min(h - 1, y * 2), y1 = std::min(h - 1, y * 2 + 1);
            for (std::uint32_t x = 0; x < nw; ++x) {
                const std::uint32_t x0 = std::min(w - 1, x * 2), x1 = std::min(w - 1, x * 2 + 1);
                for (int c = 0; c < 4; ++c) {
                    const unsigned sum = src[(static_cast<std::size_t>(y0) * w + x0) * 4 + c] +
                                         src[(static_cast<std::size_t>(y0) * w + x1) * 4 + c] +
                                         src[(static_cast<std::size_t>(y1) * w + x0) * 4 + c] +
                                         src[(static_cast<std::size_t>(y1) * w + x1) * 4 + c];
                    dst[(static_cast<std::size_t>(y) * nw + x) * 4 + c] = static_cast<std::uint8_t>((sum + 2) / 4);
                }
            }
        }
        levels.push_back(std::move(dst));
        w = nw;
        h = nh;
    }
    Texture t;
    if (createTexture(t, VK_FORMAT_R8G8B8A8_UNORM, pixels->width, pixels->height, levels, 4)) backdrops_[index] = t;
}

const StageCompositor::Lens* StageCompositor::lens(bool watch) {
    const int which = watch ? 1 : 0;
    Lens& l = lens_[which];
    if (!lensTried_[which]) {
        lensTried_[which] = true;
        rb::SimulatedGlassLens made =
            rb::simulatedGlassLens(watch ? rb::IconPlatform::WatchOS : rb::IconPlatform::Main);
        if (made.size > 0 && made.variant == 2) {
            // Meio-float: o deslocamento e uma fracao do lado da pastilha, e em
            // 8 bits um passo dele seria meio pixel de fundo.
            std::vector<std::uint8_t> half(made.rgba.size() * 2);
            for (std::size_t i = 0; i < made.rgba.size(); ++i) {
                const std::uint16_t v = toHalf(made.rgba[i]);
                std::memcpy(&half[i * 2], &v, 2);
            }
            if (createTexture(l.texture, VK_FORMAT_R16G16B16A16_SFLOAT, made.size, made.size, {half}, 8)) {
                made.rgba.clear();
                made.rgba.shrink_to_fit();
                l.info = std::move(made);
            }
        } else {
            std::fprintf(stderr, "IconComposer: stage compositor: the lens has variant %u, the shader is written "
                                 "for 2\n", made.variant);
        }
    }
    return l.texture.view ? &l : nullptr;
}

bool StageCompositor::compose(ImDrawList* dl, const ick::MonoStageDraw& d) {
    if (!ready() || !dl || sets_.empty()) return false;
    const VkImageView icon = pool_.view(d.texture);
    const Lens* l = lens(d.watch);
    if (!icon || !l || !(d.squareSide > 0.0f)) return false;

    const bool image = d.background.kind == ick::StageBackground::Kind::Image;
    const Texture* backdrop = &white_;
    if (image) {
        const auto it = backdrops_.find(d.background.image);
        if (it != backdrops_.end()) backdrop = &it->second;
    }
    const bool hasImage = backdrop != &white_;

    VkDescriptorSet set = sets_[nextSet_];
    nextSet_ = (nextSet_ + 1) % sets_.size();
    VkDescriptorImageInfo infos[3] = {
        {linear_, icon, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {trilinear_, backdrop->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {linear_, l->texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
    };
    VkWriteDescriptorSet writes[3]{};
    for (std::uint32_t i = 0; i < 3; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &infos[i];
    }
    vkUpdateDescriptorSets(gpu_.device, 3, writes, 0, nullptr);

    // Da tela do ImGui para pixels do framebuffer.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    auto fx = [&](float x) { return (x - vp->Pos.x) * scale.x; };
    auto fy = [&](float y) { return (y - vp->Pos.y) * scale.y; };

    DrawData data{};
    data.pipeline = pipeline_;
    data.layout = layout_;
    data.set = set;
    Push& p = data.push;
    p.quad[0] = fx(d.quadMin.x);
    p.quad[1] = fy(d.quadMin.y);
    p.quad[2] = fx(d.quadMax.x);
    p.quad[3] = fy(d.quadMax.y);
    p.quadUV[0] = d.uvMin.x;
    p.quadUV[1] = d.uvMin.y;
    p.quadUV[2] = d.uvMax.x;
    p.quadUV[3] = d.uvMax.y;
    const float side = d.squareSide * scale.x;
    p.square[0] = fx(d.squareMin.x);
    p.square[1] = fy(d.squareMin.y);
    p.square[2] = side;
    p.square[3] = static_cast<float>(l->info.size);
    const float stageW = (d.stageMax.x - d.stageMin.x) * scale.x, stageH = (d.stageMax.y - d.stageMin.y) * scale.y;
    p.stage[0] = fx(d.stageMin.x);
    p.stage[1] = fy(d.stageMin.y);
    p.stage[2] = std::max(1.0f, stageW);
    p.stage[3] = std::max(1.0f, stageH);
    const ick::StageCover cover =
        hasImage ? ick::stageCover(stageW, stageH, static_cast<float>(backdrop->width),
                                   static_cast<float>(backdrop->height))
                 : ick::StageCover{};
    p.cover[0] = cover.u0;
    p.cover[1] = cover.v0;
    p.cover[2] = cover.u1;
    p.cover[3] = cover.v1;
    p.solid[0] = d.background.r;
    p.solid[1] = d.background.g;
    p.solid[2] = d.background.b;
    p.solid[3] = hasImage ? 1.0f : 0.0f;
    // `simulatedGlass`: sigma = hypot(relativo * S, absoluto), com S o lado da
    // pastilha na tela e o absoluto em pontos da tela.
    const double chicletOnScreen = l->info.side / static_cast<double>(l->info.size) * side;
    p.lens[0] = l->info.scalePixels;
    p.lens[1] = static_cast<float>(std::hypot(l->info.relativeBlur * chicletOnScreen, l->info.absoluteBlur));
    p.lens[2] = hasImage ? (cover.u1 - cover.u0) * static_cast<float>(backdrop->width) / p.stage[2] : 1.0f;
    p.lens[3] = static_cast<float>((d.clear ? 1 : 0) | (d.dark ? 2 : 0));
    p.fb[0] = vp->Size.x * scale.x;
    p.fb[1] = vp->Size.y * scale.y;

    dl->AddCallback(drawCallback, &data, sizeof data);
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    return true;
}

}  // namespace icapp
