#include "Source/RenderBox/GpuResident.h"

#include <algorithm>
#include <cstring>

static const std::uint32_t kPaintSpirv[] =
#include "icon_paint.comp.inc"
    ;
static const std::uint32_t kChicletSpirv[] =
#include "icon_chiclet.comp.inc"
    ;
static const std::uint32_t kBlendSpirv[] =
#include "icon_blend.comp.inc"
    ;
static const std::uint32_t kSvgSpirv[] =
#include "icon_svg.comp.inc"
    ;
static const std::uint32_t kFinishSpirv[] =
#include "icon_finish.comp.inc"
    ;
static const std::uint32_t kMaskSpirv[] =
#include "icon_mask.comp.inc"
    ;
static const std::uint32_t kRasterSpirv[] =
#include "icon_raster.comp.inc"
    ;
static const std::uint32_t kFieldSpirv[] =
#include "icon_field.comp.inc"
    ;
static const std::uint32_t kRingSpirv[] =
#include "icon_ring.comp.inc"
    ;
static const std::uint32_t kShadowSpirv[] =
#include "icon_shadow.comp.inc"
    ;
static const std::uint32_t kBlurSpirv[] =
#include "icon_blur.comp.inc"
    ;

namespace rb::gpu {
namespace {

constexpr VkBufferUsageFlags kStorageUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                             VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                             VK_BUFFER_USAGE_TRANSFER_DST_BIT;

// A arena cresce em pedacos deste tamanho (ou do pedido, se maior).
constexpr VkDeviceSize kArenaChunk = 16u << 20;

// O teto do pool entre renders. Acima dele `trim` devolve o que sobra.
constexpr VkDeviceSize kPoolCeiling = 768u << 20;

constexpr std::uint32_t kSetsPerPool = 512;
constexpr std::uint32_t kMaxBindings = 5;

// A barreira entre dois passos gravados: toda escrita antes, toda leitura e
// escrita depois, em todo estagio. Larga de proposito -- ver GpuResident.h.
void fullBarrier(const DeviceApi& api, VkCommandBuffer cmd) {
    VkMemoryBarrier mb{};
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                       VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                       VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_HOST_READ_BIT |
                       VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    api.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0,
                             1, &mb, 0, nullptr, 0, nullptr);
}

}  // namespace

// ---- Kernel -------------------------------------------------------------------

