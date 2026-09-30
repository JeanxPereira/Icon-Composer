#pragma once
// A janela do editor: um dispositivo Vulkan so para a tela, o Dear ImGui com
// os backends, e o laco de quadros. Tudo o que o Onyx dava e o editor usava.
//
// DUAS IMPLEMENTACOES, uma por sistema:
//   ShellWin32.cpp -- a do Windows (30/09): janela Win32 criada com
//     `WS_EX_NOREDIRECTIONBITMAP`, o quadro do Vulkan numa textura D3D11
//     compartilhada e composto pelo DirectComposition com o canto de 16 pt do
//     macOS 27 [BIN], e a sombra do SkyLight numa janela propria atras dela;
//   ShellGlfw.cpp  -- a dos outros: GLFW, uma swapchain Vulkan opaca.
//
// Este dispositivo e o da TELA. O render do icone continua no `rb::Device` da
// RenderBox, que e outro (Window.cpp diz por que dois): as texturas do canvas
// passam de um para o outro pela CPU, no `TexturePool`.
#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace icapp {

// O que o `TexturePool` precisa para subir uma imagem. Nada aqui e dono de nada.
struct Gpu {
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
};

class Shell {
public:
    // Null, com o motivo em `why`, quando nao ha janela, Vulkan ou compositor.
    static std::unique_ptr<Shell> create(const std::string& title, std::string* why);
    ~Shell();
    Shell(const Shell&) = delete;
    Shell& operator=(const Shell&) = delete;

    const Gpu& gpu() const;
    // O fator do DPI do monitor da janela, com que o tema foi escalado.
    float dpiScale() const;

    // Um quadro por volta, entre `NewFrame` e `Render`, ate a janela fechar.
    void run(const std::function<void()>& frame);

    void setTitle(const std::string& title);
    // A altura da barra de titulo desenhada pelo app, em pixels de tela: e
    // onde o sistema deixa arrastar a janela (NativeWindow.h).
    void setTitleBarHeight(float px);
    // Um caminho solto sobre a janela (arrastar e soltar). Chamado na thread
    // principal, de dentro do tratamento de mensagens: quem recebe so anota.
    void onDrop(std::function<void(const std::filesystem::path&)> handler);

    void close();
    void minimize();
    void toggleMaximize();
    bool maximized() const;
    bool focused() const;
    // O raio do canto da janela na tela, em pixels: 16 pt [BIN] no Windows
    // (o recorte do compositor, ShellWin32.cpp), o do DWM (8 px) na GLFW, e
    // zero maximizada. O rim de dentro e desenhado nele.
    float cornerRadius() const;

    struct Impl;

private:
    Shell() = default;
    std::unique_ptr<Impl> impl_;
};

}  // namespace icapp
