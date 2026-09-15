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

const char* idiomLabel(icf::Idiom i) {
    switch (i) {
        case icf::Idiom::Base: return "All";
        case icf::Idiom::Square: return "Square";
        case icf::Idiom::IOS: return "iOS";
        case icf::Idiom::MacOS: return "macOS";
        case icf::Idiom::WatchOS: return "watchOS";
    }
    return "All";
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
