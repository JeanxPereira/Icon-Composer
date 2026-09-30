// A casca do Windows: a janela do macOS 27, e nao a do Windows.
//
// POR QUE NAO A GLFW AQUI. A borda do alvo e um canto CONTINUO de 16 pt [BIN]
// (NSThemeFrame, `_getCachedDefaultWindowCornerRadius`) e uma sombra do
// compositor (SkyLight: sigma 20, deslocamento 18, densidade 0,4 na janela
// ativa [BIN]); o Windows so da o canto de 8 px do DWM e a sombra dele. Para
// desenhar os nossos a janela precisa de alfa por pixel, e isso e uma decisao
// de CRIACAO (`WS_EX_NOREDIRECTIONBITMAP`) que a GLFW nao deixa tomar. E a
// swapchain Vulkan desta maquina so compoe opaca (medido 30/09:
// `supportedCompositeAlpha = OPAQUE`, Radeon RX 6750 XT).
//
// ENTAO O CAMINHO E:
//   ImGui -> Vulkan, numa imagem que E uma textura D3D11 (memoria externa,
//   `VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT`, sem copia pela CPU)
//   -> uma copia de GPU para a swapchain de composicao do DXGI
//   -> o DirectComposition, com o recorte arredondado de 16 pt (antialiasado
//      pelo proprio compositor).
// E a sombra numa JANELA A PARTE, em camadas e atravessavel pelo clique
// (`WS_EX_TRANSPARENT`), colada atras desta. Uma margem de sombra dentro da
// propria janela engoliria o clique de quem mira o desktop logo ao lado dela.
//
// O que o AquaKit faz de outro jeito, e por que nao aqui: ele desce o quadro
// para a CPU e o sobe de volta (`CompositionPresenter`), 5 MB por quadro a
// 1440x900 -- exatamente o custo que a frente do render acabou de tirar.
#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// Windows 10: `WS_EX_NOREDIRECTIONBITMAP` e o DirectComposition sao do 8.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "Source/app/Shell.h"

#include "Source/IconComposerKit/Theme.h"
#include "Source/app/NativeWindow.h"

#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_win32.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

namespace icapp {
namespace {

void checkVk(VkResult err) {
    if (err == VK_SUCCESS) return;
    std::fprintf(stderr, "iconcomposer: [vulkan] VkResult = %d\n", err);
    if (err < 0) std::abort();
}

template <class T>
void release(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// A fonte do sistema no lugar da SF Pro, a mesma troca que o CSS faz
// ("SF Pro Text", ..., "Segoe UI Variable Text"). Sem nenhuma, a embutida.
void loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    const char* candidates[] = {"C:/Windows/Fonts/SegUIVar.ttf", "C:/Windows/Fonts/segoeui.ttf"};
    for (const char* path : candidates) {
        if (FILE* f = std::fopen(path, "rb")) {
            std::fclose(f);
            if (io.Fonts->AddFontFromFileTTF(path, ick::theme::kFontSize)) return;
        }
    }
    io.Fonts->AddFontDefault();
}

// ---- a sombra do SkyLight ------------------------------------------------------
//
// [BIN] AquaKit SkyLight/WindowShadow.h; docs/re/2026-08-20-window-shadow.md §3-4:
// ativa densidade 0,4, sigma 20, deslocamento vertical 18; inativa 0,25, 8, 4;
// o raio de desfoque e ceil(trunc(sigma) x 2,8). O rim de fora: densidade 0,6
// (ativa) e 0,55 (inativa), raio 1 -- as CORES do rim sao inferidas (preto).
struct ShadowSpec {
    float density, sigma, offsetY, rim;
};
constexpr ShadowSpec kActive{0.40f, 20.0f, 18.0f, 0.60f};
constexpr ShadowSpec kInactive{0.25f, 8.0f, 4.0f, 0.55f};

// A margem que a sombra precisa em volta da janela, em pontos: o raio de
// desfoque da ativa mais o deslocamento dela, que e a maior das duas.
float shadowMarginPoints() { return std::ceil(std::trunc(kActive.sigma) * 2.8f) + kActive.offsetY; }

// A distancia com sinal a um retangulo arredondado centrado na origem.
float sdRoundRect(float px, float py, float hx, float hy, float r) {
    const float qx = std::fabs(px) - hx + r, qy = std::fabs(py) - hy + r;
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
}

class ShadowWindow {
public:
    bool create(HINSTANCE inst) {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = inst;
        wc.lpszClassName = L"IconComposerShadow";
        RegisterClassExW(&wc);
        // Em camadas (alfa por pixel via UpdateLayeredWindow), atravessavel
        // pelo mouse, fora da barra de tarefas e do Alt+Tab, e nunca ativa.
        hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                                wc.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, inst, nullptr);
        return hwnd_ != nullptr;
    }
    ~ShadowWindow() {
        if (dc_) DeleteDC(dc_);
        if (bitmap_) DeleteObject(bitmap_);
        if (hwnd_) DestroyWindow(hwnd_);
    }

