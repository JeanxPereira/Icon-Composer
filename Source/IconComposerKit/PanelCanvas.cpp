// The canvas and the diagnostics panel.
//
// THE CANVAS SHOWS THE LAST RENDER, AND SAYS WHEN IT IS STALE (spec 13/09 §6)
// ---------------------------------------------------------------------------
// The panel never asks for a render and never touches a device: it is handed a
// `RenderView` the coordinator filled in. While a newer render is on its way it
// keeps the previous texture up -- an editor that blanked the canvas on every
// keystroke would be unusable -- and marks the frame as provisional, in the
// context bar and with a dot on the canvas itself.
//
// That marking is not decoration. `appearance`, `idiom` and preview size live
// on `Session::view`, which is not behind a command and therefore does not move
// `version()`; the coordinator matches a result against the WHOLE key (version,
// context, width), so changing any of the three leaves `pending` true until the
// render that actually answers the new context lands. Those are exactly the
// frames where the pixels on screen belong to a context nobody asked for any
// more, and the only honest thing the canvas can do is say so.
//
// ZOOM IS NOT A RENDER (spec 13/09 §6). It is the same pixels drawn over a
// larger rectangle, so it is applied here and never reaches the render key.
//
// THE CANVAS CLIPS, AND THE CANVAS NAVIGATES (reprovado em uso, 15/09)
// ---------------------------------------------------------------------------
// Two defects that the headless selftest reported green through, because it
// counts `imgui errors` and `textured yes` and neither of those is a person
// trying to work:
//
//   1. Nothing clipped. The body filled a rectangle and then called `AddImage`
//      into the SAME draw list with no `PushClipRect`. A window's draw list is
//      clipped to the window's inner rect, which starts under the menu bar --
//      so the icon was free to paint over everything the body had drawn ABOVE
//      it, which is the row of combos. Past a certain zoom the icon simply ate
//      the appearance / idiom / size / zoom controls. That is the user's "o
//      viewport atravessa os botões", exactly.
//
//   2. The navigation was a sketch: a file-static `pan`, moved only by a middle
//      drag, with no wheel, no anchored zoom, no easing, no bounds and no fit.
//
// The arithmetic below is transcribed from OnyxSDK's ImageViewer
// (D:/CodingProjects/OnyxSDK/Source/Viewers/ImageViewer.cpp) -- anchored zoom
// at :100, the fit/centre pair at :154, the margin clamp at :210, the shared
// exp-decay ease at :230 -- and NOT taken by linking it. Rule 2 of the
// architecture spec: only Source/app links Onyx, and IconComposerKit is what
// `ic_tests` and the headless selftest link (Source/IconComposerKit/
// CMakeLists.txt:22 names Foundation, CoreSVG, RenderBox and imgui_lib, and
// nothing else). `Onyx::Viewers::ImageViewer` also owns a `TexturePool` and a
// `VkContext` (ImageViewer.cpp:52-59) and draws its own toolbar out of
// `Onyx::Theme` and `SFSymbols`, so the class is a viewer of a file on disk,
// not a view transform -- of its 268 lines the part this panel needs is about
// forty, and they are pure. Transcribing them cost nothing and kept the Kit
// buildable with `-DIC_BUILD_UI=OFF`.
//
// THE DIAGNOSTICS PANEL IS WHAT KEEPS THE PICTURE FROM LYING (spec 13/09 §6)
// --------------------------------------------------------------------------
// `skipped`, `shapeGaps` and `notes` go to the screen for the same reason
// `icrender` prints them: a plausible figure produced without measurement must
// not be silent. A layer the renderer declined to draw appears here by name
// instead of simply not being in the image.
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/ViewModel.h"
// `rb::kCanvasPoints` -- the selection overlay must use the same ruler the
// renderer places art with, or it would frame the layer somewhere the layer is
// not (doc 03 §22).
#include "Source/RenderBox/IconRenderer.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

