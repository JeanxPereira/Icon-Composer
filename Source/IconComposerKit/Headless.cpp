#include "Source/IconComposerKit/Headless.h"

#include "imgui.h"
#include "imgui_internal.h"

namespace ick {

HeadlessImGui::HeadlessImGui(float width, float height) {
    ctx_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx_);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(width, height);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;   // a test must not read or write imgui.ini
    io.LogFilename = nullptr;   // nor imgui_log.txt
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Recoverable errors have to STAY recoverable here, or `errors()` never gets
    // read: 1.92 defaults ConfigErrorRecoveryEnableAssert to true, and an IM_ASSERT
    // kills the process before the count can be looked at. sfsymview's selftest
    // turns the same three off for the same reason.
    io.ConfigErrorRecoveryEnableAssert = false;
    io.ConfigErrorRecoveryEnableTooltip = false;
    io.ConfigErrorRecoveryEnableDebugLog = false;
    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));
    ctx_->ErrorCallback = &HeadlessImGui::onError;
    ctx_->ErrorCallbackUserData = this;
}

HeadlessImGui::~HeadlessImGui() {
    ImGui::DestroyContext(ctx_);
    ImGui::SetCurrentContext(nullptr);
}

void HeadlessImGui::newFrame() { ImGui::NewFrame(); }
void HeadlessImGui::render() { ImGui::Render(); }

void HeadlessImGui::onError(ImGuiContext*, void* user, const char*) {
    ++static_cast<HeadlessImGui*>(user)->errors_;
}

}  // namespace ick
