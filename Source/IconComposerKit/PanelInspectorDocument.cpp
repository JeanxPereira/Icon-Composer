// The Document family of inspector sections: what the ROOT of a `.icon` carries
// besides the background `fill`, which already has its own live section.
//
// WHAT THE ROOT ACTUALLY HAS, MEASURED OVER THE 145-DOCUMENT CORPUS
// -----------------------------------------------------------------------------
// `[BIN]` The root's keys, from `JSONContent.CodingKeys` (doc 01 §2): `features`,
// `groups`, `fill`/`fill-specializations`, `specializable-language-ids`,
// `color-space-for-untagged-svg-colors`, `supported-platforms`,
// `implicit-asset-mirroring`. `[ART]` What the corpus writes:
//
//   groups                                145/145   the Layers panel's, not this one
//   supported-platforms                   145/145   squares 145/145, circles 97/145
//   fill | fill-specializations           111 | 34  already live (PanelInspector.cpp)
//   color-space-for-untagged-svg-colors    20/145   always the string "display-p3"
//   features                                3/145   ["refractivity","specular-location"]
//   specializable-language-ids              0/145
//   implicit-asset-mirroring                0/145
//
// THERE IS NO CORNER RADIUS HERE, AND THAT IS THE ANSWER, NOT AN OMISSION
// -----------------------------------------------------------------------------
// "Shape" reads as the rounded corner, and the corner is not a field of the
// document. `[ART]` 145 documents carry ZERO radius or corner keys (laudo
// 15/09 §3.4); the two `*-corner*` hits in the corpus are layer NAMES an author
// chose. The geometry is the renderer's -- a squircle of `0.26 × canvas`, the
// constant baked into the binary -- and what the FORMAT chooses is binary:
// `squares` versus `circles`. So the shape control below is that choice, and a
// radius slider is deliberately absent: it would write a key Apple never writes,
// and the Diagnostics panel would report it back as unknown on the next frame.
//
// WHY THESE SECTIONS DO NOT GO THROUGH `Section::begin` / `Section::write`
// -----------------------------------------------------------------------------
// `Section` resolves and writes under `Session::scope`, which is exactly right
// for `fill` and wrong for every key here: none of them is specializable. `[BIN]`
// the binary declares no `<key>-specializations` sibling for them, and `[ART]`
// the corpus has none (0 of 145). Under a Dark scope `Section::write` would
// invent `supported-platforms-specializations` -- a key that does not exist --
// and `Section::begin` would label a root key "inherited", which is a lie about a
// value that has no more general scope to inherit from. So these open their own
// `CollapsingHeader` and address the plain key directly.
//
// The Session is NOT bypassed. Every write still goes through
// `Session::setProperty` under the Base context, so undo stays a snapshot of the
// node and `version()` still moves and the canvas still re-renders (Session.h).
// Nothing here writes through `root()`.
#include "Source/IconComposerKit/Widgets.h"
#include "Source/IconComposerKit/InspectorSection.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ick {
namespace {

// The plain key on the selected node, read const so the const `nodeAt` overload
// is the one chosen and no mutable path into the tree is ever opened here.
const icf::json::Value* rootValue(Section& x, std::string_view key) {
    const Session& s = x.s;
    const icf::json::Value* node = icf::nodeAt(s.root(), x.path);
    return node ? node->find(key) : nullptr;
}

// Base, never `x.s.scope`: see the header note.
void writeRoot(Section& x, std::string_view key, std::optional<icf::json::Value> v) {
    x.s.setProperty(x.path, key, icf::Context{}, std::move(v), false);
}

// Opens a section and counts it the way `Section::begin` does, minus the scope
// machinery these keys do not answer to.
bool header(Section& x, const char* label) {
    const bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
    ++x.st.sections;
    return open;
}

bool isString(const icf::json::Value* v, std::string_view text) {
    return v && v->kind() == icf::json::Value::Kind::String && v->rawString() == text;
}

// ---- supported-platforms ----------------------------------------------------
// `[ART]` The two members and their forms:
//   squares  string "shared" (117 of 145) | array (28): ["macOS"] 22, ["iOS"] 3,
//            ["iOS","macOS"] 3. Never absent, never empty, never watchOS.
//   circles  absent (48 of 145) | array (97), and all 97 are exactly ["watchOS"].
// So `circles` is a yes-or-no in practice, and `squares` is one key with two
// forms -- which is why one combo picks the FORM and the checkboxes only fill it.
struct Platforms {
    bool squaresShared = true;
    bool squaresIOS = false;
    bool squaresMacOS = false;
    bool circles = false;