namespace ick {

// ─── The geometry, drawing nothing (declared in Panels.h) ────────────────────

float canvasClampZoom(float zoom) {
    if (!(zoom > 0.0f)) return kCanvasZoomMin;   // also catches NaN
    return std::clamp(zoom, kCanvasZoomMin, kCanvasZoomMax);
}

float canvasFitZoom(float availW, float availH, float sidePx) {
    if (!(sidePx > 0.0f) || !(availW > 0.0f) || !(availH > 0.0f)) return 1.0f;
    return canvasClampZoom(std::min(availW / sidePx, availH / sidePx));
}

CanvasVec canvasCentrePan(float availW, float availH, float sidePx, float zoom) {
    const float side = sidePx * zoom;
    return CanvasVec{(availW - side) * 0.5f, (availH - side) * 0.5f};
}

CanvasVec canvasZoomAnchored(CanvasVec pan, float fromZoom, float toZoom, CanvasVec anchor) {
    if (!(fromZoom > 0.0f)) return pan;
    // The anchor's position INSIDE the icon, in screen pixels, scales with the
    // magnification; keeping the anchor still means moving the corner by the
    // difference. Written against the target zoom, so two wheel clicks inside
    // one ease compose instead of fighting.
    const float scale = canvasClampZoom(toZoom) / fromZoom;
    return CanvasVec{anchor.x - (anchor.x - pan.x) * scale,
                     anchor.y - (anchor.y - pan.y) * scale};
}

CanvasVec canvasClampPan(CanvasVec pan, float availW, float availH, float sidePx, float zoom,
                         float margin) {
    const float side = sidePx * zoom;
    auto axis = [&](float v, float avail) {
        if (side <= avail) {
            // Smaller than the canvas: it lives centred, and may be nudged off
            // centre by the margin so a drag still feels like it does something.
            const float centre = (avail - side) * 0.5f;
            return std::clamp(v, centre - margin, centre + margin);
        }
        // Larger: the far edge may not come further in than the margin, which is
        // what stops the icon being dragged out of the canvas entirely.
        return std::clamp(v, avail - side - margin, margin);
    };
    return CanvasVec{axis(pan.x, availW), axis(pan.y, availH)};
}

CanvasRect canvasImageRect(CanvasVec pan, float sidePx, float zoom) {
    const float side = sidePx * zoom;
    return CanvasRect{pan.x, pan.y, pan.x + side, pan.y + side};
}

CanvasRect canvasIntersect(CanvasRect a, CanvasRect b) {
    CanvasRect r{std::max(a.x0, b.x0), std::max(a.y0, b.y0),
                 std::min(a.x1, b.x1), std::min(a.y1, b.y1)};
    if (r.empty()) return CanvasRect{r.x0, r.y0, r.x0, r.y0};
    return r;
}

std::uint32_t canvasTileSize(std::uint32_t baseSize, float zoom) {
    if (!(zoom > 1.0f) || baseSize == 0) return 0;   // `!(>)` tambem pega NaN
    return static_cast<std::uint32_t>(std::lround(static_cast<double>(baseSize) * zoom));
}

TileRect canvasTileFor(CanvasRect painted, CanvasVec imageTopLeft, std::uint32_t baseSize,
                       float zoom) {
    const std::uint32_t t = canvasTileSize(baseSize, zoom);
    if (t == 0 || painted.empty()) return TileRect{};
    // Tela -> ladrilho. O lado na tela e `base * zoom` e o ladrilho tem `t`
    // pixels, entao a razao e ~1 e so absorve o arredondamento de `t`.
    const double k = static_cast<double>(t) / (static_cast<double>(baseSize) * zoom);
    const std::int64_t tMax = static_cast<std::int64_t>(t);
    // Para FORA nos dois lados -- `floor` no canto de cima e `ceil` no de
    // baixo -- para que o ladrilho cubra todo pixel de tela que o recorte
    // toca; arredondar para dentro deixaria uma fatia de fundo do painel na
    // borda. O grampo em [0, t] e o que mantem o pedido dentro da grade.
    auto lo = [&](float v, float o) {
        return std::clamp<std::int64_t>(static_cast<std::int64_t>(std::floor((v - o) * k)), 0, tMax);
    };
    auto hi = [&](float v, float o) {
        return std::clamp<std::int64_t>(static_cast<std::int64_t>(std::ceil((v - o) * k)), 0, tMax);
    };
    const std::int64_t x0 = lo(painted.x0, imageTopLeft.x), x1 = hi(painted.x1, imageTopLeft.x);
    const std::int64_t y0 = lo(painted.y0, imageTopLeft.y), y1 = hi(painted.y1, imageTopLeft.y);
    if (x1 <= x0 || y1 <= y0) return TileRect{};
    return TileRect{static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0),
                    static_cast<std::uint32_t>(x1 - x0), static_cast<std::uint32_t>(y1 - y0)};
}