    // Segue a janela: redesenha quando o tamanho, o foco ou o DPI mudam, e so
    // move quando nao. Escondida com a janela minimizada ou maximizada.
    void follow(HWND main, float scale, bool active) {
        if (!hwnd_) return;
        if (IsIconic(main) || IsZoomed(main) || !IsWindowVisible(main)) {
            ShowWindow(hwnd_, SW_HIDE);
            return;
        }
        RECT r;
        GetWindowRect(main, &r);
        const int w = r.right - r.left, h = r.bottom - r.top;
        const int m = static_cast<int>(std::ceil(shadowMarginPoints() * scale));
        if (w != w_ || h != h_ || active != active_ || scale != scale_) {
            w_ = w;
            h_ = h;
            active_ = active;
            scale_ = scale;
            paint(m);
        }
        POINT at{r.left - m, r.top - m};
        SIZE size{w + 2 * m, h + 2 * m};
        POINT src{0, 0};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(hwnd_, nullptr, &at, &size, dc_, &src, 0, &blend, ULW_ALPHA);
        // Logo ATRAS da janela, sem roubar o foco.
        SetWindowPos(hwnd_, main, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

private:
    void paint(int m) {
        const int W = w_ + 2 * m, H = h_ + 2 * m;
        if (dc_) DeleteDC(dc_);
        if (bitmap_) DeleteObject(bitmap_);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = W;
        bi.bmiHeader.biHeight = -H;   // de cima para baixo
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        bitmap_ = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        dc_ = CreateCompatibleDC(nullptr);
        SelectObject(dc_, bitmap_);
        auto* px = static_cast<std::uint32_t*>(bits);
        std::memset(px, 0, static_cast<std::size_t>(W) * H * 4);

        const ShadowSpec& sp = active_ ? kActive : kInactive;
        const float s = scale_;
        const float r = ick::theme::kWindowRadius * s;
        const float sigma = sp.sigma * s, dy = sp.offsetY * s, rimW = 1.0f * s;
        const float hx = w_ * 0.5f, hy = h_ * 0.5f;
        const float cx = m + hx, cy = m + hy;
        const float inv = 1.0f / (sigma * 1.41421356f);
        // So a faixa fora da janela e calculada: o miolo e coberto por ela.
        for (int y = 0; y < H; ++y) {
            const float py = y + 0.5f - cy;
            for (int x = 0; x < W; ++x) {
                const float pxl = x + 0.5f - cx;
                const float d = sdRoundRect(pxl, py, hx, hy, r);
                if (d < -1.0f) {
                    // Dentro da janela (fora dos cantos que o recorte tira).
                    if (x > m + r && x < W - m - r) {
                        x = W - m - static_cast<int>(r) - 1;   // pula o miolo da linha
                    }
                    continue;
                }
                // A sombra: o retangulo deslocado, desfocado pela gaussiana.
                const float ds = sdRoundRect(pxl, py - dy, hx, hy, r);
                const float shadow = sp.density * 0.5f * std::erfc(ds * inv);
                // O rim de fora: uma faixa escura de 1 pt logo fora da borda.
                float rim = 0.0f;
                if (d >= -1.0f && d < rimW + 1.0f) {
                    const float t = std::clamp(1.0f - std::fabs(d - rimW * 0.5f) / (rimW * 0.5f + 0.5f), 0.0f, 1.0f);
                    rim = sp.rim * t;
                }
                const float a = std::clamp(rim + shadow * (1.0f - rim), 0.0f, 1.0f);
                px[static_cast<std::size_t>(y) * W + x] = static_cast<std::uint32_t>(std::lround(a * 255.0f)) << 24;
            }
        }
    }

    HWND hwnd_ = nullptr;
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    int w_ = 0, h_ = 0;
    bool active_ = true;
    float scale_ = 0.0f;
};

constexpr std::uint32_t kImages = 2;   // o ImGui quer >= 2 conjuntos de buffers

}  // namespace

struct Shell::Impl {
    HWND hwnd = nullptr;
    ShadowWindow shadow;
    float scale_ = 1.0f;
    bool quit = false;
    bool inFrame = false;
    bool active = true;
    std::string title_;
    std::function<void(const std::filesystem::path&)> drop_;
    std::function<void()> frame_;   // o quadro do laco, para desenhar tambem durante o redimensionar

