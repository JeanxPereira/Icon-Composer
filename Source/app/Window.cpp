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
#include <string_view>
#include <string>
#include <system_error>
#include <utility>

namespace icapp {
namespace {

namespace fs = std::filesystem;

// A `.icon` is a DIRECTORY (spec 13/09 §4), and the dialog that asks for one
// is `SystemOpenBundleDialog` (OnyxPorts.h), which on Windows hands back the
// folder itself. This stays because the answer is not a bundle on every path
// into it: the fallback dialog off Windows still returns a FILE, and a file
// inside the bundle names the bundle -- picking `Foo.icon/icon.json` opens
// `Foo.icon`. A folder comes back unchanged.
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
    // WHY A DROP IS QUEUED AND NOT ACTED ON. GLFW delivers it from inside
    // `glfwPollEvents`, i.e. mid-frame, and `adopt()` destroys the session the
    // panels after it are about to draw -- the same reason `act()` runs LAST,
    // from the diagnostics panel. The callback only records; `act()` opens.
    std::optional<fs::path> dropped;
    // The last thing that went wrong on a path the person took deliberately,
    // shown on the empty canvas because a GUI has no stderr. The folder picker
    // used to answer "" for a cancel and for a COM failure alike, and the two
    // are indistinguishable to whoever is clicking.
    std::string trouble;

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
            trouble = "not a `.icon` this reader can open: " + dir.string();
            std::fprintf(stderr, "iconcomposer: %s\n", trouble.c_str());
            return;
        }
        trouble.clear();
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
            std::string why;
            const fs::path p = SystemOpenBundleDialog(&why);
            if (!p.empty()) {
                open(bundleDirOf(p));
            } else if (!why.empty()) {
                // Not a cancel: the picker never got as far as asking.
                trouble = why;
                std::fprintf(stderr, "iconcomposer: %s\n", why.c_str());
            }
        }
        // A `.icon` is a folder, so DRAGGING IT IN is the gesture the format
        // actually suggests -- and it is the one path into the document that
        // does not depend on finding the right `File` menu (Onyx draws one of
        // its own, above ours, whose Open cannot accept a folder at all).
        if (dropped) {
            const fs::path p = *dropped;
            dropped.reset();
            open(bundleDirOf(p));
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

// Where a dropped path goes. See the note at the `glfwSetDropCallback` call:
// the GLFW window's user pointer belongs to Onyx, and a GLFW callback carries
// no user data of its own.
State* g_dropTarget = nullptr;

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
            // A BUTTON AND NOT A LABEL. `TextDisabled` read as a caption for a
            // window that had nothing to press, and the only working Open was
            // in the menu above -- next to Onyx's own `File > Open`, which is
            // drawn higher, looks more like the one you want, and cannot take
            // a folder. The empty canvas now carries the action itself.
            if (ImGui::Button("Open a .icon bundle...")) st.actions.open = true;
            ImGui::SameLine();
            ImGui::TextDisabled("or drag one in");
            if (!st.trouble.empty()) {
                ImGui::Spacing();
                ImGui::TextWrapped("%s", st.trouble.c_str());
            }
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
    // "Log" is Onyx's and this tree does not create it, but it is worth
    // keeping -- it prints the adapter and the swapchain. Undocked it floats
    // over the Layers tree, so it goes to the bottom as a tab, and
    // Diagnostics is docked LAST so it is the tab that comes up selected.
#ifndef ONYX_HAS_DOCUMENT_WINDOW_VISIBILITY
    // Onyx's "Viewer" is its DocumentWindow's tab host. Our documents are the
    // Session's and never become tabs there, so it can only ever say "No
    // documents open" -- which, next to an open icon, reads as the open
    // having failed. The registrar hides it outright; on an SDK pin that
    // predates `SetVisible` it cannot be hidden, and then docking it here at
    // least keeps it from floating over the Layers tree.
    ImGui::DockBuilderDockWindow("Viewer", bottom);
#endif
    ImGui::DockBuilderDockWindow("Log", bottom);
    ImGui::DockBuilderDockWindow(ick::kDiagnosticsWindow, bottom);
    ImGui::DockBuilderFinish(dockspaceId);
}

}  // namespace

