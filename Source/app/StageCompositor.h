#pragma once
// O MONO COMPOSTO NA GPU DA TELA (02/10).
//
// Ate aqui o vidro simulado e o Clear eram do job de render: CPU, numa thread
// de trabalho, e o resultado descia da GPU do render, passava pela CPU e subia
// para a da tela. Mover o icone pedia tudo de novo, e o vidro chegava quadros
// depois do icone. Isto e o mesmo calculo num fragment shader do dispositivo
// da TELA (shaders/stage.frag), desenhado dentro do passe do ImGui por um
// callback da draw list: a cada quadro, na posicao do quadro.
//
// O que ele precisa que nao muda com a posicao sobe uma vez: a lente da
// pastilha (`rb::simulatedGlassLens`) e a imagem de fundo, com mips. O icone
// de antes do vidro e a textura que o coordenador ja sobe (`TexturePool`).
#include "Source/IconComposerKit/Widgets.h"
#include "Source/RenderBox/SimulatedGlass.h"
#include "Source/app/Shell.h"
#include "Source/app/TexturePool.h"

#include "imgui.h"

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace icapp {

class StageCompositor {
public:
    StageCompositor(const Gpu& gpu, TexturePool& pool);
    ~StageCompositor();
    StageCompositor(const StageCompositor&) = delete;
    StageCompositor& operator=(const StageCompositor&) = delete;

    // Falso quando o pipeline nao pode ser montado (o motivo foi para o stderr):
    // o Kit fica com o vidro na CPU.
    bool ready() const { return pipeline_ != VK_NULL_HANDLE; }

    // A imagem de fundo `index`, com mips. Idempotente.
    void setBackdrop(int index, const std::shared_ptr<const ick::StagePixels>& pixels);

    bool compose(ImDrawList* dl, const ick::MonoStageDraw& d);

private:
    struct Texture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        std::uint32_t width = 0, height = 0;
    };
    struct Lens {
        Texture texture;
        rb::SimulatedGlassLens info;   // sem os texels
    };
    bool build();
    std::uint32_t memoryType(std::uint32_t bits, VkMemoryPropertyFlags props) const;
    // `levels[i]` e o nivel i, `bytesPerTexel` por texel.
    bool createTexture(Texture& t, VkFormat format, std::uint32_t w, std::uint32_t h,
                       const std::vector<std::vector<std::uint8_t>>& levels, std::uint32_t bytesPerTexel);
    void destroyTexture(Texture& t);
    const Lens* lens(bool watch);

    Gpu gpu_;
    TexturePool& pool_;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkSampler linear_ = VK_NULL_HANDLE, trilinear_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptors_ = VK_NULL_HANDLE;
    VkCommandPool commands_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    // Um anel de conjuntos: cada desenho escreve o seguinte, e quando o anel
    // da a volta o quadro que usou aquele conjunto ja saiu da GPU.
    std::vector<VkDescriptorSet> sets_;
    std::size_t nextSet_ = 0;
    Texture white_;
    std::map<int, Texture> backdrops_;
    Lens lens_[2];
    bool lensTried_[2] = {false, false};
};

}  // namespace icapp
