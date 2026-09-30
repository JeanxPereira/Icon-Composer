#include "Source/app/Shell.h"

#include "Source/app/NativeWindow.h"
#include "Source/IconComposerKit/Theme.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace icapp {
namespace {

void checkVk(VkResult err) {
    if (err == VK_SUCCESS) return;
    std::fprintf(stderr, "iconcomposer: [vulkan] VkResult = %d\n", err);
    if (err < 0) std::abort();
}

bool hasExtension(const std::vector<VkExtensionProperties>& all, const char* name) {
    for (const auto& p : all)
        if (std::strcmp(p.extensionName, name) == 0) return true;
    return false;
}

// A fonte do sistema no lugar da SF Pro, a mesma troca que o CSS faz
// ("SF Pro Text", ..., "Segoe UI Variable Text"). Sem nenhuma, a embutida.
void loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    const char* candidates[] = {
        "C:/Windows/Fonts/SegUIVar.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
    };
    for (const char* path : candidates) {
        if (FILE* f = std::fopen(path, "rb")) {
            std::fclose(f);
            if (io.Fonts->AddFontFromFileTTF(path, ick::theme::kFontSize)) return;
        }
    }
    io.Fonts->AddFontDefault();
}

// Duas imagens na swapchain: FIFO, sem pressa. O editor nao e um jogo.
constexpr std::uint32_t kMinImageCount = 2;

}  // namespace

struct Shell::Vk {
    VkInstance instance = VK_NULL_HANDLE;
    ImGui_ImplVulkanH_Window wd;
};

std::unique_ptr<Shell> Shell::create(const std::string& title, std::string* why) {
    std::unique_ptr<Shell> s(new Shell());
    if (!s->init(title, why)) return nullptr;
    return s;
}

