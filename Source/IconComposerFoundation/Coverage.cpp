// What the model knows about `.icon`, as data -- and the walk that reports what
// it does not.
//
// The key sets below are transcribed from doc 01, which read them out of
// `IconComposerFoundation`'s Swift reflection metadata. They are not a guess at
// what a document might hold: they are what the binary declares. The corpus gate
// then checks the other direction -- that 145 real documents contain nothing
// outside them.
#include "Source/IconComposerFoundation/IconDocument.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace icf {
namespace {

constexpr std::string_view kSpecializationSuffix = "-specializations";

// The predicate and payload of one specialization entry (doc 01 §5).
constexpr std::string_view kSpecializationKeys[] = {
    "idiom", "appearance", "localization", "language-direction", "value",
};

// Properties whose value is an OBJECT with a shape of its own. Anything not
// listed here is a scalar, or an array of scalars, and has no keys to check.
enum class Shape { Scalar, Fill, Orientation, Point, Position, Shadow, Translucency,
                   Refractivity, SupportedPlatforms, AssetMirroring };

Shape shapeOf(std::string_view property) {
    if (property == "fill") return Shape::Fill;
    if (property == "orientation") return Shape::Orientation;
    if (property == "start" || property == "stop") return Shape::Point;
    if (property == "position") return Shape::Position;
    if (property == "shadow") return Shape::Shadow;
    if (property == "translucency") return Shape::Translucency;
    if (property == "refractivity") return Shape::Refractivity;
    if (property == "supported-platforms") return Shape::SupportedPlatforms;
    // `[BIN]` `IconComposition.AssetMirroring` (descriptor `0x130ae0`) is a
    // struct with exactly one coding key, `mirrorable : Bool?` -- laudo 19/09
    // §3.3. Listing it here is what turns a misspelt sub-key into a line in the
    // Diagnostics panel instead of a member the document carries in silence.
    if (property == "asset-mirroring") return Shape::AssetMirroring;
    return Shape::Scalar;
}

std::vector<std::string_view> keysOf(Shape s) {
    switch (s) {
        case Shape::Fill:               return {"solid", "automatic-gradient",
                                                "linear-gradient", "orientation"};
        case Shape::Orientation:        return {"start", "stop"};
        case Shape::Point:              return {"x", "y"};
        case Shape::Position:           return {"scale", "translation-in-points"};
        case Shape::Shadow:             return {"kind", "opacity"};
        case Shape::Translucency:       return {"enabled", "value"};
        case Shape::Refractivity:       return {"enabled", "strength", "depth"};
        case Shape::SupportedPlatforms: return {"squares", "circles"};
        case Shape::AssetMirroring:     return {"mirrorable"};
        case Shape::Scalar:             return {};
    }
    return {};
}

enum class Level { Document, Group, Layer };

// Keys that carry no `-specializations` sibling.
std::vector<std::string_view> plainKeys(Level l) {
    switch (l) {
        case Level::Document:
            return {"features", "groups", "specializable-language-ids",
                    "color-space-for-untagged-svg-colors", "supported-platforms",
                    "implicit-asset-mirroring"};
        case Level::Group:
            return {"name", "layers"};
        case Level::Layer:
            return {"kind", "name"};
    }
    return {};
}

// Keys that appear both plain and as `<key>-specializations`.
std::vector<std::string_view> specializableKeys(Level l) {
    switch (l) {
        case Level::Document:
            return {"fill"};
        case Level::Group:
            // The four `legacy-*` are declared by the binary and occur in none of
            // the 145 documents. They are listed so that a document that does
            // carry one is UNDERSTOOD rather than reported as unknown.
            return {"hidden", "opacity", "blur-material", "refractivity", "shadow",
                    "translucency", "specular", "blend-mode", "lighting",
                    "asset-mirroring", "position",
                    "legacy-refractivity-strength", "legacy-refractivity-depth",
                    "legacy-specular-highlight"};
        case Level::Layer:
            return {"position", "fill", "material", "opacity", "blend-mode", "hidden",
                    "glass", "asset-mirroring", "image-name"};
    }
    return {};
}

bool contains(const std::vector<std::string_view>& v, std::string_view s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

bool contains(const std::string_view (&v)[5], std::string_view s) {
    for (const auto& e : v) {
        if (e == s) return true;
    }
    return false;
}

std::string join(const std::string& path, std::string_view leaf) {
    if (path.empty()) return std::string(leaf);
    return path + "/" + std::string(leaf);
}

void walkShape(const json::Value& node, Shape shape, const std::string& path,
               std::vector<std::string>& out);

// A specialization list: every entry is a predicate plus a `value` whose shape
// is the shape of the property being specialized.
void walkSpecializations(const json::Value& list, std::string_view property,
                         const std::string& path, std::vector<std::string>& out) {
    if (list.kind() != json::Value::Kind::Array) return;
    for (size_t i = 0; i < list.elements().size(); ++i) {
        const json::Value& entry = list.elements()[i];
        if (entry.kind() != json::Value::Kind::Object) continue;
        const std::string here = path + "[" + std::to_string(i) + "]";
        for (const auto& m : entry.members()) {
            if (!contains(kSpecializationKeys, m.first)) out.push_back(join(here, m.first));
        }
        if (const json::Value* v = entry.find("value")) {
            walkShape(*v, shapeOf(property), join(here, "value"), out);
        }
    }
}

void walkShape(const json::Value& node, Shape shape, const std::string& path,
               std::vector<std::string>& out) {
    if (shape == Shape::Scalar || node.kind() != json::Value::Kind::Object) return;
    const auto keys = keysOf(shape);
    for (const auto& m : node.members()) {
        if (!contains(keys, m.first)) out.push_back(join(path, m.first));
    }
    for (const auto& m : node.members()) {
        if (contains(keys, m.first)) walkShape(m.second, shapeOf(m.first), join(path, m.first), out);
    }
}

void walkLevel(const json::Value& node, Level level, const std::string& path,
               std::vector<std::string>& out) {
    const auto plain = plainKeys(level);
    const auto special = specializableKeys(level);

    // This level's own unknown keys first, then everything below it. That order
    // makes a report read from the outside in, which is how it gets diagnosed.
    for (const auto& m : node.members()) {
        if (contains(plain, m.first) || contains(special, m.first)) continue;
        if (m.first.size() > kSpecializationSuffix.size()) {
            const std::string_view base(m.first.data(),
                                        m.first.size() - kSpecializationSuffix.size());
            const std::string_view suffix(m.first.data() + base.size(),
                                          kSpecializationSuffix.size());
            if (suffix == kSpecializationSuffix && contains(special, base)) continue;
        }
        out.push_back(join(path, m.first));
    }

    for (const auto& m : node.members()) {
        const std::string here = join(path, m.first);
        if (contains(plain, m.first) || contains(special, m.first)) {
            if (m.first == "groups" || m.first == "layers") {
                if (m.second.kind() != json::Value::Kind::Array) continue;
                const Level child = (m.first == "groups") ? Level::Group : Level::Layer;
                for (size_t i = 0; i < m.second.elements().size(); ++i) {
                    const json::Value& e = m.second.elements()[i];
                    if (e.kind() != json::Value::Kind::Object) continue;
                    walkLevel(e, child, here + "[" + std::to_string(i) + "]", out);
                }
            } else {
                walkShape(m.second, shapeOf(m.first), here, out);
            }
            continue;
        }
        if (m.first.size() > kSpecializationSuffix.size()) {
            const std::string_view base(m.first.data(),
                                        m.first.size() - kSpecializationSuffix.size());
            const std::string_view suffix(m.first.data() + base.size(),
                                          kSpecializationSuffix.size());
            if (suffix == kSpecializationSuffix && contains(special, base)) {
                walkSpecializations(m.second, base, here, out);
            }
        }
    }
}

}  // namespace

std::vector<std::string> IconDocument::unknownKeys() const {
    std::vector<std::string> out;
    walkLevel(*root_, Level::Document, "", out);
    return out;
}

}  // namespace icf
