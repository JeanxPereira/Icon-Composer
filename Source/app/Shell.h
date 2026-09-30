#pragma once
// A janela do editor: GLFW, um dispositivo Vulkan so para a tela, a swapchain
// e o Dear ImGui com os dois backends. Tudo o que o Onyx dava e o editor
// usava, e nada do que ele dava e o editor escondia.
//
// Este dispositivo e o da TELA. O render do icone continua no `rb::Device` da
// RenderBox, que e outro (Window.cpp diz por que dois): as texturas do canvas
// passam de um para o outro pela CPU, no `TexturePool`.
#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

struct GLFWwindow;

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
    // Null, com o motivo em `why`, quando nao ha GLFW, Vulkan ou superficie.
    static std::unique_ptr<Shell> create(const std::string& title, std::string* why);
    ~Shell();
    Shell(const Shell&) = delete;
    Shell& operator=(const Shell&) = delete;

    GLFWwindow* window() const { return window_; }
    const Gpu& gpu() const { return gpu_; }

    // Um quadro por volta, entre `NewFrame` e `Render`, ate a janela fechar.
    void run(const std::function<void()>& frame);

    void setTitle(const std::string& title);
    // A altura da barra de titulo desenhada pelo app, em pixels de tela: e
    // onde o sistema deixa arrastar a janela (NativeWindow.h).
    void setTitleBarHeight(float px);

    void close();
    void minimize();
    void toggleMaximize();
    bool maximized() const;
    bool focused() const;

private:
    Shell() = default;
    bool init(const std::string& title, std::string* why);
    void rebuildSwapchain(int w, int h);
    void renderFrame();
    void present();

    struct Vk;
    std::unique_ptr<Vk> vk_;
    GLFWwindow* window_ = nullptr;
    Gpu gpu_;
    std::string title_;
    bool rebuild_ = false;
};

}  // namespace icapp