Result<Kernel> Kernel::create(Device& device, const std::uint32_t* spirv, std::size_t byteCount,
                              std::uint32_t bindings, std::uint32_t pushBytes) {
    Kernel k;
    k.device_ = device.handle();
    k.api_ = &device.api();
    k.bindings_ = bindings;
    k.pushBytes_ = pushBytes;
    const DeviceApi& api = device.api();

    VkShaderModuleCreateInfo smi{};
    smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smi.codeSize = byteCount;
    smi.pCode = spirv;
    if (VkResult r = api.vkCreateShaderModule(k.device_, &smi, nullptr, &k.module_); r != VK_SUCCESS) {
        return std::unexpected(std::string("vkCreateShaderModule: ") + describe(r));
    }
    std::vector<VkDescriptorSetLayoutBinding> b(bindings);
    for (std::uint32_t i = 0; i < bindings; ++i) {
        b[i].binding = i;
        b[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dsl{};
    dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl.bindingCount = bindings;
    dsl.pBindings = b.data();
    if (VkResult r = api.vkCreateDescriptorSetLayout(k.device_, &dsl, nullptr, &k.setLayout_);
        r != VK_SUCCESS) {
        return std::unexpected(std::string("vkCreateDescriptorSetLayout: ") + describe(r));
    }
    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push.size = pushBytes;
    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &k.setLayout_;
    pli.pushConstantRangeCount = pushBytes > 0 ? 1 : 0;
    pli.pPushConstantRanges = &push;
    if (VkResult r = api.vkCreatePipelineLayout(k.device_, &pli, nullptr, &k.layout_);
        r != VK_SUCCESS) {
        return std::unexpected(std::string("vkCreatePipelineLayout: ") + describe(r));
    }
    VkComputePipelineCreateInfo cpi{};
    cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.module = k.module_;
    cpi.stage.pName = "main";
    cpi.layout = k.layout_;
    if (VkResult r = api.vkCreateComputePipelines(k.device_, VK_NULL_HANDLE, 1, &cpi, nullptr,
                                                  &k.pipeline_);
        r != VK_SUCCESS) {
        return std::unexpected(std::string("vkCreateComputePipelines: ") + describe(r));
    }
    return k;
}

void Kernel::destroy() {
    if (device_ == VK_NULL_HANDLE) return;
    if (pipeline_) api_->vkDestroyPipeline(device_, pipeline_, nullptr);
    if (layout_) api_->vkDestroyPipelineLayout(device_, layout_, nullptr);
    if (setLayout_) api_->vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);
    if (module_) api_->vkDestroyShaderModule(device_, module_, nullptr);
    device_ = VK_NULL_HANDLE;
    pipeline_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    setLayout_ = VK_NULL_HANDLE;
    module_ = VK_NULL_HANDLE;
}

Kernel::~Kernel() { destroy(); }

Kernel::Kernel(Kernel&& o) noexcept { *this = std::move(o); }

Kernel& Kernel::operator=(Kernel&& o) noexcept {
    if (this != &o) {
        destroy();
        device_ = o.device_;
        api_ = o.api_;
        module_ = o.module_;
        setLayout_ = o.setLayout_;
        layout_ = o.layout_;
        pipeline_ = o.pipeline_;
        bindings_ = o.bindings_;
        pushBytes_ = o.pushBytes_;
        o.device_ = VK_NULL_HANDLE;
        o.module_ = VK_NULL_HANDLE;
        o.setLayout_ = VK_NULL_HANDLE;
        o.layout_ = VK_NULL_HANDLE;
        o.pipeline_ = VK_NULL_HANDLE;
    }
    return *this;
}

// ---- Resident -------------------------------------------------------------------

Result<Resident*> Resident::of(Device& device) {
    std::shared_ptr<void>& slot = device.residentState();
    if (slot) return static_cast<Resident*>(slot.get());

    std::shared_ptr<Resident> r(new Resident(device));
    r->alive_ = std::make_shared<Resident*>(r.get());
    r->alignment_ = std::max<VkDeviceSize>(
        16, device.limits().minStorageBufferOffsetAlignment);

    struct Spec {
        Kernel* k;
        const std::uint32_t* code;
        std::size_t bytes;
        std::uint32_t bindings;
        std::uint32_t push;
    };
    const Spec specs[] = {
        {&r->paint, kPaintSpirv, sizeof kPaintSpirv, 2, 56},
        {&r->chiclet, kChicletSpirv, sizeof kChicletSpirv, 3, 28},
        {&r->blend, kBlendSpirv, sizeof kBlendSpirv, 2, 20},
        {&r->svg, kSvgSpirv, sizeof kSvgSpirv, 5, 100},
        {&r->finish, kFinishSpirv, sizeof kFinishSpirv, 2, 20},
        {&r->mask, kMaskSpirv, sizeof kMaskSpirv, 4, 20},
        {&r->raster, kRasterSpirv, sizeof kRasterSpirv, 3, 16},
    };
    for (const Spec& s : specs) {
        auto k = Kernel::create(device, s.code, s.bytes, s.bindings, s.push);
        if (!k) return std::unexpected(k.error());
        *s.k = std::move(*k);
    }
    // Os kernels em double so existem onde o aparelho tem `shaderFloat64`; sem
    // eles o vidro fica na CPU (`Resident::float64`).
    if (device.float64()) {
        const Spec wide[] = {
            {&r->field, kFieldSpirv, sizeof kFieldSpirv, 5, 52},
            {&r->ring, kRingSpirv, sizeof kRingSpirv, 2, 12},
            {&r->shadow, kShadowSpirv, sizeof kShadowSpirv, 4, 64},
            {&r->blur, kBlurSpirv, sizeof kBlurSpirv, 3, 28},
        };
        for (const Spec& s : wide) {
            auto k = Kernel::create(device, s.code, s.bytes, s.bindings, s.push);
            if (!k) return std::unexpected(k.error());
            *s.k = std::move(*k);
        }
    }
    auto pass = CoveragePass::create(device);
    if (!pass) return std::unexpected(pass.error());
    r->coverage = std::move(*pass);

    auto dummy = r->acquire(16);
    if (!dummy) return std::unexpected(dummy.error());
    r->dummy_ = *dummy;

    slot = r;
    return r.get();
}

Resident::~Resident() {
    const DeviceApi& api = device_->api();
    VkDevice dev = device_->handle();
    *alive_ = nullptr;
    for (VkDescriptorPool p : pools_) api.vkDestroyDescriptorPool(dev, p, nullptr);
    if (covFramebuffer_) api.vkDestroyFramebuffer(dev, covFramebuffer_, nullptr);
}

void Resident::release(Buffer* b) {
    const VkDeviceSize size = b->size();
    free_[size].emplace_back(b);
}

Result<Slab> Resident::acquire(VkDeviceSize bytes) {
    std::unique_ptr<Buffer> buf;
    auto it = free_.find(bytes);
    if (it != free_.end() && !it->second.empty()) {
        buf = std::move(it->second.back());
        it->second.pop_back();
    } else {
        auto made = Buffer::create(*device_, bytes, kStorageUsage,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (!made) return std::unexpected(made.error());
        buf = std::make_unique<Buffer>(std::move(*made));
    }
    std::weak_ptr<Resident*> alive = alive_;
    return Slab(buf.release(), [alive](Buffer* b) {
        if (auto owner = alive.lock(); owner && *owner) {
            (*owner)->release(b);
        } else {
            delete b;
        }
    });
}

void Resident::trim() {
    VkDeviceSize held = 0;
    for (auto& [size, list] : free_) held += size * list.size();
    for (auto it = free_.rbegin(); it != free_.rend() && held > kPoolCeiling; ++it) {
        while (!it->second.empty() && held > kPoolCeiling) {
            held -= it->first;
            it->second.pop_back();
        }
    }
}

Result<Range> Resident::stage(const void* data, std::size_t bytes) {
    const VkDeviceSize want = std::max<VkDeviceSize>(bytes, 4);
    ArenaChunk* chunk = nullptr;
    for (ArenaChunk& c : arena_) {
        const VkDeviceSize at = (c.used + alignment_ - 1) / alignment_ * alignment_;
        if (at + want <= c.buffer.size()) {
            c.used = at;
            chunk = &c;
            break;
        }
    }
    if (!chunk) {
        auto made = Buffer::create(*device_, std::max(kArenaChunk, want),
                                   kStorageUsage, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (!made) return std::unexpected(made.error());
        arena_.push_back(ArenaChunk{std::move(*made), 0});
        chunk = &arena_.back();
    }
    Range r{chunk->buffer.handle(), chunk->used, want};
    if (bytes > 0) std::memcpy(static_cast<char*>(chunk->buffer.mapped()) + chunk->used, data, bytes);
    chunk->used += want;
    return r;
}

Result<VkDescriptorPool> Resident::newPool() {
    VkDescriptorPoolSize size{};
    size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    size.descriptorCount = kSetsPerPool * kMaxBindings;
    VkDescriptorPoolCreateInfo dpi{};
    dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.maxSets = kSetsPerPool;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &size;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (VkResult r = device_->api().vkCreateDescriptorPool(device_->handle(), &dpi, nullptr, &pool);
        r != VK_SUCCESS) {
        return std::unexpected(std::string("vkCreateDescriptorPool: ") + describe(r));
    }
    pools_.push_back(pool);
    return pool;
}

Result<VkDescriptorSet> Resident::descriptorSet(VkDescriptorSetLayout layout,
                                               std::initializer_list<Range> buffers) {
    const DeviceApi& api = device_->api();
    VkDescriptorSet set = VK_NULL_HANDLE;
    // Tenta os pools que ja existem, do ultimo para tras; um novo so se todos
    // estiverem cheios.
    bool got = false;
    for (auto it = pools_.rbegin(); it != pools_.rend() && !got; ++it) {
        VkDescriptorSetAllocateInfo dsa{};
        dsa.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsa.descriptorPool = *it;
        dsa.descriptorSetCount = 1;
        dsa.pSetLayouts = &layout;
        got = api.vkAllocateDescriptorSets(device_->handle(), &dsa, &set) == VK_SUCCESS;
    }
    if (!got) {
        auto pool = newPool();
        if (!pool) return std::unexpected(pool.error());
        VkDescriptorSetAllocateInfo dsa{};
        dsa.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsa.descriptorPool = *pool;
        dsa.descriptorSetCount = 1;
        dsa.pSetLayouts = &layout;
        if (VkResult r = api.vkAllocateDescriptorSets(device_->handle(), &dsa, &set);
            r != VK_SUCCESS) {
            return std::unexpected(std::string("vkAllocateDescriptorSets: ") + describe(r));
        }
    }
    VkDescriptorBufferInfo infos[kMaxBindings]{};
    VkWriteDescriptorSet writes[kMaxBindings]{};
    std::uint32_t n = 0;
    for (const Range& r : buffers) {
        infos[n].buffer = r.buffer;
        infos[n].offset = r.offset;
        infos[n].range = r.size;
        writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[n].dstSet = set;
        writes[n].dstBinding = n;
        writes[n].descriptorCount = 1;
        writes[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[n].pBufferInfo = &infos[n];
        ++n;
    }
    api.vkUpdateDescriptorSets(device_->handle(), n, writes, 0, nullptr);
    return set;
}

void Resident::record(std::function<void(VkCommandBuffer)> op) { ops_.push_back(std::move(op)); }

Result<void> Resident::dispatch(const Kernel& k, std::initializer_list<Range> buffers,
                                const void* push, std::uint32_t groupsX, std::uint32_t groupsY) {
    if (buffers.size() != k.bindings()) {
        return std::unexpected(std::string("dispatch com o numero errado de buffers"));
    }
    auto set = descriptorSet(k.setLayout(), buffers);
    if (!set) return std::unexpected(set.error());
    std::vector<std::uint8_t> bytes(static_cast<const std::uint8_t*>(push),
                                    static_cast<const std::uint8_t*>(push) + k.pushBytes());
    const DeviceApi* api = &device_->api();
    VkPipeline pipeline = k.pipeline();
    VkPipelineLayout layout = k.layout();
    VkDescriptorSet ds = *set;
    record([=, bytes = std::move(bytes)](VkCommandBuffer cmd) {
        api->vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        api->vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &ds, 0,
                                     nullptr);
        if (!bytes.empty()) {
            api->vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                    static_cast<std::uint32_t>(bytes.size()), bytes.data());
        }
        api->vkCmdDispatch(cmd, groupsX, groupsY, 1);
    });
    return {};
}

void Resident::fill(const Slab& s, std::uint32_t value) {
    const DeviceApi* api = &device_->api();
    VkBuffer b = s->handle();
    record([=](VkCommandBuffer cmd) { api->vkCmdFillBuffer(cmd, b, 0, VK_WHOLE_SIZE, value); });
}

void Resident::copy(Range from, const Slab& to) {
    const DeviceApi* api = &device_->api();
    VkBuffer dst = to->handle();
    const VkDeviceSize bytes = std::min(from.size, to->size());
    record([=](VkCommandBuffer cmd) {
        VkBufferCopy region{};
        region.srcOffset = from.offset;
        region.size = bytes;
        api->vkCmdCopyBuffer(cmd, from.buffer, dst, 1, &region);
    });
}

Result<void> Resident::flush() {
    if (!ops_.empty()) {
        const DeviceApi& api = device_->api();
        auto ran = device_->submitAndWait([&](VkCommandBuffer cmd) {
            for (auto& op : ops_) {
                op(cmd);
                fullBarrier(api, cmd);
            }
        });
        ops_.clear();
        if (!ran) return std::unexpected(ran.error());
    }
    for (ArenaChunk& c : arena_) c.used = 0;
    for (VkDescriptorPool p : pools_) device_->api().vkResetDescriptorPool(device_->handle(), p, 0);
    return {};
}

Result<void> Resident::download(const Slab& s, void* out, std::size_t bytes) {
    if (readback_.size() < bytes) {
        auto made = Buffer::create(*device_, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                       VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
        if (!made) {
            made = Buffer::create(*device_, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        }
        if (!made) return std::unexpected(made.error());
        // O antigo pode estar num passo gravado: esvazia antes de trocar.
        if (auto ok = flush(); !ok) return ok;
        readback_ = std::move(*made);
    }
    const DeviceApi* api = &device_->api();
    VkBuffer src = s->handle();
    VkBuffer dst = readback_.handle();
    record([=](VkCommandBuffer cmd) {
        VkBufferCopy region{};
        region.size = bytes;
        api->vkCmdCopyBuffer(cmd, src, dst, 1, &region);
    });
    if (auto ok = flush(); !ok) return ok;
    std::memcpy(out, readback_.mapped(), bytes);
    return {};
}

Result<void> Resident::upload(const Slab& s, const void* data, std::size_t bytes) {
    auto staged = stage(data, bytes);
    if (!staged) return std::unexpected(staged.error());
    copy(*staged, s);
    return {};
}

Result<void> Resident::ensureCoverage(std::uint32_t width, std::uint32_t height) {
    if (covImage_.handle() != VK_NULL_HANDLE && covImage_.width() == width &&
        covImage_.height() == height) {
        return {};
    }
    // O alvo antigo pode estar em passos gravados.
    if (auto ok = flush(); !ok) return ok;
    const DeviceApi& api = device_->api();
    if (covFramebuffer_) {
        api.vkDestroyFramebuffer(device_->handle(), covFramebuffer_, nullptr);
        covFramebuffer_ = VK_NULL_HANDLE;
    }
    auto img = Image::create(*device_, width, height);
    if (!img) return std::unexpected(img.error());
    covImage_ = std::move(*img);
    VkImageView view = covImage_.view();
    VkFramebufferCreateInfo fbi{};
    fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbi.renderPass = coverage.renderPass();
    fbi.attachmentCount = 1;
    fbi.pAttachments = &view;
    fbi.width = width;
    fbi.height = height;
    fbi.layers = 1;
    if (VkResult r = api.vkCreateFramebuffer(device_->handle(), &fbi, nullptr, &covFramebuffer_);
        r != VK_SUCCESS) {
        return std::unexpected(std::string("vkCreateFramebuffer: ") + describe(r));
    }
    auto buf = acquire(static_cast<VkDeviceSize>(width) * height * 4);
    if (!buf) return std::unexpected(buf.error());
    covBuffer_ = *buf;
    return {};
}

}  // namespace rb::gpu
