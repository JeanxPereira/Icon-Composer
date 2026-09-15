// The Layers tree: the document's structure, and the door every other panel
// comes through (spec 13/09 §7).
//
// Selection is simple in this round and it drives the inspector, so this file
// owns exactly two kinds of state that are not the document's: which row is
// being renamed, and what the row's context menu asked for. Everything that
// touches the document goes through a Session command -- writing into the tree
// through `root()` would break undo and leave `version()` where it was, which is
// a canvas showing a document that is no longer in memory (Session.h).
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/ViewModel.h"
#include "imgui.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string>

namespace ick {
namespace {

// A structural edit asked for DURING the frame, run after it.
//
// Add, Delete and Move all reorder the very vectors this panel is walking: a
// Delete picked from a row's context menu frees the node whose toggles are drawn
// three lines further down, and shortens the `groups` array the loop is indexing.
// So a row only records what was asked; `drawLayers` runs it once the tree is
// closed, which also makes at most one structural command per frame.
enum class Act { None, MoveUp, MoveDown, Delete, AddGroup, AddLayer };

struct Pending {
    Act act = Act::None;
    icf::NodePath path{};
};

// The rename in progress. It outlives the frame that opened it and the Session
// stores no UI state, so it lives here; one editor window edits one document.
std::optional<icf::NodePath> g_renaming;
char g_renameBuffer[256] = {};
bool g_renameFocus = false;

void beginRename(icf::NodePath path, const std::string& title) {
    g_renaming = path;
    g_renameFocus = true;
    const std::size_t n = std::min(title.size(), sizeof g_renameBuffer - 1);
    std::memcpy(g_renameBuffer, title.data(), n);
    g_renameBuffer[n] = '\0';
}

// One row: the name (selectable, renamable), then the toggles at the right edge.
void drawRow(Session& s, icf::NodePath path, LayersStats& st, Pending& pending, bool isLayer) {
    const icf::json::Value* node = icf::nodeAt(s.root(), path);
    if (!node) return;
    ImGui::PushID(static_cast<int>(path.group.value_or(0)));
    ImGui::PushID(path.layer ? static_cast<int>(*path.layer) : -1);

    const std::string title = nodeTitle(s, path);
    const bool selected = s.selection && *s.selection == path;

    // The toggles sit at the right edge, so the name takes what is left of the row.
    const float toggles = isLayer ? 60.0f : 30.0f;
    const float nameWidth = std::max(ImGui::GetContentRegionAvail().x - toggles, 1.0f);

    if (g_renaming && *g_renaming == path) {
        if (g_renameFocus) {
            // Only on the frame the rename opened. Asking every frame re-activates
            // the field, and the field drops what was typed into it when it is.
            ImGui::SetKeyboardFocusHere();
            g_renameFocus = false;
        }
        ImGui::SetNextItemWidth(nameWidth);
        if (ImGui::InputText("##rename", g_renameBuffer, sizeof g_renameBuffer,
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            s.rename(path, g_renameBuffer);
            g_renaming.reset();
        } else if (ImGui::IsItemDeactivated()) {
            // Enter is the commit; Escape and the focus moving away discard.
            g_renaming.reset();
        }
    } else {
        if (ImGui::Selectable(title.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick,
                              ImVec2(nameWidth, 0.0f))) {
            if (!selected) st.selectionChanged = true;
            s.selection = path;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) beginRename(path, title);
        }
        if (ImGui::BeginPopupContextItem("row-menu")) {
            if (ImGui::MenuItem("Move Up")) pending = Pending{Act::MoveUp, path};
            if (ImGui::MenuItem("Move Down")) pending = Pending{Act::MoveDown, path};
            ImGui::Separator();
            if (ImGui::MenuItem("Delete")) pending = Pending{Act::Delete, path};
            ImGui::EndPopup();
        }
    }

    // The toggles read and write under the BASE scope on purpose: this sidebar is
    // the document's structure, and the per-appearance answer belongs to the
    // inspector, which carries the scope selector (spec 13/09 §7).
    ImGui::SameLine();
    const icf::json::Value* hidden = icf::resolve(*node, "hidden", icf::Context{});
    // `hidden` is stored inverted from what the eye sees. The checkbox is
    // "visible", so the sign is flipped here and nowhere else.
    bool visible = !(hidden && hidden->kind() == icf::json::Value::Kind::Bool && hidden->boolean());
    if (ImGui::Checkbox("##visible", &visible)) {
        // Visible again REMOVES this scope's entry instead of writing `false`:
        // The checkbox writes the boolean it asserts, it does not remove the key.
        // `[ART]` Both spellings live in the corpus -- 250 nodes carry
        // `"hidden" : false` and one carries `true`, while the rest carry no key
        // at all -- so absence is NOT "how the corpus says not hidden", and
        // removing would delete a key Apple's own encoder wrote. Writing the
        // value keeps this toggle symmetric with the glass one right below.
        s.setProperty(path, "hidden", icf::Context{}, icf::json::Value::boolean(!visible));
    }
    ImGui::SetItemTooltip("Toggle visibility");

    if (isLayer) {
        ImGui::SameLine();
        const icf::json::Value* glass = icf::resolve(*node, "glass", icf::Context{});
        bool on = glass && glass->kind() == icf::json::Value::Kind::Bool && glass->boolean();
        if (ImGui::Checkbox("##glass", &on)) {
            s.setProperty(path, "glass", icf::Context{}, icf::json::Value::boolean(on));
        }
        ImGui::SetItemTooltip("Enable or disable glass effects on this layer");
    }

    ImGui::PopID();
    ImGui::PopID();
}

}  // namespace

LayersStats drawLayers(Session& s) {
    LayersStats st;
    if (!ImGui::Begin(kLayersWindow)) {
        ImGui::End();
        return st;
    }
    Pending pending;

    const icf::json::Value* groups = s.root().find("groups");
    const std::size_t count = groups && groups->kind() == icf::json::Value::Kind::Array
                                  ? groups->elements().size()
                                  : 0;
    for (std::size_t g = 0; g < count; ++g) {
        ++st.groups;
        const icf::NodePath gp{g, std::nullopt};
        ImGui::PushID(static_cast<int>(g));
        // The arrow is the whole widget; the name beside it is a Selectable, so
        // clicking the name selects instead of collapsing (OpenOnArrow).
        const bool open =
            ImGui::TreeNodeEx("##group", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap |
                                             ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow);
        ImGui::SameLine();
        drawRow(s, gp, st, pending, false);
        if (open) {
            const icf::json::Value* group = icf::nodeAt(s.root(), gp);
            const icf::json::Value* layers = group ? group->find("layers") : nullptr;
            const std::size_t n =
                layers && layers->kind() == icf::json::Value::Kind::Array ? layers->elements().size() : 0;
            for (std::size_t l = 0; l < n; ++l) {
                ++st.layers;
                drawRow(s, icf::NodePath{g, l}, st, pending, true);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::Button("+")) ImGui::OpenPopup("add-menu");
    ImGui::SetItemTooltip("Opens menu to add new group or image layer");
    if (ImGui::BeginPopup("add-menu")) {
        if (ImGui::MenuItem("Add Group")) pending = Pending{Act::AddGroup, icf::NodePath{}};
        // A layer needs a group to live in; with none, the item says so by being grey.
        if (ImGui::MenuItem("Add Image Layer", nullptr, false, count > 0)) {
            const std::size_t g = s.selection && s.selection->group ? *s.selection->group : count - 1;
            pending = Pending{Act::AddLayer, icf::NodePath{g, std::nullopt}};
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("-") && s.selection && s.selection->group) pending = Pending{Act::Delete, *s.selection};

    ImGui::End();

    // The tree is drawn and closed; only now may the structure move under it.
    if (pending.act != Act::None) g_renaming.reset();
    switch (pending.act) {
        case Act::None:
            break;
        case Act::MoveUp:
            s.moveNode(pending.path, -1);
            break;
        case Act::MoveDown:
            s.moveNode(pending.path, +1);
            break;
        case Act::Delete:
            s.removeNode(pending.path);
            break;
        case Act::AddGroup:
            // `image-name` stays empty until the import round fills it, which is
            // what a group with no layers already is: drawn, and rendering nothing.
            if (auto i = s.addGroup("Group")) {
                s.selection = icf::NodePath{*i, std::nullopt};
                st.selectionChanged = true;
            }
            break;
        case Act::AddLayer:
            if (pending.path.group) {
                if (auto i = s.addLayer(*pending.path.group, "Layer", "")) {
                    s.selection = icf::NodePath{*pending.path.group, *i};
                    st.selectionChanged = true;
                }
            }
            break;
    }
    return st;
}

}  // namespace ick
