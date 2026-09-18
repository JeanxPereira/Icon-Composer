// THE CANVAS: DOES IT STAY INSIDE ITSELF, AND DOES IT NAVIGATE?
//
// WHY THIS FILE EXISTS AT ALL
// -----------------------------------------------------------------------------
// The headless selftest reports `imgui errors 0` and `textured yes`, and it
// reported both, every run, while the canvas was painting the icon straight
// over the row of combos above it. It could not have noticed: nothing it counts
// changes when a draw command lands on the wrong pixels. Taking that green as
// evidence of a usable canvas is the mistake that produced the state the user
// reprovou, so this file measures the two things he actually reported.
//
// It measures them in the only way that is honest without a person at the
// screen: the arithmetic of the view transform is pure and therefore checkable,
// and the panel now REPORTS the rectangles it used (`CanvasStats::clip`,
// `image`, `painted`), so a case can drive the real `drawCanvas` and read back
// where the icon went rather than re-deriving it and agreeing with itself.
//
// WHAT IT CANNOT MEASURE, said here rather than left to be assumed: whether
// 1.15 per wheel notch is the right rate, whether a ~150ms ease feels smooth or
// sluggish, whether 80px is the right leash, and whether left-drag-to-pan is
// the right chord. Those are judgements about feel and only a person makes
// them.
//
// Behind `if(TARGET IconComposerKit)` in Tests/CMakeLists.txt, like
// test_kit_idiom.cpp: `-DIC_BUILD_UI=OFF` has no Kit to link and must stay
// green.
#include "check.h"
#include "Source/IconComposerKit/Headless.h"
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/RenderCoordinator.h"
#include "Source/IconComposerKit/Session.h"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

const char* kDoc = R"({
  "supported-platforms" : { "squares" : "shared" },
  "fill" : "automatic",
  "groups" : [ { "name" : "G", "layers" : [ { "name" : "art", "image-name" : "art.svg" } ] } ] })";

fs::path makeBundle(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("ic-canvas-" + name + ".icon");
    fs::remove_all(dir);
    fs::create_directories(dir / "Assets");
    std::ofstream(dir / "icon.json", std::ios::binary) << kDoc;
    std::ofstream(dir / "Assets" / "art.svg", std::ios::binary) << "<svg/>";
    return dir;
}

// A render that has already landed. No device: `AddImage` only records the id in
// a draw command, and nothing headless ever samples it. NOT 1 -- Headless.cpp:27
// gives the font atlas that id, and the whole point of the id here is that the
// icon's own draw command can be picked out of the finished draw data.
constexpr ImTextureID kIconTex = static_cast<ImTextureID>(4242);

ick::RenderView landed(std::uint32_t side) {
    ick::RenderView v;
    v.texture = kIconTex;
    v.width = side;
    v.height = side;
    // The whole canvas, at `side`: the grid IS the texture and the origin is
    // the corner. Every case that uses this one also leaves `view.size` at its
    // default 512, which is the ruler since the viewport render (spec
    // 2026-09-16) -- so `landed(512)` is still "the render this canvas asked
    // for has arrived", exactly as it was before the ruler moved.
    v.gridSize = side;
    v.drawn = 1;
    v.total = 1;
    return v;
}

// A TILE that has already landed: `w` x `h` texels at (`ox`, `oy`) of a grid
// of `grid`. This is what a render above 100% comes back as.
ick::RenderView landedTile(std::uint32_t grid, std::int32_t ox, std::int32_t oy, std::uint32_t w,
                           std::uint32_t h) {
    ick::RenderView v;
    v.texture = kIconTex;
    v.width = w;
    v.height = h;
    v.gridSize = grid;
    v.originX = ox;
    v.originY = oy;
    v.drawn = 1;
    v.total = 1;
    return v;
}

// THE SCISSOR THE BACKEND WOULD ACTUALLY SET FOR THE ICON, read out of ImGui's
// finished draw data. This is the part of the clip claim that is not a
// restatement of the panel's own intent: `CanvasStats::painted` is the
// intersection the panel computed, and a test that only checked the panel's
// arithmetic against itself would pass on a panel that never pushed a clip at
// all. `ImDrawCmd::ClipRect` is what the renderer scissors with, so a rectangle
// that ends below the toolbar is a toolbar the icon cannot reach.
//
// Valid between `ImGui::Render()` and the next `NewFrame()`, which is exactly
// where `canvasFrame` leaves us.
bool iconScissor(ImVec4& out) {
    ImDrawData* d = ImGui::GetDrawData();
    if (!d) return false;
    for (int i = 0; i < d->CmdListsCount; ++i) {
        const ImDrawList* list = d->CmdLists[i];
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback) continue;
            if (cmd.ElemCount == 0) continue;
            if (cmd.GetTexID() == kIconTex) {
                out = cmd.ClipRect;
                return true;
            }
        }
    }
    return false;
}

