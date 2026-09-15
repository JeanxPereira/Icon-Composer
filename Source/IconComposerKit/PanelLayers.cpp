// The Layers tree: the document's structure, and the door every other panel
// comes through (spec 13/09 §7).
//
// WHAT THE TARGET'S PANEL IS, AND WHY THIS ONE CHANGED SHAPE (laudo 15/09)
// -----------------------------------------------------------------------------
// `[BIN]` In `IconComposerKit.arm64` the panel is `LayerOutlineView`, an
// `NSViewRepresentable` around a private `ICOutlineView : NSOutlineView`, with a
// `Coordinator` conforming to `NSOutlineViewDataSource` and `NSOutlineViewDelegate`.
// The selector stubs that class sends say what the panel can do, and each one is
// an affordance this file used to be missing:
//
//   registerForDraggedTypes:  draggingPasteboard  draggingSourceOperationMask
//   setDropItem:dropChildIndex:  setDraggingFormation:      -> reorder by DRAG,
//       and the drop lands at (parent item, child index) -- BETWEEN two rows,
//       not "one step towards the top". `WithDragIndicatorDrawBugWorkaroundPixel-
//       ModifierV` is a whole type about drawing that indicator.
//   expandItem:  expandItem:expandChildren:  rowForItem:  itemAtRow:  clickedRow
//       -> disclosure is the PANEL's state, addressed by item, and a row can be
//       found from the model (to select it, to scroll to it).
//   setAllowsMultipleSelection:  selectRowIndexes:byExtendingSelection:
//       selectedRowIndexes, plus `IconMemberSelection.ModifierFlags` and
//       `.HomogeneousContent`  -> multi-selection. NOT built here; see below.
//
// and in the SwiftUI half, `LayerListV`'s body carries `onDeleteCommand(perform:)`,
// `onKeyPress(characters:phases:action:)`, `keyboardShortcut`, a
// `LargeHitAreaButtonStyle`, and a `ForEach` over `IconComposerFoundation.Diagnostic`
// drawing an `Image` + `Text` + `Color` -- the keyboard, a row you can actually
// hit, and problems said out loud IN the list.
//
// WHAT THIS FILE OWNS
// -----------------------------------------------------------------------------
// Everything that touches the document goes through a Session command -- writing
// into the tree through `root()` would break undo and leave `version()` where it
// was, which is a canvas showing a document that is no longer in memory
// (Session.h). So the panel owns exactly the state that is NOT the document's:
// which row is being renamed, which groups are open, what the gesture asked for.
//
// TWO THINGS THE TARGET HAS THAT ARE DELIBERATELY NOT HERE
// -----------------------------------------------------------------------------
// * MULTI-SELECTION. `Session::selection` is one `optional<NodePath>`, and it is
//   read by the inspector and the canvas. Widening it is a Session round, not a
//   panel round, and doing it from here would change a type two other panels are
//   being edited against right now.
// * DUPLICATE / COPY-PASTE (`PasteboardLayerV`, `PasteboardMembersV`,
//   `PasteboardCommandHandlerV`). There is no single model command for "copy this
//   node", and composing one out of `addLayer` plus a `setProperty` per key would
//   put one gesture on the undo stack as a dozen commands -- the very thing the
//   coalescing below exists to prevent. It needs `Session::duplicate`.
// * REPARENTING BY DRAG. `setDropItem:dropChildIndex:` can drop a layer into
//   ANOTHER group; `moveNode` only swaps siblings, so a cross-group drop would be
//   add+remove, two commands and a new node id. Dropping outside the dragged
//   row's own parent is therefore refused rather than half-done.
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/ViewModel.h"
#include "imgui.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace ick {
namespace {

// A structural edit asked for DURING the frame, run after it.
//
// Add, Delete and Move all reorder the very vectors this panel is walking: a
// Delete picked from a row's context menu frees the node whose toggles are drawn
// three lines further down, and shortens the `groups` array the loop is indexing.
// So a row only records what was asked; `drawLayers` runs it once the tree is
// closed, which also makes at most one structural gesture per frame.
enum class Act { None, MoveUp, MoveDown, ToFront, ToBack, Drop, Delete, AddGroup, AddLayer };

struct Pending {
    Act act = Act::None;
    icf::NodePath path{};
    std::size_t gap = 0;    // Drop only: the insertion slot, in `planDrop`'s counting
};

// ---- the panel's own state -------------------------------------------------
// It outlives the frame and the Session stores no UI state, so it lives here;
// one editor window edits one document. `g_owner` is what makes that honest: if
// a different Session is ever drawn, everything below is about another file.
const Session* g_owner = nullptr;

std::optional<icf::NodePath> g_renaming;
char g_renameBuffer[256] = {};
bool g_renameFocus = false;
// The rename ENDED on this frame. Enter both commits the field and is the key
// that opens one, so without this the commit re-opens the field it just closed.
bool g_renameClosed = false;

// Open/closed, by group index.
//
// This used to be ImGui's, stored under the tree node's ID -- and that ID was
// the group's POSITION, so moving a group down swapped its disclosure state with
// its neighbour's and deleting a group handed its state to whoever slid up into
// its index. The target addresses disclosure by ITEM (`expandItem:`), and the
// format gives a node no id to be an item by: `addGroup` writes `name` and
// `layers` and nothing else (Edit.h), and no corpus document carries a key that
// identifies a node. So the state stays positional, and this file permutes it in
// the same step that permutes the groups -- which is exactly the thing ImGui's
// storage could not do. Undo and redo happen in the menu bar, outside this file;
// a group deleted and undone comes back open.
std::vector<unsigned char> g_expanded;

// The selection as this panel last saw it, and whether THIS panel moved it. A
// selection that moved from somewhere else -- the canvas, an undo -- may be on a
// row that is scrolled away, which is `rowForItem:` in the target: the model
// names a node, the view finds its row and brings it into sight.
std::optional<icf::NodePath> g_lastSelection;
bool g_selfSelected = false;

constexpr const char* kDragType = "IC_LAYER_ROW";
static_assert(std::is_trivially_copyable_v<icf::NodePath>,
              "the drag payload is a memcpy of the path; ImGui copies it byte for byte");

// A warm red, the same one PanelInspectorAsset uses for the same fact: the
// document names a file and `Assets/` does not have it.
const ImVec4 kAlarm(0.95f, 0.45f, 0.35f, 1.0f);

void beginRename(icf::NodePath path, const std::string& title) {
    g_renaming = path;
    g_renameFocus = true;
    const std::size_t n = std::min(title.size(), sizeof g_renameBuffer - 1);
    std::memcpy(g_renameBuffer, title.data(), n);
    g_renameBuffer[n] = '\0';
}

std::size_t groupCount(const icf::json::Value& root) {
    const icf::json::Value* groups = root.find("groups");
    return groups && groups->kind() == icf::json::Value::Kind::Array ? groups->elements().size() : 0;
}

std::size_t layerCount(const icf::json::Value& root, std::size_t g) {
    const icf::json::Value* group = icf::nodeAt(root, icf::NodePath{g, std::nullopt});
    const icf::json::Value* layers = group ? group->find("layers") : nullptr;
    return layers && layers->kind() == icf::json::Value::Kind::Array ? layers->elements().size() : 0;
}

bool isOpen(std::size_t g) { return g >= g_expanded.size() || g_expanded[g] != 0; }

void setOpen(std::size_t g, bool open) {
    if (g >= g_expanded.size()) g_expanded.resize(g + 1, 1);
    g_expanded[g] = open ? 1 : 0;
}

void swapExpanded(std::size_t a, std::size_t b) {
    const std::size_t hi = std::max(a, b);
    if (hi >= g_expanded.size()) g_expanded.resize(hi + 1, 1);
    std::swap(g_expanded[a], g_expanded[b]);
}

// Two paths can trade places only inside the same parent (see the header note).
bool sameParent(icf::NodePath a, icf::NodePath b) {
    if (a.layer.has_value() != b.layer.has_value()) return false;
    return !a.layer || a.group == b.group;
}

// ---- what a row has to say -------------------------------------------------
// Three states, and they are three different facts -- the same three
// PanelInspectorAsset spells out for the selected layer, said here for every
// layer at once so a document with one broken reference among forty does not
// need forty clicks to find it.
enum class Trouble { None, NoArt, MissingArt };

Trouble troubleOf(const Session& s, icf::NodePath path, std::string& fileName) {
    if (!path.layer) return Trouble::None;
    const icf::json::Value* node = icf::nodeAt(s.root(), path);
    if (!node) return Trouble::None;
    // Under the composition the canvas is SHOWING, not under Base: a layer whose
    // art is specialized per idiom is fine in one idiom and broken in another,
    // and the list has to agree with the picture beside it.
    const icf::json::Value* name = icf::resolve(*node, "image-name", s.view.context);
    if (!name || name->kind() != icf::json::Value::Kind::String || name->rawString().empty())
        return Trouble::NoArt;
    fileName = std::string(name->rawString());
    const std::vector<std::string>& files = s.bundle().assetFiles();
    return std::find(files.begin(), files.end(), fileName) == files.end() ? Trouble::MissingArt
                                                                          : Trouble::None;
}

// ---- one row ---------------------------------------------------------------
// The row is ONE full-width Selectable, and everything else is drawn over it.
//
// It used to be a TreeNodeEx spanning the width with a Selectable laid on top of
// it, sized to the width minus the toggles: the two overlapped, the tree node won
// the empty space to the right of the name, and the tree node was OpenOnArrow --
// so a click in the gap did nothing at all. `[BIN]` The target names its answer
// `LayerList.LargeHitAreaButtonStyle`.
struct RowResult {
    bool clicked = false;
    bool doubleClicked = false;
};

RowResult drawRow(Session& s, icf::NodePath path, LayersStats& st, Pending& pending) {
    RowResult r;
    const icf::json::Value* node = icf::nodeAt(s.root(), path);
    if (!node) return r;
    const bool isLayer = path.layer.has_value();

    ImGui::PushID(static_cast<int>(path.group.value_or(0)));
    ImGui::PushID(isLayer ? static_cast<int>(*path.layer) : -1);

    const std::string title = nodeTitle(s, path);
    const bool selected = s.selection && *s.selection == path;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float rowH = ImGui::GetFrameHeight();
    const float arrowW = rowH;                                 // the disclosure column
    const float indent = isLayer ? style.IndentSpacing : 0.0f;

    const ImVec2 rowPos = ImGui::GetCursorScreenPos();
    const bool renaming = g_renaming && *g_renaming == path;

    // 1. The hit area: the whole row, always, renaming or not.
    r.clicked = ImGui::Selectable("##row", selected,
                                  ImGuiSelectableFlags_AllowOverlap |
                                      ImGuiSelectableFlags_AllowDoubleClick,
                                  ImVec2(0.0f, rowH));
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mx = ImGui::GetItemRectMax();
    r.doubleClicked = r.clicked && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

    // 2. The drag, both ends of it, on that same item.
    if (!renaming && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
        ImGui::SetDragDropPayload(kDragType, &path, sizeof path);
        ImGui::TextUnformatted(title.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        // AcceptPeekOnly = peek before the button is released, and draw nothing
        // by default: the indicator below is the only thing that should appear,
        // and it has to appear BEFORE the drop to be worth anything.
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kDragType, ImGuiDragDropFlags_AcceptPeekOnly)) {
            icf::NodePath src{};
            std::memcpy(&src, p->Data, sizeof src);
            // A layer over the header of its OWN group means the top of that
            // group -- the one place inside a group that has no layer row above
            // it. `[BIN]` setDropItem:dropChildIndex: with childIndex 0.
            const bool intoOwnGroupTop = src.layer && !isLayer && src.group == path.group;
            if (sameParent(src, path) || intoOwnGroupTop) {
                const bool below = !intoOwnGroupTop && ImGui::GetIO().MousePos.y > (mn.y + mx.y) * 0.5f;
                const std::size_t here = intoOwnGroupTop ? 0 : (isLayer ? *path.layer : *path.group);
                const std::size_t gap = intoOwnGroupTop ? 0 : here + (below ? 1 : 0);
                const std::size_t from = src.layer ? *src.layer : *src.group;
                const std::size_t count = src.layer ? layerCount(s.root(), *src.group) : groupCount(s.root());
                const DropPlan plan = planDrop(from, gap, count);
                if (plan.valid) {
                    const float y = intoOwnGroupTop ? mx.y : (below ? mx.y : mn.y);
                    ImGui::GetWindowDrawList()->AddLine(ImVec2(mn.x, y), ImVec2(mx.x, y),
                                                        ImGui::GetColorU32(ImGuiCol_DragDropTarget), 2.0f);
                    if (p->IsDelivery()) pending = Pending{Act::Drop, src, gap};
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    // 3. The context menu, on that same item. `clickedRow` in the target: the
    //    menu belongs to the row under the pointer, not to the selection.
    if (!renaming && ImGui::BeginPopupContextItem("row-menu")) {
        if (ImGui::MenuItem("Rename", "F2")) beginRename(path, title);
        ImGui::Separator();
        // `[BIN]` MenuContent.ArrangeSection.Subsections -- two subsections, and
        // the four items AppKit puts in them. To Front / To Back are the drag a
        // person would otherwise have to perform to the end of a long list.
        if (ImGui::MenuItem("Bring to Front")) pending = Pending{Act::ToFront, path};
        if (ImGui::MenuItem("Move Up", "Ctrl+Up")) pending = Pending{Act::MoveUp, path};
        if (ImGui::MenuItem("Move Down", "Ctrl+Down")) pending = Pending{Act::MoveDown, path};
        if (ImGui::MenuItem("Send to Back")) pending = Pending{Act::ToBack, path};
        ImGui::Separator();
        if (!isLayer && ImGui::MenuItem("Add Image Layer")) pending = Pending{Act::AddLayer, path};
        if (ImGui::MenuItem("Delete", "Del")) pending = Pending{Act::Delete, path};
        ImGui::EndPopup();
    }

    // 4. Everything visible, drawn back over the row.
    float x = rowPos.x + indent;
    if (!isLayer) {
        ImGui::SetCursorScreenPos(ImVec2(x, rowPos.y));
        if (ImGui::ArrowButton("##disclose", isOpen(*path.group) ? ImGuiDir_Down : ImGuiDir_Right))
            setOpen(*path.group, !isOpen(*path.group));
    }
    x += arrowW;

    // The toggles sit at the right edge and are real items, because they are the
    // only things on the row besides the arrow that take a click of their own.
    const float toggleW = rowH + style.ItemSpacing.x;
    float right = mx.x - style.FramePadding.x;
    const icf::json::Value* hidden = icf::resolve(*node, "hidden", icf::Context{});
    // `hidden` is stored inverted from what the eye sees. The checkbox is
    // "visible", so the sign is flipped here and nowhere else.
    bool visible = !(hidden && hidden->kind() == icf::json::Value::Kind::Bool && hidden->boolean());
    right -= rowH;
    ImGui::SetCursorScreenPos(ImVec2(right, rowPos.y));
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
        const icf::json::Value* glass = icf::resolve(*node, "glass", icf::Context{});
        bool on = glass && glass->kind() == icf::json::Value::Kind::Bool && glass->boolean();
        right -= toggleW;
        ImGui::SetCursorScreenPos(ImVec2(right, rowPos.y));
        if (ImGui::Checkbox("##glass", &on)) {
            s.setProperty(path, "glass", icf::Context{}, icf::json::Value::boolean(on));
        }
        ImGui::SetItemTooltip("Enable or disable glass effects on this layer");
    }

    // The name, or the field that is replacing it. Everything left of the
    // toggles belongs to it.
    const float nameWidth = std::max(right - style.ItemSpacing.x - x, 1.0f);
    if (renaming) {
        ImGui::SetCursorScreenPos(ImVec2(x, rowPos.y));
        if (g_renameFocus) {
            // Only on the frame the rename opened. Asking every frame re-activates
            // the field, and the field drops what was typed into it when it is.
            ImGui::SetKeyboardFocusHere();
            g_renameFocus = false;
        }
        ImGui::SetNextItemWidth(nameWidth);
        const bool committed = ImGui::InputText("##rename", g_renameBuffer, sizeof g_renameBuffer,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
        if (committed) {
            s.rename(path, g_renameBuffer);
            g_renaming.reset();
            g_renameClosed = true;
        } else if (ImGui::IsItemDeactivated()) {
            // Escape throws the edit away; anything else -- Enter, or the focus
            // moving to another row -- keeps it. It used to discard on both, so
            // clicking away from a field mid-rename silently lost the typing.
            if (!ImGui::IsKeyPressed(ImGuiKey_Escape, false)) s.rename(path, g_renameBuffer);
            g_renaming.reset();
            g_renameClosed = true;
        }
    } else {
        std::string file;
        const Trouble t = troubleOf(s, path, file);
        if (t != Trouble::None) ++st.problems;
        // `[BIN]` LayerList's body draws a ForEach over IconComposerFoundation's
        // Diagnostic as Image + Text + Color; `[ONYX]` DocumentBrowser paints a
        // failed entry in its failure colour and explains it on hover. Both say
        // the same thing: the list is where a broken node is found, not the
        // inspector you have to click into first.
        const ImVec4 tint = t == Trouble::MissingArt  ? kAlarm
                            : t == Trouble::NoArt     ? style.Colors[ImGuiCol_TextDisabled]
                                                      : style.Colors[ImGuiCol_Text];
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(ImVec2(x, mn.y), ImVec2(x + nameWidth, mx.y), true);
        std::string shown = title;
        if (t == Trouble::MissingArt) shown = "! " + shown;
        dl->AddText(ImVec2(x, rowPos.y + style.FramePadding.y), ImGui::GetColorU32(tint), shown.c_str());
        dl->PopClipRect();
        if (t != Trouble::None && ImGui::IsMouseHoveringRect(mn, mx) &&
            ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
            if (t == Trouble::MissingArt) {
                ImGui::SetTooltip(
                    "Assets/ does not have \"%s\", so this layer renders nothing under the "
                    "composition on screen. The Inspector's Image Asset section imports it.",
                    file.c_str());
            } else {
                ImGui::SetTooltip("This layer names no image and draws nothing.");
            }
        }
    }

    // Back to the left edge, one row down, for whoever draws next.
    ImGui::SetCursorScreenPos(ImVec2(rowPos.x, mx.y + style.ItemSpacing.y));
    ImGui::PopID();
    ImGui::PopID();
    return r;
}

// Runs a plan's worth of swaps as ONE undoable gesture, and keeps the panel's
// disclosure state alongside the groups it is about.
icf::NodePath runMoves(Session& s, icf::NodePath path, int delta, std::size_t steps) {
    for (std::size_t i = 0; i < steps; ++i) {
        if (!s.moveNode(path, delta, /*coalesce=*/true)) break;
        std::size_t& index = path.layer ? *path.layer : *path.group;
        const std::size_t next = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(index) + delta);
        if (!path.layer) swapExpanded(index, next);
        index = next;
    }
    s.endCoalescing();
    return path;
}

}  // namespace

// ---- the arithmetic, testable without a window ------------------------------

DropPlan planDrop(std::size_t from, std::size_t gap, std::size_t count) {
    DropPlan plan;
    if (from >= count || gap > count) return plan;
    // A row dropped into the slot just above or just below itself does not move:
    // both name the place it already occupies. Returning "0 steps, valid" would
    // push an empty command; returning invalid is what "nothing happened" is.
    if (gap == from || gap == from + 1) return plan;
    // Above itself, the slot IS the destination index. Below itself, removing the
    // row first shifts every later slot down by one.
    plan.to = gap < from ? gap : gap - 1;
    plan.delta = plan.to < from ? -1 : +1;
    plan.steps = plan.to < from ? from - plan.to : plan.to - from;
    plan.valid = true;
    return plan;
}

std::vector<icf::NodePath> visibleRows(const icf::json::Value& root,
                                       const std::vector<unsigned char>& expanded) {
    std::vector<icf::NodePath> rows;
    const icf::json::Value* groups = root.find("groups");
    if (!groups || groups->kind() != icf::json::Value::Kind::Array) return rows;
    const auto& gs = groups->elements();
    for (std::size_t g = 0; g < gs.size(); ++g) {
        rows.push_back(icf::NodePath{g, std::nullopt});
        if (g < expanded.size() && expanded[g] == 0) continue;
        const icf::json::Value* layers = gs[g].find("layers");
        if (!layers || layers->kind() != icf::json::Value::Kind::Array) continue;
        for (std::size_t l = 0; l < layers->elements().size(); ++l)
            rows.push_back(icf::NodePath{g, l});
    }
    return rows;
}

LayersStats drawLayers(Session& s) {
    LayersStats st;
    if (&s != g_owner) {
        // Another document. None of the state below is about it.
        g_owner = &s;
        g_renaming.reset();
        g_expanded.clear();
        g_lastSelection.reset();
        g_selfSelected = false;
    }
    if (!ImGui::Begin(kLayersWindow)) {
        ImGui::End();
        return st;
    }
    Pending pending;
    g_renameClosed = false;

    // A selection that arrived from elsewhere -- the canvas, an undo, the
    // inspector -- should be on screen. `[BIN]` rowForItem: exists for exactly
    // this: the model names a node and the view finds the row.
    const bool scrollToSelection = !g_selfSelected && s.selection && g_lastSelection != s.selection;
    g_selfSelected = false;

    const std::size_t count = groupCount(s.root());
    if (g_expanded.size() < count) g_expanded.resize(count, 1);
    for (std::size_t g = 0; g < count; ++g) {
        ++st.groups;
        ++st.rows;
        const icf::NodePath gp{g, std::nullopt};
        const bool selectedHere = s.selection && *s.selection == gp;
        const RowResult r = drawRow(s, gp, st, pending);
        if (scrollToSelection && selectedHere) ImGui::SetScrollHereY(0.5f);
        if (r.clicked) {
            if (!selectedHere) st.selectionChanged = true;
            s.selection = gp;
            g_selfSelected = true;
            if (r.doubleClicked) beginRename(gp, nodeTitle(s, gp));
        }
        if (!isOpen(g)) {
            st.layers += layerCount(s.root(), g);   // counted, not drawn
            continue;
        }
        const std::size_t n = layerCount(s.root(), g);
        for (std::size_t l = 0; l < n; ++l) {
            ++st.layers;
            ++st.rows;
            const icf::NodePath lp{g, l};
            const bool selectedLayer = s.selection && *s.selection == lp;
            const RowResult lr = drawRow(s, lp, st, pending);
            if (scrollToSelection && selectedLayer) ImGui::SetScrollHereY(0.5f);
            if (lr.clicked) {
                if (!selectedLayer) st.selectionChanged = true;
                s.selection = lp;
                g_selfSelected = true;
                if (lr.doubleClicked) beginRename(lp, nodeTitle(s, lp));
            }
        }
    }

    // ---- the keyboard ------------------------------------------------------
    // `[BIN]` LayerList carries onDeleteCommand(perform:), onKeyPress(characters:
    // phases:action:) and keyboardShortcut -- a list you can work without the
    // mouse. It only listens while this window has the focus and no field is open.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !g_renaming && !g_renameClosed) {
        const std::vector<icf::NodePath> rows = visibleRows(s.root(), g_expanded);
        const bool ctrl = ImGui::GetIO().KeyCtrl;
        std::ptrdiff_t at = -1;
        if (s.selection) {
            const auto it = std::find(rows.begin(), rows.end(), *s.selection);
            if (it != rows.end()) at = it - rows.begin();
        }
        const auto step = [&](std::ptrdiff_t d) {
            if (rows.empty()) return;
            const std::ptrdiff_t next = at < 0 ? (d > 0 ? 0 : static_cast<std::ptrdiff_t>(rows.size()) - 1)
                                               : at + d;
            if (next < 0 || next >= static_cast<std::ptrdiff_t>(rows.size())) return;
            s.selection = rows[static_cast<std::size_t>(next)];
            st.selectionChanged = true;
        };
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
            if (ctrl && s.selection) pending = Pending{Act::MoveDown, *s.selection};
            else step(+1);
        } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
            if (ctrl && s.selection) pending = Pending{Act::MoveUp, *s.selection};
            else step(-1);
        } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true) && s.selection) {
            // Right opens a closed group, then walks into it -- the Finder rule.
            if (!s.selection->layer && !isOpen(*s.selection->group)) setOpen(*s.selection->group, true);
            else step(+1);
        } else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true) && s.selection) {
            if (s.selection->layer) {
                s.selection = icf::NodePath{s.selection->group, std::nullopt};
                st.selectionChanged = true;
            } else {
                setOpen(*s.selection->group, false);
            }
        } else if ((ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
                    ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) &&
                   s.selection) {
            pending = Pending{Act::Delete, *s.selection};
        } else if ((ImGui::IsKeyPressed(ImGuiKey_F2, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) &&
                   s.selection) {
            // F2 has no anchor in the target -- it is this platform's spelling of
            // the Return the target binds. Both are bound so neither habit loses.
            beginRename(*s.selection, nodeTitle(s, *s.selection));
        }
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
    // `[ONYX]` Widgets::IconButtonOpts carries `disabled` and the panels grey a
    // button that cannot act rather than letting it click into nothing -- which
    // is what this one did with no selection.
    const bool canDelete = s.selection && s.selection->group;
    ImGui::BeginDisabled(!canDelete);
    if (ImGui::Button("-") && canDelete) pending = Pending{Act::Delete, *s.selection};
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Deletes the selected group or layer");

    // A tree with nothing in it should say so rather than look broken.
    if (count == 0) ImGui::TextDisabled("No groups yet -- use + to add one.");

    ImGui::End();

    g_lastSelection = s.selection;

    // The tree is drawn and closed; only now may the structure move under it.
    if (pending.act != Act::None) g_renaming.reset();
    switch (pending.act) {
        case Act::None:
            break;
        case Act::MoveUp:
            runMoves(s, pending.path, -1, 1);
            break;
        case Act::MoveDown:
            runMoves(s, pending.path, +1, 1);
            break;
        case Act::ToFront:
            if (pending.path.group) {
                const std::size_t i = pending.path.layer ? *pending.path.layer : *pending.path.group;
                runMoves(s, pending.path, -1, i);
            }
            break;
        case Act::ToBack:
            if (pending.path.group) {
                const std::size_t i = pending.path.layer ? *pending.path.layer : *pending.path.group;
                const std::size_t n = pending.path.layer ? layerCount(s.root(), *pending.path.group)
                                                         : groupCount(s.root());
                if (n > 0) runMoves(s, pending.path, +1, n - 1 - i);
            }
            break;
        case Act::Drop: {
            const std::size_t from = pending.path.layer ? *pending.path.layer : *pending.path.group;
            const std::size_t n = pending.path.layer ? layerCount(s.root(), *pending.path.group)
                                                     : groupCount(s.root());
            const DropPlan plan = planDrop(from, pending.gap, n);
            if (plan.valid) {
                // The row a person dragged is the row they want to keep looking
                // at, whether or not it was the selected one when the drag began.
                const icf::NodePath landed = runMoves(s, pending.path, plan.delta, plan.steps);
                s.selection = landed;
                st.selectionChanged = true;
                g_selfSelected = true;
            }
            break;
        }
        case Act::Delete:
            // The disclosure vector is positional, so a removed group takes its
            // entry with it -- and only if the removal actually happened.
            if (s.removeNode(pending.path) && !pending.path.layer && pending.path.group &&
                *pending.path.group < g_expanded.size()) {
                g_expanded.erase(g_expanded.begin() + static_cast<std::ptrdiff_t>(*pending.path.group));
            }
            break;
        case Act::AddGroup:
            // `image-name` stays empty until the import round fills it, which is
            // what a group with no layers already is: drawn, and rendering nothing.
            if (auto i = s.addGroup("Group")) {
                setOpen(*i, true);
                s.selection = icf::NodePath{*i, std::nullopt};
                st.selectionChanged = true;
                g_selfSelected = true;
            }
            break;
        case Act::AddLayer:
            if (pending.path.group) {
                if (auto i = s.addLayer(*pending.path.group, "Layer", "")) {
                    setOpen(*pending.path.group, true);   // or the new layer lands out of sight
                    s.selection = icf::NodePath{*pending.path.group, *i};
                    st.selectionChanged = true;
                    g_selfSelected = true;
                }
            }
            break;
    }
    return st;
}

}  // namespace ick