bool Shell::init(const std::string& title, std::string* why) {
    auto fail = [why](const char* what) {
        if (why) *why = what;
        return false;
    };
    glfwSetErrorCallback([](int code, const char* desc) {
        std::fprintf(stderr, "iconcomposer: [glfw %d] %s\n", code, desc);
    });
    if (!glfwInit()) return fail("GLFW did not initialise");
    if (!glfwVulkanSupported()) return fail("GLFW found no Vulkan loader");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    // Sem a moldura do sistema: a barra de titulo e desenhada pelo app, com
    // as luzes do macOS, e o Win32 so aprende onde ela esta (NativeWindow.h).
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    const float scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    title_ = title;
    window_ = glfwCreateWindow(static_cast<int>(1440 * scale), static_cast<int>(900 * scale),
                               title.c_str(), nullptr, nullptr);
    if (!window_) return fail("GLFW could not create the window");

    vk_ = std::make_unique<Vk>();

    // ---- instancia ----------------------------------------------------------
    std::uint32_t glfwCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwCount);
    std::vector<const char*> exts(glfwExts, glfwExts + glfwCount);
    std::uint32_t n = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> available(n);
    vkEnumerateInstanceExtensionProperties(nullptr, &n, available.data());
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    if (hasExtension(available, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
        exts.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Icon Composer";
    app.apiVersion = VK_API_VERSION_1_3;
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = static_cast<std::uint32_t>(exts.size());
    ici.ppEnabledExtensionNames = exts.data();
    if (vkCreateInstance(&ici, nullptr, &vk_->instance) != VK_SUCCESS)
        return fail("vkCreateInstance failed");

    // ---- dispositivo --------------------------------------------------------
    gpu_.physical = ImGui_ImplVulkanH_SelectPhysicalDevice(vk_->instance);
    if (gpu_.physical == VK_NULL_HANDLE) return fail("no Vulkan physical device");
    gpu_.queueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(gpu_.physical);
    if (gpu_.queueFamily == static_cast<std::uint32_t>(-1)) return fail("no graphics queue");
    const char* devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = gpu_.queueFamily;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qi;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = devExts;
    if (vkCreateDevice(gpu_.physical, &dci, nullptr, &gpu_.device) != VK_SUCCESS)
        return fail("vkCreateDevice failed");
    vkGetDeviceQueue(gpu_.device, gpu_.queueFamily, 0, &gpu_.queue);

    // ---- superficie e swapchain ---------------------------------------------
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(vk_->instance, window_, nullptr, &surface) != VK_SUCCESS)
        return fail("glfwCreateWindowSurface failed");
    VkBool32 wsi = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(gpu_.physical, gpu_.queueFamily, surface, &wsi);
    if (wsi != VK_TRUE) return fail("the graphics queue cannot present to this window");
    auto& wd = vk_->wd;
    wd.Surface = surface;
    const VkFormat formats[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
    wd.SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(gpu_.physical, surface, formats, 2,
                                                             VK_COLORSPACE_SRGB_NONLINEAR_KHR);
    const VkPresentModeKHR modes[] = {VK_PRESENT_MODE_FIFO_KHR};
    wd.PresentMode = ImGui_ImplVulkanH_SelectPresentMode(gpu_.physical, surface, modes, 1);
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(window_, &fbw, &fbh);
    rebuildSwapchain(fbw, fbh);

    // ---- ImGui --------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    io.ConfigDpiScaleFonts = true;
    ick::theme::apply(scale);
    loadFonts();

    ImGui_ImplGlfw_InitForVulkan(window_, true);
    ImGui_ImplVulkan_InitInfo ii{};
    ii.ApiVersion = VK_API_VERSION_1_3;
    ii.Instance = vk_->instance;
    ii.PhysicalDevice = gpu_.physical;
    ii.Device = gpu_.device;
    ii.QueueFamily = gpu_.queueFamily;
    ii.Queue = gpu_.queue;
    // O backend cria o proprio pool: as texturas do app (canvas, miniaturas)
    // saem dele por `ImGui_ImplVulkan_AddTexture`. 256 e folga, nao medida.
    ii.DescriptorPoolSize = 256;
    ii.MinImageCount = kMinImageCount;
    ii.ImageCount = wd.ImageCount;
    ii.PipelineInfoMain.RenderPass = wd.RenderPass;
    ii.PipelineInfoMain.Subpass = 0;
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ii.CheckVkResultFn = checkVk;
    if (!ImGui_ImplVulkan_Init(&ii)) return fail("the ImGui Vulkan backend did not initialise");

    // DEPOIS do backend da GLFW: ele instala os callbacks dele, e o
    // procedimento de janela do Win32 tem de ficar por fora de tudo.
    NativeWindow::install(window_);
    glfwShowWindow(window_);
    return true;
}

Shell::~Shell() {
    if (!vk_) {
        if (window_) glfwDestroyWindow(window_);
        glfwTerminate();
        return;
    }
    if (gpu_.device) vkDeviceWaitIdle(gpu_.device);
    if (ImGui::GetCurrentContext()) {
        if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplVulkan_Shutdown();
        if (ImGui::GetIO().BackendPlatformUserData) ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    if (gpu_.device) {
        ImGui_ImplVulkanH_DestroyWindow(vk_->instance, gpu_.device, &vk_->wd, nullptr);
        vkDestroyDevice(gpu_.device, nullptr);
    }
    if (vk_->wd.Surface) vkDestroySurfaceKHR(vk_->instance, vk_->wd.Surface, nullptr);
    if (vk_->instance) vkDestroyInstance(vk_->instance, nullptr);
    if (window_) glfwDestroyWindow(window_);
    glfwTerminate();
}

void Shell::rebuildSwapchain(int w, int h) {
    ImGui_ImplVulkanH_CreateOrResizeWindow(vk_->instance, gpu_.physical, gpu_.device, &vk_->wd,
                                           gpu_.queueFamily, nullptr, w, h, kMinImageCount, 0);
    vk_->wd.FrameIndex = 0;
    rebuild_ = false;
}

void Shell::renderFrame() {
    auto& wd = vk_->wd;
    VkSemaphore acquired = wd.FrameSemaphores[wd.SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore complete = wd.FrameSemaphores[wd.SemaphoreIndex].RenderCompleteSemaphore;
    VkResult err = vkAcquireNextImageKHR(gpu_.device, wd.Swapchain, UINT64_MAX, acquired,
                                         VK_NULL_HANDLE, &wd.FrameIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) rebuild_ = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
    if (err != VK_SUBOPTIMAL_KHR) checkVk(err);

    ImGui_ImplVulkanH_Frame* fd = &wd.Frames[wd.FrameIndex];
    checkVk(vkWaitForFences(gpu_.device, 1, &fd->Fence, VK_TRUE, UINT64_MAX));
    checkVk(vkResetFences(gpu_.device, 1, &fd->Fence));
    checkVk(vkResetCommandPool(gpu_.device, fd->CommandPool, 0));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkVk(vkBeginCommandBuffer(fd->CommandBuffer, &bi));

    // O fundo por tras de tudo e o da janela do tema: some sob o dockspace,
    // mas e o que aparece num quadro de redimensionamento.
    const ImVec4 bg = ick::theme::kWindowBg;
    wd.ClearValue.color = {{bg.x, bg.y, bg.z, 1.0f}};
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = wd.RenderPass;
    rp.framebuffer = fd->Framebuffer;
    rp.renderArea.extent.width = wd.Width;
    rp.renderArea.extent.height = wd.Height;
    rp.clearValueCount = 1;
    rp.pClearValues = &wd.ClearValue;
    vkCmdBeginRenderPass(fd->CommandBuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), fd->CommandBuffer);
    vkCmdEndRenderPass(fd->CommandBuffer);

    const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &acquired;
    si.pWaitDstStageMask = &stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &fd->CommandBuffer;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &complete;
    checkVk(vkEndCommandBuffer(fd->CommandBuffer));
    checkVk(vkQueueSubmit(gpu_.queue, 1, &si, fd->Fence));
}

void Shell::present() {
    if (rebuild_) return;
    auto& wd = vk_->wd;
    VkSemaphore complete = wd.FrameSemaphores[wd.SemaphoreIndex].RenderCompleteSemaphore;
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &complete;
    pi.swapchainCount = 1;
    pi.pSwapchains = &wd.Swapchain;
    pi.pImageIndices = &wd.FrameIndex;
    const VkResult err = vkQueuePresentKHR(gpu_.queue, &pi);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) rebuild_ = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
    if (err != VK_SUBOPTIMAL_KHR) checkVk(err);
    wd.SemaphoreIndex = (wd.SemaphoreIndex + 1) % wd.SemaphoreCount;
}

void Shell::run(const std::function<void()>& frame) {
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        int w = 0, h = 0;
        glfwGetFramebufferSize(window_, &w, &h);
        if (w > 0 && h > 0 && (rebuild_ || vk_->wd.Width != w || vk_->wd.Height != h)) {
            ImGui_ImplVulkan_SetMinImageCount(kMinImageCount);
            rebuildSwapchain(w, h);
        }
        if (glfwGetWindowAttrib(window_, GLFW_ICONIFIED)) {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        NativeWindow::clearControlRects();
        frame();
        ImGui::Render();
        const ImDrawData* dd = ImGui::GetDrawData();
        if (dd->DisplaySize.x > 0.0f && dd->DisplaySize.y > 0.0f) {
            renderFrame();
            present();
        }
    }
    vkDeviceWaitIdle(gpu_.device);
}

void Shell::setTitle(const std::string& title) {
    if (title == title_) return;
    title_ = title;
    glfwSetWindowTitle(window_, title.c_str());
}

void Shell::setTitleBarHeight(float px) { NativeWindow::setTitleBarHeight(px); }

void Shell::close() { glfwSetWindowShouldClose(window_, GLFW_TRUE); }
void Shell::minimize() { glfwIconifyWindow(window_); }
void Shell::toggleMaximize() {
    if (maximized()) glfwRestoreWindow(window_);
    else glfwMaximizeWindow(window_);
}
bool Shell::maximized() const { return glfwGetWindowAttrib(window_, GLFW_MAXIMIZED) != 0; }
bool Shell::focused() const { return glfwGetWindowAttrib(window_, GLFW_FOCUSED) != 0; }

}  // namespace icapp