    // Vulkan
    VkInstance instance = VK_NULL_HANDLE;
    Gpu gpu_;
    VkRenderPass pass = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    PFN_vkGetMemoryWin32HandlePropertiesKHR getHandleProps = nullptr;

    // O alvo: a textura D3D11 compartilhada, vista pelo Vulkan.
    std::uint32_t width = 0, height = 0;
    ID3D11Texture2D* shared = nullptr;
    HANDLE sharedHandle = nullptr;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;

    // D3D11, DXGI e o compositor.
    ID3D11Device* d3d = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain1* swap = nullptr;
    IDCompositionDevice* dcomp = nullptr;
    IDCompositionTarget* target = nullptr;
    IDCompositionVisual* visual = nullptr;
    IDCompositionRectangleClip* clip = nullptr;

    bool init(const std::string& title, std::string* why);
    ~Impl();
    bool createTarget(std::uint32_t w, std::uint32_t h);
    void destroyTarget();
    void resize();
    void applyClip();
    void drawFrame();
    LRESULT proc(UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK thunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};

LRESULT CALLBACK Shell::Impl::thunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Impl* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->proc(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Shell::Impl::proc(UINT msg, WPARAM wp, LPARAM lp) {
    // O ImGui ve a entrada primeiro (mouse, teclado, foco, DPI).
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 1;
    std::intptr_t handled = 0;
    if (NativeWindow::handleBorderless(hwnd, msg, static_cast<std::uintptr_t>(wp), static_cast<std::intptr_t>(lp), handled))
        return static_cast<LRESULT>(handled);
    switch (msg) {
        case WM_CLOSE:
            quit = true;
            return 0;
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED && d3d) {
                resize();
                // O LACO MODAL DO REDIMENSIONAR nao volta ao nosso laco ate o
                // botao subir: o quadro e desenhado daqui, ou a janela congela
                // esticada enquanto a pessoa arrasta a borda.
                if (frame_ && !inFrame) drawFrame();
            }
            shadow.follow(hwnd, scale_, active);
            return 0;
        case WM_WINDOWPOSCHANGED:
            shadow.follow(hwnd, scale_, active);
            break;
        case WM_ACTIVATE:
            active = LOWORD(wp) != WA_INACTIVE;
            shadow.follow(hwnd, scale_, active);
            break;
        case WM_DPICHANGED: {
            scale_ = HIWORD(wp) / 96.0f;
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wp);
            wchar_t path[MAX_PATH * 4];
            if (drop_ && DragQueryFileW(drop, 0, path, static_cast<UINT>(std::size(path))) > 0)
                drop_(std::filesystem::path(path));
            DragFinish(drop);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool Shell::Impl::init(const std::string& title, std::string* why) {
    auto fail = [why](const char* what) {
        if (why) *why = what;
        return false;
    };
    ImGui_ImplWin32_EnableDpiAwareness();
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof wc};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &Impl::thunk;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"IconComposerWindow";
    RegisterClassExW(&wc);

    // O tamanho inicial, no monitor principal, centrado na area util.
    POINT origin{0, 0};
    HMONITOR mon = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    scale_ = ImGui_ImplWin32_GetDpiScaleForMonitor(mon);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    const int w = std::min(static_cast<int>(1440 * scale_), static_cast<int>(mi.rcWork.right - mi.rcWork.left));
    const int h = std::min(static_cast<int>(900 * scale_), static_cast<int>(mi.rcWork.bottom - mi.rcWork.top));
    const int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2;
    const int y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - h) / 2;
    title_ = title;
    // WS_OVERLAPPEDWINDOW mesmo sem moldura: e o que da redimensionar,
    // maximizar, a animacao de minimizar e o Snap; a moldura some no
    // WM_NCCALCSIZE (NativeWindow.cpp). E a janela nasce sem o bitmap de
    // redirecionamento: quem a mostra e o compositor, e nada mais.
    CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_APPWINDOW, wc.lpszClassName, widen(title).c_str(),
                    WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr, inst, this);
    if (!hwnd) return fail("CreateWindowExW failed");
    // O canto e o NOSSO (o recorte do compositor), nao o do Windows 11.
    const DWORD noRound = 1;   // DWMWCP_DONOTROUND
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &noRound, sizeof noRound);
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    DragAcceptFiles(hwnd, TRUE);
    shadow.create(inst);

