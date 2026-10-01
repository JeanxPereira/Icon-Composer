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

// A projecao e o deslocamento do desenho, separados da matriz.
struct CoverageViewport {
    std::int32_t originX = 0, originY = 0;
    std::uint32_t width = 0, height = 0;   // 0 == a extensao do alvo
};

// O que vai no push constant. `PathGlobals` e a struct do ALVO e tem 52 bytes
// cravados; a origem do buffer nao e dele e por isso mora depois, em dois
// escalares (um `ivec2` alinharia a 8 e empurraria o offset para 56).
struct CoveragePush {
    PathGlobals globals;
    std::int32_t gridOriginX = 0;
    std::int32_t gridOriginY = 0;
};
static_assert(sizeof(CoveragePush) == 60, "52 do alvo mais dois inteiros");

class CoveragePass {
public:
    static Result<CoveragePass> create(Device& device, VkFormat format = kCoverageFormat);

    CoveragePass() = default;
    ~CoveragePass();
    CoveragePass(CoveragePass&&) noexcept;
    CoveragePass& operator=(CoveragePass&&) noexcept;
    CoveragePass(const CoveragePass&) = delete;
    CoveragePass& operator=(const CoveragePass&) = delete;

    // Clears `target` and draws every edge of `path` into it. Leaves the image in
    // TRANSFER_SRC_OPTIMAL, ready for readBack.
    Result<void> draw(Device& device, Image& target, const PathBuffer& path,
                      const PathGlobals& globals, CoverageViewport viewport = CoverageViewport{});

    // AS DUAS METADES DE `draw`, PARA QUEM GRAVA O SEU PROPRIO COMMAND BUFFER.
    //
    // `draw` submete e espera, uma vez por forma: e o que o caminho de CPU quer,
    // porque ele le a cobertura de volta logo em seguida. O caminho residente
    // (`renderIconGpu`) nao le: ele grava o desenho, a copia da cobertura e o
    // resolve de todas as formas de uma camada NUM command buffer so. `draw` e
    // exatamente `check` + `record` com o conjunto e o framebuffer dele, entao os
    // dois caminhos emitem os mesmos comandos por forma.
    //
    // `check` e a guarda de `draw` (cabecalho contra segmentos, limites de
    // viewport do aparelho). `record` usa `set`, que o chamador aloca de
    // `setLayout()` com o buffer de segmentos no binding 0, e um framebuffer
    // feito sobre `renderPass()`. O alvo termina em TRANSFER_SRC_OPTIMAL.
    Result<void> check(const Device& device, std::uint32_t targetWidth,
                       std::uint32_t targetHeight, const PathBuffer& path,
                       CoverageViewport viewport) const;
    void record(VkCommandBuffer cmd, VkFramebuffer framebuffer, VkDescriptorSet set,
                std::uint32_t width, std::uint32_t height, const PathBuffer& path,
                const PathGlobals& globals, CoverageViewport viewport) const;
    VkRenderPass renderPass() const { return renderPass_; }
    VkDescriptorSetLayout setLayout() const { return setLayout_; }

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
