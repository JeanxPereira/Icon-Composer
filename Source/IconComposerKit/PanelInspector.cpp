// The Inspector: one section per specializable property of the selected node.
//
// THE SCOPE IS THE WHOLE POINT (spec 13/09 §7, §4.3)
// -----------------------------------------------------------------------------
// Every section reads and writes under ONE scope, chosen once at the top of the
// panel and held in `Session::scope`. The value shown is what `resolve` gives
// for that scope; the grey "inherited" beside it says that the scope has no
// entry of its OWN and is reading something more general (`hasOwnEntry`, over
// the corpus's 890 specialization lists). Those two answers are different
// questions about the same node, and collapsing them is how an editor lets
// someone edit Dark while believing they are editing Base -- a lie the document
// only confesses on save. So "Remove override" appears exactly when there is an
// own entry to remove, and never under Base, where there is no override, only
// the value itself.
//
// TWO VOCABULARIES, AND ONLY ONE OF THEM GOES ON SCREEN
// -----------------------------------------------------------------------------
// `blendModeLabel` and friends answer what the target puts in its menu ("Plus
// Lighter"); `blendModeToString` answers what the format writes ("plus-lighter",
// doc 01 §6). A combo previews and lists the LABEL and writes the STRING. The
// two are the same length and the same shape, and swapping them corrupts the
// document without any error at all.
//
// The seven inspectors this round does not build are drawn disabled with the
// reason in their tooltip rather than hidden (spec 13/09 §7): a control that is
// missing teaches nothing, and one that is greyed says what is coming.
#include "Source/IconComposerKit/InspectorSection.h"
#include "Source/IconComposerKit/Theme.h"
#include "Source/IconComposerKit/Widgets.h"

