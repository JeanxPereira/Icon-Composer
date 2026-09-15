#pragma once
// The four panels and the menu, each a function from the Session to what it
// drew. The return values are MEASUREMENTS of the frame -- how many rows, how
// many sections, whether a texture was shown -- so the selftest can assert on a
// frame without a window.
//
// Window titles, and why one carries `##ic`: Onyx registers its own "Inspector"
// panel, and two ImGui windows with the same title are one window. The label a
// person reads stops at `##`.
#include "Source/IconComposerKit/Ports.h"
#include "Source/IconComposerKit/Session.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ick {

inline constexpr const char* kLayersWindow = "Layers";
inline constexpr const char* kCanvasWindow = "Canvas";
inline constexpr const char* kInspectorWindow = "Inspector##ic";
inline constexpr const char* kDiagnosticsWindow = "Diagnostics";

struct LayersStats {
    std::size_t groups = 0, layers = 0;
    bool selectionChanged = false;
    std::size_t rows = 0;       // rows actually on screen: a closed group hides its layers
    std::size_t problems = 0;   // of those, rows drawn with a diagnostic (no art, or art gone)
};
LayersStats drawLayers(Session& s);

// THE TWO PIECES OF THE LAYER PANEL THAT ARE ARITHMETIC, NOT DRAWING
// -----------------------------------------------------------------------------
// The selftest counts `imgui errors` and cannot see a gesture, so the gesture's
// logic is lifted out of the frame and asserted on its own (Tests/test_kit_layers.cpp).

// What a drop between two rows costs in `Session::moveNode` calls.
//
// `moveNode` SWAPS two siblings one position apart (Edit.h), so it is the only
// move the model has, and a drag of five positions has to become five of them --
// but exactly five, and never six. `gap` is the INSERTION SLOT the row was
// dropped at, counted like an iterator: 0 is above the first sibling, `count` is
// below the last, so slot `g` sits between siblings `g-1` and `g`.
struct DropPlan {
    bool valid = false;         // false: the drop changes nothing, or is out of range
    int delta = 0;              // -1 (up) or +1 (down), applied `steps` times
    std::size_t steps = 0;      // how many single-position swaps
    std::size_t to = 0;         // the index the dragged sibling ends at
};
DropPlan planDrop(std::size_t from, std::size_t gap, std::size_t count);

// The tree flattened to what a person can see and therefore walk with the arrow
// keys: every group, and the layers of the OPEN ones, top to bottom. `expanded`
// is indexed by group; a group past its end counts as open, which is how a group
// that was just added arrives.
std::vector<icf::NodePath> visibleRows(const icf::json::Value& root,
                                       const std::vector<unsigned char>& expanded);

// The last render the canvas has to show, and what it did not draw.
struct RenderView {
    // `ImTextureID_Invalid`, never a literal 0: ImGui 1.92 is mid-migration to
    // `ImTextureRef`, and Onyx's own TexturePool already spells it this way.
    ImTextureID texture = ImTextureID_Invalid;
    std::uint32_t width = 0, height = 0;
    bool pending = false;   // a newer render is on its way
    std::size_t drawn = 0, total = 0;
    std::vector<std::string> skipped, shapeGaps, notes;
    std::string error;
};

// What the menu asked the app to do this frame. The Kit cannot open a file
// dialog or quit; the app reads these after the frame and does it.
struct MenuActions {
    bool newDocument = false, open = false, save = false, saveAs = false, close = false, quit = false;
};

struct MenuStats {
    std::size_t menus = 0, items = 0, disabled = 0;
};
// Draws inside the current window's menu bar: the caller opened the window with
// ImGuiWindowFlags_MenuBar (the canvas does, like sfsymview's "Symbols").
MenuStats drawMenuBar(Session& s, MenuActions& actions);

struct CanvasStats {
    bool textured = false;
    std::size_t contextControls = 0;   // appearance, idiom, size, zoom
    MenuStats menu;
};
CanvasStats drawCanvas(Session& s, const RenderView& view, MenuActions& actions);

struct InspectorStats {
    std::string title;
    std::size_t sections = 0;    // enabled sections drawn
    std::size_t inherited = 0;   // of those, marked as inherited under the scope
    std::size_t disabled = 0;    // sections drawn greyed, with the reason
};
InspectorStats drawInspector(Session& s);

struct DiagnosticsStats {
    std::size_t rows = 0;
};
DiagnosticsStats drawDiagnostics(const Session& s, const RenderView& view);

}  // namespace ick