float canvasEase(float deltaSeconds) {
    // Clamped both ways: a first frame with dt 0 would freeze the ease at zero
    // and a hitch of half a second would teleport. ~150ms settle either way.
    const float dt = std::clamp(deltaSeconds, 1.0f / 240.0f, 1.0f / 30.0f);
    return 1.0f - std::exp(-18.0f * dt);
}

namespace {

// Seconds, with enough digits at both ends of the range this pipeline actually
// spans: a no-glass 512 is a few hundredths and a glass 1024 has been eighty-
// seven. Two decimals, so "0.04 s" and "87.31 s" read the same way. The unit is
// in the string because a bare number in a table of gap sentences reads as an
// index, not a duration.
std::string secondsText(double s) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f s", s);
    return buf;
}

// The four controls the target keeps on the canvas toolbar and footer (spec
// 13/09 §7). Each writes `Session::view` directly: what a person is looking
// THROUGH is not part of the document, so none of these is a command and none
// of them belongs on the undo stack.
std::size_t contextBar(Session& s) {
    std::size_t n = 0;

    ImGui::SetNextItemWidth(90);
    if (ImGui::BeginCombo("##appearance", appearanceLabel(s.view.context.appearance))) {
        for (auto a : {icf::Appearance::Base, icf::Appearance::Light, icf::Appearance::Dark,
                       icf::Appearance::Tinted}) {
            if (ImGui::Selectable(appearanceLabel(a), a == s.view.context.appearance)) {
                s.view.context.appearance = a;
            }
        }
        ImGui::EndCombo();
    }
    ++n;

    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    // Three unlabelled 90px combos in a row is three ways to pick the wrong one,
    // and this is the one that changes the PROPORTIONS -- a `position`
    // specialization moves the art's scale and offset. The tooltip goes on the
    // combo button, which is the last item straight after `BeginCombo`, not
    // after `EndCombo` (which closes the popup).
    const bool idiomOpen = ImGui::BeginCombo("##idiom", idiomLabel(s.view.context.idiom));
    {
        const icf::Idiom declared = declaredIdiom(s.root());
        std::string tip = "Idiom -- which platform's composition the canvas draws. "
                          "`position`, `hidden` and `image-name` can each be specialized per idiom, "
                          "so this is what makes the art change size or move.\nThis document declares "
                          "supported-platforms " + declaredPlatformsText(s.root()) + ", so it opens on " +
                          idiomLabel(declared) + ".";
        if (s.view.context.idiom != declared) {
            tip += "\nYou are looking at ";
            tip += idiomLabel(s.view.context.idiom);
            tip += ", which is not what the document declares.";
        }
        ImGui::SetItemTooltip("%s", tip.c_str());
    }
    if (idiomOpen) {
        for (auto i : {icf::Idiom::Base, icf::Idiom::Square, icf::Idiom::IOS, icf::Idiom::MacOS,
                       icf::Idiom::WatchOS}) {
            if (ImGui::Selectable(idiomLabel(i), i == s.view.context.idiom)) s.view.context.idiom = i;
        }
        ImGui::EndCombo();
    }
    ++n;

    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    char sizeLabel[16];
    std::snprintf(sizeLabel, sizeof sizeLabel, "%u px", s.view.size);
    if (ImGui::BeginCombo("##size", sizeLabel)) {
        if (ImGui::Selectable("512 px", s.view.size == 512)) s.view.size = 512;
        if (ImGui::Selectable("1024 px", s.view.size == 1024)) s.view.size = 1024;
        ImGui::EndCombo();
    }
    ++n;

    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    // TWO CONTROLS THAT DISAGREE ARE WORSE THAN ONE. The combo reads and writes
    // the SAME number the wheel does, so a wheel zoom to 137% shows "137%" here
    // rather than leaving a stale "100%" next to an icon that is plainly not at
    // 100%. It shows the TARGET, not the eased value: mid-ease the number would
    // otherwise flicker through every intermediate percentage.
    char zoomLabel[16];
    std::snprintf(zoomLabel, sizeof zoomLabel, "%d%%",
                  static_cast<int>(s.view.zoomTarget * 100.0f + 0.5f));
    if (ImGui::BeginCombo("##zoom", zoomLabel)) {
        for (float z : {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 4.0f}) {
            char l[16];
            std::snprintf(l, sizeof l, "%d%%", static_cast<int>(z * 100.0f + 0.5f));
            // The request, not the target: the canvas is the only place that
            // knows where the viewport centre is, and a zoom that is not
            // anchored anywhere throws the icon off screen (Session.h).
            if (ImGui::Selectable(l, s.view.zoomTarget == z)) s.view.zoomRequest = z;
        }
        ImGui::EndCombo();
    }
    ++n;

    return n;
}

