#include "Source/app/Window.h"

#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/RenderCoordinator.h"
#include "Source/IconComposerKit/Session.h"
#include "Source/app/OnyxPorts.h"

#include <Onyx/App/App.h>
#include <Onyx/App/IPanel.h>
#include <Onyx/App/UIHelpers.h>
#include <Onyx/App/Window.h>
#include <Onyx/Services/Threading.h>
#include "imgui.h"
#include "imgui_internal.h"

#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace icapp {
namespace {

namespace fs = std::filesystem;

// A `.icon` is a DIRECTORY (spec 13/09 §4), and Onyx's open dialog is
// `GetOpenFileNameA` with OFN_FILEMUSTEXIST -- read on the dddce38 checkout,
// Source/App/Platform/SystemFileDialog.cpp -- which cannot return a folder.
// So the dialog asks for a file and a file inside the bundle names the bundle:
// picking `Foo.icon/icon.json` opens `Foo.icon`.
fs::path bundleDirOf(const fs::path& picked) {
    std::error_code ec;
    if (fs::is_directory(picked, ec)) return picked;
    const fs::path parent = picked.parent_path();
    return parent.empty() ? picked : parent;
}

// Everything the panels share, owned by run() so destruction order is stated
// once: the coordinator returns its texture before the sink that owns the pool
// goes, and the sink goes before the VkContext (the window outlives this).
struct State {
    // The window handle, kept because `glfwGetCurrentContext()` is an OpenGL
    // call and returns null in a Vulkan app -- Quit through it would silently
    // do nothing.
    GLFWwindow* window = nullptr;
    std::optional<ick::Session> session;
    std::unique_ptr<OnyxTextureSink> sink;
    std::unique_ptr<JobScheduler> scheduler;
    std::unique_ptr<ick::RenderCoordinator> coordinator;
    ick::MenuActions actions;
    Onyx::App::App* app = nullptr;
    bool quit = false;

    // The coordinator is recreated with the session because it caches the last
    // request it made; keeping it across a document would have it waiting for
    // an answer about a document that is gone.
    void adopt(std::optional<ick::Session> s) {
        coordinator.reset();
        session = std::move(s);
        if (session) coordinator = std::make_unique<ick::RenderCoordinator>(*scheduler, *sink);
    }
    void open(const fs::path& dir) {
        auto s = ick::Session::open(dir);
        if (!s) {
            std::fprintf(stderr, "iconcomposer: not a bundle this reader can open: %s\n",
                         dir.string().c_str());
            return;
        }
        adopt(std::move(s));
    }
    void close() { adopt(std::nullopt); }

    // Runs after the frame: the Kit asked, the app answers with dialogs and files.
    void act() {
        ick::MenuActions a = actions;
        actions = {};
        if (a.newDocument) {
            const std::string p = SystemSaveFileDialog("Untitled.icon");
            if (!p.empty()) {
                auto s = ick::Session::create(p);
                if (!s) std::fprintf(stderr, "iconcomposer: could not create %s\n", p.c_str());
                else adopt(std::move(s));
            }
        }
        if (a.open) {
            const std::string p = SystemOpenFileDialog({{"Icon Composer document", {"json"}}});
            if (!p.empty()) open(bundleDirOf(p));
        }
        if (a.save && session) {
            const std::string r = session->save();
            if (!r.empty()) std::fprintf(stderr, "save: %s\n", r.c_str());
        }
        if (a.saveAs && session) {
            const std::string p = SystemSaveFileDialog(session->bundle().path().filename().string());
            if (!p.empty()) {
                const std::string r = session->saveAs(p);
                if (!r.empty()) std::fprintf(stderr, "save as: %s\n", r.c_str());
            }
        }
        if (a.close) close();
        if (a.quit) quit = true;
    }

    void title() {
        if (!app) return;
        auto* config = app->getConfig();
        if (!config) return;
        std::string t = "Icon Composer";
        if (session) {
            t += " - " + session->bundle().path().filename().string();
            if (session->isDirty()) t += " *";
        }
        config->windowTitle = t;
    }
};

struct LayersPanel : Onyx::App::IPanel {
    explicit LayersPanel(State& s) : st(s) {}
    void Draw() override {
        if (st.session) {
            ick::drawLayers(*st.session);
        } else {
            ImGui::Begin(ick::kLayersWindow);
            ImGui::TextDisabled("No document");
            ImGui::End();
        }
    }
    std::string_view getName() const override { return ick::kLayersWindow; }
    State& st;
};

struct CanvasPanel : Onyx::App::IPanel {
    explicit CanvasPanel(State& s) : st(s) {}
    void Draw() override {
        if (st.coordinator && st.session) {
            // The coordinator ticks BEFORE the canvas draws: it may create or
            // replace the texture the canvas is about to show (spec 13/09 §6).
            st.coordinator->tick(*st.session);
            ick::drawCanvas(*st.session, st.coordinator->view(), st.actions);
        } else {
            // With no document the Kit's menu bar has nothing to hang from, so
            // the empty canvas carries the two items that can still be acted on.
            ImGui::Begin(ick::kCanvasWindow, nullptr, ImGuiWindowFlags_MenuBar);
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("New...", "Ctrl+N")) st.actions.newDocument = true;
                    if (ImGui::MenuItem("Open...", "Ctrl+O")) st.actions.open = true;
                    if (ImGui::MenuItem("Quit", "Ctrl+Q")) st.actions.quit = true;
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            ImGui::TextDisabled("Open a .icon bundle");
            ImGui::End();
        }
    }
    std::string_view getName() const override { return ick::kCanvasWindow; }
    State& st;
};

