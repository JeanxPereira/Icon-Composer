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

#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

namespace ick {
namespace {

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
    if (ImGui::BeginCombo("##idiom", idiomLabel(s.view.context.idiom))) {
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
    char zoomLabel[16];
    std::snprintf(zoomLabel, sizeof zoomLabel, "%d%%", static_cast<int>(s.view.zoom * 100));
    if (ImGui::BeginCombo("##zoom", zoomLabel)) {
        for (float z : {0.5f, 0.75f, 1.0f, 1.5f, 2.0f}) {
            char l[16];
            std::snprintf(l, sizeof l, "%d%%", static_cast<int>(z * 100));
            if (ImGui::Selectable(l, s.view.zoom == z)) s.view.zoom = z;
        }
        ImGui::EndCombo();
    }
    ++n;

    return n;
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
    if (view.pending) {
        ImGui::SameLine();
        ImGui::TextDisabled("rendering…");
    }

    // Pan survives the frame; it is a property of looking, like zoom, and is not
    // worth a field on the Session that nothing else would ever read.
    static ImVec2 pan(0.0f, 0.0f);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 corner(origin.x + avail.x, origin.y + avail.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Flat colour behind the icon (spec 13/09 §6). Flat and KNOWN: the six
    // `sine-*` backdrops of the target are round 5, and a backdrop taken from
    // the theme could be fully transparent, which would put the icon's own
    // alpha over the window and make transparency unreadable.
    dl->AddRectFilled(origin, corner, IM_COL32(40, 40, 44, 255));
    ImGui::InvisibleButton("##canvas", ImVec2(avail.x > 1.0f ? avail.x : 1.0f,
                                              avail.y > 1.0f ? avail.y : 1.0f));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        pan.x += ImGui::GetIO().MouseDelta.x;
        pan.y += ImGui::GetIO().MouseDelta.y;
    }

    if (view.texture != ImTextureID_Invalid && view.width > 0) {
        const float side = static_cast<float>(view.width) * s.view.zoom;
        const ImVec2 tl(origin.x + (avail.x - side) * 0.5f + pan.x,
                        origin.y + (avail.y - side) * 0.5f + pan.y);
        dl->AddImage(view.texture, tl, ImVec2(tl.x + side, tl.y + side));
        st.textured = true;

        if (s.selection && s.selection->layer) drawSelectionOverlay(s, dl, tl, side);
    }

    // The provisional mark: these pixels are not the answer to what the context
    // bar now says. Small and in the corner, because it is on screen during
    // every edit and must not compete with the icon.
    if (view.pending) {
        dl->AddCircleFilled(ImVec2(corner.x - 12.0f, origin.y + 12.0f), 4.0f, IM_COL32(230, 180, 60, 255));
    }

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
        row("render", std::to_string(view.drawn) + " of " + std::to_string(view.total) + " layer(s) drawn");
        if (!view.error.empty()) row("render", view.error);
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
