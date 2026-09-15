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
#include "Source/IconComposerKit/Session.h"

#include "imgui.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

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