struct InspectorPanel : Onyx::App::IPanel {
    explicit InspectorPanel(State& s) : st(s) {}
    void Draw() override {
        if (st.session) {
            ick::drawInspector(*st.session);
        } else {
            ImGui::Begin(ick::kInspectorWindow);
            ImGui::End();
        }
    }
    std::string_view getName() const override { return ick::kInspectorWindow; }
    State& st;
};

struct DiagnosticsPanel : Onyx::App::IPanel {
    explicit DiagnosticsPanel(State& s) : st(s) {}
    void Draw() override {
        if (st.session && st.coordinator) {
            ick::drawDiagnostics(*st.session, st.coordinator->view());
        } else {
            ImGui::Begin(ick::kDiagnosticsWindow);
            ImGui::End();
        }
        // End of frame work, here because this panel is registered LAST: `act()`
        // can replace or close the session, and a swap mid-frame would leave the
        // panels after it drawing against state that changed under them.
        // `advanceFrame` likewise belongs after every upload this frame made.
        st.sink->advanceFrame();
        st.act();
        st.title();
        if (st.quit && st.window) glfwSetWindowShouldClose(st.window, 1);
    }
    std::string_view getName() const override { return ick::kDiagnosticsWindow; }
    State& st;
};

// `[OBS]` 20% / 28% / 22% are sfsymview's fractions, not the target's
// WindowLayoutConstants, which have not been read (spec 13/09 §2.1).
void defaultLayout(ImGuiID dockspaceId) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);
    ImGuiID centre = dockspaceId;
    const ImGuiID left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.20f, nullptr, &centre);
    const ImGuiID right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.28f, nullptr, &centre);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.22f, nullptr, &centre);
    ImGui::DockBuilderDockWindow(ick::kLayersWindow, left);
    ImGui::DockBuilderDockWindow(ick::kCanvasWindow, centre);
    ImGui::DockBuilderDockWindow(ick::kInspectorWindow, right);
    // Onyx brings windows of its own that this tree does not place: "Viewer",
    // drawn unconditionally by its DocumentWindow (not a panel, so
    // `setPanelVisible` does not reach it, and with no Onyx document open it
    // only ever says "No documents open"), and "Log", which is worth keeping --
    // it prints the adapter and the swapchain. Undocked, either one floats over
    // the Layers tree. They go to the bottom as tabs, and Diagnostics is docked
    // LAST so it is the tab that comes up selected.
    ImGui::DockBuilderDockWindow("Viewer", bottom);
    ImGui::DockBuilderDockWindow("Log", bottom);
    ImGui::DockBuilderDockWindow(ick::kDiagnosticsWindow, bottom);
    ImGui::DockBuilderFinish(dockspaceId);
}

}  // namespace

int run(const fs::path& initial) {
    // Validation off: nobody here is checking the driver, and the tower's
    // default (on) costs every frame (spec 13/09 §6). Two VkInstances in one
    // process -- Onyx's and RenderBox's -- is that section's decision too.
    auto device = rb::Device::create(rb::DeviceOptions{.validation = false});
    if (!device) {
        std::fprintf(stderr, "iconcomposer: no Vulkan device: %s\n", device.error().c_str());
        return 2;
    }

    Onyx::Threading::MarkMainThread();
    Onyx::App::Window::initNative();
    Onyx::App::Window window;

    State state;
    state.window = window.getGLFWwindow();
    state.sink = std::make_unique<OnyxTextureSink>(window.vkContext());
    state.scheduler = std::make_unique<JobScheduler>(window.workspace().Jobs(), *device);

    window.app().SetDefaultLayout(&defaultLayout);
    window.app().SetRegistrar([&state, initial](Onyx::App::App& app) {
        state.app = &app;
        if (auto* config = app.getConfig()) config->windowTitle = "Icon Composer";
        // Onyx's generic panels are for game archives; ours replace them.
        app.setPanelVisible("Documents", false);
        app.setPanelVisible("Inspector", false);
        app.addPanel(std::make_unique<LayersPanel>(state));
        app.addPanel(std::make_unique<CanvasPanel>(state));
        app.addPanel(std::make_unique<InspectorPanel>(state));
        app.addPanel(std::make_unique<DiagnosticsPanel>(state));
        if (!initial.empty()) state.open(initial);
    });
    window.run();
    // The coordinator returns its texture to the pool before the pool goes, and
    // the pool goes before the VkContext (the window outlives `state`).
    state.close();
    return 0;
}

}  // namespace icapp
