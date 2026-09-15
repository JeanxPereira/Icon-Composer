#pragma once
// A Dear ImGui context with no backend, for tests and the selftest.
//
// ImGui runs with no platform and no renderer as long as `io.DisplaySize` is
// set and the font atlas has been built and given a texture id -- sfsymview's
// selftest is the precedent. Every panel in this Kit is exercised through this
// class before it is ever shown in a window.
#include <cstdint>

struct ImGuiContext;

namespace ick {

class HeadlessImGui {
public:
    explicit HeadlessImGui(float width = 1440.0f, float height = 900.0f);
    ~HeadlessImGui();
    HeadlessImGui(const HeadlessImGui&) = delete;
    HeadlessImGui& operator=(const HeadlessImGui&) = delete;

    void newFrame();
    void render();   // ImGui::Render(); the draw data is measured, never consumed
    std::uint64_t errors() const { return errors_; }

private:
    static void onError(ImGuiContext*, void* user, const char* msg);
    ImGuiContext* ctx_ = nullptr;
    std::uint64_t errors_ = 0;
};

}  // namespace ick
