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
    // Onde a textura fica: `width` x `height` a partir de (`originX`,
    // `originY`) numa grade de `gridSize` (spec 2026-09-16, "O que o Kit
    // faz"). Zero = a textura e o canvas inteiro em `width`, que e o que ela
    // sempre foi antes desta frente.
    std::uint32_t gridSize = 0;
    std::int32_t originX = 0, originY = 0;
    // False: o ladrilho pedido nao coube (o teto de area ou o do aparelho) e
    // isto e o canvas inteiro na resolucao base, esticado.
    bool refined = true;
    bool pending = false;   // a newer render is on its way
    std::size_t drawn = 0, total = 0;
    std::vector<std::string> skipped, shapeGaps, notes;
    std::string error;

    // HOW LONG THE RENDER TOOK, AND HOW LONG THE ONE IN FLIGHT HAS BEEN GOING.
    //
    // This is the number whose absence made 2026-09-15 possible. Six fronts
    // landed in an afternoon and together took a 1024 px glass render from about
    // a second to eighty-seven; the editor showed an empty canvas and the
    // Diagnostics panel said `pending`, which is the same thing it says when a
    // render takes forty milliseconds. There was no failure to report -- nothing
    // was wrong except that the work does not finish inside a human's patience --
    // so the only place that could have said so is a clock, and there was none.
    //
    // `lastRenderSeconds` is negative until a render has completed, which is not
    // the same as zero and must not print as `0.00 s`.
    // `pendingSeconds` is wall time since the request was made, so it counts the
    // queue as well as the draw: it is what the person is actually waiting for.
    double lastRenderSeconds = -1.0;
    double pendingSeconds = 0.0;
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

// ─────────────────────────────────────────────────────────────────────────────
// THE CANVAS'S GEOMETRY, AS FUNCTIONS THAT DRAW NOTHING
//
// Everything the canvas has to get RIGHT -- where Fit puts the icon, where the
// wheel leaves the pixel under the cursor, how far a drag may go, what the
// clip rectangle is -- is arithmetic, and arithmetic can be checked. The
// headless selftest counts `imgui errors 0` and `textured yes` and would have
// reported both while the icon was painting over the toolbar, so it is not the
// thing that keeps these honest; Tests/test_kit_canvas.cpp is.
//
// The convention every one of these shares: a pan is the icon's TOP-LEFT CORNER
// in canvas-local pixels (x right, y down, origin at the canvas's top-left), and
// `sidePx` is the texture's own side in texels, so the icon on screen is
// `sidePx * zoom` wide. Zoom 1.0 therefore means one texel to one screen pixel,
// which is what the `1:1` button asks for.
// ─────────────────────────────────────────────────────────────────────────────
struct CanvasVec { float x = 0.0f, y = 0.0f; };

struct CanvasRect {
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    float width() const { return x1 - x0; }
    float height() const { return y1 - y0; }
    bool empty() const { return !(x1 > x0 && y1 > y0); }
    // Tolerant by a quarter pixel: these are floats that have been through a
    // multiply and a lerp, and a rect that pokes out by 1e-6 of a pixel is not
    // the bug this asks about.
    bool contains(const CanvasRect& in, float slack = 0.25f) const {
        return in.empty() || (in.x0 >= x0 - slack && in.y0 >= y0 - slack &&
                              in.x1 <= x1 + slack && in.y1 <= y1 + slack);
    }
};

// The same limits Onyx's ImageViewer uses, so the two viewers in this person's
// day behave alike: eight times out, sixteen times in.
inline constexpr float kCanvasZoomMin = 0.125f;
inline constexpr float kCanvasZoomMax = 16.0f;
// How far past the edge a drag may push the icon. Without it the icon can be
// dragged entirely out of the canvas and there is nothing left on screen to drag
// back; with it there is always this much of it still in view.
inline constexpr float kCanvasPanMargin = 80.0f;

float canvasClampZoom(float zoom);
// The largest zoom at which the whole icon still fits in the canvas.
float canvasFitZoom(float availW, float availH, float sidePx);
// The pan that puts the icon in the middle of the canvas at that zoom.
CanvasVec canvasCentrePan(float availW, float availH, float sidePx, float zoom);
// Zoom about a point: the pan that keeps whatever is under `anchor` (canvas
// local) under `anchor` after the magnification changes. This is the whole of
// "the thing I pointed at did not run away when I scrolled".
CanvasVec canvasZoomAnchored(CanvasVec pan, float fromZoom, float toZoom, CanvasVec anchor);
// Pan bounded by `margin`: an icon smaller than the canvas may be nudged that
// far off centre, a larger one may be dragged until that much of it is left.
CanvasVec canvasClampPan(CanvasVec pan, float availW, float availH, float sidePx, float zoom,
                         float margin = kCanvasPanMargin);
CanvasRect canvasImageRect(CanvasVec pan, float sidePx, float zoom);
CanvasRect canvasIntersect(CanvasRect a, CanvasRect b);

// ─── O LADRILHO (spec 2026-09-16, "O que o Kit faz") ─────────────────────────
//
// Quanto tempo o pan e o zoom precisam ficar parados antes de um pedido sair.
// Curto de proposito: e o intervalo que separa "a pessoa parou de arrastar" de
// "a pessoa esta arrastando", e nao um atraso que ela deva sentir. Casa com o
// settle do ease (`canvasEase`, ~150 ms), entao um zoom com a roda acaba de
// assentar na tela quando o pedido sai.
inline constexpr float kTileSettleSeconds = 0.15f;
// A resolucao do ladrilho para um zoom: zero (a base) ate 100%, e a base
// ampliada acima disso. Com zoom <= 1 nada muda -- vale o caminho de sempre.
std::uint32_t canvasTileSize(std::uint32_t baseSize, float zoom);
// O retangulo pintado (`CanvasStats::painted`, em pixels de tela), levado ao
// espaco de um ladrilho de `canvasTileSize`. `imageTopLeft` e o canto da imagem
// na tela. Vazio (`w == 0`) quando nao ha nada pintado.
TileRect canvasTileFor(CanvasRect painted, CanvasVec imageTopLeft, std::uint32_t baseSize,
                       float zoom);
// The per-frame coefficient of the exponential ease, clamped against both a
// stalled frame and a 500 Hz one so the settle is ~150ms on any machine.
float canvasEase(float deltaSeconds);

struct CanvasStats {
    bool textured = false;
    std::size_t contextControls = 0;   // appearance, idiom, size, zoom
    std::size_t zoomControls = 0;      // -, +, 1:1, Fit
    MenuStats menu;

    // THE FRAME'S GEOMETRY, IN SCREEN COORDINATES, so a test can assert on what
    // the canvas actually did instead of re-deriving it. `clip` is the rectangle
    // the canvas pushed and nothing it draws escapes; `image` is where the icon
    // went, unclipped and possibly far larger than the canvas; `painted` is the
    // two intersected -- the pixels that can reach the screen.
    CanvasRect clip, image, painted;
    float zoom = 1.0f;   // the eased magnification this frame, not the target
    // O aviso de que estes pixels sao a base esticada esteve na barra neste
    // frame. Espelha `RenderView::refined`, que ate 18/09 era escrito pelo
    // coordenador e lido por ninguem.
    bool stretchedNotice = false;
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
