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

#include "Source/IconComposerFoundation/Values.h"
#include "Source/IconComposerKit/ViewModel.h"
#include "imgui.h"

#include <cstddef>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ick {
namespace {

// Numbers keep their source lexeme in the tree (Json.h), so a control reads one
// back through `strtod` and writes it through `json::Value::number(double)`,
// which is the shortest round-tripping form Apple's encoder writes (spec 13/09
// §2.2). A property the node does not carry falls back to the renderer's
// default rather than to zero.



void visible(Section& x) {
    PropertyView v;
    if (x.begin("Visible", "hidden", v)) {
        bool on = !booleanOr(v.value, false);
        if (ImGui::Checkbox("Visible", &on)) x.write("hidden", icf::json::Value::boolean(!on), false);
    }
    x.end();
}

void opacity(Section& x) {
    PropertyView v;
    if (x.begin("Opacity", "opacity", v)) {
        float f = static_cast<float>(numberOr(v.value, 1.0));
        if (ImGui::SliderFloat("##opacity", &f, 0.0f, 1.0f, "%.2f")) {
            x.write("opacity", icf::json::Value::number(static_cast<double>(f)), true);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) x.s.endCoalescing();
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
        if (ImGui::BeginCombo("##blend", blendModeLabel(current))) {
            for (auto m : kModes) {
                if (ImGui::Selectable(blendModeLabel(m), m == current)) {
                    x.write("blend-mode", icf::json::Value::string(std::string(icf::blendModeToString(m))), false);
                }
            }
            ImGui::EndCombo();
        }
    }
    x.end();
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
        float xy[2] = {static_cast<float>(p.translation.x), static_cast<float>(p.translation.y)};
        float scale = static_cast<float>(p.scale * 100.0);
        bool changed = ImGui::DragFloat2("Position (pt)", xy, 1.0f);
        bool released = ImGui::IsItemDeactivatedAfterEdit();
        changed |= ImGui::DragFloat("Scale (%)", &scale, 1.0f, 1.0f, 1000.0f);
        released |= ImGui::IsItemDeactivatedAfterEdit();
        if (changed) {
            p.translation = {xy[0], xy[1]};
            p.scale = scale / 100.0;
            x.write("position", icf::positionToJson(p), true);
        }
        if (released) x.s.endCoalescing();
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
        if (ImGui::BeginCombo("##kind", fillKindLabel(f.kind))) {
            for (auto k : kKinds) {
                if (ImGui::Selectable(fillKindLabel(k), k == f.kind)) {
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
                    x.write("fill", icf::fillToJson(next), false);
                }
            }
            ImGui::EndCombo();
        }
        // The ramp is edited in place, never grown: adding a stop is not this
        // round's (spec 13/09 §9).
        bool changed = false, released = false;
        for (std::size_t i = 0; i < f.colors.size(); ++i) {
            icf::Color& c = f.colors[i];
            ImGui::PushID(static_cast<int>(i));
            // The grey spaces carry two components, the RGB spaces four, and the
            // count is a property of the space -- so the control follows the
            // value rather than normalising it (Values.h).
            if (c.count == 4) {
                float rgba[4] = {static_cast<float>(c.components[0]), static_cast<float>(c.components[1]),
                                 static_cast<float>(c.components[2]), static_cast<float>(c.components[3])};
                if (ImGui::ColorEdit4("##colour", rgba, ImGuiColorEditFlags_Float)) {
                    for (int k = 0; k < 4; ++k) c.components[k] = rgba[k];
                    changed = true;
                }
            } else {
                float ga[2] = {static_cast<float>(c.components[0]), static_cast<float>(c.components[1])};
                if (ImGui::DragFloat2("gray, alpha", ga, 0.01f, 0.0f, 1.0f)) {
                    c.components[0] = ga[0];
                    c.components[1] = ga[1];
                    changed = true;
                }
            }
            released |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::PopID();
        }
        if (changed) x.write("fill", icf::fillToJson(f), true);
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
        if (ImGui::BeginCombo("##kind", shadowKindLabel(sh.kind))) {
            for (auto k : kKinds) {
                if (ImGui::Selectable(shadowKindLabel(k), k == sh.kind)) {
                    icf::Shadow next = sh;
                    next.kind = k;
                    x.write("shadow", icf::shadowToJson(next), false);
                }
            }
            ImGui::EndCombo();
        }
        float op = static_cast<float>(sh.opacity);
        if (ImGui::SliderFloat("Opacity", &op, 0.0f, 1.0f, "%.2f")) {
            sh.opacity = op;
            x.write("shadow", icf::shadowToJson(sh), true);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) x.s.endCoalescing();
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
        if (ImGui::Checkbox("Enabled", &t.enabled)) x.write("translucency", icf::translucencyToJson(t), false);
        float val = static_cast<float>(t.value);
        if (ImGui::SliderFloat("Value", &val, 0.0f, 1.0f, "%.2f")) {
            t.value = val;
            x.write("translucency", icf::translucencyToJson(t), true);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) x.s.endCoalescing();
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
        if (ImGui::BeginCombo("##specular", specularLabel(current))) {
            for (auto c : kCases) {
                if (ImGui::Selectable(specularLabel(c), c == current)) {
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
        if (ImGui::Checkbox("Glass", &on)) x.write("glass", icf::json::Value::boolean(on), false);
    }
    x.end();
}

// One selector for the whole panel, not one per section: the scope is a property
// of what is being inspected, and repeating it eight times only multiplies the
// chance of two sections disagreeing about which scope is being edited.
void scopeSelector(Session& s) {
    static const icf::Appearance kA[] = {icf::Appearance::Base, icf::Appearance::Light, icf::Appearance::Dark,
                                         icf::Appearance::Tinted};
    static const icf::Idiom kI[] = {icf::Idiom::Base, icf::Idiom::Square, icf::Idiom::IOS, icf::Idiom::MacOS,
                                    icf::Idiom::WatchOS};
    ImGui::TextUnformatted("Scope");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::BeginCombo("##scope-a", appearanceLabel(s.scope.appearance))) {
        for (auto a : kA) {
            if (ImGui::Selectable(appearanceLabel(a), a == s.scope.appearance)) s.scope.appearance = a;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::BeginCombo("##scope-i", idiomLabel(s.scope.idiom))) {
        for (auto i : kI) {
            if (ImGui::Selectable(idiomLabel(i), i == s.scope.idiom)) s.scope.idiom = i;
        }
        ImGui::EndCombo();
    }
}

}  // namespace

InspectorStats drawInspector(Session& s) {
    InspectorStats st;
    if (!ImGui::Begin(kInspectorWindow)) {
        ImGui::End();
        return st;
    }
    // The selection names a node by index, so a structural edit can leave it
    // pointing past the end; the panel checks the node rather than the index.
    if (!s.selection || !icf::nodeAt(s.root(), *s.selection)) {
        st.title = "Nothing selected";
        ImGui::TextDisabled("%s", st.title.c_str());
        ImGui::End();
        return st;
    }
    const icf::NodePath path = *s.selection;
    st.title = nodeTitle(s, path);
    ImGui::TextUnformatted(st.title.c_str());
    scopeSelector(s);
    ImGui::Separator();

    Section x{s, path, st};
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
