#pragma once
// O CAMINHO RESIDENTE: o que `renderIconGpu` precisa para manter a cadeia na
// GPU do comeco ao fim (frente GPU, G1 -- `Docs/Plans/2026-09-29-render-gpu.md`).
//
// TRES PECAS, E POR QUE ESTAS TRES
// --------------------------------
// 1. UM LOTE. `Device::submitAndWait` e uma submissao com espera; o caminho de
//    CPU faz uma por forma (a cobertura e lida de volta na hora). Aqui os passos
//    sao GRAVADOS numa lista e so vao para a fila quando alguem precisa de um
//    numero de volta (`flush`): um readback declarado, ou o fim do render. Entre
//    dois passos gravados vai uma barreira de memoria global -- a ordem dos
//    passos e a ordem da CPU, e o custo de uma barreira larga e nada perto do
//    custo de uma espera.
// 2. MEMORIA. Os buffers da GPU (acumulador, alvo de grupo, arte de camada,
//    mascara de clip) sao pedacos de blocos grandes (`kHeapBlock`) e voltam ao
//    bloco quando o ultimo `shared_ptr` morre. `[ART]` Ate 29/09 eram um pool
//    por TAMANHO EXATO de alocacoes proprias: um `vkAllocateMemory` de 16 MB
//    custa ~7 ms neste aparelho (RX 6750 XT), todo tamanho novo (um ladrilho de
//    outra largura, uma grade de segmentos) pagava um, e o que o `RenderCache`
//    guardava nunca voltava ao pool -- o primeiro render com um cache vazio saia
//    MAIS LENTO que sem cache (Apollo 1024: 148 contra 88 ms). Agora um buffer
//    novo e um `vkCreateBuffer` + bind num pedaco livre. Reusar um buffer DENTRO
//    do lote e seguro pela mesma barreira global. O que sobe da CPU (segmentos
//    de caminho, paradas de rampa, uma sombra feita na CPU) vai para uma arena
//    visivel ao host, reposta a cada `flush`.
// 3. OS KERNELS. Sete pipelines de compute (shaders/icon_*.comp) e a passada de
//    cobertura, montados UMA vez por aparelho e guardados em
//    `Device::residentState()`.
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "Source/RenderBox/Buffer.h"
#include "Source/RenderBox/CoveragePass.h"
#include "Source/RenderBox/Device.h"
#include "Source/RenderBox/Image.h"

namespace rb::gpu {

// Um pipeline de compute com `bindings` buffers de armazenamento (0..n-1) e um
// bloco de push constants. Grupos de 16x16.
class Kernel {
public:
    static Result<Kernel> create(Device& device, const std::uint32_t* spirv,
                                 std::size_t byteCount, std::uint32_t bindings,
                                 std::uint32_t pushBytes);
    Kernel() = default;
    ~Kernel();
    Kernel(Kernel&&) noexcept;
    Kernel& operator=(Kernel&&) noexcept;
    Kernel(const Kernel&) = delete;
    Kernel& operator=(const Kernel&) = delete;

    VkPipeline pipeline() const { return pipeline_; }
    VkPipelineLayout layout() const { return layout_; }
    VkDescriptorSetLayout setLayout() const { return setLayout_; }
    std::uint32_t bindings() const { return bindings_; }
    std::uint32_t pushBytes() const { return pushBytes_; }

private:
    void destroy();
    VkDevice device_ = VK_NULL_HANDLE;
    const DeviceApi* api_ = nullptr;
    VkShaderModule module_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    std::uint32_t bindings_ = 0;
    std::uint32_t pushBytes_ = 0;
};

// Um pedaco de buffer para um binding.
struct Range {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = VK_WHOLE_SIZE;
};

using Slab = std::shared_ptr<Buffer>;
inline Range whole(const Slab& s) { return Range{s->handle(), 0, s->size()}; }

class Resident {
public:
    // O estado residente deste aparelho, montado na primeira chamada.
    static Result<Resident*> of(Device& device);

    ~Resident();

    Device& device() { return *device_; }
    // Um render residente por vez por aparelho: o lote e a arena sao unicos.
    std::mutex& mutex() { return mutex_; }

    Kernel paint, chiclet, blend, svg, finish, raster;
    // O vidro (GpuGlass.cpp). Os em double so sao montados com `float64()`.
    Kernel field, ring, shadow, blur, highlight, glassMask, displace, refract;
    bool float64() const { return device_->float64(); }
    CoveragePass coverage;