#include "Source/IconComposerFoundation/Values.h"
#include "Source/IconComposerKit/ViewModel.h"
#include "imgui.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ick {
namespace {

// Numbers keep their source lexeme in the tree (Json.h), so a control reads one
// back through `strtod` and writes it through `json::Value::number(double)`,
// which is the shortest round-tripping form Apple's encoder writes (spec 13/09
// §2.2). A property the node does not carry falls back to the renderer's
// default rather than to zero.
//
// A PROPERTY THE DOCUMENT DOES NOT CARRY IS NOT A PROPERTY SET TO ITS DEFAULT
// -----------------------------------------------------------------------------
// The fill's `orientation` is where those two come apart. It is optional; its
// absence means the gradient draws on `(0,0) -> (0,1)`, which is also what an
// orientation spelling that axis means; and 61 of the corpus's 97
// `linear-gradient` fills say nothing. An editor that showed the resolved axis
// in a plain control would write one on the first drag -- or worse, on merely
// opening the document -- and the file would come back with a member Apple's
// encoder never wrote. So the axis row draws the absence as an absence, with a
// button to leave it, and every other control in the section edits the fill's
// node IN PLACE rather than re-spelling it whole.



void visible(Section& x) {
    PropertyView v;
    if (x.begin("Visible", "hidden", v)) {
        bool on = !booleanOr(v.value, false);
        if (ui::toggle("Visible", &on)) x.write("hidden", icf::json::Value::boolean(!on), false);
    }
    x.end();
}

void opacity(Section& x) {
    PropertyView v;
    if (x.begin("Opacity", "opacity", v)) {
        // Labelled, not `##opacity`: the section header is a heading and the
        // control is a control, and a panel of bare frames is a panel where the
        // only way to know what a row is, is to remember the order.
        double f = numberOr(v.value, 1.0);
        NumberEdit e = sliderNumber("Opacity", &f, 0.0, 1.0, "%.3f",
                                    "Opacity -- 0 is invisible, 1 is opaque.");
        if (e.changed) x.write("opacity", icf::json::Value::number(f), true);
        if (e.released) x.s.endCoalescing();
    }
    x.end();
}

void blendMode(Section& x) {
    // Ten, not seventeen: the other seven have no spelling in the format
    // (doc 01 §6).
    static const icf::BlendMode kModes[] = {
        icf::BlendMode::Normal, icf::BlendMode::PlusLighter, icf::BlendMode::PlusDarker, icf::BlendMode::Overlay,
        icf::BlendMode::Multiply, icf::BlendMode::SoftLight, icf::BlendMode::HardLight, icf::BlendMode::Darken,
        icf::BlendMode::Lighten, icf::BlendMode::Screen};
    PropertyView v;
    if (x.begin("Blend Mode", "blend-mode", v)) {
        icf::BlendMode current = icf::BlendMode::Normal;
        if (v.value && v.value->kind() == icf::json::Value::Kind::String) {
            if (auto m = icf::blendModeFromString(v.value->rawString())) current = *m;
        }
        if (ui::combo("Mode", blendModeLabel(current))) {
            for (auto m : kModes) {
                if (ui::menuItem(blendModeLabel(m), nullptr, m == current)) {
                    x.write("blend-mode", icf::json::Value::string(std::string(icf::blendModeToString(m))), false);
                }
            }
            ImGui::EndCombo();
        }
    }
    x.end();
}

// ---- the position is patched IN the node, member by member ------------------
// The same rule the fill section states at length, and for the same reason: a
// number nobody touched has to keep the lexeme it arrived with. The old control
// read the whole `Position` through FLOATS and wrote it back whole, so dragging
// the scale re-printed both translation coordinates through a float -- and a
// corpus coordinate spelled `0.5000000000000001` came back `0.5`. Nothing about
// that is visible on screen; it shows up as a document that no longer matches
// the bytes it was opened from.
icf::json::Value positionNode(const PropertyView& v, const icf::Position& p) {
    if (v.value && v.value->kind() == icf::json::Value::Kind::Object) return *v.value;
    // No object yet: only then does an edit have to spell a whole position.
    return icf::positionToJson(p);
}

void writeScale(Section& x, const PropertyView& v, const icf::Position& p, double scale) {
    icf::json::Value node = positionNode(v, p);
    if (node.kind() != icf::json::Value::Kind::Object) return;
    node.set("scale", icf::json::Value::number(scale));
    x.write("position", std::move(node), true);
}

void writeTranslation(Section& x, const PropertyView& v, const icf::Position& p, double tx, double ty) {
    icf::json::Value node = positionNode(v, p);
    if (node.kind() != icf::json::Value::Kind::Object) return;
    icf::json::Value* arr = node.find("translation-in-points");
    if (arr && arr->kind() == icf::json::Value::Kind::Array && arr->elements().size() == 2) {
        // Only the coordinate that MOVED, exactly as `writeAxis` does below.
        if (tx != p.translation.x) arr->elements()[0] = icf::json::Value::number(tx);
        if (ty != p.translation.y) arr->elements()[1] = icf::json::Value::number(ty);
    } else {
        node.set("translation-in-points",
                 icf::json::Value::array({icf::json::Value::number(tx), icf::json::Value::number(ty)}));
    }
    x.write("position", std::move(node), true);
}

void geometry(Section& x) {
    PropertyView v;
    if (x.begin("Geometry", "position", v)) {
        icf::Position p;
        if (v.value) {
            if (auto read = icf::positionFrom(*v.value)) p = *read;
        }
        // Scale is a factor on disk and a percentage on screen, the way the
        // target shows it.
        double xy[2] = {p.translation.x, p.translation.y};
        double scale = p.scale * 100.0;
        // A QUARTER of a unit per pixel, not a whole one. A drag whose step is
        // the unit itself cannot STOP on a round number -- it lands wherever the
        // pixel fell -- and the values a person means here are round ones: 0,
        // -25, 80%. A finer step lets the drag approach one, and the typed entry
        // the helper advertises is what actually lands on it.
        NumberEdit t = dragNumbers("Translation (pt)", xy, 2, 0.25f, nullptr, nullptr, "%.4f",
                                   "Translation -- points from the canvas centre, +y DOWN "
                                   "(doc 03 sec. 22). Not clamped: the canvas is 1024 points and "
                                   "art may legitimately sit outside it.");
        // 0, not the old 1%: a factor of zero is a value the format can hold and
        // the floor at one percent was nothing the corpus or the binary asked
        // for. The ceiling stays where it was.
        static const double kScaleLo = 0.0, kScaleHi = 1000.0;
        NumberEdit sc = dragNumbers("Scale (%)", &scale, 1, 0.25f, &kScaleLo, &kScaleHi, "%.4f %%",
                                    "Scale -- shown as a percentage, stored as a factor "
                                    "(100 % is 1.0 on disk).");
        if (t.changed) writeTranslation(x, v, p, xy[0], xy[1]);
        if (sc.changed) writeScale(x, v, p, scale / 100.0);
        if (t.released || sc.released) x.s.endCoalescing();
    }
    x.end();
}

// A fresh colour for a kind that just gained one: opaque sRGB, the space the
// format writes when nothing says otherwise.
icf::Color opaqueSrgb() {
    icf::Color c;
    c.space = icf::ColorSpace::SRGB;
    c.count = 4;
    c.components[3] = 1;
    return c;
}

// ---- the fill is edited IN the node the document carries -------------------
// Every writer below starts from `fillNode`: the object already in the tree,
// with the ONE member that moved replaced. Going back through `fillToJson`
// would re-print every colour and every coordinate of a value where a single
// number changed, and the 135 byte-exact documents are byte-exact precisely
// because a number nobody touched keeps the lexeme it arrived with (Json.h).
//
// `orientation` turns that from a nicety into the rule of this section. It is
// OPTIONAL and its ABSENCE IS A VALUE: `[BIN]` a nil placement is substituted
// by the draw path with `GradientPlacement.default`, `(0,0) -> (0,1)`
// (doc 03 §30.7), and `[ART]` 61 of the corpus's 97 `linear-gradient` fills
// name none. So no control here may write an `orientation` into a fill that had
// none -- that is a byte the gate compares -- except the one button whose whole
// purpose is to ask for an axis, and its opposite, which gives the absence back.
icf::json::Value fillNode(const PropertyView& v, const icf::Fill& f) {
    if (v.value && v.value->kind() == icf::json::Value::Kind::Object) return *v.value;
    // No object yet: a bare-string kind, or a property nobody has written. Only
    // then does an edit have to spell a whole fill, and `fillToJson` writes the
    // `orientation` member exactly when the `Fill` carries one.
    return icf::fillToJson(f);
}

// The member a kind keeps its colours under: an array for the ramp, a string
// for the two single-colour kinds, nothing for the other four.
const char* colorKey(icf::FillKind k) {
    switch (k) {
        case icf::FillKind::Solid: return "solid";
        case icf::FillKind::AutomaticGradient: return "automatic-gradient";
        case icf::FillKind::LinearGradient: return "linear-gradient";
        default: return nullptr;
    }
}

// Replaces one colour where it lives, leaving the rest of the fill's text --
// the other stops, and the axis -- exactly as it was.
void writeColor(Section& x, const PropertyView& v, const icf::Fill& f, std::size_t i,
                const icf::Color& c) {
    const char* key = colorKey(f.kind);
    if (!key) return;
    icf::json::Value node = fillNode(v, f);
    icf::json::Value spelled = icf::json::Value::string(icf::colorToString(c));
    if (f.kind == icf::FillKind::LinearGradient) {
        icf::json::Value* ramp = node.find(key);
        if (!ramp || ramp->kind() != icf::json::Value::Kind::Array) return;
        if (i >= ramp->elements().size()) return;
        ramp->elements()[i] = std::move(spelled);
    } else {
        node.set(key, std::move(spelled));
    }
    x.write("fill", std::move(node), true);
}

// Growing and shrinking the ramp, which until now it could not do. The array is
// spliced in place, so the stops that stay keep their own text.
void writeRamp(Section& x, const PropertyView& v, const icf::Fill& f,
               std::optional<std::size_t> remove, bool append) {
    icf::json::Value node = fillNode(v, f);
    icf::json::Value* ramp = node.find("linear-gradient");
    if (!ramp || ramp->kind() != icf::json::Value::Kind::Array) return;
    std::vector<icf::json::Value>& stops = ramp->elements();
    if (remove) {
        if (*remove >= stops.size()) return;
        stops.erase(stops.begin() + static_cast<std::ptrdiff_t>(*remove));
    }
    if (append) {
        // A new stop is a COPY of the last one, so the ramp gains a handle and
        // not a colour nobody chose -- and the copy is of the text, not of a
        // number re-printed on the way through.
        stops.push_back(stops.empty() ? icf::json::Value::string(icf::colorToString(opaqueSrgb()))
                                      : stops.back());
    }
    x.write("fill", std::move(node), false);
}

icf::json::Value pointJson(const icf::Point& p) {
    return icf::json::Value::object(
        {{"x", icf::json::Value::number(p.x)}, {"y", icf::json::Value::number(p.y)}});
}

// Writes only the coordinates of the axis that actually MOVED. Dragging
// `stop.y` must not re-print `stop.x`: the corpus spells one of them
// `0.5000000000000001` and another `0.49999999999999983`, and a coordinate
// nobody touched keeping its lexeme is the round trip staying byte-exact.
void writeAxis(Section& x, const PropertyView& v, const icf::Fill& f, const icf::Orientation& want) {
    if (!f.orientation) return;   // the absence is a value; only the button ends it
    icf::json::Value node = fillNode(v, f);
    icf::json::Value* o = node.find("orientation");
    if (!o || o->kind() != icf::json::Value::Kind::Object) return;
    const struct { const char* end; const char* axis; double have, want; } kCoords[] = {
        {"start", "x", f.orientation->start.x, want.start.x},
        {"start", "y", f.orientation->start.y, want.start.y},
        {"stop", "x", f.orientation->stop.x, want.stop.x},
        {"stop", "y", f.orientation->stop.y, want.stop.y},
    };
    bool moved = false;
    for (const auto& c : kCoords) {
        if (c.have == c.want) continue;
        icf::json::Value* end = o->find(c.end);
        if (!end || end->kind() != icf::json::Value::Kind::Object) continue;
        end->set(c.axis, icf::json::Value::number(c.want));
        moved = true;
    }
    if (moved) x.write("fill", std::move(node), true);
}

// The two controls allowed to cross the absence, and the only ones. Gaining an
// axis writes the default the renderer was already substituting, so the pixels
// do not move on the click -- what changes is that the document now SAYS it.
void setAxisPresence(Section& x, const PropertyView& v, const icf::Fill& f, bool present) {
    icf::json::Value node = fillNode(v, f);
    if (node.kind() != icf::json::Value::Kind::Object) return;
    if (present) {
        node.set("orientation", icf::json::Value::object({{"start", pointJson({0, 0})},
                                                          {"stop", pointJson({0, 1})}}));
    } else if (!node.erase("orientation")) {
        return;
    }
    x.write("fill", std::move(node), false);
}

// What the stop's colour space is CALLED on screen. A seventh vocabulary, and
// as with the other six the screen spelling is not the disk spelling --
// `colorToString` is what writes the file, and nothing here goes near it.
const char* colorSpaceLabel(icf::ColorSpace s) {
    switch (s) {
        case icf::ColorSpace::DisplayP3: return "Display P3";
        case icf::ColorSpace::SRGB: return "sRGB";
        case icf::ColorSpace::ExtendedSRGB: return "Extended sRGB";
        case icf::ColorSpace::Gray: return "Gray";
        case icf::ColorSpace::ExtendedGray: return "Extended Gray";
    }
    return "colour";
}

// The same name where the long one does not fit beside the control.
const char* colorSpaceShortLabel(icf::ColorSpace s) {
    switch (s) {
        case icf::ColorSpace::DisplayP3: return "P3";
        case icf::ColorSpace::SRGB: return "sRGB";
        case icf::ColorSpace::ExtendedSRGB: return "Ext. sRGB";
        case icf::ColorSpace::Gray: return "Gray";
        case icf::ColorSpace::ExtendedGray: return "Ext. Gray";
    }
    return "colour";
}

// A colour as the UI can show it. A PREVIEW, not a render: it ignores the
// stop's colour space, so a display-p3 ramp draws here a little duller than the
// canvas will draw it.
ImVec4 previewColor(const icf::Color& c) {
    const float first = static_cast<float>(c.components[0]);
    // The grey spaces carry (luminance, alpha); the RGB spaces (r, g, b, a).
    if (c.count == 2) return ImVec4(first, first, first, static_cast<float>(c.components[1]));
    return ImVec4(first, static_cast<float>(c.components[1]), static_cast<float>(c.components[2]),
                  static_cast<float>(c.components[3]));
}

// The ramp as a strip, in document order. Four numbers in a row do not say
// which end of the gradient they are; a band does.
//
// It is an item like any other in a row: the width the row armed for it
// (`ui::leftLabel`), a field's height and a field's corner. Each span is a
// rounded rectangle whose vertices are recoloured along x afterwards --
// `AddRectFilledMultiColor` has no rounding -- so a stop's alpha survives, over
// the same chequer the colour well shows it on.
void rampPreview(const std::vector<icf::Color>& stops) {
    if (stops.size() < 2) return;
    const float k = ui::dpi();
    const float w = ImGui::CalcItemWidth();
    const float h = ImGui::GetFrameHeight();
    const float r = theme::kControlRadius * k;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 q(p.x + w, p.y + h);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool seeThrough = false;
    for (const icf::Color& c : stops) seeThrough = seeThrough || previewColor(c).w < 1.0f;
    if (seeThrough) ui::checkerboard(dl, p, q, 6.0f * k, r);
    // Evenly spaced: the format gives a stop a colour and no location, and
    // inventing one would be a picture of a document that does not exist.
    const float step = w / static_cast<float>(stops.size() - 1);
    for (std::size_t i = 0; i + 1 < stops.size(); ++i) {
        const ImVec4 a = previewColor(stops[i]);
        const ImVec4 b = previewColor(stops[i + 1]);
        const bool last = i + 2 == stops.size();
        const float x0 = p.x + step * static_cast<float>(i);
        // A span that is not the last runs one pixel under its neighbour, so
        // the two anti-aliased edges do not leave a seam at the stop.
        const float x1 = last ? q.x : x0 + step + 1.0f;
        ImDrawFlags corners = 0;
        if (i == 0) corners |= ImDrawFlags_RoundCornersLeft;
        if (last) corners |= ImDrawFlags_RoundCornersRight;
        if (corners == 0) corners = ImDrawFlags_RoundCornersNone;
        const int first = dl->VtxBuffer.Size;
        dl->AddRectFilled(ImVec2(x0, p.y), ImVec2(x1, q.y), IM_COL32_WHITE, r, corners);
        for (int n = first; n < dl->VtxBuffer.Size; ++n) {
            ImDrawVert& vert = dl->VtxBuffer[n];
            const float t = std::clamp((vert.pos.x - x0) / step, 0.0f, 1.0f);
            // The anti-aliasing fringe arrives with alpha 0, and keeps it.
            const float edge = static_cast<float>((vert.col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
            vert.col = ImGui::ColorConvertFloat4ToU32(ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                                                             a.z + (b.z - a.z) * t,
                                                             (a.w + (b.w - a.w) * t) * edge));
        }
    }
    dl->AddRect(p, q, IM_COL32(255, 255, 255, 36), r, 0, 1.0f);
    ImGui::Dummy(ImVec2(w, h));
}

// A row of the box with its name on the left and a button against the right
// edge, as wide as its own text -- the row "Edit this" has in the scope box.
bool buttonRow(const char* label, const char* button, const char* tooltip) {
    ui::rowStart();
    const float startX = ImGui::GetCursorPosX();
    const float avail = ui::rowAvail();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    const float bw = ImGui::CalcTextSize(button).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(startX + avail - bw);
    const bool pressed = ImGui::Button(button);
    ImGui::SetItemTooltip("%s", tooltip);
    return pressed;
}

void fill(Section& x) {
    static const icf::FillKind kKinds[] = {icf::FillKind::None, icf::FillKind::Automatic, icf::FillKind::Solid,
                                           icf::FillKind::AutomaticGradient, icf::FillKind::LinearGradient,
                                           icf::FillKind::SystemLight, icf::FillKind::SystemDark};
    PropertyView v;
    if (x.begin("Fill", "fill", v)) {
        icf::Fill f;
        if (v.value) {
            if (auto read = icf::fillFrom(*v.value)) f = *read;
        }
        if (ui::combo("Kind", fillKindLabel(f.kind))) {
            for (auto k : kKinds) {
                if (ui::menuItem(fillKindLabel(k), nullptr, k == f.kind)) {
                    // A kind change carries over what the new kind can hold and
                    // nothing else: `solid` and `automatic-gradient` hold one
                    // colour, `linear-gradient` the ramp, the rest none.
                    icf::Fill next;
                    next.kind = k;
                    if (k == icf::FillKind::Solid || k == icf::FillKind::AutomaticGradient) {
                        next.colors.push_back(f.colors.empty() ? opaqueSrgb() : f.colors[0]);
                    } else if (k == icf::FillKind::LinearGradient) {
                        next.colors = f.colors;
                        if (next.colors.size() < 2) next.colors.assign(2, opaqueSrgb());
                    }
                    // The axis carries over between the two kinds the corpus
                    // writes one on, and is dropped by the five that never do.
                    // It is still only ever carried, never invented: a fill
                    // that had no `orientation` gains none from a kind change.
                    if (k == icf::FillKind::LinearGradient || k == icf::FillKind::AutomaticGradient) {
                        next.orientation = f.orientation;
                    }
                    x.write("fill", icf::fillToJson(next), false);
                }
            }
            ImGui::EndCombo();
        }

        const bool ramp = f.kind == icf::FillKind::LinearGradient;
        const bool gradient = ramp || f.kind == icf::FillKind::AutomaticGradient;

        // The strip, in the value column the selector above it is in.
        if (gradient && f.colors.size() >= 2) {
            ui::leftLabel("Ramp");
            rampPreview(f.colors);
        }

        // ---- the stops ----
        // One row each: its name on the left, the colour on the right in the
        // column the number boxes of the other sections are in.
        bool released = false;
        std::optional<std::size_t> edited, remove;
        bool append = false;
        // Two is the floor, not a preference: every one of the corpus's 97
        // ramps carries exactly two, and the target decodes a ramp into
        // `primaryColor` and `secondaryColor`. Shrinking past two would be a
        // ramp with nothing to interpolate.
        const bool removable = ramp && f.colors.size() > 2;
        const float k = ui::dpi();
        const float button = ImGui::GetFrameHeight();
        for (std::size_t i = 0; i < f.colors.size(); ++i) {
            icf::Color& c = f.colors[i];
            ImGui::PushID(static_cast<int>(i));
            // THE SPACE IS IN THE LABEL, and it is not decoration. The picker
            // and the swatch are sRGB, the stop may be display-p3, and the same
            // four numbers mean two different colours in the two. The control
            // cannot convert -- `colorToString` writes the numbers back under
            // the space they arrived with -- so the least it can do is name the
            // space it is not honouring.
            // The grey spaces carry two components, the RGB spaces four, and the
            // count is a property of the space -- so the control follows the
            // value rather than normalising it (Values.h).
            const float control = c.count == 4 ? kNumboxWidth : numboxesWidth(2);
            const float fixed = control + (removable ? button / k + 4.0f : 0.0f);
            // The long name of the space where it fits beside the control, the
            // short one where it does not: in a narrow inspector "Extended sRGB"
            // was cut in the middle of the word.
            const float room = ui::rowAvail() - (fixed + 6.0f) * k;
            char label[64];
            auto spell = [&](const char* space) {
                if (ramp) {
                    std::snprintf(label, sizeof label, "Stop %d · %s", static_cast<int>(i) + 1, space);
                } else {
                    std::snprintf(label, sizeof label, "Color · %s", space);
                }
            };
            spell(colorSpaceLabel(c.space));
            if (ImGui::CalcTextSize(label).x > room) spell(colorSpaceShortLabel(c.space));
            const char* id = ui::leftLabelFixed(label, fixed);
            if (removable) {
                // To the LEFT of the control, so the control stays in its column.
                if (ui::plainButton("##remove", "minus", ImVec2(button, button), 12.0f, "-", true,
                                    "Remove this stop")) {
                    remove = i;
                }
                ImGui::SameLine(0.0f, 4.0f * k);
                ImGui::SetNextItemWidth(std::min(control * k, ui::rowAvail()));
            }
            if (c.count == 4) {
                float rgba[4] = {static_cast<float>(c.components[0]), static_cast<float>(c.components[1]),
                                 static_cast<float>(c.components[2]), static_cast<float>(c.components[3])};
                char tip[256];
                std::snprintf(tip, sizeof tip,
                              "Components are stored in %s and written back in it. The picker and this "
                              "swatch are sRGB, so a wide-gamut stop draws here duller than the canvas "
                              "draws it.",
                              colorSpaceLabel(c.space));
                bool done = false;
                if (ui::colorWell(id, rgba, &done, tip)) {
                    for (int n = 0; n < 4; ++n) c.components[n] = rgba[n];
                    edited = i;
                }
                released |= done;
            } else {
                double ga[2] = {c.components[0], c.components[1]};
                static const double kZero = 0.0, kOne = 1.0;
                // "%.5f": `[ART]` a colour component is written with five places
                // in 1,978 of the corpus's 1,978 (spec 13/09 sec. 2.2), so the
                // control shows exactly the precision the file will keep.
                NumberEdit e = dragNumbersAt(id, ga, 2, 0.005f, &kZero, &kOne, "%.5f",
                                             "Luminance and alpha -- the grey spaces carry two "
                                             "components, not four.");
                if (e.changed) {
                    c.components[0] = ga[0];
                    c.components[1] = ga[1];
                    edited = i;
                }
                released |= e.released;
            }
            ImGui::PopID();
        }
        if (edited) writeColor(x, v, f, *edited, f.colors[*edited]);
        if (ramp) {
            if (buttonRow("Stops", "Add stop", "Appends a copy of the last stop")) append = true;
            if (f.colors.size() != 2) {
                // `[BIN]` Said out loud rather than prevented. The document may
                // hold a ramp of any length; the target's converter reads two
                // named slots and `FillResolve` refuses any other count rather
                // than truncating it, so this layer stops drawing until the
                // ramp is two again.
                char text[96];
                std::snprintf(text, sizeof text, "%d stops: the renderer draws only two-colour ramps.",
                              static_cast<int>(f.colors.size()));
                ui::note(text, ImVec4(0.95f, 0.65f, 0.25f, 1.0f));
            }
        }

        // ---- the axis ----
        bool giveAxis = false, dropAxis = false;
        if (gradient) {
            if (!f.orientation) {
                // Offered for the ramp only. An `automatic-gradient` is the one
                // kind no converter of the four reads an orientation from, so a
                // button here would only add a member with nowhere to be read.
                // The 8 corpus `automatic-gradient` fills that DO name one still
                // get the controls below -- what is already written is shown.
                if (ramp && buttonRow("Axis", "Give it its own axis",
                                      "Writes the default axis down, so it can then be moved")) {
                    giveAxis = true;
                }
                // THE ABSENCE IS THE VALUE, and this is the line that says so
                // instead of quietly writing one.
                ui::note("Default axis, (0, 0) -> (0, 1).");
                ImGui::SetItemTooltip(
                    "This fill names no orientation, and the absence has a meaning: the draw path "
                    "substitutes GradientPlacement.default, (0,0) to (0,1). Nothing here writes an "
                    "orientation unless it is asked to.");
            } else {
                // Normalised over the box, NOT points: the draw path reads
                // `point = rect.origin + unit * (width, height)` (doc 03 §30.7).
                // Unbounded on purpose -- the corpus has a stop.y of 1.029, and
                // a clamp here would silently rewrite that document.
                icf::Orientation want = *f.orientation;
                double start[2] = {want.start.x, want.start.y};
                double stop[2] = {want.stop.x, want.stop.y};
                // The axis is the clearest case in the panel for TYPING: the
                // values that mean something here are (0,0), (0,1), (0.5,0.5)
                // and (1,1), and a 0.005-per-pixel drag reaches none of them on
                // purpose. Unbounded, still: the corpus has a stop.y of 1.029.
                const char* kAxisHint =
                    "Normalised over the layer's box, not points: the draw path reads "
                    "origin + unit * (width, height) (doc 03 sec. 30.7). Deliberately not "
                    "clamped -- the corpus carries a stop.y of 1.029.";
                NumberEdit a = dragNumbers("Start (x, y)", start, 2, 0.005f, nullptr, nullptr,
                                           "%.6f", kAxisHint);
                NumberEdit b = dragNumbers("Stop (x, y)", stop, 2, 0.005f, nullptr, nullptr, "%.6f",
                                           kAxisHint);
                const bool moved = a.changed || b.changed;
                released |= a.released || b.released;
                if (moved) {
                    want.start = {start[0], start[1]};
                    want.stop = {stop[0], stop[1]};
                    writeAxis(x, v, f, want);
                }
                if (buttonRow("Axis", "Back to the default",
                              "Removes the orientation member, giving back the absence the document "
                              "may have arrived with")) {
                    dropAxis = true;
                }
            }
            // Where the target throws the axis away (doc 03 §30.8): only the
            // LAYER's `linear-gradient` builds a placement from it. Saying so
            // here is cheaper than a person wondering why the canvas ignores a
            // control that works.
            if (f.kind == icf::FillKind::AutomaticGradient) {
                ui::note("An automatic-gradient always draws on the default axis.");
            } else if (kindOf(x.path) == NodeKind::Root) {
                ui::note("A background gradient always draws on the default axis.");
            }
        }

        if (remove || append) writeRamp(x, v, f, remove, append);
        if (giveAxis) setAxisPresence(x, v, f, true);
        if (dropAxis) setAxisPresence(x, v, f, false);
        if (released) x.s.endCoalescing();
    }
    x.end();
}

void shadow(Section& x) {
    static const icf::ShadowKind kKinds[] = {icf::ShadowKind::Automatic, icf::ShadowKind::Neutral,
                                             icf::ShadowKind::LayerColor, icf::ShadowKind::None};
    PropertyView v;
    if (x.begin("Shadow", "shadow", v)) {
        icf::Shadow sh;
        if (v.value) {
            if (auto read = icf::shadowFrom(*v.value)) sh = *read;
        }
        if (ui::combo("Kind", shadowKindLabel(sh.kind))) {
            for (auto k : kKinds) {
                if (ui::menuItem(shadowKindLabel(k), nullptr, k == sh.kind)) {
                    icf::Shadow next = sh;
                    next.kind = k;
                    x.write("shadow", icf::shadowToJson(next), false);
                }
            }
            ImGui::EndCombo();
        }
        double op = sh.opacity;
        NumberEdit e = sliderNumber("Opacity", &op, 0.0, 1.0, "%.3f",
                                    "Shadow opacity. `none` draws no shadow whatever this says.");
        if (e.changed) {
            sh.opacity = op;
            x.write("shadow", icf::shadowToJson(sh), true);
        }
        if (e.released) x.s.endCoalescing();
    }
    x.end();
}

void translucency(Section& x) {
    PropertyView v;
    if (x.begin("Translucency", "translucency", v)) {
        icf::Translucency t;
        if (v.value) {
            if (auto read = icf::translucencyFrom(*v.value)) t = *read;
        }
        if (ui::toggle("Enabled", &t.enabled)) x.write("translucency", icf::translucencyToJson(t), false);
        double val = t.value;
        NumberEdit e = sliderNumber("Value", &val, 0.0, 1.0, "%.3f",
                                    "Translucency amount. The `Enabled` box above and this number "
                                    "are two members of one value, and both are written together.");
        if (e.changed) {
            t.value = val;
            x.write("translucency", icf::translucencyToJson(t), true);
        }
        if (e.released) x.s.endCoalescing();
    }
    x.end();
}

void specular(Section& x) {
    static const icf::SpecularHighlight kCases[] = {icf::SpecularHighlight::Off, icf::SpecularHighlight::Automatic,
                                                    icf::SpecularHighlight::Inside, icf::SpecularHighlight::Outside};
    PropertyView v;
    if (x.begin("Specular", "specular", v)) {
        icf::SpecularHighlight current = icf::SpecularHighlight::Automatic;
        if (v.value && v.value->kind() == icf::json::Value::Kind::String) {
            if (auto read = icf::specularHighlightFromString(v.value->rawString())) current = *read;
        }
        if (ui::combo("Highlight", specularLabel(current))) {
            for (auto c : kCases) {
                if (ui::menuItem(specularLabel(c), nullptr, c == current)) {
                    x.write("specular", icf::json::Value::string(std::string(icf::specularHighlightToString(c))),
                            false);
                }
            }
            ImGui::EndCombo();
        }
    }
    x.end();
}

void glass(Section& x) {
    PropertyView v;
    if (x.begin("Liquid Glass", "glass", v)) {
        bool on = booleanOr(v.value, false);
        if (ui::toggle("Glass", &on)) x.write("glass", icf::json::Value::boolean(on), false);
    }
    x.end();
}

// One selector for the whole panel, not one per section: the scope is a property
// of what is being inspected, and repeating it eight times only multiplies the
// chance of two sections disagreeing about which scope is being edited.
//
// THE SCOPE AND THE VIEW ARE TWO DIFFERENT PAIRS, AND THE PANEL USED TO SHOW ONE
// -----------------------------------------------------------------------------
// `Session::view.context` is what the CANVAS renders; `Session::scope` is what
// this panel WRITES. They are deliberately independent -- a person edits Dark
// while looking at Light on purpose, and `Session::open` moves the view to the
// declared idiom while leaving the scope on Base on purpose (ViewModel.h, and
// the case `kit_canvas_opens_on_the_idiom_the_document_declares` fixes that).
//
// The defect was that the panel drew only `scope`, in two unlabelled 100 px
// combos, and never mentioned `view` at all. On any document that declares
// `squares` -- `[ART]` 145 of 145 -- the two disagree from the very first frame:
// the canvas is on `square`, the inspector writes the plain key, and the person
// is looking at one composition and editing another with nothing on screen
// saying so. That is exactly the lie this file's own header says the panel
// exists to prevent, and it was being told by the panel.
//
// So the two combos are labelled, the disagreement is stated in words when there
// is one, and there is one button that ends it.
void scopeSelector(Session& s) {
    static const icf::Appearance kA[] = {icf::Appearance::Base, icf::Appearance::Light, icf::Appearance::Dark,
                                         icf::Appearance::Tinted};
    static const icf::Idiom kI[] = {icf::Idiom::Base, icf::Idiom::Square, icf::Idiom::IOS, icf::Idiom::MacOS,
                                    icf::Idiom::WatchOS};
    // A caixa do escopo, no mesmo desenho das secoes: duas linhas, e uma
    // terceira so quando o canvas e o inspetor discordam.
    ui::sectionHead("Editing scope##scope-head");
    const bool same = s.scope.appearance == s.view.context.appearance &&
                      s.scope.idiom == s.view.context.idiom;
    if (same) ui::sectionStatus("matches the canvas", theme::kText3);
    ui::boxBegin();
    if (ui::combo("Appearance##scope-a", appearanceLabel(s.scope.appearance))) {
        for (auto a : kA) {
            if (ui::menuItem(appearanceLabel(a), nullptr, a == s.scope.appearance)) s.scope.appearance = a;
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip(
        "The appearance every section below reads and WRITES under. Base is the plain key; any "
        "other value appends to the property's specialization list.");
    if (ui::combo("Idiom##scope-i", idiomLabel(s.scope.idiom))) {
        for (auto i : kI) {
            if (ui::menuItem(idiomLabel(i), nullptr, i == s.scope.idiom)) s.scope.idiom = i;
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip(
        "The idiom every section below reads and WRITES under. This is NOT the idiom the canvas "
        "draws -- that one is in the canvas's own bar, and the line below says when the two "
        "disagree.");

    if (!same) {
        // A warm amber, the colour this editor already uses for "true, and you
        // need to know it" (the ramp-length note).
        ui::rowStart();
        char canvas[96];
        std::snprintf(canvas, sizeof canvas, "Canvas shows %s / %s",
                      appearanceLabel(s.view.context.appearance), idiomLabel(s.view.context.idiom));
        const float avail = ui::rowAvail();
        const float startX = ImGui::GetCursorPosX();
        const char* kMatch = "Edit this";
        const float bw = ImGui::CalcTextSize(kMatch).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        // A frase para a 8 pt do botao: na coluna estreita ela corria por baixo
        // dele. O que foi cortado esta inteiro no tooltip.
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushClipRect(p, ImVec2(p.x + std::max(0.0f, avail - bw - 8.0f * ui::dpi()),
                                      p.y + ImGui::GetFrameHeight()), true);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1.0f), "%s", canvas);
        ImGui::PopClipRect();
        ImGui::SetItemTooltip(
            "The canvas is showing %s / %s -- you are editing %s / %s.\nLegitimate -- editing one "
            "scope while looking at another is a real thing to want. But an edit made here may not "
            "move a pixel on screen, and that is a different fact from the edit not having happened.",
            appearanceLabel(s.view.context.appearance), idiomLabel(s.view.context.idiom),
            appearanceLabel(s.scope.appearance), idiomLabel(s.scope.idiom));
        ImGui::SameLine(startX + avail - bw);
        if (ImGui::Button(kMatch)) {
            // Only the SCOPE moves. Pulling the canvas to the scope instead
            // would drag the view off the idiom the document declares, which is
            // the one thing `Session::open` went out of its way to get right.
            s.scope = s.view.context;
        }
        ImGui::SetItemTooltip("Sets the editing scope to the appearance and idiom the canvas is drawing.");
    }
    ui::boxEnd();
}

}  // namespace

// THE DOCUMENT HAD NO DOOR
// -----------------------------------------------------------------------------
// `drawInspector` has always had a `NodeKind::Root` branch -- the background
// `fill`, and Platforms, SVG Color Space and Features from
// PanelInspectorDocument.cpp -- and `nodeTitle` has always answered "Document"
// for a path with no group. Nothing in the running editor could ever produce
// that path: the Layers tree lists groups and layers and no root row,
// `Session::selection` starts empty, and no menu item sets it. So four sections
// of this panel, including the only control over `supported-platforms` -- the
// key `[ART]` 145 of 145 corpus documents carry, and the one that decides
// whether the icon is a squircle or a circle -- were unreachable in the built
// program while being fully written, tested and drawn in the selftest (which
// selects through `firstSelectable`, a group).
//
// The row belongs in the Layers tree, where the rest of the document's structure
// is; a front is in that file this round, so the door is opened here instead,
// from the panel the sections are in. It costs one line and stops being dead
// code the moment it is drawn.
// O titulo do que esta selecionado, e no fim da linha a porta do documento.
void titleRow(Session& s, const std::string& title, bool dim, bool isRoot) {
    const float k = ui::dpi();
    const float startX = ImGui::GetCursorPosX();
    const float avail = ImGui::GetContentRegionAvail().x;
    const char* kDoc = "Document";
    const float bw = isRoot ? 0.0f : ImGui::CalcTextSize(kDoc).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    // O nome para a 8 pt do botao: um nome de camada comprido e cortado ali, em
    // vez de correr por baixo dele na coluna estreita.
    const float nameEnd = ImGui::GetCursorScreenPos().x + avail - (isRoot ? 0.0f : bw + 8.0f * k);
    ImGui::PushFont(nullptr, 15.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::SetCursorPosX(startX + 10.0f * k);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushClipRect(p, ImVec2(std::max(p.x, nameEnd), p.y + ImGui::GetFrameHeight()), true);
    ImGui::PushStyleColor(ImGuiCol_Text, dim ? theme::kText3 : theme::kText);
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopStyleColor();
    ImGui::PopClipRect();
    ImGui::PopFont();
    if (!isRoot) {
        ImGui::SameLine(startX + avail - bw);
        if (ImGui::Button(kDoc)) s.selection = icf::NodePath{};
        ImGui::SetItemTooltip(
            "The root of the .icon: the background fill, the platforms it ships for, the SVG colour "
            "space and the feature list. Selecting a layer or a group in the Layers panel comes back "
            "here.");
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f * k));
}

InspectorStats drawInspector(Session& s, MenuActions& actions) {
    InspectorStats st;
    if (!ImGui::Begin(kInspectorWindow)) {
        ImGui::End();
        return st;
    }
    // O TOPO DA COLUNA, 52 pt na altura da barra de titulo (`.sidebar-top`,
    // `.inspector-top` do Tauri). Vazio aqui: na sidebar o app desenha as luzes
    // nele; e a faixa por onde a janela se arrasta.
    ImGui::Dummy(ImVec2(1.0f, theme::kTitleBarH * ui::dpi() - ImGui::GetStyle().WindowPadding.y));
    // The selection names a node by index, so a structural edit can leave it
    // pointing past the end; the panel checks the node rather than the index.
    //
    // NOTHING SELECTED IS THE DOCUMENT. Until 2026-10-01 this panel said
    // "Nothing selected" and stopped, and the root -- the background `fill`
    // among it -- was behind the "Document" button alone: a new, empty document
    // opened on a white chiclet with no visible way to change it. The root is
    // what there is to inspect when no group or layer is picked, so it is shown
    // without touching `selection`.
    const bool selected = s.selection && icf::nodeAt(s.root(), *s.selection);
    const icf::NodePath path = selected ? *s.selection : icf::NodePath{};
    st.title = nodeTitle(s, path);
    titleRow(s, st.title, false, !path.group);
    scopeSelector(s);

    Section x{s, path, st, actions};
    // Only the properties `Values.h` types and the renderer already consumes get
    // a live section (spec 13/09 §7); the rest are named and greyed.
    switch (kindOf(path)) {
        case NodeKind::Root:
            fill(x);
            drawDocumentSections(x);
            break;
        case NodeKind::Group:
            visible(x);
            opacity(x);
            blendMode(x);
            geometry(x);
            shadow(x);
            translucency(x);
            specular(x);
            drawGroupEffectSections(x);
            // Not an effect and not the layer's: `asset-mirroring` is declared
            // on both snapshots (laudo 19/09 §3.2), so it is drawn at both
            // levels from the one place it is written.
            drawAssetMirroringSection(x);
            break;
        case NodeKind::Layer:
            visible(x);
            opacity(x);
            blendMode(x);
            geometry(x);
            fill(x);
            glass(x);
            drawLayerAssetSections(x);
            break;
    }
    ImGui::End();
    return st;
}

}  // namespace ick