int run(const fs::path& initial) {
    // Validation off: nobody here is checking the driver, and the tower's
    // default (on) costs every frame (spec 13/09 §6). Two VkInstances in one
    // process -- Onyx's and RenderBox's -- is that section's decision too.
    //
    // AND WHAT MAKES THAT SAFE IS NOT THIS LINE. It is that RenderBox reaches
    // every device-level entry point through a table loaded from ITS OWN device
    // (`volkLoadDeviceTable`, Source/RenderBox/VulkanApi.h). Onyx calls
    // `volkLoadDevice` (VkContext.cpp:362), which owns volk's GLOBAL table for
    // the rest of the process; a second device that read that table would post
    // this render to ONYX's device, and post it without crashing. So the
    // condition is exact and it is checkable: this second device is legal only
    // while `IC_RB_DEVICE_FUNCTIONS` names every device-level function the tower
    // calls. Writing a bare `vkFoo(device, ...)` anywhere in RenderBox -- rather
    // than `api().vkFoo(...)` -- compiles in both builds and re-opens the hole in
    // the UI build alone, where nothing would report it.
    //
    // This was challenged on 15/09 by a branch that adopted Onyx's device
    // instead, on the premise that one dispatch table per process leaves no
    // choice. Measured and rejected: laudo 2026-09-15-device-do-onyx.md.
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

    // DROPPING A `.icon` ON THE WINDOW OPENS IT. Onyx installs an EMPTY drop
    // callback of its own (`Source/App/Window.cpp:191` on the dddce38
    // checkout), so until now every drop was swallowed without a trace. This
    // replaces it: GLFW keeps one callback per window and the last writer
    // wins, so this must run after the `Window` constructor.
    //
    // Only the FIRST path is taken. A multi-selection drop has no meaning for
    // an editor that holds one document, and picking one silently beats
    // opening the last of several.
    // THE USER POINTER IS NOT OURS. Onyx stores its own `Window*` there
    // (`Source/App/Window.cpp:305`) and casts it back in three callbacks
    // (:310, :319, :404); writing ours over it made those read a `State` as a
    // `Window` and the process died on the first resize -- measured, not
    // feared. GLFW passes no user data to a callback, so the one window this
    // process owns is reached through a file-static instead. `run()` is the
    // only writer and there is exactly one `State` per process.
    if (GLFWwindow* w = window.getGLFWwindow()) {
        g_dropTarget = &state;
        glfwSetDropCallback(w, [](GLFWwindow*, int count, const char** paths) {
            if (count < 1 || !paths || !paths[0]) return;
            if (g_dropTarget) g_dropTarget->dropped = fs::path(paths[0]);
        });
    }

    window.app().SetDefaultLayout(&defaultLayout);
    window.app().SetRegistrar([&state, initial](Onyx::App::App& app) {
        state.app = &app;
        if (auto* config = app.getConfig()) config->windowTitle = "Icon Composer";
#ifdef ONYX_HAS_OPEN_FILE_HANDLER
        // ONYX'S `File > Open` IS THE ONE PEOPLE CLICK, so it is the one that
        // has to work. Left to itself it builds its filters from the Workspace
        // modules and probes the path for an owner; this app registers no
        // module, so the filters collapse to "All Files", the dialog is for
        // FILES and a `.icon` is a DIRECTORY, and anything picked is dropped
        // with a warning nobody reads. Claiming it points that item at the
        // same action our own menu raises, and takes Onyx's global Ctrl+O with
        // it so one keystroke stops opening two dialogs.
        //
        // Only the REQUEST is recorded here: this runs mid-frame, and `act()`
        // swaps the session after the panels have drawn.
        //
        // The `#ifdef` is not decoration -- `IC_ONYX_SOURCE_DIR` may be absent
        // and the SHA pinned in CMakeLists.txt predates the hook, so this file
        // has to compile against both.
        app.SetOpenFileHandler([&state] { state.actions.open = true; });
#endif
        // Onyx's generic panels are for game archives; ours replace them.
        app.setPanelVisible("Documents", false);
        app.setPanelVisible("Inspector", false);
#ifdef ONYX_HAS_MENU_ENTRY_FILTER
        // A barra do Onyx fica ACIMA da nossa e tem a cara de menu principal,
        // entao o que esta nela e inerte aqui e pior do que ausente: e um
        // convite. Tres entradas sao dessa especie, medidas 18/09 contra o
        // checkout `dddce38`:
        //
        //   `Export`          -- glTF, DDS e Copy Hash, permanentemente
        //                        cinzas, e o comentario do proprio Onyx diz
        //                        que nunca tiveram corpo. Num editor de icone
        //                        e ruido, e contradiz o nosso `File > Export
        //                        Icon as Image...` logo abaixo.
        //   `File > Close All`-- fecha documentos do Workspace e abas do
        //                        DocumentWindow. Nao registramos nenhum
        //                        documento la e escondemos o DocumentWindow,
        //                        entao o item nao faz nada -- e parece o
        //                        fechar do app, que e o nosso `File > Close`.
        //   `File > Recent Files` -- os recentes do Onyx, rotulados com dica
        //                        de jogo (GOW1/GOW2/GOWR) e abertos pelo
        //                        Workspace. Para nos, lista vazia ou arquivos
        //                        de outro app.
        //
        // `File > Open` fica (nos o reivindicamos acima), `File > Exit` fica
        // (desde esta rodada ele fecha pela porta normal em vez de `exit(0)`),
        // `Options` e `View` ficam, porque funcionam.
        app.SetMenuEntryFilter([](std::string_view menu, std::string_view item) {
            if (menu == "Export") return false;
            if (menu == "File" && (item == "Close All" || item == "Recent Files")) return false;
            return true;
        });
#endif
#ifdef ONYX_HAS_DOCUMENT_WINDOW_VISIBILITY
        // And "Viewer" is not a panel, so `setPanelVisible` never reached it:
        // it is the DocumentWindow's own tab host, drawn straight from
        // `App::frame()`. Our documents belong to the Session, not to Onyx's
        // Workspace (Rule 2 of the architecture spec -- the same reason this
        // app registers no GameModule), so no tab is ever added to it and the
        // window has exactly one thing it can say: "No documents open."
        // Measured 18/09 with `AppIcon-27.icon` open on screen and that line
        // underneath it -- true about that tab host, and read by anyone
        // looking at the screen as the open having silently failed. It is the
        // same trap as the File menu that swallowed the pick, one panel down.
        app.getDocumentWindow().SetVisible(false);
#endif
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