// WHERE THE ICON'S QUAD ACTUALLY LANDED, in screen pixels, read out of the
// vertices ImGui emitted rather than out of the panel's own arithmetic. Since
// the viewport render (spec 2026-09-16) the texture may be a tile, and "the
// tile went in the rectangle that is its own" is a claim about these four
// numbers and nothing else. `AddImage` emits one quad and does not clip its
// geometry -- the clip is the scissor `iconScissor` reads -- so the bounding
// box of the command's vertices IS the rectangle the image was placed in.
bool iconQuad(ImVec4& out) {
    ImDrawData* d = ImGui::GetDrawData();
    if (!d) return false;
    for (int i = 0; i < d->CmdListsCount; ++i) {
        const ImDrawList* list = d->CmdLists[i];
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback) continue;
            if (cmd.ElemCount == 0) continue;
            if (cmd.GetTexID() != kIconTex) continue;
            ImVec4 box(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
            for (unsigned int n = 0; n < cmd.ElemCount; ++n) {
                const ImDrawVert& v =
                    list->VtxBuffer[cmd.VtxOffset + list->IdxBuffer[cmd.IdxOffset + n]];
                box.x = std::min(box.x, v.pos.x);
                box.y = std::min(box.y, v.pos.y);
                box.z = std::max(box.z, v.pos.x);
                box.w = std::max(box.w, v.pos.y);
            }
            out = box;
            return true;
        }
    }
    return false;
}

// One frame of the canvas alone, in a window of a known size -- `drawCanvas`
// calls `Begin` without sizing it, exactly as the window does.
ick::CanvasStats canvasFrame(ick::HeadlessImGui& gui, ick::Session& s, const ick::RenderView& view,
                             float w = 840.0f, float h = 700.0f) {
    ick::MenuActions actions;
    gui.newFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ick::CanvasStats st = ick::drawCanvas(s, view, actions);
    gui.render();
    return st;
}

// Where the anchor sits INSIDE the icon, measured in the icon's own texels. This
// is the quantity a cursor-anchored zoom has to leave alone: it is "the pixel I
// am pointing at".
ick::CanvasVec texelUnder(ick::CanvasVec anchor, ick::CanvasVec pan, float zoom) {
    return ick::CanvasVec{(anchor.x - pan.x) / zoom, (anchor.y - pan.y) / zoom};
}

bool near(float a, float b, float slack = 0.01f) { return std::fabs(a - b) <= slack; }

// THE SCHEDULER AND THE SINK, for the one case that closes the loop
// canvas -> coordinator -> canvas. Neither renders nor uploads anything: the
// request is recorded and the answer is handed back verbatim, which is all the
// question "did the canvas ask for the right tile, and did it put the answer
// where the answer says it is?" needs. The sink hands out `kIconTex` so
// `iconQuad` can pick the icon's own draw command out of the frame.
struct TileRecorder : ick::RenderScheduler {
    struct Ask {
        std::uint32_t size = 0;
        ick::TileRect tile;
        std::uint32_t fallbackSize = 0;
    };
    std::vector<Ask> asks;
    std::optional<ick::RenderResult> ready;

    void request(ick::RenderRequest r) override { asks.push_back({r.size, r.tile, r.fallbackSize}); }
    std::optional<ick::RenderResult> poll() override {
        std::optional<ick::RenderResult> r = std::move(ready);
        ready.reset();
        return r;
    }
    // The pixels ARE the tile that was asked for, on the grid it was asked on.
    void answerTile(std::uint64_t version, icf::Context ctx, const Ask& ask) {
        ick::RenderResult res;
        res.version = version;
        res.context = ctx;
        res.size = ask.size;   // the echo: what was ASKED (Ports.h)
        res.tile = ask.tile;
        res.gridSize = ask.size;
        const bool whole = ask.tile.w == 0;
        res.originX = whole ? 0 : ask.tile.x;
        res.originY = whole ? 0 : ask.tile.y;
        res.width = whole ? ask.size : ask.tile.w;
        res.height = whole ? ask.size : ask.tile.h;
        res.rgba8.assign(static_cast<std::size_t>(res.width) * res.height * 4, 255);
        res.drawn = 1;
        res.total = 1;
        ready = std::move(res);
    }
};