    // ---- Vulkan: instancia e dispositivo, sem superficie ----------------------
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Icon Composer";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS) return fail("vkCreateInstance failed");
    gpu_.physical = ImGui_ImplVulkanH_SelectPhysicalDevice(instance);
    if (gpu_.physical == VK_NULL_HANDLE) return fail("no Vulkan physical device");
    gpu_.queueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(gpu_.physical);
    if (gpu_.queueFamily == static_cast<std::uint32_t>(-1)) return fail("no graphics queue");
    const char* devExts[] = {VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME};
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
        return fail("vkCreateDevice failed (VK_KHR_external_memory_win32?)");
    vkGetDeviceQueue(gpu_.device, gpu_.queueFamily, 0, &gpu_.queue);
    getHandleProps = reinterpret_cast<PFN_vkGetMemoryWin32HandlePropertiesKHR>(
        vkGetDeviceProcAddr(gpu_.device, "vkGetMemoryWin32HandlePropertiesKHR"));
    if (!getHandleProps) return fail("vkGetMemoryWin32HandlePropertiesKHR missing");

    // ---- D3D11 no MESMO adaptador (pelo LUID) --------------------------------
    VkPhysicalDeviceIDProperties idp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    p2.pNext = &idp;
    vkGetPhysicalDeviceProperties2(gpu_.physical, &p2);
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))))
        return fail("CreateDXGIFactory1 failed");
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d;
        adapter->GetDesc1(&d);
        if (idp.deviceLUIDValid && std::memcmp(&d.AdapterLuid, idp.deviceLUID, sizeof(LUID)) == 0) break;
        release(adapter);
    }
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    const HRESULT made = D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                           D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2, D3D11_SDK_VERSION, &d3d,
                                           nullptr, &ctx);
    release(adapter);
    if (FAILED(made)) {
        release(factory);
        return fail("D3D11CreateDevice failed");
    }

    // ---- o compositor: alvo, visual com o recorte ------------------------------
    IDXGIDevice* dxgiDevice = nullptr;
    d3d->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
    const HRESULT dc = DCompositionCreateDevice(dxgiDevice, __uuidof(IDCompositionDevice), reinterpret_cast<void**>(&dcomp));
    release(dxgiDevice);
    if (FAILED(dc)) {
        release(factory);
        return fail("DCompositionCreateDevice failed");
    }
    dcomp->CreateTargetForHwnd(hwnd, TRUE, &target);
    dcomp->CreateVisual(&visual);
    dcomp->CreateRectangleClip(&clip);
    visual->SetClip(clip);
    target->SetRoot(visual);

    RECT cr;
    GetClientRect(hwnd, &cr);
    width = static_cast<std::uint32_t>(std::max<LONG>(1, cr.right - cr.left));
    height = static_cast<std::uint32_t>(std::max<LONG>(1, cr.bottom - cr.top));
    IDXGIFactory2* factory2 = nullptr;
    factory->QueryInterface(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory2));
    release(factory);
    if (!factory2) return fail("IDXGIFactory2 missing");
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = width;
    sd.Height = height;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    // O conteudo e opaco: o canto transparente e o recorte, nao o alfa.
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    const HRESULT sc = factory2->CreateSwapChainForComposition(d3d, &sd, nullptr, &swap);
    release(factory2);
    if (FAILED(sc)) return fail("CreateSwapChainForComposition failed");
    visual->SetContent(swap);

    // ---- o passe do ImGui, o comando e a cerca ----------------------------------
    VkAttachmentDescription att{};
    att.format = VK_FORMAT_B8G8R8A8_UNORM;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // GENERAL no fim: quem le a seguir e o D3D11, fora do Vulkan.
    att.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpi.attachmentCount = 1;
    rpi.pAttachments = &att;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sub;
    checkVk(vkCreateRenderPass(gpu_.device, &rpi, nullptr, &pass));
    VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpi.queueFamilyIndex = gpu_.queueFamily;
    checkVk(vkCreateCommandPool(gpu_.device, &cpi, nullptr, &pool));
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    checkVk(vkAllocateCommandBuffers(gpu_.device, &cai, &cmd));
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    checkVk(vkCreateFence(gpu_.device, &fi, nullptr, &fence));
    if (!createTarget(width, height)) return fail("could not share a D3D11 texture with Vulkan");
    applyClip();
    dcomp->Commit();

    // ---- ImGui ------------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    io.ConfigDpiScaleFonts = true;
    ick::theme::apply(scale_);
    loadFonts();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplVulkan_InitInfo ii{};
    ii.ApiVersion = VK_API_VERSION_1_3;
    ii.Instance = instance;
    ii.PhysicalDevice = gpu_.physical;
    ii.Device = gpu_.device;
    ii.QueueFamily = gpu_.queueFamily;
    ii.Queue = gpu_.queue;
    ii.DescriptorPoolSize = 256;
    ii.MinImageCount = kImages;
    ii.ImageCount = kImages;
    ii.PipelineInfoMain.RenderPass = pass;
    ii.PipelineInfoMain.Subpass = 0;
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ii.CheckVkResultFn = checkVk;
    if (!ImGui_ImplVulkan_Init(&ii)) return fail("the ImGui Vulkan backend did not initialise");

    ShowWindow(hwnd, SW_SHOW);
    shadow.follow(hwnd, scale_, true);
    return true;
}

