#include "Source/IconComposerKit/ViewModel.h"

namespace ick {

// The two questions are asked of the SAME node, and they are different
// questions: `resolve` walks from the scope outwards to whatever entry is most
// specific among those that match (doc 01 §5), while `hasOwnEntry` asks only
// whether this exact scope wrote something. A Tinted scope over a document that
// only spells the plain key resolves to a value and owns nothing -- that gap is
// precisely what the inspector marks as inherited (spec 13/09 §7).
PropertyView viewProperty(const Session& s, icf::NodePath path, std::string_view prop) {
    PropertyView v;
    const icf::json::Value* node = icf::nodeAt(s.root(), path);
    if (!node) return v;
    v.value = icf::resolve(*node, prop, s.scope);
    v.own = icf::hasOwnEntry(*node, prop, s.scope);
    return v;
}

NodeKind kindOf(icf::NodePath p) {
    if (!p.group) return NodeKind::Root;
    return p.layer ? NodeKind::Layer : NodeKind::Group;
}

// `name` is optional on a node (spec 13/09 §4.4: a fresh layer carries `name`,
// but the corpus has unnamed ones), so the fallback numbers the node the way the
// tree shows it -- one-based, like the target's outline.
std::string nodeTitle(const Session& s, icf::NodePath p) {
    if (!p.group) return "Document";
    const icf::json::Value* node = icf::nodeAt(s.root(), p);
    if (node) {
        if (const icf::json::Value* n = node->find("name")) {
            if (n->kind() == icf::json::Value::Kind::String && !n->rawString().empty()) return n->rawString();
        }
    }
    return p.layer ? "Layer " + std::to_string(*p.layer + 1) : "Group " + std::to_string(*p.group + 1);
}

// ---- what the document declares ---------------------------------------------
// The forms of `supported-platforms` are the ones PanelInspectorDocument.cpp
// measured for its editor -- `squares` is a string "shared" or a list drawn from
// {iOS, macOS}, `circles` is present or absent and always ["watchOS"] -- and
// this is the same reading asked a different question: not "what may I write?"
// but "which composition should the canvas open on?".
icf::Idiom declaredIdiom(const icf::json::Value& root) {
    const icf::json::Value* sp = root.find("supported-platforms");
    if (!sp || sp->kind() != icf::json::Value::Kind::Object) return icf::Idiom::Base;

    if (const icf::json::Value* sq = sp->find("squares")) {
        if (sq->kind() == icf::json::Value::Kind::String) return icf::Idiom::Square;
        if (sq->kind() == icf::json::Value::Kind::Array) {
            // Exactly one member names itself; two or more, and the only scope
            // that covers both is the family. An empty list declares nothing.
            const auto& members = sq->elements();
            if (members.size() == 1 && members[0].kind() == icf::json::Value::Kind::String) {
                if (auto parsed = icf::idiomFromString(members[0].rawString())) return *parsed;
            }
            if (!members.empty()) return icf::Idiom::Square;
        }
    }
    // 0 of 145 documents get this far with `circles` alone, but a document that
    // ships only on the round family should open there and not on a square.
    if (sp->find("circles")) return icf::Idiom::WatchOS;
    return icf::Idiom::Base;
}

std::string declaredPlatformsText(const icf::json::Value& root) {
    const icf::json::Value* sp = root.find("supported-platforms");
    if (!sp || sp->kind() != icf::json::Value::Kind::Object) return "nothing declared";
    std::string out;
    for (const auto& m : sp->members()) {
        if (!out.empty()) out += ", ";
        out += m.first;
        out += ": ";
        if (m.second.kind() == icf::json::Value::Kind::String) {
            out += m.second.rawString();
        } else if (m.second.kind() == icf::json::Value::Kind::Array) {
            bool first = true;
            for (const auto& e : m.second.elements()) {
                if (!first) out += "/";
                first = false;
                out += e.kind() == icf::json::Value::Kind::String ? e.rawString() : icf::json::write(e);
            }
        } else {
            out += icf::json::write(m.second);
        }
    }
    return out.empty() ? "nothing declared" : out;
}

// ---- the vocabularies, spelled for the screen -------------------------------
// NOT the disk spelling: `appearanceToString` and friends answer what the format
// writes ("dark", "plus-lighter"), and these answer what the target puts in the
// menu. Each vocabulary is closed (doc 01 §6), so every case is listed; the
// trailing return is for a value that came from outside the enum.

const char* appearanceLabel(icf::Appearance a) {
    switch (a) {
        case icf::Appearance::Base: return "Default";
        case icf::Appearance::Light: return "Light";
        case icf::Appearance::Dark: return "Dark";
        case icf::Appearance::Tinted: return "Tinted";
    }
    return "Default";
}

// `[BIN]` "All" was the wrong word and it read as the opposite of what the case
// does: `Idiom::Base` matches only the entries that name NO idiom, so it is the
// one reading that shows no platform's composition. The target's own Swift case
// name is `unspecified` -- the reflection field descriptor at 0x121358 of
// `IconComposerFoundation.arm64` spells the five cases `unspecified`, `square`,
// `iOS`, `macOS`, `watchOS`, against the disk names `base`, `square`, `iOS`,
// `macOS`, `watchOS` at 0x1213E8. The menu takes the target's word.
const char* idiomLabel(icf::Idiom i) {
    switch (i) {
        case icf::Idiom::Base: return "Unspecified";
        case icf::Idiom::Square: return "Square";
        case icf::Idiom::IOS: return "iOS";
        case icf::Idiom::MacOS: return "macOS";
        case icf::Idiom::WatchOS: return "watchOS";
    }
    return "Unspecified";
}

const char* blendModeLabel(icf::BlendMode m) {
    switch (m) {
        case icf::BlendMode::Normal: return "Normal";
        case icf::BlendMode::PlusLighter: return "Plus Lighter";
        case icf::BlendMode::PlusDarker: return "Plus Darker";
        case icf::BlendMode::Overlay: return "Overlay";
        case icf::BlendMode::Multiply: return "Multiply";
        case icf::BlendMode::SoftLight: return "Soft Light";
        case icf::BlendMode::HardLight: return "Hard Light";
        case icf::BlendMode::Darken: return "Darken";
        case icf::BlendMode::Lighten: return "Lighten";
        case icf::BlendMode::Screen: return "Screen";
    }
    return "Normal";
}

const char* shadowKindLabel(icf::ShadowKind k) {
    switch (k) {
        case icf::ShadowKind::Automatic: return "Automatic";
        case icf::ShadowKind::Neutral: return "Neutral";
        case icf::ShadowKind::LayerColor: return "Layer Color";
        case icf::ShadowKind::None: return "None";
    }
    return "None";
}

const char* specularLabel(icf::SpecularHighlight h) {
    switch (h) {
        case icf::SpecularHighlight::Off: return "Off";
        case icf::SpecularHighlight::Automatic: return "Automatic";
        case icf::SpecularHighlight::Inside: return "Inside";
        case icf::SpecularHighlight::Outside: return "Outside";
    }
    return "Off";
}

const char* fillKindLabel(icf::FillKind k) {
    switch (k) {
        case icf::FillKind::None: return "None";
        case icf::FillKind::Automatic: return "Automatic";
        case icf::FillKind::Solid: return "Solid";
        case icf::FillKind::AutomaticGradient: return "Automatic Gradient";
        case icf::FillKind::LinearGradient: return "Linear Gradient";
        case icf::FillKind::SystemLight: return "System Light";
        case icf::FillKind::SystemDark: return "System Dark";
    }
    return "None";
}

}  // namespace ick
