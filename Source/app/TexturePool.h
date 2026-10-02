#pragma once
// As texturas que o app desenha (o canvas, as miniaturas), subidas da CPU
// para o dispositivo da TELA e entregues ao ImGui como `ImTextureID`.
//
// Duas regras, as mesmas que o pool do Onyx seguia:
//  - `update` nao muda o tamanho: quem precisa de outro tamanho remove e cria
//    (o `RenderCoordinator` ja faz isso);
//  - `remove` nao destroi na hora. Um quadro ainda na GPU pode ter um comando
//    de desenho contra o id, entao ele so morre `kFramesInFlight` chamadas de
//    `advanceFrame` depois.
#include "Source/app/Shell.h"

#include "imgui.h"

#include <cstdint>
#include <string>
#include <vector>

namespace icapp {

class TexturePool {
public:
    explicit TexturePool(const Gpu& gpu);
    ~TexturePool();
    TexturePool(const TexturePool&) = delete;
    TexturePool& operator=(const TexturePool&) = delete;

    // Zero, com o motivo em `why`, quando nao deu.
    ImTextureID create(std::uint32_t w, std::uint32_t h, const std::uint8_t* rgba8, std::string* why);
    bool update(ImTextureID id, const std::uint8_t* rgba8, std::string* why);
    void remove(ImTextureID id);
    // A vista da imagem por tras de um id, para quem a amostra num pipeline
    // proprio (StageCompositor.h). Nula se o id nao e daqui ou ja foi removido.
    VkImageView view(ImTextureID id) const;
    // Uma vez por quadro desenhado, depois de todo upload dele.
    void advanceFrame();

private:
    static constexpr int kFramesInFlight = 3;
    struct Tex {
        ImTextureID id = 0;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        std::uint32_t w = 0, h = 0;
        int retireIn = -1;   // -1: viva; >= 0: quadros ate morrer
    };
    bool upload(Tex& t, const std::uint8_t* rgba8, bool firstTime, std::string* why);
    void destroy(Tex& t);
    std::uint32_t memoryType(std::uint32_t bits, VkMemoryPropertyFlags props) const;

    Gpu gpu_;
    VkCommandPool commands_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    std::vector<Tex> textures_;
};

}  // namespace icapp
