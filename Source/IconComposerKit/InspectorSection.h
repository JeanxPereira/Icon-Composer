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

#include <cstdio>
#include <initializer_list>
#include <string>
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

// ---- A NUMBER THE PERSON CAN DRAG *OR TYPE* ---------------------------------
// `[BIN]` The target types its numbers. In `IconComposerKit.arm64`, 13 symbols
// name `IconComposerKit.MultiNumericTextField`, 11 name `InspectorTextFieldStyle`
// and its `InlineLabel` case, 2 name `BufferedTextField`, and 4 name
// `Scrubbable`: a number in that inspector is a TEXT FIELD with an inline label
// you may also drag, not a slider you may also type into.
//
// ImGui's Drag and Slider do accept a typed value, but only behind Ctrl+click,
// which this editor never said anywhere and which almost nobody finds. For the
// values a person wants EXACT -- a scale of 80%, a gradient axis of 0.5, a
// translation of -25 pt -- that is the difference between a control and a
// guess. So every continuous control in the inspector goes through the two
// helpers below, and each does three things no call site has to remember:
//
//   1. Says "Ctrl+click to type" on the control itself, which is the only place
//      that affordance can be learned.
//   2. Passes `AlwaysClamp`, so a TYPED value obeys the same limits the drag
//      does. Without it ImGui lets a typed number past min/max and the two ways
//      of setting one property disagree about what the property can be.
//   3. Reads `IsItemDeactivatedAfterEdit` BEFORE the tooltip is submitted.
//      `SetItemTooltip` opens a tooltip window and writes text into it, and the
//      "last item" from then on is that text and not the control -- so a caller
//      that tooltips first and asks afterwards never sees the drag end, never
//      ends the coalescing, and leaves the drag's command open for whatever
//      edit comes next to fold into.
//
// DOUBLES, NOT FLOATS. The document's numbers are doubles and `[ART]` the corpus
// spells coordinates like `0.5000000000000001`; a float round trip rewrites a
// lexeme nobody touched. That is the rule `writeAxis` in PanelInspector.cpp is
// written against, applied to every other number in the panel too.
struct NumberEdit {
    bool changed = false;    // the value moved this frame
    bool released = false;   // the drag ended this frame -- time to end coalescing
};

inline void numberHint(const char* what) {
    ImGui::SetItemTooltip("%s\nDrag to scrub; Ctrl+click (or double-click) to type an exact value.",
                          what);
}

// `lo`/`hi` may be null, and that is a real case rather than laziness: the
// gradient axis is normalised over the box but NOT clamped -- the corpus has a
// stop.y of 1.029, and a clamp here would silently rewrite that document.
inline NumberEdit dragNumbers(const char* label, double* v, int count, float speed, const double* lo,
                              const double* hi, const char* fmt, const char* what) {
    NumberEdit e;
    e.changed = ImGui::DragScalarN(label, ImGuiDataType_Double, v, count, speed, lo, hi, fmt,
                                   ImGuiSliderFlags_AlwaysClamp);
    e.released = ImGui::IsItemDeactivatedAfterEdit();
    numberHint(what);
    return e;
}

inline NumberEdit sliderNumber(const char* label, double* v, double lo, double hi, const char* fmt,
                               const char* what) {
    NumberEdit e;
    e.changed = ImGui::SliderScalar(label, ImGuiDataType_Double, v, &lo, &hi, fmt,
                                    ImGuiSliderFlags_AlwaysClamp);
    e.released = ImGui::IsItemDeactivatedAfterEdit();
    numberHint(what);
    return e;
}

// ---- WHERE ELSE THIS PROPERTY IS WRITTEN ------------------------------------
// `[BIN]` The target does not show one scope at a time. `SpecializablePropertyInspector`
// (31 symbols) is built over `SpecializationSlice` and `EnumeratedSpecializationSlice`,
// and it carries an `AllVariantsContextMenu` -- the whole point of which is that
// a person editing one variant can see that the others exist.
//
// This panel showed exactly one scope and said nothing about the rest, and that
// is the failure the user actually hit: the canvas opens on the idiom the
// document declares (`[ART]` 145 of 145 declare one) while the inspector opens
// on Base, so the value on screen is very often coming from a specialization the
// panel never mentions. Moving the control then edits Base, which changes a
// number nobody is looking at.
//
// There is no enumeration API on the document -- `Edit.h` answers only
// `hasOwnEntry(node, prop, scope)` -- so the twenty scopes are simply asked, one
// at a time. Twenty lookups per section per frame is nothing next to the render
// they sit beside, and it is the honest answer rather than a cached one that can
// go stale against an edit.
struct OtherScopes {
    int count = 0;
    std::string list;
};

