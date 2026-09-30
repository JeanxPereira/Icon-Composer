#include "Source/app/NativeWindow.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include "imgui.h"
#include "imgui_internal.h"

#include <cstring>
#include <vector>

// Os SDKs do MinGW nao trazem todos.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

namespace icapp::NativeWindow {
namespace {

WNDPROC g_previous = nullptr;
GLFWwindow* g_window = nullptr;
float g_titleBar = 0.0f;
struct Rect {
    float x0, y0, x1, y1;
};
std::vector<Rect> g_controls;

LRESULT callPrevious(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return CallWindowProcW(g_previous, hwnd, msg, wp, lp);
}

// O cursor do ImGui, porque sem moldura o Win32 nao sabe qual pedir.
void setCursorFromImGui() {
    LPCTSTR id = IDC_ARROW;
    switch (ImGui::GetMouseCursor()) {
        case ImGuiMouseCursor_Hand: id = IDC_HAND; break;
        case ImGuiMouseCursor_ResizeEW: id = IDC_SIZEWE; break;
        case ImGuiMouseCursor_ResizeNS: id = IDC_SIZENS; break;
        case ImGuiMouseCursor_ResizeNWSE: id = IDC_SIZENWSE; break;
        case ImGuiMouseCursor_ResizeNESW: id = IDC_SIZENESW; break;
        case ImGuiMouseCursor_ResizeAll: id = IDC_SIZEALL; break;
        case ImGuiMouseCursor_NotAllowed: id = IDC_NO; break;
        case ImGuiMouseCursor_TextInput: id = IDC_IBEAM; break;
        default: break;
    }
    SetCursor(LoadCursor(nullptr, id));
}

LRESULT hitTest(HWND hwnd, LPARAM lp) {
    if (g_window && glfwGetWindowMonitor(g_window)) return HTCLIENT;   // tela cheia
    RECT r;
    if (!GetWindowRect(hwnd, &r)) return HTNOWHERE;
    const POINT c{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};

    // A borda de redimensionar, que so existe fora do maximizado.
    if (!IsZoomed(hwnd)) {
        const int bx = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
        const int by = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
        const bool left = c.x < r.left + bx, right = c.x >= r.right - bx;
        const bool top = c.y < r.top + by, bottom = c.y >= r.bottom - by;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
    }

    // A barra: a faixa do topo, onde a janela de baixo e a hospedeira e nao
    // ha item nem popup. O estado do ImGui e o do quadro anterior, que e o
    // que esta na tela.
    // Um controle da barra e sempre cliente, com ou sem hover no ImGui.
    POINT local = c;
    ScreenToClient(hwnd, &local);
    for (const Rect& k : g_controls)
        if (local.x >= k.x0 && local.x < k.x1 && local.y >= k.y0 && local.y < k.y1) return HTCLIENT;

    if (c.y < r.top + static_cast<LONG>(g_titleBar) && ImGui::GetCurrentContext()) {
        const ImGuiContext& g = *GImGui;
        // A faixa de cima de QUALQUER coluna dockada: a raiz da arvore de dock
        // dela e a hospedeira. Uma janela flutuante (um popup, o Diagnostics)
        // nao e barra.
        const ImGuiWindow* root = g.HoveredWindow ? g.HoveredWindow->RootWindowDockTree : nullptr;
        const bool overHost = root && std::strcmp(root->Name, kHostWindow) == 0;
        if (overHost && !ImGui::IsAnyItemHovered() &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
            return HTCAPTION;
    }
    return HTCLIENT;
}

LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCCALCSIZE:
            // A area cliente e a janela inteira. Maximizada, o Windows poe a
            // janela a borda para fora do monitor; recolher a borda de volta
            // e o que impede o topo e as laterais de sumirem.
            if (wp == TRUE && lp) {
                auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
                if (IsZoomed(hwnd)) {
                    const int bx = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                    const int by = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                    p->rgrc[0].left += bx;
                    p->rgrc[0].right -= bx;
                    p->rgrc[0].top += by;
                    p->rgrc[0].bottom -= by;
                }
                return 0;
            }
            break;
        case WM_NCHITTEST:
            return hitTest(hwnd, lp);
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                setCursorFromImGui();
                return TRUE;
            }
            break;
        case WM_NCACTIVATE:
            // -1 no lParam: o DWM nao repinta uma moldura que nao existe.
            return DefWindowProcW(hwnd, msg, wp, -1);
        default:
            break;
    }
    return callPrevious(hwnd, msg, wp, lp);
}

}  // namespace

void install(GLFWwindow* window) {
    g_window = window;
    HWND hwnd = glfwGetWin32Window(window);
    // A GLFW cria a janela sem moldura como WS_POPUP; com WS_OVERLAPPEDWINDOW
    // de volta o sistema oferece redimensionar, maximizar e o Snap, e o
    // WM_NCCALCSIZE acima esconde a moldura que isso traria.
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    style = (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW;
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    g_previous = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&proc)));

    // A sombra do DWM precisa de uma margem de moldura, por menor que seja.
    const MARGINS m{0, 0, 1, 0};
    DwmExtendFrameIntoClientArea(hwnd, &m);
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
    const DWORD round = 2;   // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &round, sizeof round);
    // O RIM DE FORA do macOS 27: escuro, densidade 0,6 (Theme.h, `kRimOuter`).
    // O DWM so pinta uma borda opaca de 1 px; preto sobre o preenchimento de
    // 30/30/30 e o que mais se parece com o preto a 60% do compositor da Apple.
    const COLORREF rim = RGB(0, 0, 0);
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &rim, sizeof rim);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
}

void setTitleBarHeight(float px) { g_titleBar = px; }
void clearControlRects() { g_controls.clear(); }
void addControlRect(float x0, float y0, float x1, float y1) { g_controls.push_back({x0, y0, x1, y1}); }

}  // namespace icapp::NativeWindow

#else

namespace icapp::NativeWindow {
void install(GLFWwindow*) {}
void setTitleBarHeight(float) {}
void clearControlRects() {}
void addControlRect(float, float, float, float) {}
}  // namespace icapp::NativeWindow

#endif