// Zoom out / zoom in / 1:1 / Fit -- the four buttons Onyx's viewer carries
// (ImageViewer.cpp:130-136). They write the same two request fields the combo
// writes, so there is exactly one path into the view transform.
std::size_t zoomBar(Session& s) {
    ImGui::SameLine();
    if (ImGui::SmallButton("-")) s.view.zoomRequest = s.view.zoomTarget / 1.5f;
    ImGui::SetItemTooltip("Zoom out. The mouse wheel over the canvas does the same, "
                          "anchored on the pointer.");
    ImGui::SameLine();
    if (ImGui::SmallButton("+")) s.view.zoomRequest = s.view.zoomTarget * 1.5f;
    ImGui::SetItemTooltip("Zoom in.");
    ImGui::SameLine();
    if (ImGui::SmallButton("1:1")) s.view.zoomRequest = 1.0f;
    ImGui::SetItemTooltip("One texel of the preview to one pixel on screen.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Fit")) s.view.fitRequest = true;
    ImGui::SetItemTooltip("Fit the whole icon in the canvas and centre it. "
                          "This is where the canvas opens.");
    return 4;
}

// The selection rectangle, on the renderer's own ruler: a square of the whole
// canvas scaled by `position.scale`, moved by `position.translation` measured in
// `rb::kCanvasPoints` from the canvas CENTRE with +y DOWN (doc 03 §22).
void drawSelectionOverlay(const Session& s, ImDrawList* dl, ImVec2 topLeft, float side) {
    const icf::json::Value* node = icf::nodeAt(s.root(), *s.selection);
    if (!node) return;

    icf::Position p;
    // Resolved under the context the CANVAS renders, not the inspector's scope:
    // the rectangle has to frame the art that is on screen.
    if (const icf::json::Value* pv = icf::resolve(*node, "position", s.view.context)) {
        if (auto read = icf::positionFrom(*pv)) p = *read;
    }

    const float pointsToPixels = side / static_cast<float>(rb::kCanvasPoints);
    const float half = side * static_cast<float>(p.scale) * 0.5f;
    const ImVec2 centre(topLeft.x + side * 0.5f + static_cast<float>(p.translation.x) * pointsToPixels,
                        topLeft.y + side * 0.5f + static_cast<float>(p.translation.y) * pointsToPixels);
    dl->AddRect(ImVec2(centre.x - half, centre.y - half), ImVec2(centre.x + half, centre.y + half),
                IM_COL32(80, 160, 255, 255), 0.0f, 0, 2.0f);
}

}  // namespace

