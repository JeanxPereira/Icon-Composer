#include "Source/IconComposerFoundation/Values.h"

#include <charconv>
#include <string>

namespace icf {
namespace {

// How many components a space carries. The grey spaces are (luminance, alpha);
// the others are (r, g, b, a). Measured over the corpus's 576 colours: 4 for
// every display-p3/srgb/extended-srgb, 2 for every gray/extended-gray, without
// exception.
struct SpaceInfo {
    std::string_view name;
    ColorSpace space;
    int count;
};

constexpr SpaceInfo kSpaces[] = {
    {"display-p3", ColorSpace::DisplayP3, 4},
    {"srgb", ColorSpace::SRGB, 4},
    {"extended-srgb", ColorSpace::ExtendedSRGB, 4},
    {"gray", ColorSpace::Gray, 2},
    {"extended-gray", ColorSpace::ExtendedGray, 2},
};

// `from_chars` for a double, requiring that the WHOLE field is the number.
// `std::stod` would accept "1.0red" and stop at the letter; a component this
// reader does not fully understand must be a refusal, not a prefix.
bool wholeDouble(std::string_view field, double& out) {
    if (field.empty()) return false;
    const char* first = field.data();
    const char* last = field.data() + field.size();
    auto r = std::from_chars(first, last, out);
    return r.ec == std::errc() && r.ptr == last;
}

// A JSON number, from its lexeme. The tree keeps the source text (Json.h), so
// this is where a number becomes a value -- once, at the edge, on demand.
bool asDouble(const json::Value* v, double& out) {
    if (!v || v->kind() != json::Value::Kind::Number) return false;
    return wholeDouble(v->number(), out);
}

bool asBool(const json::Value* v, bool& out) {
    if (!v || v->kind() != json::Value::Kind::Bool) return false;
    out = v->boolean();
    return true;
}

std::optional<Color> colorMember(const json::Value& node, std::string_view key) {
    const json::Value* v = node.find(key);
    if (!v || v->kind() != json::Value::Kind::String) return std::nullopt;
    return colorFromString(v->rawString());
}

std::optional<Point> pointFrom(const json::Value* v) {
    if (!v || v->kind() != json::Value::Kind::Object) return std::nullopt;
    Point p;
    if (!asDouble(v->find("x"), p.x)) return std::nullopt;
    if (!asDouble(v->find("y"), p.y)) return std::nullopt;
    return p;
}

}  // namespace

std::optional<Color> colorFromString(std::string_view s) {
    const size_t colon = s.find(':');
    if (colon == std::string_view::npos) return std::nullopt;
    const std::string_view name = s.substr(0, colon);

    const SpaceInfo* info = nullptr;
    for (const auto& e : kSpaces) {
        if (e.name == name) {
            info = &e;
            break;
        }
    }
    if (!info) return std::nullopt;

    Color c;
    c.space = info->space;
    c.count = info->count;

    std::string_view rest = s.substr(colon + 1);
    for (int i = 0; i < info->count; ++i) {
        const size_t comma = rest.find(',');
        const std::string_view field =
            (comma == std::string_view::npos) ? rest : rest.substr(0, comma);
        if (!wholeDouble(field, c.components[i])) return std::nullopt;
        if (i + 1 == info->count) {
            // The last component must end the string: a trailing comma or an
            // extra field means this is not a colour of this space.
            if (comma != std::string_view::npos) return std::nullopt;
            return c;
        }
        if (comma == std::string_view::npos) return std::nullopt;  // too few
        rest = rest.substr(comma + 1);
    }
    return c;
}

std::optional<BlendMode> blendModeFromString(std::string_view s) {
    if (s == "normal") return BlendMode::Normal;
    if (s == "plus-lighter") return BlendMode::PlusLighter;
    if (s == "plus-darker") return BlendMode::PlusDarker;
    if (s == "overlay") return BlendMode::Overlay;
    if (s == "multiply") return BlendMode::Multiply;
    if (s == "soft-light") return BlendMode::SoftLight;
    if (s == "hard-light") return BlendMode::HardLight;
    if (s == "darken") return BlendMode::Darken;
    if (s == "lighten") return BlendMode::Lighten;
    if (s == "screen") return BlendMode::Screen;
    return std::nullopt;
}

std::optional<ShadowKind> shadowKindFromString(std::string_view s) {
    if (s == "automatic") return ShadowKind::Automatic;
    if (s == "neutral") return ShadowKind::Neutral;
    if (s == "layer-color") return ShadowKind::LayerColor;
    if (s == "none") return ShadowKind::None;
    return std::nullopt;
}

std::optional<SpecularHighlight> specularHighlightFromString(std::string_view s) {
    if (s == "off") return SpecularHighlight::Off;
    if (s == "automatic") return SpecularHighlight::Automatic;
    if (s == "inside") return SpecularHighlight::Inside;
    if (s == "outside") return SpecularHighlight::Outside;
    return std::nullopt;
}

std::optional<Lighting> lightingFromString(std::string_view s) {
    if (s == "individual") return Lighting::Individual;
    if (s == "combined") return Lighting::Combined;
    return std::nullopt;
}

std::optional<FillKind> fillKindFromString(std::string_view s) {
    if (s == "none") return FillKind::None;
    if (s == "automatic") return FillKind::Automatic;
    if (s == "solid") return FillKind::Solid;
    if (s == "automatic-gradient") return FillKind::AutomaticGradient;
    if (s == "linear-gradient") return FillKind::LinearGradient;
    if (s == "system-light") return FillKind::SystemLight;
    if (s == "system-dark") return FillKind::SystemDark;
    return std::nullopt;
}

std::optional<Fill> fillFrom(const json::Value& v) {
    // A bare string names a kind that carries no colour of its own.
    if (v.kind() == json::Value::Kind::String) {
        auto kind = fillKindFromString(v.rawString());
        if (!kind) return std::nullopt;
        Fill f;
        f.kind = *kind;
        return f;
    }
    if (v.kind() != json::Value::Kind::Object) return std::nullopt;

    Fill f;
    if (v.find("solid")) {
        auto c = colorMember(v, "solid");
        if (!c) return std::nullopt;
        f.kind = FillKind::Solid;
        f.colors.push_back(*c);
    } else if (v.find("automatic-gradient")) {
        // ONE colour. The rest of the ramp is derived by the renderer and is not
        // written down anywhere in the document (doc 01 §10.4).
        auto c = colorMember(v, "automatic-gradient");
        if (!c) return std::nullopt;
        f.kind = FillKind::AutomaticGradient;
        f.colors.push_back(*c);
    } else if (const json::Value* ramp = v.find("linear-gradient")) {
        if (ramp->kind() != json::Value::Kind::Array) return std::nullopt;
        f.kind = FillKind::LinearGradient;
        for (const auto& e : ramp->elements()) {
            if (e.kind() != json::Value::Kind::String) return std::nullopt;
            auto c = colorFromString(e.rawString());
            if (!c) return std::nullopt;
            f.colors.push_back(*c);
        }
    } else {
        return std::nullopt;
    }

    if (const json::Value* o = v.find("orientation")) {
        auto start = pointFrom(o->find("start"));
        auto stop = pointFrom(o->find("stop"));
        if (!start || !stop) return std::nullopt;
        f.orientation = Orientation{*start, *stop};
    }
    return f;
}

std::optional<Shadow> shadowFrom(const json::Value& v) {
    if (v.kind() != json::Value::Kind::Object) return std::nullopt;
    Shadow s;
    const json::Value* kind = v.find("kind");
    if (!kind || kind->kind() != json::Value::Kind::String) return std::nullopt;
    auto k = shadowKindFromString(kind->rawString());
    if (!k) return std::nullopt;
    s.kind = *k;
    if (!asDouble(v.find("opacity"), s.opacity)) return std::nullopt;
    return s;
}

std::optional<Position> positionFrom(const json::Value& v) {
    if (v.kind() != json::Value::Kind::Object) return std::nullopt;
    Position p;
    if (!asDouble(v.find("scale"), p.scale)) return std::nullopt;
    const json::Value* t = v.find("translation-in-points");
    if (!t || t->kind() != json::Value::Kind::Array || t->elements().size() != 2) {
        return std::nullopt;
    }
    if (!asDouble(&t->elements()[0], p.translation.x)) return std::nullopt;
    if (!asDouble(&t->elements()[1], p.translation.y)) return std::nullopt;
    return p;
}

std::optional<Translucency> translucencyFrom(const json::Value& v) {
    if (v.kind() != json::Value::Kind::Object) return std::nullopt;
    Translucency t;
    if (!asBool(v.find("enabled"), t.enabled)) return std::nullopt;
    if (!asDouble(v.find("value"), t.value)) return std::nullopt;
    return t;
}

std::optional<Refractivity> refractivityFrom(const json::Value& v) {
    if (v.kind() != json::Value::Kind::Object) return std::nullopt;
    Refractivity r;
    if (!asBool(v.find("enabled"), r.enabled)) return std::nullopt;
    if (!asDouble(v.find("strength"), r.strength)) return std::nullopt;
    if (!asDouble(v.find("depth"), r.depth)) return std::nullopt;
    return r;
}

}  // namespace icf