struct TexSink : ick::TextureSink {
    int live = 0;
    ImTextureID create(std::uint32_t, std::uint32_t, const std::uint8_t*) override {
        ++live;
        return kIconTex;
    }
    bool update(ImTextureID, std::uint32_t, std::uint32_t, const std::uint8_t*) override { return true; }
    void remove(ImTextureID) override { --live; }
};

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// 1. THE POINT UNDER THE CURSOR DOES NOT MOVE
//
// The user's words were that the zoom "não funciona igual o onyx disponibiliza".
// The difference is this property and nothing else: Onyx's wheel keeps the texel
// under the pointer under the pointer, and the old canvas had no wheel at all.
// Swept over both directions, several anchors and several starting pans, because
// one anchor at the origin would pass even for an unanchored zoom.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(canvas_anchored_zoom_keeps_the_texel_under_the_cursor) {
    const float zooms[] = {0.2f, 0.5f, 1.0f, 2.5f, 9.0f};
    const float factors[] = {1.15f, 1.0f / 1.15f, 2.0f, 0.25f};
    const ick::CanvasVec pans[] = {{0.0f, 0.0f}, {-317.0f, 44.0f}, {120.5f, -980.25f}};
    const ick::CanvasVec anchors[] = {{0.0f, 0.0f}, {420.0f, 350.0f}, {839.0f, 12.0f}};

    int checked = 0;
    for (float z : zooms) {
        for (float f : factors) {
            const float target = ick::canvasClampZoom(z * f);
            for (ick::CanvasVec pan : pans) {
                for (ick::CanvasVec a : anchors) {
                    const ick::CanvasVec before = texelUnder(a, pan, z);
                    const ick::CanvasVec moved = ick::canvasZoomAnchored(pan, z, target, a);
                    const ick::CanvasVec after = texelUnder(a, moved, target);
                    CHECK(near(before.x, after.x, 0.02f));
                    CHECK(near(before.y, after.y, 0.02f));
                    ++checked;
                }
            }
        }
    }
    CHECK_EQ(checked, 180);

    // And the clamp is a real fence, not a suggestion: past the ends the zoom
    // stops, and the anchor is still honoured at the zoom it stopped at.
    CHECK_EQ(ick::canvasClampZoom(1000.0f), ick::kCanvasZoomMax);
    CHECK_EQ(ick::canvasClampZoom(0.0001f), ick::kCanvasZoomMin);
    CHECK_EQ(ick::canvasClampZoom(-3.0f), ick::kCanvasZoomMin);
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. FIT FITS EXACTLY -- it touches the short axis and never overflows either.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(canvas_fit_fits_exactly_and_centres) {
    struct Case { float w, h, side; };
    const Case cases[] = {
        {840.0f, 640.0f, 512.0f},   // wider than tall: height is the binding axis
        {400.0f, 900.0f, 512.0f},   // taller than wide: width binds
        {600.0f, 600.0f, 1024.0f},  // square, icon larger than the canvas
        {2000.0f, 1500.0f, 512.0f}, // square, icon smaller: Fit magnifies
    };
    for (const Case& c : cases) {
        const float z = ick::canvasFitZoom(c.w, c.h, c.side);
        const ick::CanvasVec pan = ick::canvasCentrePan(c.w, c.h, c.side, z);
        const ick::CanvasRect img = ick::canvasImageRect(pan, c.side, z);
        const ick::CanvasRect canvas{0.0f, 0.0f, c.w, c.h};

        CHECK(canvas.contains(img));
        // Exactly: the icon's side equals the canvas's shorter side.
        CHECK(near(c.side * z, std::min(c.w, c.h), 0.02f));
        // Centred on both axes.
        CHECK(near((img.x0 + img.x1) * 0.5f, c.w * 0.5f));
        CHECK(near((img.y0 + img.y1) * 0.5f, c.h * 0.5f));
    }

    // A canvas nobody has laid out yet, and a render that has not landed: Fit
    // must still answer something usable rather than a zero or a NaN.
    CHECK_EQ(ick::canvasFitZoom(0.0f, 0.0f, 512.0f), 1.0f);
    CHECK_EQ(ick::canvasFitZoom(840.0f, 700.0f, 0.0f), 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. THE PAN CANNOT THROW THE ICON AWAY
//
// Without the clamp there is a drag that leaves an empty canvas and nothing on
// screen to drag back. The property: after clamping, the icon and the canvas
// always overlap by at least `min(canvas, icon) - margin` on each axis.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(canvas_clamped_pan_always_leaves_the_icon_in_view) {
    const float w = 840.0f, h = 700.0f;
    const float sides[] = {512.0f, 1024.0f};
    const float zooms[] = {0.125f, 0.5f, 1.0f, 1.37f, 4.0f, 16.0f};
    const float wild[] = {-100000.0f, -3000.0f, -400.0f, 0.0f, 250.0f, 5000.0f, 100000.0f};

    int checked = 0;
    for (float side : sides) {
        for (float z : zooms) {
            const float drawn = side * z;
            for (float px : wild) {
                for (float py : wild) {
                    const ick::CanvasVec p =
                        ick::canvasClampPan(ick::CanvasVec{px, py}, w, h, side, z);
                    const ick::CanvasRect img = ick::canvasImageRect(p, side, z);
                    const ick::CanvasRect seen =
                        ick::canvasIntersect(img, ick::CanvasRect{0.0f, 0.0f, w, h});
                    CHECK(!seen.empty());
                    CHECK(seen.width() >= std::min(w, drawn) - ick::kCanvasPanMargin - 0.01f);
                    CHECK(seen.height() >= std::min(h, drawn) - ick::kCanvasPanMargin - 0.01f);
                    ++checked;
                }
            }
        }
    }
    CHECK_EQ(checked, 588);

    // A pan already legal is left alone -- the clamp must not creep.
    const ick::CanvasVec centred = ick::canvasCentrePan(w, h, 512.0f, 1.0f);
    const ick::CanvasVec again = ick::canvasClampPan(centred, w, h, 512.0f, 1.0f);
    CHECK(near(centred.x, again.x));
    CHECK(near(centred.y, again.y));
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. THE RECORTE, ON A REAL FRAME
//
// The defect, concretely: the body filled its rectangle and then called
// `AddImage` into the same draw list with no clip. A window's draw list is
// clipped to the window's INNER rect, which starts under the menu bar and so
// includes the row of combos -- an icon whose top-left had gone above the
// canvas painted over them, and that is "o viewport atravessa os botões".
//
// The case drives the real `drawCanvas` with an icon forced far larger than the
// canvas and far above it, and asserts BOTH halves: that the icon genuinely
// does want to draw outside (or the case would pass on any canvas at all), and
// that what can reach the screen is inside the canvas's own rectangle.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(canvas_never_paints_outside_the_canvas) {
    auto s = ick::Session::open(makeBundle("clip"));
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui;
    const ick::RenderView view = landed(512);

    // Frame one lays it out, so `clip` below is the canvas the window gave us.
    const ick::CanvasStats fitted = canvasFrame(gui, *s, view);
    REQUIRE(fitted.textured);
    REQUIRE(!fitted.clip.empty());

    // Now the user's situation: he raised the scale of the icon. Set the view
    // where a few wheel notches would put it, past the end of the ease so the
    // frame draws it rather than eases toward it.
    s->view.zoom = s->view.zoomTarget = 8.0f;
    s->view.panX = s->view.panTargetX = -1800.0f;
    s->view.panY = s->view.panTargetY = -1500.0f;

    const ick::CanvasStats st = canvasFrame(gui, *s, view);
    REQUIRE(st.textured);

    // The icon really is trying to leave -- above and to the left of the canvas,
    // which is where the combos are.
    CHECK(st.image.y0 < st.clip.y0);
    CHECK(st.image.x0 < st.clip.x0);
    CHECK(st.image.x1 > st.clip.x1);
    CHECK(st.image.y1 > st.clip.y1);
    CHECK(!st.clip.contains(st.image));

    // And what can reach the screen does not.
    CHECK(st.clip.contains(st.painted));
    CHECK(st.painted.y0 >= st.clip.y0 - 0.25f);
    CHECK(st.painted.x0 >= st.clip.x0 - 0.25f);
    CHECK(!st.painted.empty());

    // THE SAME CLAIM, MADE BY IMGUI RATHER THAN BY THE PANEL. The icon's own
    // draw command carries the scissor the backend will set, and it stops at the
    // canvas -- not at the window's inner rect, which is where it stopped before
    // and which includes the toolbar.
    ImVec4 scissor;
    REQUIRE(iconScissor(scissor));
    CHECK(scissor.y >= st.clip.y0 - 0.25f);
    CHECK(scissor.x >= st.clip.x0 - 0.25f);
    CHECK(scissor.z <= st.clip.x1 + 0.25f);
    CHECK(scissor.w <= st.clip.y1 + 0.25f);

    // The canvas begins BELOW the controls it drew: the top of the clip is under
    // the menu bar and the combo row, so "inside the clip" is exactly "not over
    // the buttons". The toolbar sits in [0, clip.y0) and the scissor starts at
    // clip.y0, so there is no pixel of the icon in it.
    CHECK(st.clip.y0 > 0.0f);
    CHECK(scissor.y >= st.clip.y0 - 0.25f);
    CHECK_EQ(st.contextControls, std::size_t(4));
    CHECK_EQ(st.zoomControls, std::size_t(4));
    CHECK_EQ(gui.errors(), std::uint64_t(0));
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. THE CANVAS OPENS FITTED, AND THE NEXT DOCUMENT DOES TOO
//
// The second half is the regression that `static ImVec2 pan` guaranteed: a
// file-static is one state shared by every document the process opens, so a
// document dragged into a corner left the NEXT one in that corner. The pan is
// now a field of `Session::view`, so a second Session cannot inherit it -- and
// this case is what says so out loud.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(canvas_opens_fitted_and_does_not_inherit_the_previous_pan) {
    ick::HeadlessImGui gui;
    const ick::RenderView view = landed(512);

    auto first = ick::Session::open(makeBundle("first"));
    REQUIRE(first.has_value());
    const ick::CanvasStats a = canvasFrame(gui, *first, view);
    REQUIRE(a.textured);
    CHECK(a.clip.contains(a.image));
    CHECK(near((a.image.x0 + a.image.x1) * 0.5f, (a.clip.x0 + a.clip.x1) * 0.5f, 0.5f));
    CHECK(near((a.image.y0 + a.image.y1) * 0.5f, (a.clip.y0 + a.clip.y1) * 0.5f, 0.5f));
    CHECK(first->view.fitted);

    // Drag it into the corner and zoom right in, the way somebody working does.
    first->view.zoom = first->view.zoomTarget = 6.0f;
    first->view.panX = first->view.panTargetX = -900.0f;
    first->view.panY = first->view.panTargetY = -650.0f;
    const ick::CanvasStats dragged = canvasFrame(gui, *first, view);
    CHECK(dragged.image.x0 < dragged.clip.x0);

    // Open another document. It opens fitted and centred, not where the last one
    // was left.
    auto second = ick::Session::open(makeBundle("second"));
    REQUIRE(second.has_value());
    CHECK(!second->view.fitted);
    CHECK_EQ(second->view.panX, 0.0f);
    const ick::CanvasStats b = canvasFrame(gui, *second, view);
    REQUIRE(b.textured);
    CHECK(b.clip.contains(b.image));
    CHECK(near((b.image.x0 + b.image.x1) * 0.5f, (b.clip.x0 + b.clip.x1) * 0.5f, 0.5f));
    CHECK(near((b.image.y0 + b.image.y1) * 0.5f, (b.clip.y0 + b.clip.y1) * 0.5f, 0.5f));
    CHECK_EQ(gui.errors(), std::uint64_t(0));
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. ONE NUMBER, NOT TWO
//
// The zoom combo, the View>Zoom menu and the four buttons all write
// `view.zoomRequest`, and the canvas turns that into the same `zoomTarget` the
// wheel moves and anchors it the same way. The combo's tick and its label read
// that target back, so a wheel zoom to 137% cannot leave the combo saying 100%.
// This case drives the request through the real panel and reads the target.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(canvas_zoom_request_and_wheel_share_one_number) {
    auto s = ick::Session::open(makeBundle("zoom"));
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui;
    const ick::RenderView view = landed(512);

    canvasFrame(gui, *s, view);   // Fit

    // The combo asks for 200%.
    s->view.zoomRequest = 2.0f;
    const ick::CanvasStats at2 = canvasFrame(gui, *s, view);
    CHECK_EQ(s->view.zoomTarget, 2.0f);
    CHECK_EQ(s->view.zoomRequest, 0.0f);   // consumed, not left to re-fire
    // Anchored on the viewport centre: a centred icon stays centred.
    CHECK(near((at2.image.x0 + at2.image.x1) * 0.5f, (at2.clip.x0 + at2.clip.x1) * 0.5f, 0.5f));
    CHECK(near((at2.image.y0 + at2.image.y1) * 0.5f, (at2.clip.y0 + at2.clip.y1) * 0.5f, 0.5f));

    // The ease is on its way there and gets there: `zoom` closes on
    // `zoomTarget` and then stops moving.
    CHECK(s->view.zoom < s->view.zoomTarget);
    for (int i = 0; i < 400; ++i) canvasFrame(gui, *s, view);
    CHECK_EQ(s->view.zoom, s->view.zoomTarget);

    // A request past the fence is clamped, and the clamped value is what the
    // combo will read back -- the control cannot claim a zoom the canvas is not
    // at.
    s->view.zoomRequest = 500.0f;
    canvasFrame(gui, *s, view);
    CHECK_EQ(s->view.zoomTarget, ick::kCanvasZoomMax);

    // Fit is the same single path: a request, consumed, ending inside the canvas.
    s->view.fitRequest = true;
    const ick::CanvasStats refit = canvasFrame(gui, *s, view);
    CHECK(!s->view.fitRequest);
    for (int i = 0; i < 400; ++i) canvasFrame(gui, *s, view);
    const ick::CanvasStats settled = canvasFrame(gui, *s, view);
    CHECK(settled.clip.contains(settled.image));
    CHECK(refit.clip.contains(refit.painted));
    CHECK_EQ(gui.errors(), std::uint64_t(0));
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. THE EASE IS BOUNDED AT BOTH ENDS
//
// A frame that reports dt 0 (the first one, a stalled one) would freeze the
// ease at zero and the canvas would never move again; a hitch of half a second
// would teleport past the smoothing that is the point of having it.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(canvas_ease_moves_on_every_frame_and_never_teleports) {
    for (float dt : {0.0f, 1.0f / 500.0f, 1.0f / 60.0f, 0.5f, 10.0f}) {
        const float k = ick::canvasEase(dt);
        CHECK(k > 0.0f);
        CHECK(k < 1.0f);
    }
    // Monotone in between, and roughly a 150ms settle at 60Hz: nine frames get
    // more than nine tenths of the way.
    CHECK(ick::canvasEase(1.0f / 120.0f) < ick::canvasEase(1.0f / 60.0f));
    float x = 0.0f;
    for (int i = 0; i < 9; ++i) x += (1.0f - x) * ick::canvasEase(1.0f / 60.0f);
    CHECK(x > 0.9f);
    CHECK(x < 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// 8. THE TILE, AS ARITHMETIC (spec 2026-09-16, "O que o Kit faz")
//
// Two functions and two claims. The resolution: zero up to 100%, because with
// zoom <= 1 nothing changes and the canvas asks what it always asked; the base
// magnified above it. The rectangle: everything the canvas can paint is inside
// it -- a tile that came up short would leave a strip of panel background along
// an edge, which is the one failure a person would see immediately.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(canvas_tile_resolution_and_rectangle) {
    // Zoom <= 1 is the path of always, and so is a base of zero.
    CHECK_EQ(ick::canvasTileSize(512, 1.0f), 0u);
    CHECK_EQ(ick::canvasTileSize(512, 0.25f), 0u);
    CHECK_EQ(ick::canvasTileSize(512, 0.999f), 0u);
    CHECK_EQ(ick::canvasTileSize(0, 4.0f), 0u);
    CHECK_EQ(ick::canvasTileSize(512, std::nanf("")), 0u);
    // Above it, `round(size * zoom)`, which the renderer accepts at any value
    // (IconRenderer.cpp:589 -- the 512/1024 restriction is the combo's, not
    // the renderer's).
    CHECK_EQ(ick::canvasTileSize(512, 4.0f), 2048u);
    CHECK_EQ(ick::canvasTileSize(512, 1.37f), 701u);    // 701.44
    CHECK_EQ(ick::canvasTileSize(1024, 16.0f), 16384u);

    // Nothing painted, nothing to ask for.
    CHECK_EQ(ick::canvasTileFor(ick::CanvasRect{}, ick::CanvasVec{}, 512, 4.0f).w, 0u);
    // Painted, but below 100%: still the whole canvas at the base.
    CHECK_EQ(ick::canvasTileFor(ick::CanvasRect{0, 0, 100, 100}, ick::CanvasVec{}, 512, 1.0f).w, 0u);

    // THE COVER. A sweep of zooms and offsets: map the tile back to the screen
    // and it has to contain every pixel of `painted` that is inside the icon.
    const float zooms[] = {1.01f, 1.37f, 2.0f, 4.0f, 7.5f, 16.0f};
    const float pans[] = {-2000.0f, -333.25f, -0.5f, 0.0f, 37.0f};
    int checked = 0;
    for (float z : zooms) {
        for (float px : pans) {
            for (float py : pans) {
                const ick::CanvasVec tl{px, py};
                const ick::CanvasRect clip{0.0f, 0.0f, 840.0f, 700.0f};
                const ick::CanvasRect img = ick::canvasImageRect(tl, 512.0f, z);
                const ick::CanvasRect painted = ick::canvasIntersect(img, clip);
                const ick::TileRect t = ick::canvasTileFor(painted, tl, 512, z);
                // A pan of -2000 at 101% puts the whole icon off the canvas:
                // nothing painted, nothing to ask for, and the coordinator
                // falls back to the whole canvas at the base rather than
                // asking for a tile of nothing.
                if (painted.empty()) {
                    CHECK_EQ(t.w, 0u);
                    ++checked;
                    continue;
                }
                REQUIRE(t.w > 0);

                const std::uint32_t grid = ick::canvasTileSize(512, z);
                // Inside the grid, on both axes: a tile that pokes out is a
                // buffer the renderer cannot place.
                CHECK(t.x >= 0);
                CHECK(t.y >= 0);
                CHECK(static_cast<std::uint32_t>(t.x) + t.w <= grid);
                CHECK(static_cast<std::uint32_t>(t.y) + t.h <= grid);

                // Back to the screen, at the scale the panel draws it.
                const float k = (512.0f * z) / static_cast<float>(grid);
                const ick::CanvasRect on{tl.x + t.x * k, tl.y + t.y * k,
                                         tl.x + (t.x + static_cast<float>(t.w)) * k,
                                         tl.y + (t.y + static_cast<float>(t.h)) * k};
                CHECK(on.contains(painted, 0.5f));
                ++checked;
            }
        }
    }
    CHECK_EQ(checked, 150);
}

// ─────────────────────────────────────────────────────────────────────────────
// 9. NO REQUEST PER FRAME (spec 2026-09-16, "O que o Kit faz")
//
// The third of the three things the spec says the first version left implicit.
// During a drag the key would move every frame, the scheduler would spend the
// whole gesture discarding work, and the interval with no sharp picture would
// be the drag itself. So `view.tile` is written only after the pan and the zoom
// have been still for `kTileSettleSeconds`, and this case drives real frames
// and watches the field: it must not move while the pan does.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(canvas_writes_the_tile_only_after_the_pan_settles) {
    auto s = ick::Session::open(makeBundle("settle"));
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui;
    const ick::RenderView view = landed(512);

    canvasFrame(gui, *s, view);   // Fit
    // Where a few wheel notches leave it, past the end of the ease so the
    // frames below are still ones.
    s->view.zoom = s->view.zoomTarget = 4.0f;
    s->view.panX = s->view.panTargetX = -100.0f;
    s->view.panY = s->view.panTargetY = -100.0f;
    s->view.settledSeconds = 0.0f;

    // Eight frames at 60Hz is 133ms: not yet.
    for (int i = 0; i < 8; ++i) canvasFrame(gui, *s, view);
    CHECK_EQ(s->view.tileSize, 0u);
    CHECK_EQ(s->view.tile.w, 0u);

    // The ninth crosses 150ms, and now there is a tile -- at the resolution the
    // zoom asks for, and offset by the pan.
    const ick::CanvasStats at4 = canvasFrame(gui, *s, view);
    CHECK_EQ(s->view.tileSize, 2048u);
    CHECK(s->view.tile.w > 0u);
    // The scale is exactly 1 here (2048 texels over 512*4 screen pixels), so
    // the tile's corner is the pan, to the pixel.
    CHECK_EQ(s->view.tile.x, 100);
    CHECK_EQ(s->view.tile.y, 100);
    CHECK_EQ(s->view.tile.w, static_cast<std::uint32_t>(std::ceil(at4.painted.width())));
    CHECK_EQ(s->view.tile.h, static_cast<std::uint32_t>(std::ceil(at4.painted.height())));

    // NOW THE DRAG. `v.panTargetX += io.MouseDelta.x` is what the panel does
    // with a held button, and writing the target is therefore the whole of the
    // gesture as far as this field is concerned. Twenty frames of it, which is
    // twice the settle: the tile does not move, and `settledSeconds` stays at
    // the floor, so nothing downstream can have been asked for.
    const ick::TileRect held = s->view.tile;
    for (int i = 0; i < 20; ++i) {
        s->view.panTargetX -= 7.0f;
        canvasFrame(gui, *s, view);
        CHECK_EQ(s->view.settledSeconds, 0.0f);
        CHECK(s->view.tile == held);
    }

    // Let go: the ease arrives, and a tile for where it stopped is written.
    for (int i = 0; i < 60; ++i) canvasFrame(gui, *s, view);
    CHECK(s->view.settledSeconds >= ick::kTileSettleSeconds);
    CHECK(!(s->view.tile == held));
    CHECK_EQ(s->view.tileSize, 2048u);

    // And back to 100%: the tile is cleared, so the coordinator asks the
    // question it always asked -- the whole canvas at the base.
    // (Enough frames for the clamp to pull the pan back to where an icon
    // smaller than the canvas is allowed to sit, and for the ease to arrive:
    // the tile is only rewritten once everything is still again.)
    s->view.zoom = s->view.zoomTarget = 1.0f;
    for (int i = 0; i < 60; ++i) canvasFrame(gui, *s, view);
    CHECK_EQ(s->view.tileSize, 0u);
    CHECK_EQ(s->view.tile.w, 0u);
    CHECK_EQ(gui.errors(), std::uint64_t(0));
}

// ─────────────────────────────────────────────────────────────────────────────
// 10. THE TEXTURE GOES IN THE RECTANGLE THAT IS ITS OWN
//
// The first of the three implicit things: the draw used `view.width * zoom` and
// assumed the texture was the whole canvas. Two claims, both read out of the
// vertices ImGui emitted rather than out of the panel's own numbers:
//
//   1. a whole-canvas render lands exactly where it landed before, so zoom <= 1
//      is untouched;
//   2. a tile lands at its own origin, at its own extent -- and the icon's
//      place on screen (`st.image`, which Fit, the clamp and the overlay all
//      measure with) does NOT move when a tile arrives. That is what the ruler
//      change is for: measuring the canvas by the texture would have moved the
//      icon every time a tile landed.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(canvas_places_a_tile_at_its_own_origin_and_does_not_move_the_icon) {
    auto s = ick::Session::open(makeBundle("place"));
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui;

    canvasFrame(gui, *s, landed(512));
    s->view.zoom = s->view.zoomTarget = 4.0f;
    s->view.panX = s->view.panTargetX = -100.0f;
    s->view.panY = s->view.panTargetY = -100.0f;

    // The whole canvas, stretched: one texel of the 512 grid over four screen
    // pixels, the corner of the image at the corner of the quad.
    const ick::CanvasStats whole = canvasFrame(gui, *s, landed(512));
    REQUIRE(whole.textured);
    ImVec4 q;
    REQUIRE(iconQuad(q));
    CHECK(near(q.x, whole.image.x0, 0.05f));
    CHECK(near(q.y, whole.image.y0, 0.05f));
    CHECK(near(q.z, whole.image.x1, 0.05f));
    CHECK(near(q.w, whole.image.y1, 0.05f));
    CHECK(near(whole.image.width(), 512.0f * 4.0f, 0.05f));

    // Now a tile of the 2048 grid: 700x600 texels at (100, 100). The scale is
    // 1, so it must land 100 screen pixels in from the image's corner and be
    // 700x600 on screen.
    const ick::CanvasStats tiled = canvasFrame(gui, *s, landedTile(2048, 100, 100, 700, 600));
    REQUIRE(tiled.textured);
    ImVec4 t;
    REQUIRE(iconQuad(t));
    CHECK(near(t.x, tiled.image.x0 + 100.0f, 0.05f));
    CHECK(near(t.y, tiled.image.y0 + 100.0f, 0.05f));
    CHECK(near(t.z - t.x, 700.0f, 0.05f));
    CHECK(near(t.w - t.y, 600.0f, 0.05f));

    // THE ICON DID NOT MOVE. Same pan, same zoom, a texture of a different
    // extent -- and the rectangle the canvas measures everything else with is
    // the one it was.
    CHECK(near(tiled.image.x0, whole.image.x0, 0.01f));
    CHECK(near(tiled.image.x1, whole.image.x1, 0.01f));
    CHECK(near(tiled.image.y0, whole.image.y0, 0.01f));
    CHECK(near(tiled.image.y1, whole.image.y1, 0.01f));

    // A render from before this frente -- `gridSize` still zero -- is read as
    // the whole canvas in `width`, which is what it was.
    ick::RenderView old = landed(512);
    old.gridSize = 0;
    const ick::CanvasStats legacy = canvasFrame(gui, *s, old);
    ImVec4 l;
    REQUIRE(iconQuad(l));
    CHECK(near(l.x, legacy.image.x0, 0.05f));
    CHECK(near(l.z - l.x, 512.0f * 4.0f, 0.05f));
    CHECK_EQ(gui.errors(), std::uint64_t(0));
}