CanvasStats drawCanvas(Session& s, const RenderView& view, MenuActions& actions) {
    CanvasStats st;
    // The canvas is the window that carries the menu bar: Onyx owns the frame,
    // and one of our windows has to hold the bar we draw ourselves (spec §7).
    if (!ImGui::Begin(kCanvasWindow, nullptr, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        return st;
    }
    st.menu = drawMenuBar(s, actions);
    st.contextControls = contextBar(s);
    st.zoomControls = zoomBar(s);
    if (view.pending) {
        ImGui::SameLine();
        ImGui::TextDisabled("rendering…");
    }

    ViewContext& v = s.view;
    const ImVec2 availRaw = ImGui::GetContentRegionAvail();
    const float availW = availRaw.x > 1.0f ? availRaw.x : 1.0f;
    const float availH = availRaw.y > 1.0f ? availRaw.y : 1.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 corner(origin.x + availW, origin.y + availH);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // The ruler for every number below is the PREVIEW SIZE, not the texture:
    // since the viewport render (spec 2026-09-16) the texture may be a tile of
    // a larger grid, and measuring the canvas by it would move the icon every
    // time a tile landed. It was already the stand-in before the first render
    // lands -- it is what that render will be, so the fit computed from it is
    // the fit the picture arrives into -- and now it is the ruler always.
    const float sidePx = static_cast<float>(v.size);

    // ── Fit on open ─────────────────────────────────────────────────────────
    // `fitted` is false in a freshly opened Session and nowhere else, so this is
    // "when a document opens" without an event to plumb. No ease: there is
    // nothing on screen yet to ease away from.
    if (!v.fitted) {
        v.zoomTarget = canvasFitZoom(availW, availH, sidePx);
        const CanvasVec c = canvasCentrePan(availW, availH, sidePx, v.zoomTarget);
        v.panTargetX = c.x;
        v.panTargetY = c.y;
        v.zoom = v.zoomTarget;
        v.panX = c.x;
        v.panY = c.y;
        v.fitted = true;
    }

    // Flat colour behind the icon (spec 13/09 §6). Flat and KNOWN: the six
    // `sine-*` backdrops of the target are round 5, and a backdrop taken from
    // the theme could be fully transparent, which would put the icon's own
    // alpha over the window and make transparency unreadable.
    dl->AddRectFilled(origin, corner, IM_COL32(40, 40, 44, 255));

    // Left OR middle drag pans, which is what Onyx's viewer accepts
    // (ImageViewer.cpp:199-200). Left as well as middle because a middle button
    // is the one control a trackpad does not have, and the canvas has no other
    // use for a left drag -- selection happens in the Layers panel.
    ImGui::InvisibleButton("##canvas", ImVec2(availW, availH),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    ImGuiIO& io = ImGui::GetIO();

    // One path into the view transform, used by the wheel, the combo and the
    // three zoom buttons alike.
    auto zoomTo = [&](float wanted, CanvasVec anchor) {
        const float target = canvasClampZoom(wanted);
        if (target == v.zoomTarget) return;
        const CanvasVec p =
            canvasZoomAnchored(CanvasVec{v.panTargetX, v.panTargetY}, v.zoomTarget, target, anchor);
        v.panTargetX = p.x;
        v.panTargetY = p.y;
        v.zoomTarget = target;
    };
    const CanvasVec centre{availW * 0.5f, availH * 0.5f};

    if (v.fitRequest) {
        v.fitRequest = false;
        v.zoomTarget = canvasFitZoom(availW, availH, sidePx);
        const CanvasVec c = canvasCentrePan(availW, availH, sidePx, v.zoomTarget);
        v.panTargetX = c.x;
        v.panTargetY = c.y;
    }
    // A control with no pointer of its own zooms about the middle of the canvas,
    // which is where the icon is when nobody has dragged it.
    if (v.zoomRequest > 0.0f) {
        zoomTo(v.zoomRequest, centre);
        v.zoomRequest = 0.0f;
    }

    // THE WHEEL, ANCHORED ON THE POINTER. 1.15 per notch is Onyx's rate
    // (ImageViewer.cpp:191) and the anchoring is the whole point: the texel
    // under the cursor is the one thing that must not move while the rest grows
    // around it.
    if (hovered && io.MouseWheel != 0.0f) {
        zoomTo(v.zoomTarget * std::pow(1.15f, io.MouseWheel),
               CanvasVec{io.MousePos.x - origin.x, io.MousePos.y - origin.y});
    }

    if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) ||
                   ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))) {
        v.panTargetX += io.MouseDelta.x;
        v.panTargetY += io.MouseDelta.y;
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    } else if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    // Bounded against the TARGET, so the target stays somewhere the ease can
    // actually arrive at; clamping the eased value instead would let the two
    // disagree forever and the icon would creep.
    const CanvasVec bounded =
        canvasClampPan(CanvasVec{v.panTargetX, v.panTargetY}, availW, availH, sidePx, v.zoomTarget);
    v.panTargetX = bounded.x;
    v.panTargetY = bounded.y;

    // ── The ease: the "pan suave" that was missing ──────────────────────────
    // One coefficient for zoom and both pan axes, because the anchored zoom's
    // arithmetic only holds while they move together -- ease them at different
    // rates and the anchored point drifts during the settle.
    const float k = canvasEase(io.DeltaTime);
    v.zoom += (v.zoomTarget - v.zoom) * k;
    v.panX += (v.panTargetX - v.panX) * k;
    v.panY += (v.panTargetY - v.panY) * k;
    // Snap inside a sub-pixel, or the lerp never ends and the canvas re-draws a
    // still picture forever.
    if (std::fabs(v.zoomTarget - v.zoom) < 0.0005f) v.zoom = v.zoomTarget;
    if (std::fabs(v.panTargetX - v.panX) < 0.25f) v.panX = v.panTargetX;
    if (std::fabs(v.panTargetY - v.panY) < 0.25f) v.panY = v.panTargetY;

    const ImVec2 tl(origin.x + v.panX, origin.y + v.panY);
    st.clip = CanvasRect{origin.x, origin.y, corner.x, corner.y};
    st.image = canvasImageRect(CanvasVec{tl.x, tl.y}, sidePx, v.zoom);
    st.painted = canvasIntersect(st.image, st.clip);
    st.zoom = v.zoom;

    // ── O LADRILHO, SO COM O PAN E O ZOOM PARADOS ───────────────────────────
    // Spec 2026-09-16, "O que o Kit faz": nao ha pedido por quadro. Durante um
    // arrasto a chave mudaria a cada quadro e o agendador so descartaria
    // trabalho, e o intervalo sem imagem nitida viraria o arrasto inteiro. O
    // coordenador pede o que estiver em `v.tile`, e enquanto a pessoa arrasta
    // isto nao muda -- o ladrilho velho e o que continua na tela.
    //
    // A comparacao e de igualdade exata porque o ease acima GRAMPEIA o valor
    // no alvo dentro de meio pixel (as tres linhas de snap); sem elas o pan
    // nunca chegaria ao alvo e nada aqui jamais assentaria.
    const bool settled = v.zoom == v.zoomTarget && v.panX == v.panTargetX && v.panY == v.panTargetY;
    v.settledSeconds = settled ? v.settledSeconds + io.DeltaTime : 0.0f;
    if (v.settledSeconds >= kTileSettleSeconds) {
        const TileRect want = canvasTileFor(st.painted, CanvasVec{tl.x, tl.y}, v.size, v.zoom);
        v.tileSize = want.w ? canvasTileSize(v.size, v.zoom) : 0;
        v.tile = want;
    }

    // ── THE RECORTE ─────────────────────────────────────────────────────────
    // Everything from here to PopClipRect is the canvas's own rectangle and
    // nothing else. The window's draw list is otherwise clipped to the window's
    // INNER rect, which begins under the menu bar and therefore includes the row
    // of combos this function drew a few lines ago -- an icon larger than the
    // canvas painted straight over them. `true` intersects with the current clip
    // rather than replacing it, so the window's own bounds still apply.
    dl->PushClipRect(origin, corner, true);

    // A TEXTURA VAI NO RETANGULO QUE ELA COBRE, e nao mais no canvas inteiro
    // (spec 2026-09-16, "O que o Kit faz"). `fullSide` e o canvas inteiro na
    // tela; `perTexel` leva um pixel da grade do render a um pixel de tela, e
    // a textura ocupa `width` x `height` a partir de (`originX`, `originY`)
    // DESSA grade. Com a grade igual ao canvas e origem zero -- todo render
    // que nao e ladrilho -- isto e exatamente o `AddImage` de antes.
    //
    // E e por aqui que a espera funciona: a grade vem do render que esta na
    // tela, nao do que foi pedido, entao o ladrilho velho fica ancorado na
    // imagem e acompanha o pan; um zoom o estica ate o novo chegar.
    const float fullSide = sidePx * v.zoom;
    if (view.texture != ImTextureID_Invalid && view.width > 0) {
        const float grid = view.gridSize > 0 ? static_cast<float>(view.gridSize)
                                             : static_cast<float>(view.width);
        const float perTexel = fullSide / grid;
        const ImVec2 a(tl.x + static_cast<float>(view.originX) * perTexel,
                       tl.y + static_cast<float>(view.originY) * perTexel);
        const ImVec2 b(a.x + static_cast<float>(view.width) * perTexel,
                       a.y + static_cast<float>(view.height) * perTexel);
        dl->AddImage(view.texture, a, b);
        st.textured = true;

        // O lado do CANVAS INTEIRO na tela, que e o que este overlay recebia
        // quando a textura era o canvas: ele mede em `rb::kCanvasPoints` sobre
        // o canvas, e um ladrilho nao muda onde a camada esta.
        if (s.selection && s.selection->layer) drawSelectionOverlay(s, dl, tl, fullSide);
    }

    // The provisional mark: these pixels are not the answer to what the context
    // bar now says. Small and in the corner, because it is on screen during
    // every edit and must not compete with the icon. Inside the clip, like
    // everything else the canvas draws.
    if (view.pending) {
        dl->AddCircleFilled(ImVec2(corner.x - 12.0f, origin.y + 12.0f), 4.0f, IM_COL32(230, 180, 60, 255));
    }

    dl->PopClipRect();

    ImGui::End();
    return st;
}

