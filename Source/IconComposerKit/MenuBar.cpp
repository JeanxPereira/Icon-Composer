// The menu bar, drawn by us inside the canvas window.
//
// WHY WE DRAW IT AND NOT THE TOOLKIT (spec 13/09 §7)
// --------------------------------------------------
// Onyx draws `File > Open` and `Recent Files` on its own and offers no hook to
// them, so an editor that let Onyx own the bar would ship two File menus that
// disagree. We draw ours; Onyx keeps the layout, the theme and the window.
//
// WHY THE ACTIONS ARE RETURNED INSTEAD OF PERFORMED
// -------------------------------------------------
// Rule 2 of the architecture spec: the Kit links no toolkit, so it cannot open
// a file dialog and cannot close a window. `MenuActions` is the whole of what
// this function can ask for; the app reads it after the frame and does it.
//
// The items this round does not implement -- Export, Copy/Paste Properties,
// Localization -- are drawn DISABLED with the reason in a tooltip (spec 13/09
// §7), not hidden. A menu that omits what the target has teaches the wrong
// shape of the program.
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/ViewModel.h"

#include "imgui.h"

#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <optional>
#include <string_view>

namespace ick {
namespace {

// Every item goes through here so that `MenuStats` counts what was actually
// drawn: a disabled item that stopped being counted is a menu that can quietly
// lose an entry without any frame noticing.
struct Builder {
    MenuStats& st;
    bool item(const char* label, const char* shortcut, bool enabled = true, const char* why = nullptr,
              bool selected = false) {
        ++st.items;
        if (!enabled) ++st.disabled;
        const bool pressed = ImGui::MenuItem(label, shortcut, selected, enabled);
        if (why) ImGui::SetItemTooltip("%s", why);
        return pressed;
    }
};

constexpr const char* kRound5 = "Round 5: not built yet";

// Reads a boolean property as the canvas would see it under the base context.
// A property that is absent, or present with another type, is `false` -- the
// same reading `resolve` gives a node that never wrote the key.
bool booleanUnderBase(const icf::json::Value& node, std::string_view prop) {
    const icf::json::Value* v = icf::resolve(node, prop, icf::Context{});
    return v && v->kind() == icf::json::Value::Kind::Bool && v->boolean();
}

}  // namespace

MenuStats drawMenuBar(Session& s, MenuActions& a) {
    MenuStats st;
    if (!ImGui::BeginMenuBar()) return st;
    Builder b{st};

    ++st.menus;
    if (ImGui::BeginMenu("File")) {
        if (b.item("New…", "Ctrl+N")) a.newDocument = true;
        if (b.item("Open…", "Ctrl+O")) a.open = true;
        ImGui::Separator();
        // "Dirty" is the undo stack not being where it was at the last save, so
        // a document edited and then fully undone offers nothing to save.
        if (b.item("Save", "Ctrl+S", s.isDirty())) a.save = true;
        if (b.item("Save As…", "Ctrl+Shift+S")) a.saveAs = true;
        b.item("Export Icon as Image…", nullptr, false, kRound5);
        ImGui::Separator();
        if (b.item("Close", "Ctrl+W")) a.close = true;
        if (b.item("Quit", "Ctrl+Q")) a.quit = true;
        ImGui::EndMenu();
    }

    ++st.menus;
    if (ImGui::BeginMenu("Edit")) {
        if (b.item("Undo", "Ctrl+Z", s.canUndo())) s.undo();
        if (b.item("Redo", "Ctrl+Y", s.canRedo())) s.redo();
        ImGui::Separator();
        if (b.item("Delete", "Del", s.selection && s.selection->group)) s.removeNode(*s.selection);
        b.item("Copy Properties", nullptr, false, kRound5);
        b.item("Paste Properties", nullptr, false, kRound5);
        b.item("Localization", nullptr, false, kRound5);
        ImGui::EndMenu();
    }

    ++st.menus;
    if (ImGui::BeginMenu("View")) {
        // These four write `Session::view` directly. They are NOT commands: the
        // context a person is looking through is not part of the document, it
        // does not move `version()`, and it is not on the undo stack.
        if (ImGui::BeginMenu("Appearance")) {
            for (auto ap : {icf::Appearance::Base, icf::Appearance::Light, icf::Appearance::Dark,
                            icf::Appearance::Tinted}) {
                if (b.item(appearanceLabel(ap), nullptr, true, nullptr, s.view.context.appearance == ap)) {
                    s.view.context.appearance = ap;
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Idiom")) {
            for (auto id : {icf::Idiom::Base, icf::Idiom::Square, icf::Idiom::IOS, icf::Idiom::MacOS,
                            icf::Idiom::WatchOS}) {
                if (b.item(idiomLabel(id), nullptr, true, nullptr, s.view.context.idiom == id)) {
                    s.view.context.idiom = id;
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Preview Size")) {
            if (b.item("512", nullptr, true, nullptr, s.view.size == 512)) s.view.size = 512;
            if (b.item("1024", nullptr, true, nullptr, s.view.size == 1024)) s.view.size = 1024;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Zoom")) {
            // Zoom is NOT a render (spec 13/09 §6): it is the same pixels shown
            // larger, so it never reaches the render key.
            for (float z : {0.5f, 0.75f, 1.0f, 1.5f, 2.0f}) {
                char label[16];
                std::snprintf(label, sizeof label, "%d%%", static_cast<int>(z * 100));
                if (b.item(label, nullptr, true, nullptr, s.view.zoom == z)) s.view.zoom = z;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    ++st.menus;
    if (ImGui::BeginMenu("Layer")) {
        const bool hasSel = s.selection && s.selection->group;
        const bool isLayer = hasSel && s.selection->layer;
        const icf::json::Value* groupsNode = s.root().find("groups");
        const std::size_t groups = groupsNode ? groupsNode->elements().size() : 0;

        if (b.item("Add Group", nullptr)) {
            if (auto i = s.addGroup("Group")) s.selection = icf::NodePath{*i, std::nullopt};
        }
        // A layer needs a group to live in, and a fresh document has none.
        if (b.item("Add Image Layer", nullptr, groups > 0)) {
            const std::size_t g = hasSel ? *s.selection->group : groups - 1;
            if (auto i = s.addLayer(g, "Layer", "")) s.selection = icf::NodePath{g, *i};
        }
        ImGui::Separator();
        // Both toggles write the BASE scope: the menu is the coarse switch, and
        // the per-appearance override is the inspector's scope selector (§7).
        if (b.item("Toggle Glass", nullptr, isLayer)) {
            if (const icf::json::Value* node = icf::nodeAt(s.root(), *s.selection)) {
                s.setProperty(*s.selection, "glass", icf::Context{},
                              icf::json::Value::boolean(!booleanUnderBase(*node, "glass")));
            }
        }
        // `hidden` absent IS visible, so showing a node again removes the entry
        // rather than writing `false` -- the document keeps the shape it had
        // before anything was hidden.
        if (b.item("Toggle Visibility", nullptr, hasSel)) {
            if (const icf::json::Value* node = icf::nodeAt(s.root(), *s.selection)) {
                const bool hidden = booleanUnderBase(*node, "hidden");
                s.setProperty(*s.selection, "hidden", icf::Context{},
                              hidden ? std::optional<icf::json::Value>{}
                                     : std::optional<icf::json::Value>{icf::json::Value::boolean(true)});
            }
        }
        ImGui::Separator();
        if (b.item("Move Up", nullptr, hasSel)) s.moveNode(*s.selection, -1);
        if (b.item("Move Down", nullptr, hasSel)) s.moveNode(*s.selection, +1);
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
    return st;
}

}  // namespace ick