// ─────────────────────────────────────────────────────────────────────────────
// 11. THE WHOLE LOOP, WITHOUT A WINDOW
//
// Canvas -> coordinator -> answer -> canvas, over real frames, which is as
// close as this suite gets to the wheel-and-drag pass a person does by hand.
// What it pins is the sequence the spec describes: the zoom settles and ONE
// tile is asked for; while the answer is missing the old texture stays on
// screen, anchored in the image so a drag carries it; the answer lands in its
// own rectangle; and 100% goes back to the whole canvas at the base.
//
// What it cannot pin, and what is left for a person at the keyboard: whether
// the wait is bearable on a heavy icon, and whether the panel background around
// a partial tile reads as "still working" rather than as damage.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(canvas_and_coordinator_ask_for_one_tile_and_place_the_answer) {
    auto s = ick::Session::open(makeBundle("loop"));
    REQUIRE(s.has_value());
    ick::HeadlessImGui gui;
    TileRecorder sched;
    TexSink sink;
    ick::RenderCoordinator coord(sched, sink);

    // The frame the window runs: the coordinator ticks first, because it may
    // replace the texture the canvas is about to show (Window.cpp).
    auto frame = [&] {
        coord.tick(*s);
        return canvasFrame(gui, *s, coord.view());
    };

    // Frame one: the whole canvas at the base, and the fallback is that same
    // base -- never zero, so the job always has somewhere to fall.
    frame();
    REQUIRE(sched.asks.size() == 1);
    CHECK_EQ(sched.asks[0].size, 512u);
    CHECK_EQ(sched.asks[0].tile.w, 0u);
    CHECK_EQ(sched.asks[0].fallbackSize, 512u);
    sched.answerTile(s->version(), s->view.context, sched.asks[0]);
    frame();
    CHECK(!coord.view().pending);
    CHECK_EQ(coord.view().gridSize, 512u);
    CHECK(coord.view().refined);

    // 100%, settled: still one ask. Zoom is not a render (spec 13/09 §6), and
    // below 100% it still is not.
    s->view.zoomRequest = 1.0f;
    for (int i = 0; i < 40; ++i) frame();
    CHECK_EQ(sched.asks.size(), std::size_t{1});

    // THE WHEEL, to 400%. The ease is on its way and nothing is asked for while
    // it moves -- that is the "no request per frame" rule seen from the other
    // side of the wire.
    s->view.zoomRequest = 4.0f;
    for (int i = 0; i < 8; ++i) {
        frame();
        CHECK_EQ(sched.asks.size(), std::size_t{1});
    }
    // It arrives, it settles, and exactly one tile is asked for.
    for (int i = 0; i < 60; ++i) frame();
    REQUIRE(sched.asks.size() == 2);
    const TileRecorder::Ask& ask = sched.asks[1];
    CHECK_EQ(ask.size, 2048u);
    CHECK(ask.tile.w > 0u);
    CHECK(ask.tile.h > 0u);
    CHECK_EQ(ask.fallbackSize, 512u);
    CHECK(coord.view().pending);

    // DURING THE WAIT the 512 that is on screen keeps being drawn, stretched
    // over the whole icon, and a drag carries it: the offset between the quad
    // and the image's corner does not change, because the texture is anchored
    // in the image and not in the canvas.
    const ick::CanvasStats waiting = canvasFrame(gui, *s, coord.view());
    ImVec4 w0;
    REQUIRE(iconQuad(w0));
    const float dx = w0.x - waiting.image.x0;
    for (int i = 0; i < 10; ++i) {
        s->view.panTargetX -= 9.0f;
        const ick::CanvasStats moved = frame();
        ImVec4 w1;
        REQUIRE(iconQuad(w1));
        CHECK(near(w1.x - moved.image.x0, dx, 0.05f));
    }
    CHECK_EQ(sched.asks.size(), std::size_t{2});   // the drag asked for nothing

    // THE TILE LANDS, in its own rectangle, on the grid it was asked on.
    sched.answerTile(s->version(), s->view.context, ask);
    frame();
    CHECK(!coord.view().pending);
    CHECK_EQ(coord.view().gridSize, 2048u);
    CHECK_EQ(coord.view().width, ask.tile.w);
    CHECK_EQ(coord.view().originX, ask.tile.x);
    CHECK_EQ(coord.view().originY, ask.tile.y);
    CHECK(coord.view().refined);
    const ick::CanvasStats placed = canvasFrame(gui, *s, coord.view());
    ImVec4 p;
    REQUIRE(iconQuad(p));
    CHECK(near(p.x, placed.image.x0 + static_cast<float>(ask.tile.x), 0.05f));
    CHECK(near(p.z - p.x, static_cast<float>(ask.tile.w), 0.05f));

    // BACK TO 100%: the whole canvas at the base again, which is the state the
    // editor had before this frente.
    s->view.zoomRequest = 1.0f;
    for (int i = 0; i < 80; ++i) frame();
    REQUIRE(sched.asks.size() == 3);
    CHECK_EQ(sched.asks[2].size, 512u);
    CHECK_EQ(sched.asks[2].tile.w, 0u);
    CHECK_EQ(s->view.tileSize, 0u);
    CHECK_EQ(gui.errors(), std::uint64_t(0));
}