bool Shell::Impl::createTarget(std::uint32_t w, std::uint32_t h) {
    // A textura nasce no D3D11, compartilhavel por handle NT, e o Vulkan a
    // importa: e a direcao que os dois lados suportam sem negociar formato.
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
    if (FAILED(d3d->CreateTexture2D(&td, nullptr, &shared))) return false;
    IDXGIResource1* res = nullptr;
    shared->QueryInterface(__uuidof(IDXGIResource1), reinterpret_cast<void**>(&res));
    if (!res || FAILED(res->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                               nullptr, &sharedHandle))) {
        release(res);
        return false;
    }
    release(res);

    VkExternalMemoryImageCreateInfo ext{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    ext.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.pNext = &ext;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_B8G8R8A8_UNORM;
    ici.extent = {w, h, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(gpu_.device, &ici, nullptr, &image) != VK_SUCCESS) return false;

    VkMemoryWin32HandlePropertiesKHR hp{VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
    getHandleProps(gpu_.device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT, sharedHandle, &hp);
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(gpu_.device, image, &req);
    const std::uint32_t bits = req.memoryTypeBits & (hp.memoryTypeBits ? hp.memoryTypeBits : ~0u);
    std::uint32_t type = UINT32_MAX;
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(gpu_.physical, &mp);
    for (std::uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if (bits & (1u << i)) {
            type = i;
            if (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) break;
        }
    if (type == UINT32_MAX) return false;
    VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dedicated.image = image;
    VkImportMemoryWin32HandleInfoKHR import{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};
    import.pNext = &dedicated;
    import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
    import.handle = sharedHandle;
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.pNext = &import;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = type;
    if (vkAllocateMemory(gpu_.device, &mai, nullptr, &memory) != VK_SUCCESS) return false;
    if (vkBindImageMemory(gpu_.device, image, memory, 0) != VK_SUCCESS) return false;

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_B8G8R8A8_UNORM;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(gpu_.device, &vi, nullptr, &view) != VK_SUCCESS) return false;
    VkFramebufferCreateInfo fbi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fbi.renderPass = pass;
    fbi.attachmentCount = 1;
    fbi.pAttachments = &view;
    fbi.width = w;
    fbi.height = h;
    fbi.layers = 1;
    if (vkCreateFramebuffer(gpu_.device, &fbi, nullptr, &framebuffer) != VK_SUCCESS) return false;
    width = w;
    height = h;
    return true;
}

void Shell::Impl::destroyTarget() {
    if (framebuffer) vkDestroyFramebuffer(gpu_.device, framebuffer, nullptr);
    if (view) vkDestroyImageView(gpu_.device, view, nullptr);
    if (image) vkDestroyImage(gpu_.device, image, nullptr);
    if (memory) vkFreeMemory(gpu_.device, memory, nullptr);
    framebuffer = VK_NULL_HANDLE;
    view = VK_NULL_HANDLE;
    image = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
    if (sharedHandle) CloseHandle(sharedHandle);
    sharedHandle = nullptr;
    release(shared);
}

void Shell::Impl::applyClip() {
    // O CANTO DO macOS 27: 16 pt [BIN], recortado pelo compositor. Maximizada,
    // a janela e a tela e nao tem canto.
    const float r = IsZoomed(hwnd) ? 0.0f : ick::theme::kWindowRadius * scale_;
    clip->SetLeft(0.0f);
    clip->SetTop(0.0f);
    clip->SetRight(static_cast<float>(width));
    clip->SetBottom(static_cast<float>(height));
    clip->SetTopLeftRadiusX(r);
    clip->SetTopLeftRadiusY(r);
    clip->SetTopRightRadiusX(r);
    clip->SetTopRightRadiusY(r);
    clip->SetBottomLeftRadiusX(r);
    clip->SetBottomLeftRadiusY(r);
    clip->SetBottomRightRadiusX(r);
    clip->SetBottomRightRadiusY(r);
}

void Shell::Impl::resize() {
    RECT cr;
    GetClientRect(hwnd, &cr);
    const auto w = static_cast<std::uint32_t>(std::max<LONG>(1, cr.right - cr.left));
    const auto h = static_cast<std::uint32_t>(std::max<LONG>(1, cr.bottom - cr.top));
    if (w != width || h != height) {
        vkDeviceWaitIdle(gpu_.device);
        destroyTarget();
        ctx->ClearState();
        ctx->Flush();
        swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
        if (!createTarget(w, h)) std::fprintf(stderr, "iconcomposer: could not recreate the shared target\n");
    }
    applyClip();
    dcomp->Commit();
}

void Shell::Impl::drawFrame() {
    inFrame = true;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    NativeWindow::clearControlRects();
    frame_();
    ImGui::Render();
    ImDrawData* dd = ImGui::GetDrawData();

    checkVk(vkResetCommandBuffer(cmd, 0));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkVk(vkBeginCommandBuffer(cmd, &bi));
    const ImVec4 bg = ick::theme::kWindowBg;
    VkClearValue clear{};
    clear.color = {{bg.x, bg.y, bg.z, 1.0f}};
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = pass;
    rp.framebuffer = framebuffer;
    rp.renderArea.extent = {width, height};
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    if (dd->DisplaySize.x > 0.0f && dd->DisplaySize.y > 0.0f) ImGui_ImplVulkan_RenderDrawData(dd, cmd);
    vkCmdEndRenderPass(cmd);
    checkVk(vkEndCommandBuffer(cmd));
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    checkVk(vkResetFences(gpu_.device, 1, &fence));
    checkVk(vkQueueSubmit(gpu_.queue, 1, &si, fence));
    // O D3D11 so le depois de o Vulkan acabar de escrever: a espera e na
    // CPU, e custa o tempo do quadro na GPU -- um editor, nao um jogo.
    checkVk(vkWaitForFences(gpu_.device, 1, &fence, VK_TRUE, UINT64_MAX));

    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) && back) {
        ctx->CopyResource(back, shared);
        back->Release();
    }
    swap->Present(1, 0);
    inFrame = false;
}

