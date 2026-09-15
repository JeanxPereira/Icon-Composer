#pragma once
// The Inspector's shared machinery, so a section can live in its own file.
//
// `Section` is the contract every inspector section is written against: it opens
// the header, resolves the property under the panel's ONE scope, counts the
// frame's measurements into InspectorStats, draws the "inherited"/"not set"
// silence and the "Remove override" button, and routes every write through the
// Session so undo stays honest and the canvas's version keeps moving.
//
// Sections were all in PanelInspector.cpp while there were seven of them. They
// moved out when the file became the meeting point of several people at once:
// one file per family is what lets them be written side by side.
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/Session.h"
#include "Source/IconComposerKit/ViewModel.h"
#include "Source/IconComposerFoundation/Edit.h"
#include "Source/IconComposerFoundation/Values.h"
#include "imgui.h"

#include <string_view>

namespace ick {

// `NodeKind` and `kindOf` live in ViewModel.h, included above.

inline double numberOr(const icf::json::Value* v, double fallback) {
    if (!v || v->kind() != icf::json::Value::Kind::Number) return fallback;
    return std::strtod(v->number().c_str(), nullptr);
}

inline bool booleanOr(const icf::json::Value* v, bool fallback) {
    if (!v || v->kind() != icf::json::Value::Kind::Bool) return fallback;
    return v->boolean();
}

struct Section {
    Session& s;
    icf::NodePath path;
    InspectorStats& st;

    // Opens a section for `prop`; returns the view and whether the body draws.
    bool begin(const char* label, std::string_view prop, PropertyView& view) {
        view = viewProperty(s, path, prop);
        ImGui::PushID(label);
        const bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
        ++st.sections;
        if (!view.own) ++st.inherited;
        if (open) {
            if (!view.own) {
                // Two different silences: a value inherited from a more general
                // scope, and a property nobody has written anywhere.
                ImGui::TextDisabled(view.value ? "inherited" : "not set");
            } else if (s.scope.appearance != icf::Appearance::Base || s.scope.idiom != icf::Idiom::Base) {
                if (ImGui::SmallButton("Remove override")) {
                    s.setProperty(path, prop, s.scope, std::nullopt);
                }
            }
        }
        return open;
    }

    void end() { ImGui::PopID(); }

    void disabled(const char* label, const char* why) {
        ImGui::BeginDisabled();
        ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_Leaf);
        ImGui::EndDisabled();
        // The default tooltip hover flags carry AllowWhenDisabled, so the reason
        // still reaches a greyed header.
        ImGui::SetItemTooltip("%s", why);
        ++st.disabled;
    }

    // Every mutation goes through the Session, never through `root()`: that is
    // what keeps undo honest and the canvas's version moving (Session.h).
    // Ending a coalesced drag is the caller's, because a section built from
    // several controls only knows it was released after it has drawn them all.
    void write(std::string_view prop, icf::json::Value v, bool coalesce) {
        s.setProperty(path, prop, s.scope, std::move(v), coalesce);
    }
};

// ---- the families, one file each ------------------------------------------
// Built and live:
void drawCommonSections(Section& x, NodeKind kind);   // PanelInspector.cpp
// Round 3, each in its own translation unit:
void drawLayerAssetSections(Section& x);              // PanelInspectorAsset.cpp
void drawGroupEffectSections(Section& x);             // PanelInspectorEffects.cpp
void drawDocumentSections(Section& x);                // PanelInspectorDocument.cpp

}  // namespace ick