inline OtherScopes otherScopesOwning(const Session& s, icf::NodePath path, std::string_view prop) {
    OtherScopes r;
    const icf::json::Value* node = icf::nodeAt(s.root(), path);
    if (!node) return r;
    for (auto a : {icf::Appearance::Base, icf::Appearance::Light, icf::Appearance::Dark,
                   icf::Appearance::Tinted}) {
        for (auto i : {icf::Idiom::Base, icf::Idiom::Square, icf::Idiom::IOS, icf::Idiom::MacOS,
                       icf::Idiom::WatchOS}) {
            if (a == s.scope.appearance && i == s.scope.idiom) continue;   // the one on screen
            if (!icf::hasOwnEntry(*node, prop, icf::Context{a, i})) continue;
            ++r.count;
            if (!r.list.empty()) r.list += ", ";
            r.list += appearanceLabel(a);
            r.list += " / ";
            r.list += idiomLabel(i);
        }
    }
    return r;
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
        // O inventario, gravado aqui porque aqui e o unico caminho (Panels.h,
        // `SectionInfo`).
        st.drawn.push_back(SectionInfo{label, std::string(prop), view.own, open});
        if (open) {
            // Said BEFORE the value, and whether or not this scope owns one: a
            // person about to move a control needs to know that the number under
            // his hand is not the only one this property has.
            const OtherScopes others = otherScopesOwning(s, path, prop);
            if (others.count > 0) {
                ImGui::TextDisabled("also written under %d other scope%s", others.count,
                                    others.count == 1 ? "" : "s");
                ImGui::SetItemTooltip(
                    "This property has its own entry under: %s.\nEditing here changes only "
                    "%s / %s; the others keep the values they have, and one of them may be what "
                    "the canvas is drawing.",
                    others.list.c_str(), appearanceLabel(s.scope.appearance),
                    idiomLabel(s.scope.idiom));
            }
        }
        if (open) {
            if (!view.own) {
                // Two different silences: a value inherited from a more general
                // scope, and a property nobody has written anywhere.
                //
                // AND WHAT THE NEXT EDIT WILL DO, WHICH IS THE PART THAT WAS
                // MISSING. Touching a control here does not change a number in
                // place: it ADDS an entry the document did not have -- a plain
                // key under Base, an entry in `<prop>-specializations` under any
                // other scope. That is a byte the round-trip gate compares, and
                // the person moving the slider is the one who has to know it is
                // about to happen. The scope is named in the line, so the answer
                // does not depend on reading the selector above it.
                const bool base = s.scope.appearance == icf::Appearance::Base &&
                                  s.scope.idiom == icf::Idiom::Base;
                ImGui::TextDisabled("%s -- editing writes %s", view.value ? "inherited" : "not set",
                                    base ? "the plain key" : "a new override here");
                if (base) {
                    ImGui::SetItemTooltip(
                        "No value of its own under Base. The first edit writes the plain key into "
                        "this node, which is a member the document did not carry.");
                } else {
                    ImGui::SetItemTooltip(
                        "This scope (%s / %s) has no entry of its own; the value shown is resolved "
                        "from a more general one. The first edit appends an entry to this "
                        "property's specialization list, which is a member the document did not "
                        "carry -- and 'Remove override' will then be here to take it back out.",
                        appearanceLabel(s.scope.appearance), idiomLabel(s.scope.idiom));
                }
            } else if (s.scope.appearance != icf::Appearance::Base || s.scope.idiom != icf::Idiom::Base) {
                // The scope is named on the button, not only in the selector at
                // the top: this button DELETES an entry, and a person about to
                // press it should not have to look somewhere else to find out
                // which one.
                char label[96];
                std::snprintf(label, sizeof label, "Remove %s / %s override",
                              appearanceLabel(s.scope.appearance), idiomLabel(s.scope.idiom));
                const bool pressed = ImGui::SmallButton(label);
                ImGui::SetItemTooltip(
                    "Deletes this scope's own entry for the property. The value then goes back to "
                    "being resolved from a more general scope, which is what the line above says "
                    "when there is no entry.");
                if (pressed) s.setProperty(path, prop, s.scope, std::nullopt);
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
