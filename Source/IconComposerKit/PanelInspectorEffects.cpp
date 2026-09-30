// The Effects family of inspector sections: the material keys the GROUP carries
// that PanelInspector.cpp does not already draw.
//
// WHERE THESE KEYS LIVE, AND WHY THIS FILE IS ONLY REACHED FROM A GROUP
// -----------------------------------------------------------------------------
// `[ART]` A structural sweep of the 145 corpus documents puts every occurrence
// of `blur-material` (123), `lighting` (82), `refractivity` (5) and `specular`
// (103) inside `groups[]` and nowhere else. No layer carries one. That is the
// same finding `GlassMaterial.h` states for `glass`: the LAYER says who
// participates, the GROUP carries the material.
//
// THE RULE THIS FILE IS WRITTEN AGAINST
// -----------------------------------------------------------------------------
// A slider that moves no pixel and writes no byte is worse than a greyed header,
// because it lies to the person editing. So each of the four headers below was
// put through the same three questions before it was allowed a control:
//
//   1. Is there something to read and write SAFELY -- a `Values.h` type, or a
//      scalar the JSON layer round-trips?
//   2. `[ART]` How much of the corpus carries it?
//   3. Does `Source/RenderBox/` consume it, all the way to a pixel?
//
// 1 and 2 yes with 3 no is still a legitimate section -- editing a property the
// document really carries is worth doing before the render draws it -- but then
// the section has to SAY so, in the section, and not only in a commit message.
// 1 no is a greyed header with the specific reason, which is what the generic
// "Round 3: not built yet" was standing in for.
//
// The answers, key by key:
//
//   `blur-material`  1 yes (`number | null`, both `json::Value` constructors)
//                    2 `[ART]` 159 of 271 groups, 88 of 145 documents; 36 groups
//                      spell it as `blur-material-specializations`
//                    3 NO. `readGlassMaterial` reads it and
//                      `denormaliseBlurRadius` turns it into a radius in points
//                      (`[BIN]` `min(b,1) x 64`, doc 03 §29.3, and the laudo of
//                      15/09 closed that the `b` at `0x4A948` IS
//                      `material.blurStrength`) -- but no caller in
//                      `IconRenderer.cpp` consumes `blurRadiusPoints`. Live,
//                      with the warning.
//
//   `refractivity`   1 yes (`icf::Refractivity`, `refractivityFrom/ToJson`)
//                    2 `[ART]` 5 of 271 groups, 3 of 145 documents, 0
//                      specialization lists. Thin, and real.
//                    3 YES, and it is the only one of the four that is:
//                      `strength` becomes `displacementShaderArgument` and
//                      `depth` becomes `refractionHeightPoints`, both of which
//                      `IconRenderer.cpp` hands to `glassDisplacementMap`, which
//                      moves pixels. Live, and no warning about the render --
//                      only about the `enabled` bit, which is a different
//                      warning and a true one.
//
//   `lighting`       1 yes (`icf::Lighting`, `lightingFrom/ToString`)
//                    2 `[ART]` 99 of 271 groups, 74 of 145 documents, 17
//                      specialization lists; `individual` 64, `combined` 18
//                    3 NO, and deliberately so: `readGlassMaterial` reads it and
//                      `glassMaterialFrom` drops it, because `[INF]` it maps to
//                      `Icon.Layer.performsLightingByElement`, which is not a
//                      field of `GlassMaterial`. `DenormalisedGlass` has no slot
//                      for it at all. Live, with the warning.
//
//   Group Effects    1 NO. Greyed, with the reason in the tooltip.
//
// THE TWO VOCABULARIES
// -----------------------------------------------------------------------------
// `lightingLabel` below is what the person reads; `icf::lightingToString` is the
// spelling that goes on disk. The combo lists the LABEL and writes the STRING,
// as `PanelInspector.cpp` does for the other five vocabularies. Swapping them
// corrupts the `icon.json` with no error at all.
#include "Source/IconComposerKit/Widgets.h"
#include "Source/IconComposerKit/InspectorSection.h"

#include "Source/IconComposerFoundation/Values.h"
#include "imgui.h"

#include <optional>
#include <string>