    // ---- memoria ----------------------------------------------------------
    // Um buffer da GPU de `bytes` (conteudo indefinido). Volta ao pool quando o
    // ultimo dono solta.
    Result<Slab> acquire(VkDeviceSize bytes);
    // `bytes` copiados para a arena visivel ao host; valido ate o proximo flush.
    Result<Range> stage(const void* data, std::size_t bytes);

    // ---- o lote -----------------------------------------------------------
    // Grava um passo; uma barreira global vai depois dele.
    void record(std::function<void(VkCommandBuffer)> op);
    Result<void> dispatch(const Kernel& k, std::initializer_list<Range> buffers,
                          const void* push, std::uint32_t groupsX, std::uint32_t groupsY);
    // Um descriptor set do lote para `layout`, com `buffers` nos bindings 0..n-1.
    Result<VkDescriptorSet> descriptorSet(VkDescriptorSetLayout layout,
                                          std::initializer_list<Range> buffers);
    void fill(const Slab& s, std::uint32_t value);
    void copy(Range from, const Slab& to);
    // Envia o lote e espera.
    Result<void> flush();

    // Um buffer inteiro de volta para a CPU: grava a copia e faz `flush`.
    Result<void> download(const Slab& s, void* out, std::size_t bytes);
    // A CPU para um buffer: arena + copia gravada (sem flush).
    Result<void> upload(const Slab& s, const void* data, std::size_t bytes);

    // ---- a cobertura ------------------------------------------------------
    // O alvo `R16G16` da passada de cobertura e o buffer para onde ele e copiado,
    // do tamanho pedido (refeitos, com flush, quando o tamanho muda).
    Result<void> ensureCoverage(std::uint32_t width, std::uint32_t height);
    Image& coverageImage() { return covImage_; }
    VkFramebuffer coverageFramebuffer() const { return covFramebuffer_; }
    const Slab& coverageBuffer() const { return covBuffer_; }

    // Um buffer de uma palavra, para binding que o shader nao le.
    const Slab& dummy() const { return dummy_; }

    // Devolve ao aparelho os blocos vazios alem de um teto.
    void trim();
    // Quanto o heap tem alocado no aparelho (para medir).
    VkDeviceSize heapBytes() const;

private:
    explicit Resident(Device& device) : device_(&device) {}
    Result<VkDescriptorPool> newPool();
    void release(Buffer* b);

    Device* device_;
    std::mutex mutex_;

    std::vector<std::function<void(VkCommandBuffer)>> ops_;

    std::vector<VkDescriptorPool> pools_;

    struct ArenaChunk {
        Buffer buffer;
        VkDeviceSize used = 0;
    };
    std::vector<ArenaChunk> arena_;
    VkDeviceSize alignment_ = 256;

    // O HEAP: blocos grandes de memoria do aparelho, e cada `Slab` e um buffer
    // posto num pedaco de um deles. Um pedaco livre por bloco, `offset ->
    // tamanho`, juntado aos vizinhos quando volta.
    struct HeapBlock {
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        std::map<VkDeviceSize, VkDeviceSize> free;
    };
    struct Piece {
        std::size_t block = 0;
        VkDeviceSize offset = 0;
        VkDeviceSize size = 0;
    };
    Result<Buffer::Placement> carve(const VkMemoryRequirements& req, Piece& piece);
    void giveBack(const Piece& piece);
    std::vector<HeapBlock> heap_;
    std::map<const Buffer*, Piece> pieces_;
    // Os VkBuffer soltos desde o ultimo `flush` (os passos gravados ainda os citam).
    std::vector<std::unique_ptr<Buffer>> retired_;
    std::uint32_t heapType_ = UINT32_MAX;
    // O que os pedacos vivos somam, e o maior valor desde o ultimo `trim`.
    VkDeviceSize inUse_ = 0;
    VkDeviceSize peak_ = 0;
    // Os picos dos ultimos renders (o teto de `trim`).
    std::array<VkDeviceSize, 8> peaks_{};
    std::size_t nextPeak_ = 0;
    // Um sinal de vida para o apagador dos `Slab`.
    std::shared_ptr<Resident*> alive_;

    Buffer readback_;

    Image covImage_;
    VkFramebuffer covFramebuffer_ = VK_NULL_HANDLE;
    Slab covBuffer_;
    Slab dummy_;
};

// Grupos de 16 que cobrem `n`.
inline std::uint32_t groups16(std::uint32_t n) { return (n + 15u) / 16u; }

}  // namespace rb::gpu