    bool operator==(const Platforms&) const = default;
};

Platforms readPlatforms(const icf::json::Value* v) {
    Platforms p;
    if (!v || v->kind() != icf::json::Value::Kind::Object) return p;
    if (const icf::json::Value* sq = v->find("squares")) {
        if (sq->kind() == icf::json::Value::Kind::Array) {
            p.squaresShared = false;
            for (const auto& e : sq->elements()) {
                if (e.kind() != icf::json::Value::Kind::String) continue;
                if (e.rawString() == "iOS") p.squaresIOS = true;
                else if (e.rawString() == "macOS") p.squaresMacOS = true;
            }
        }
    }
    // Present at all means the round family is on: the corpus never writes any
    // other content for it.
    p.circles = v->find("circles") != nullptr;
    return p;
}

icf::json::Value platformsToJson(const Platforms& p) {
    std::vector<icf::json::Value::Member> m;
    if (p.circles) {
        std::vector<icf::json::Value> list;
        list.push_back(icf::json::Value::string("watchOS"));
        m.emplace_back("circles", icf::json::Value::array(std::move(list)));
    }
    if (p.squaresShared) {
        m.emplace_back("squares", icf::json::Value::string("shared"));
    } else {
        // `iOS` before `macOS`: that is the order in the three documents that
        // carry both. `json::write` sorts KEYS, never array elements, so the
        // order written here is the order saved.
        std::vector<icf::json::Value> list;
        if (p.squaresIOS) list.push_back(icf::json::Value::string("iOS"));
        if (p.squaresMacOS) list.push_back(icf::json::Value::string("macOS"));
        m.emplace_back("squares", icf::json::Value::array(std::move(list)));
    }
    return icf::json::Value::object(std::move(m));
}

void platforms(Section& x) {
    ImGui::PushID("Platforms");
    if (header(x, "Platforms")) {
        ImGui::SetItemTooltip(
            "The only shape choice the format has. The square family is drawn as a squircle and the "
            "round one as a circle; the corner radius belongs to the renderer, not to the document "
            "(0 of 145 documents carry a radius key).");
        const Platforms p = readPlatforms(rootValue(x, "supported-platforms"));
        Platforms next = p;

        static const char* kShared = "Shared";
        static const char* kSpecific = "Specific platforms";
        if (ImGui::BeginCombo(ui::leftLabel("Squares"), p.squaresShared ? kShared : kSpecific)) {
            if (ImGui::Selectable(kShared, p.squaresShared)) next.squaresShared = true;
            if (ImGui::Selectable(kSpecific, !p.squaresShared)) {
                next.squaresShared = false;
                // The list form is never empty in the corpus, so it starts at
                // the commonest one it can be: macOS alone, 22 of 28.
                if (!next.squaresIOS && !next.squaresMacOS) next.squaresMacOS = true;
            }
            ImGui::EndCombo();
        }
        if (!p.squaresShared) {
            ImGui::Indent();
            // A list of one cannot be emptied: 0 of 28 lists in the corpus are
            // empty, and an empty one would claim the icon ships for nothing.
            // The last box left standing is greyed rather than silently ignored.
            const bool onlyIOS = p.squaresIOS && !p.squaresMacOS;
            const bool onlyMacOS = p.squaresMacOS && !p.squaresIOS;
            ImGui::BeginDisabled(onlyIOS);
            ui::toggle("iOS", &next.squaresIOS);
            ImGui::EndDisabled();
            if (onlyIOS) ImGui::SetItemTooltip("The list cannot be empty; switch to Shared instead.");
            ImGui::BeginDisabled(onlyMacOS);
            ui::toggle("macOS", &next.squaresMacOS);
            ImGui::EndDisabled();
            if (onlyMacOS) ImGui::SetItemTooltip("The list cannot be empty; switch to Shared instead.");
            ImGui::Unindent();
        }

        ui::toggle("Circles (watchOS)", &next.circles);
        ImGui::SetItemTooltip(
            "The round family. Present in 97 of 145 documents, and in all 97 its value is exactly "
            "[\"watchOS\"] -- so the key is a yes or a no.");

        // One write for the whole object: `supported-platforms` is a single
        // property whose value happens to have two members, and writing it in
        // halves would record two undo steps for one decision.
        if (next != p) writeRoot(x, "supported-platforms", platformsToJson(next));
    }
    ImGui::PopID();
}

// ---- color-space-for-untagged-svg-colors ------------------------------------
// `[BIN]` `AssumedSVGColorSpace` has exactly one case, `displayP3`; `[ART]` the
// corpus writes the string "display-p3" in all 20 documents that carry the key.
// One case and one value means the control is the key's PRESENCE, and that is a
// checkbox. The disk spelling is the one exception to the kebab-case rule
// (doc 01 §1), so it is written literally and never derived.
void svgColorSpace(Section& x) {
    ImGui::PushID("SVG Color Space");
    if (header(x, "SVG Color Space")) {
        const icf::json::Value* v = rootValue(x, "color-space-for-untagged-svg-colors");
        bool on = isString(v, "display-p3");
        if (v && !on) {
            // A value outside the sealed vocabulary: shown, and left alone. A
            // checkbox that silently rewrote it would destroy what it cannot read.
            ImGui::TextDisabled("unreadable value -- left untouched");
        } else if (ui::toggle("Assume Display P3 for untagged SVG colors", &on)) {
            writeRoot(x, "color-space-for-untagged-svg-colors",
                      on ? std::optional<icf::json::Value>(icf::json::Value::string("display-p3"))
                         : std::nullopt);
        }
    }
    ImGui::PopID();
}

// ---- features ---------------------------------------------------------------
// `[BIN]` `Snapshot.Feature`: `refractivity`, `specularLocation`. `[ART]` the key
// occurs in 3 of 145 documents, as ["refractivity","specular-location"] twice and
// ["specular-location"] once -- an opt-in list, never empty. So unchecking both
// REMOVES the key rather than writing `[]`, which no document does.
void features(Section& x) {
    ImGui::PushID("Features");
    if (header(x, "Features")) {
        const icf::json::Value* v = rootValue(x, "features");
        bool refractivity = false, specularLocation = false;
        if (v && v->kind() == icf::json::Value::Kind::Array) {
            for (const auto& e : v->elements()) {
                if (e.kind() != icf::json::Value::Kind::String) continue;
                if (e.rawString() == "refractivity") refractivity = true;
                else if (e.rawString() == "specular-location") specularLocation = true;
            }
        }
        bool changed = ui::toggle("Refractivity", &refractivity);
        changed |= ui::toggle("Specular Location", &specularLocation);
        if (changed) {
            if (!refractivity && !specularLocation) {
                writeRoot(x, "features", std::nullopt);
            } else {
                // `refractivity` first: the order the two documents that carry
                // both are written in.
                std::vector<icf::json::Value> list;
                if (refractivity) list.push_back(icf::json::Value::string("refractivity"));
                if (specularLocation) list.push_back(icf::json::Value::string("specular-location"));
                writeRoot(x, "features", icf::json::Value::array(std::move(list)));
            }
        }
    }
    ImGui::PopID();
}

// ---- implicit-asset-mirroring -----------------------------------------------
// `[BIN]` The root of the mirroring chain, and the one member of it that is a
// BARE BOOL. `IconComposition.implicitAssetMirroring` is `Bool` non-optional
// (`fieldmd` "Sb", descriptors `0x13070c` and `0x1317cc`), it is NOT
// specializable -- the binary declares no `-specializations` sibling for it --
// and its default is `false`: the default initialiser at `0x1A48` is
// `mov w0, #0 ; ret` (laudo 19/09 §3.3). The key's spelling is read, not
// derived: `__cstring` `0x12a750`, referenced from the document's `CodingKeys`
// at `0x418C4`, `0x41D68`, `0xC8EC4`, `0xC8FC4` (§3.2).
//
// So this is a plain checkbox and not the group's and the layer's three-position
// control: the value here has no third state to have. A node that says nothing
// lands on THIS bool (`efetivo = mirrorable ?? herdado`, §3.4), which is what
// makes it worth its own line rather than being folded into Features.
//
// UNCHECKING REMOVES THE KEY rather than writing `false`, which is the same rule
// `features` and the SVG colour space above follow, and for the same reason:
// `[ART]` 145 of 145 documents leave the key out, the binary's default for it is
// `false`, and a key that only ever restates the default is a byte the
// round-trip gate has to carry for nothing.
void implicitMirroring(Section& x) {
    ImGui::PushID("Implicit Asset Mirroring");
    if (header(x, "Implicit Asset Mirroring")) {
        const icf::json::Value* v = rootValue(x, "implicit-asset-mirroring");
        bool on = booleanOr(v, false);
        if (ui::toggle("Mirror assets for right-to-left languages", &on)) {
            writeRoot(x, "implicit-asset-mirroring",
                      on ? std::optional<icf::json::Value>(icf::json::Value::boolean(true))
                         : std::nullopt);
        }
        ImGui::SetItemTooltip(
            "What every group and layer falls back to when its own Asset Mirroring is left "
            "Inherited. Off is the binary's default, and 0 of the 145 corpus documents write this "
            "key at all -- so turning it off again takes the key back out.");
    }
    ImGui::PopID();
}

}  // namespace

void drawDocumentSections(Section& x) {
    platforms(x);
    svgColorSpace(x);
    features(x);
    implicitMirroring(x);
    // The last root key the binary declares that still has nothing behind it.
    // `[ART]` it occurs in none of the 145 documents, and unlike
    // `implicit-asset-mirroring` -- whose type, default and semantics were read
    // out of the binary on 19/09 -- nothing has ever shown what its value LOOKS
    // like. A control invented over an unmeasured shape writes a document nobody
    // can read back. Named and greyed says that; hiding it would not.
    x.disabled("Language IDs",
               "Declared by the binary and written by 0 of the 145 corpus documents: no measured "
               "value shape to edit (doc 01 section 2)");
}

}  // namespace ick