namespace ick {
namespace {

// `ViewModel.h` publishes a `*Label()` for six vocabularies and `Lighting` is
// not one of them. It is spelled here rather than added there because
// `ViewModel.h` is shared with the other fronts writing the neighbouring
// families this round, and one screen label is not worth a shared header.
const char* lightingLabel(icf::Lighting l) {
    return l == icf::Lighting::Combined ? "Combined" : "Individual";
}

// A section that edits a property the renderer does not consume has to say so
// where the control is. Short line in the section, long reason on hover -- the
// same division `Section::disabled` makes for a greyed header.
void caveat(const char* line, const char* detail) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", line);
    ImGui::PopTextWrapPos();
    ImGui::SetItemTooltip("%s", detail);
}

void blurMaterial(Section& x) {
    PropertyView v;
    if (x.begin("Blur Material", "blur-material", v)) {
        // Three states, not two: the key may be absent, may be an explicit
        // `null`, or may be a number, and `resolve` answering with a null
        // POINTER is a different fact from it answering with a Null NODE. The
        // combo previews which of the three the group is in and offers the two
        // it can write; going back to absent is "Remove override", the same as
        // every other section.
        const bool absent = v.value == nullptr;
        const bool isNull = !absent && v.value->kind() == icf::json::Value::Kind::Null;
        // `[ART]` 0.5 is the corpus's most common explicit strength, 41 of 75,
        // so it is what a switch from `null` to a number starts from.
        double strength = numberOr(v.value, 0.5);
        const char* preview = absent ? "not set" : (isNull ? "null" : "Explicit strength");
        if (ImGui::BeginCombo(ui::leftLabel("Value"), preview)) {
            if (ImGui::Selectable("null", isNull)) {
                x.write("blur-material", icf::json::Value::null(), false);
            }
            if (ImGui::Selectable("Explicit strength", !absent && !isNull)) {
                x.write("blur-material", icf::json::Value::number(strength), false);
            }
            ImGui::EndCombo();
        }
        if (!isNull) {
            // `[BIN]` The denormalisation has a ceiling at 1 and NO floor, so a
            // negative strength is a negative radius rather than a clamp. The
            // slider stops at 0 because the corpus does -- 0.05 is its smallest
            // -- and not because the format does.
            NumberEdit e = sliderNumber("Strength", &strength, 0.0, 1.0, "%.4f",
                                        "Blur strength, 0 to 1. [BIN] the radius is min(b, 1) x 64 "
                                        "points; the floor at 0 is the corpus's habit, not the "
                                        "format's rule.");
            if (e.changed) x.write("blur-material", icf::json::Value::number(strength), true);
            if (e.released) x.s.endCoalescing();
        }
        caveat("The document stores this; the renderer does not draw it yet.",
               "readGlassMaterial reads blur-material and denormaliseBlurRadius turns it into a "
               "radius in points -- [BIN] min(b, 1) x 64. Nothing in IconRenderer.cpp consumes "
               "that radius, so editing this changes the saved icon.json and no pixel.");
        caveat("What `null` means was not read.",
               "[ART] 48 of the 123 groups that carry this key write null, which makes it the "
               "single most common value the key takes. It is the target's BlurMaterial "
               "{enabled, explicitStrength} pair collapsed into one slot, and nothing in the "
               "document says whether null is 'blur off' or 'blur on at an automatic strength'. "
               "So it is offered as itself rather than under a name that would decide it.");
    }
    x.end();
}

void refractivity(Section& x) {
    PropertyView v;
    if (x.begin("Refractivity", "refractivity", v)) {
        icf::Refractivity r;
        // `[BIN]` The target's five-argument GlassMaterial init fills the tail
        // from the constant at `0x93B30`: height 0.5, strength 0. A group that
        // does not carry the key is therefore refracting at depth 0.5 with
        // strength 0, and starting the controls anywhere else would show a state
        // the renderer is not in.
        r.depth = 0.5;
        if (v.value) {
            if (auto read = icf::refractivityFrom(*v.value)) r = *read;
        }
        if (ui::toggle("Enabled", &r.enabled)) {
            x.write("refractivity", icf::refractivityToJson(r), false);
        }
        double strength = r.strength;
        double depth = r.depth;
        // `[BIN]` The strength keeps its SIGN and clamps only its magnitude --
        // which is why this slider is bipolar and the depth's is not. `[ART]`
        // both enabled entries in the whole corpus are negative
        // (-0.5269921875 and -0.3591796875), so a 0..1 slider could not reach
        // either of the two real values the format is known to carry. Those two
        // numbers are also why this row had to gain typed entry: neither is
        // reachable by dragging, and they are the only two the format is known
        // to hold.
        NumberEdit a = sliderNumber("Strength", &strength, -1.0, 1.0, "%.7f",
                                    "Signed: [BIN] only the magnitude is clamped, so the sign is "
                                    "the direction of the displacement. [ART] the corpus's two "
                                    "enabled values are -0.5269921875 and -0.3591796875.");
        // `[BIN]` The depth is clamped on both sides before the power.
        NumberEdit b = sliderNumber("Depth", &depth, 0.0, 1.0, "%.7f",
                                    "[BIN] clamped on both sides before the power. A group that "
                                    "carries no refractivity key is already at 0.5.");
        if (a.changed || b.changed) {
            r.strength = strength;
            r.depth = depth;
            x.write("refractivity", icf::refractivityToJson(r), true);
        }
        if (a.released || b.released) x.s.endCoalescing();
        caveat("`Enabled` does not gate the refraction.",
               "[OBS] glassMaterialFrom carries strength and depth across whether or not the bit "
               "is set: the eight-field GlassMaterial has no counterpart for it and where it "
               "collapses was never read. Clearing this checkbox writes the bit into the document "
               "and leaves the picture exactly as it was; the Strength slider is what stops the "
               "refraction, at 0.");
        caveat("Apple's editor also lists this in the document's root `features`; this one does not.",
               "[ART] Both corpus groups that enable refractivity sit in a document whose root "
               "features array contains \"refractivity\"; the one document that carries the key "
               "without that entry has it disabled in all three of its groups. This editor does "
               "not write the root key, and whether the target needs it to honour the group's was "
               "not read.");
    }
    x.end();
}

void lighting(Section& x) {
    static const icf::Lighting kCases[] = {icf::Lighting::Individual, icf::Lighting::Combined};
    PropertyView v;
    if (x.begin("Lighting", "lighting", v)) {
        // `[ART]` `individual` 64, `combined` 18, and nothing else: the
        // vocabulary is closed and the corpus stays inside it.
        icf::Lighting current = icf::Lighting::Individual;
        if (v.value && v.value->kind() == icf::json::Value::Kind::String) {
            if (auto read = icf::lightingFromString(v.value->rawString())) current = *read;
        }
        if (ImGui::BeginCombo(ui::leftLabel("Mode"), lightingLabel(current))) {
            for (auto c : kCases) {
                if (ImGui::Selectable(lightingLabel(c), c == current)) {
                    x.write("lighting", icf::json::Value::string(std::string(icf::lightingToString(c))),
                            false);
                }
            }
            ImGui::EndCombo();
        }
        caveat("The document stores this; the renderer does not draw it yet.",
               "readGlassMaterial reads lighting and glassMaterialFrom deliberately drops it: "
               "[INF] it maps to Icon.Layer.performsLightingByElement, which lives on the layer "
               "struct and not inside GlassMaterial, and DenormalisedGlass has no slot for it. No "
               "arithmetic or branch on it was read, so editing this changes the saved icon.json "
               "and no pixel.");
    }
    x.end();
}

}  // namespace

void drawGroupEffectSections(Section& x) {
    blurMaterial(x);
    refractivity(x);
    lighting(x);
    // The target's `GroupEffectsInspector` is two views: `RefractivityView`,
    // which is the section immediately above, and a stroke/border
    // (`StrokeShapeView`), which has nothing to edit here. An exhaustive key
    // census of the 145 corpus documents finds 50 distinct keys and not one of
    // them names a stroke, a border or a shape, so there is no property to
    // resolve, no `Values.h` type to read it with, and nothing a control could
    // write that the format would carry. The renderer's `borderWidth` is a
    // shader parameter the target sets to 0.0 and never reads from a document
    // (laudo 15/09, translucência), which is the same answer from the other
    // side.
    x.disabled("Group Effects",
               "Half of this is the Refractivity section above. The other half is a "
               "stroke/border, and the format has no key for one: an exhaustive census of the "
               "145 corpus documents finds 50 distinct keys and none of them names a stroke or "
               "a border. Values.h types nothing to read or write, and the renderer's "
               "borderWidth is a shader constant fixed at 0, not a document value.");
}

}  // namespace ick
