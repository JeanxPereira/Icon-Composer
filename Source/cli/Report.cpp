#include "Source/cli/Report.h"

#include "Source/IconComposerFoundation/Values.h"

#include <sstream>

namespace icf::cli {
namespace {

// A scalar as the DOCUMENT spells it. Numbers keep their lexeme and colours keep
// their source text -- the same discipline the reader holds (Json.h). A report
// that reformatted them would show the user something the file does not say.
std::string scalarText(const json::Value& v) {
    switch (v.kind()) {
        case json::Value::Kind::Null: return "null";
        case json::Value::Kind::Bool: return v.boolean() ? "true" : "false";
        case json::Value::Kind::Number: return v.number();
        case json::Value::Kind::String: return v.rawString();
        default: return json::write(v);
    }
}

std::string fillText(const json::Value& v) {
    if (v.kind() == json::Value::Kind::String) return v.rawString();
    if (v.kind() != json::Value::Kind::Object) return json::write(v);
    if (const json::Value* c = v.find("solid")) return "solid " + scalarText(*c);
    if (const json::Value* c = v.find("automatic-gradient")) {
        return "automatic-gradient " + scalarText(*c);
    }
    if (const json::Value* ramp = v.find("linear-gradient")) {
        std::ostringstream o;
        o << "linear-gradient (" << ramp->elements().size() << " stops";
        if (v.find("orientation")) o << ", oriented";
        o << ")";
        return o.str();
    }
    return json::write(v);
}

// The object-shaped properties, compact. A tree that prints three lines of JSON
// where two words belong stops being readable exactly where it gets interesting.
std::string compactText(std::string_view property, const json::Value& v) {
    if (property == "fill") return fillText(v);
    if (property == "shadow") {
        if (const json::Value* k = v.find("kind")) {
            std::string out = scalarText(*k);
            if (const json::Value* op = v.find("opacity")) out += " " + scalarText(*op);
            return out;
        }
    }
    if (property == "translucency" || property == "refractivity") {
        std::ostringstream o;
        bool first = true;
        for (const auto& m : v.members()) {
            if (!first) o << " ";
            first = false;
            o << m.first << "=" << scalarText(m.second);
        }
        return o.str();
    }
    if (property == "position") {
        std::ostringstream o;
        if (const json::Value* sc = v.find("scale")) o << "scale=" << scalarText(*sc);
        if (const json::Value* t = v.find("translation-in-points");
            t && t->elements().size() == 2) {
            o << " at (" << scalarText(t->elements()[0]) << ", "
              << scalarText(t->elements()[1]) << ")";
        }
        return o.str();
    }
    if (v.kind() == json::Value::Kind::Object || v.kind() == json::Value::Kind::Array) {
        return json::write(v);
    }
    return scalarText(v);
}

std::string platformsText(const json::Value& root) {
    const json::Value* p = root.find("supported-platforms");
    if (!p || p->kind() != json::Value::Kind::Object) return "(none declared)";
    std::ostringstream o;
    bool first = true;
    for (const auto& m : p->members()) {
        if (!first) o << ", ";
        first = false;
        o << m.first << ": ";
        if (m.second.kind() == json::Value::Kind::Array) {
            bool f2 = true;
            for (const auto& e : m.second.elements()) {
                if (!f2) o << "/";
                f2 = false;
                o << scalarText(e);
            }
        } else {
            o << scalarText(m.second);
        }
    }
    return o.str();
}

// One resolved property, or nothing when neither the property nor a matching
// specialization provides a value.
void appendProperty(std::ostringstream& o, const json::Value& owner,
                    std::string_view name, Context ctx, const char* label) {
    const json::Value* v = resolve(owner, name, ctx);
    if (!v) return;
    o << "  " << label << " " << compactText(name, *v);
}

const char* appearanceName(Appearance a) {
    switch (a) {
        case Appearance::Base: return "base";
        case Appearance::Light: return "light";
        case Appearance::Dark: return "dark";
        case Appearance::Tinted: return "tinted";
    }
    return "?";
}

const char* idiomName(Idiom i) {
    switch (i) {
        case Idiom::Base: return "base";
        case Idiom::Square: return "square";
        case Idiom::IOS: return "iOS";
        case Idiom::MacOS: return "macOS";
        case Idiom::WatchOS: return "watchOS";
    }
    return "?";
}

}  // namespace

std::string summary(const IconBundle& bundle) {
    auto doc = bundle.document();
    int layers = 0;
    for (const auto& g : doc.groups()) layers += static_cast<int>(g.layers().size());

    std::ostringstream o;
    o << bundle.path().filename().string() << "\n";
    o << "  composition  " << doc.groups().size() << " groups, " << layers << " layers\n";
    o << "  assets       " << bundle.assetFiles().size() << " assets, "
      << bundle.referencedImageNames().size() << " referenced\n";
    o << "  platforms    " << platformsText(bundle.json()) << "\n";
    const json::Value* fill = bundle.json().find("fill");
    if (fill) o << "  fill         " << fillText(*fill) << "\n";
    return o.str();
}

std::string tree(const IconBundle& bundle, Context ctx) {
    auto doc = bundle.document();
    std::ostringstream o;
    o << bundle.path().filename().string()
      << "  [" << appearanceName(ctx.appearance) << " / " << idiomName(ctx.idiom) << "]\n";

    for (const auto& g : doc.groups()) {
        o << "\n" << (g.name().empty() ? "(unnamed group)" : std::string(g.name()));
        std::ostringstream gp;
        appendProperty(gp, g.json(), "opacity", ctx, "opacity");
        appendProperty(gp, g.json(), "blend-mode", ctx, "blend-mode");
        appendProperty(gp, g.json(), "blur-material", ctx, "blur-material");
        appendProperty(gp, g.json(), "shadow", ctx, "shadow");
        appendProperty(gp, g.json(), "hidden", ctx, "hidden");
        if (!gp.str().empty()) o << "  --" << gp.str();
        o << "\n";

        for (const auto& l : g.layers()) {
            o << "    " << (l.name().empty() ? "(unnamed)" : std::string(l.name()));
            const json::Value* glass = l.resolve("glass", ctx);
            if (glass && glass->kind() == json::Value::Kind::Bool && glass->boolean()) {
                o << " [glass]";
            }
            const json::Value* image = l.resolve("image-name", ctx);
            if (image) o << "  <- " << scalarText(*image);
            std::ostringstream lp;
            appendProperty(lp, l.json(), "fill", ctx, "fill");
            appendProperty(lp, l.json(), "opacity", ctx, "opacity");
            appendProperty(lp, l.json(), "blend-mode", ctx, "blend-mode");
            appendProperty(lp, l.json(), "hidden", ctx, "hidden");
            if (!lp.str().empty()) o << " " << lp.str();
            o << "\n";
        }
    }
    return o.str();
}

std::string assets(const IconBundle& bundle) {
    std::ostringstream o;
    o << bundle.path().filename().string() << "/Assets\n";
    const auto referenced = bundle.referencedImageNames();
    const auto missing = bundle.missingAssets();
    const auto unused = bundle.unusedAssets();

    for (const auto& name : referenced) {
        bool absent = false;
        for (const auto& m : missing) {
            if (m == name) absent = true;
        }
        o << (absent ? "  MISSING  " : "  ok       ") << name << "\n";
    }
    for (const auto& name : unused) o << "  UNUSED   " << name << "\n";

    o << "  " << bundle.assetFiles().size() << " file(s), " << referenced.size()
      << " reference(s), " << missing.size() << " missing, " << unused.size() << " unused\n";
    return o.str();
}

}  // namespace icf::cli
