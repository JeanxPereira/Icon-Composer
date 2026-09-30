#include "Source/app/TexturePool.h"

#include "imgui_impl_vulkan.h"

#include <algorithm>
#include <cstring>

namespace icapp {

TexturePool::TexturePool(const Gpu& gpu) : gpu_(gpu) {
    VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    ci.queueFamilyIndex = gpu_.queueFamily;
    vkCreateCommandPool(gpu_.device, &ci, nullptr, &commands_);
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(gpu_.device, &fi, nullptr, &fence_);
}

TexturePool::~TexturePool() {
    vkDeviceWaitIdle(gpu_.device);
    for (Tex& t : textures_) destroy(t);
    if (fence_) vkDestroyFence(gpu_.device, fence_, nullptr);
    if (commands_) vkDestroyCommandPool(gpu_.device, commands_, nullptr);
}

std::uint32_t TexturePool::memoryType(std::uint32_t bits, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(gpu_.physical, &mp);
    for (std::uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    return UINT32_MAX;
}

ImTextureID TexturePool::create(std::uint32_t w, std::uint32_t h, const std::uint8_t* rgba8,
                                std::string* why) {
    if (w == 0 || h == 0 || !rgba8) {
        if (why) *why = "empty image";
        return 0;
    }
    Tex t;
    t.w = w;
    t.h = h;
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent = {w, h, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(gpu_.device, &ii, nullptr, &t.image) != VK_SUCCESS) {
        if (why) *why = "vkCreateImage failed";
        return 0;
    }
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(gpu_.device, t.image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(gpu_.device, &ai, nullptr, &t.memory) != VK_SUCCESS) {
        destroy(t);
        if (why) *why = "no device memory for the texture";
        return 0;
    }
    vkBindImageMemory(gpu_.device, t.image, t.memory, 0);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = t.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(gpu_.device, &vi, nullptr, &t.view);
    if (!upload(t, rgba8, true, why)) {
        destroy(t);
        return 0;
    }
    t.set = ImGui_ImplVulkan_AddTexture(t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    t.id = reinterpret_cast<ImTextureID>(t.set);
    textures_.push_back(t);
    return t.id;
}

bool TexturePool::update(ImTextureID id, const std::uint8_t* rgba8, std::string* why) {
    auto it = std::find_if(textures_.begin(), textures_.end(),
                           [id](const Tex& t) { return t.id == id && t.retireIn < 0; });
    if (it == textures_.end()) {
        if (why) *why = "unknown texture";
        return false;
    }
    // A imagem pode estar num quadro em voo; a fila e a mesma da tela, entao
    // esperar por ela e esperar por todo quadro que a le. Custa um quadro, e
    // so acontece quando um render termina.
    vkQueueWaitIdle(gpu_.queue);
    return upload(*it, rgba8, false, why);
}

void TexturePool::remove(ImTextureID id) {
    for (Tex& t : textures_)
        if (t.id == id && t.retireIn < 0) t.retireIn = kFramesInFlight;
}

void TexturePool::advanceFrame() {
    for (Tex& t : textures_)
        if (t.retireIn > 0) --t.retireIn;
    auto dead = std::partition(textures_.begin(), textures_.end(),
                               [](const Tex& t) { return t.retireIn != 0; });
    for (auto it = dead; it != textures_.end(); ++it) destroy(*it);
    textures_.erase(dead, textures_.end());
}

bool TexturePool::upload(Tex& t, const std::uint8_t* rgba8, bool firstTime, std::string* why) {
    const VkDeviceSize bytes = VkDeviceSize(t.w) * t.h * 4;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = bytes;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (vkCreateBuffer(gpu_.device, &bi, nullptr, &staging) != VK_SUCCESS) {
        if (why) *why = "vkCreateBuffer failed";
        return false;
    }
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(gpu_.device, staging, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(gpu_.device, &ai, nullptr, &stagingMem) != VK_SUCCESS) {
        vkDestroyBuffer(gpu_.device, staging, nullptr);
        if (why) *why = "no host memory for the upload";
        return false;
    }
    vkBindBufferMemory(gpu_.device, staging, stagingMem, 0);
    void* mapped = nullptr;
    vkMapMemory(gpu_.device, stagingMem, 0, bytes, 0, &mapped);
    std::memcpy(mapped, rgba8, static_cast<std::size_t>(bytes));
    vkUnmapMemory(gpu_.device, stagingMem);

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
    toDst.oldLayout = firstTime ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = t.image;
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toDst.srcAccessMask = firstTime ? 0 : VK_ACCESS_SHADER_READ_BIT;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cb, firstTime ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {t.w, t.h, 1};
    vkCmdCopyBufferToImage(cb, staging, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    VkImageMemoryBarrier toRead = toDst;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &toRead);
    vkEndCommandBuffer(cb);

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    vkResetFences(gpu_.device, 1, &fence_);
    const VkResult sent = vkQueueSubmit(gpu_.queue, 1, &si, fence_);
    if (sent == VK_SUCCESS) vkWaitForFences(gpu_.device, 1, &fence_, VK_TRUE, UINT64_MAX);
    vkFreeCommandBuffers(gpu_.device, commands_, 1, &cb);
    vkDestroyBuffer(gpu_.device, staging, nullptr);
    vkFreeMemory(gpu_.device, stagingMem, nullptr);
    if (sent != VK_SUCCESS) {
        if (why) *why = "vkQueueSubmit failed";
        return false;
    }
    return true;
}

void TexturePool::destroy(Tex& t) {
    if (t.set) ImGui_ImplVulkan_RemoveTexture(t.set);
    if (t.view) vkDestroyImageView(gpu_.device, t.view, nullptr);
    if (t.image) vkDestroyImage(gpu_.device, t.image, nullptr);
    if (t.memory) vkFreeMemory(gpu_.device, t.memory, nullptr);
    t = Tex{};
}

}  // namespace icapp