DiagnosticsStats drawDiagnostics(const Session& s, const RenderView& view) {
    DiagnosticsStats st;
    if (!ImGui::Begin(kDiagnosticsWindow)) {
        ImGui::End();
        return st;
    }

    auto row = [&](const char* origin, const std::string& text) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(origin);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("%s", text.c_str());
        ++st.rows;
    };

    if (ImGui::BeginTable("diag", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("origin", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("text");

        // Always present, even when nothing is wrong: "191 of 194" is the line
        // that makes a missing layer visible at a glance.
        //
        // AND HOW LONG IT TOOK, since 2026-09-15. This row used to end at the
        // layer count, and on the day six fronts turned a 1024 px glass render
        // into eighty-seven seconds the panel had nothing to say: no layer was
        // missing, no gap was reported, the arithmetic was right, and the canvas
        // was empty. `RenderView::lastRenderSeconds` is negative until a render
        // has finished, which is not zero and must not print as `0.00 s`.
        std::string what =
            std::to_string(view.drawn) + " of " + std::to_string(view.total) + " layer(s) drawn";
        if (view.lastRenderSeconds >= 0.0) what += " in " + secondsText(view.lastRenderSeconds);
        row("render", what);
        // THE ONE IN FLIGHT, counted while it is still running. The canvas
        // already shows a yellow dot for `pending`, and a dot cannot tell forty
        // milliseconds from a minute and a half -- which is exactly the sentence
        // the user needed and did not get.
        if (view.pending) {
            row("render", "a newer render has been running for " +
                              secondsText(view.pendingSeconds));
        }
        if (!view.error.empty()) row("render", view.error);

        // WHICH COMPOSITION IS ON SCREEN, ALWAYS, AND WHETHER IT IS THE ONE THE
        // DOCUMENT DECLARES. A `position` specialization changes the art's scale
        // and offset, so "the proportions look wrong" and "I am on the wrong
        // idiom" are the same sentence -- and until this row existed there was
        // nothing on screen that said which idiom was active. The combo alone
        // could not: it is 90px of unlabelled text among two identical
        // neighbours.
        const icf::Idiom declared = declaredIdiom(s.root());
        std::string where = std::string("idiom ") + idiomLabel(s.view.context.idiom) + ", appearance " +
                            appearanceLabel(s.view.context.appearance) + "; the document declares " +
                            declaredPlatformsText(s.root());
        if (s.view.context.idiom != declared) {
            where += " -- which is ";
            where += idiomLabel(declared);
            where += ", not what the canvas is showing";
        }
        row("view", where);
        for (const auto& x : view.skipped) row("skipped", x);
        for (const auto& x : view.shapeGaps) row("shape", x);
        // `[OBS]` -- drawn, but on a ruler or a shape that was guessed rather
        // than read. The mark is the same one the docs use.
        for (const auto& x : view.notes) row("[OBS]", x);

        // These two are the DOCUMENT's gaps, not the render's: they are true
        // whether or not a render has ever run.
        const std::vector<std::string> missing = s.bundle().missingAssets();
        for (const auto& x : missing) row("asset", "missing: " + x);
        const std::vector<std::string> unknown = s.bundle().document().unknownKeys();
        for (const auto& x : unknown) row("key", "unknown: " + x);

        ImGui::EndTable();
    }

    ImGui::End();
    return st;
}

}  // namespace ick