Shell::Impl::~Impl() {
    if (gpu_.device) vkDeviceWaitIdle(gpu_.device);
    if (ImGui::GetCurrentContext()) {
        if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplVulkan_Shutdown();
        if (ImGui::GetIO().BackendPlatformUserData) ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    if (gpu_.device) {
        destroyTarget();
        if (fence) vkDestroyFence(gpu_.device, fence, nullptr);
        if (pool) vkDestroyCommandPool(gpu_.device, pool, nullptr);
        if (pass) vkDestroyRenderPass(gpu_.device, pass, nullptr);
        vkDestroyDevice(gpu_.device, nullptr);
    }
    if (instance) vkDestroyInstance(instance, nullptr);
    release(clip);
    release(visual);
    release(target);
    release(dcomp);
    release(swap);
    release(ctx);
    release(d3d);
    if (hwnd) DestroyWindow(hwnd);
}

// ---- a Shell -----------------------------------------------------------------------

std::unique_ptr<Shell> Shell::create(const std::string& title, std::string* why) {
    std::unique_ptr<Shell> s(new Shell());
    s->impl_ = std::make_unique<Impl>();
    if (!s->impl_->init(title, why)) return nullptr;
    return s;
}

Shell::~Shell() = default;

const Gpu& Shell::gpu() const { return impl_->gpu_; }
float Shell::dpiScale() const { return impl_->scale_; }

void Shell::run(const std::function<void()>& frame) {
    Impl& m = *impl_;
    m.frame_ = frame;
    while (!m.quit) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) m.quit = true;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (m.quit) break;
        if (IsIconic(m.hwnd)) {
            Sleep(10);
            continue;
        }
        m.drawFrame();
    }
    m.frame_ = nullptr;
    vkDeviceWaitIdle(m.gpu_.device);
}

void Shell::setTitle(const std::string& title) {
    if (title == impl_->title_) return;
    impl_->title_ = title;
    SetWindowTextW(impl_->hwnd, widen(title).c_str());
}

void Shell::setTitleBarHeight(float px) { NativeWindow::setTitleBarHeight(px); }
void Shell::onDrop(std::function<void(const std::filesystem::path&)> handler) { impl_->drop_ = std::move(handler); }
void Shell::close() { impl_->quit = true; }
void Shell::minimize() { ShowWindow(impl_->hwnd, SW_MINIMIZE); }
void Shell::toggleMaximize() { ShowWindow(impl_->hwnd, IsZoomed(impl_->hwnd) ? SW_RESTORE : SW_MAXIMIZE); }
bool Shell::maximized() const { return IsZoomed(impl_->hwnd) != FALSE; }
bool Shell::focused() const { return impl_->active; }
float Shell::cornerRadius() const {
    return IsZoomed(impl_->hwnd) ? 0.0f : ick::theme::kWindowRadius * impl_->scale_;
}

}  // namespace icapp

#endif
