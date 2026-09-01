#include "Source/IconComposerFoundation/IconDocument.h"

#include <string>

namespace icf {

std::optional<Appearance> appearanceFromString(std::string_view s) {
    if (s == "base") return Appearance::Base;
    if (s == "light") return Appearance::Light;
    if (s == "dark") return Appearance::Dark;
    if (s == "tinted") return Appearance::Tinted;
    return std::nullopt;
}

std::optional<Idiom> idiomFromString(std::string_view s) {
    if (s == "base") return Idiom::Base;
    if (s == "square") return Idiom::Square;
    if (s == "iOS") return Idiom::IOS;
    if (s == "macOS") return Idiom::MacOS;
    if (s == "watchOS") return Idiom::WatchOS;
    return std::nullopt;
}

namespace {

// How many predicate keys an entry constrains. The entry with the most of them
// among those that match is the one that wins.
int specificity(const json::Value& entry) {
    int n = 0;
    for (const char* key : {"idiom", "appearance", "localization", "language-direction"}) {
        if (entry.find(key)) ++n;
    }
    return n;
}

// Does this entry's predicate hold in `ctx`?
//
// `localization` and `language-direction` are declared by the binary and occur
// in ZERO of the corpus's 1,740 specialization entries, so no context here can
// satisfy one. An entry that carries either never matches -- which is the
// conservative answer, and it is visible rather than silent: such an entry
// simply never wins, and the property falls back.
bool matches(const json::Value& entry, Context ctx) {
    if (entry.find("localization") || entry.find("language-direction")) return false;
    if (const json::Value* a = entry.find("appearance")) {
        if (a->kind() != json::Value::Kind::String) return false;
        auto want = appearanceFromString(a->rawString());
        if (!want || *want != ctx.appearance) return false;
    }
    if (const json::Value* i = entry.find("idiom")) {
        if (i->kind() != json::Value::Kind::String) return false;
        auto want = idiomFromString(i->rawString());
        if (!want || *want != ctx.idiom) return false;
    }
    return true;
}

}  // namespace

const json::Value* resolve(const json::Value& owner, std::string_view property, Context ctx) {
    std::string listKey(property);
    listKey += "-specializations";
    if (const json::Value* list = owner.find(listKey)) {
        if (list->kind() == json::Value::Kind::Array) {
            const json::Value* best = nullptr;
            int bestScore = -1;
            for (const auto& entry : list->elements()) {
                if (entry.kind() != json::Value::Kind::Object) continue;
                if (!matches(entry, ctx)) continue;
                const json::Value* value = entry.find("value");
                if (!value) continue;  // an override with nothing to override with
                const int score = specificity(entry);
                if (score > bestScore) {
                    bestScore = score;
                    best = value;
                }
            }
            if (best) return best;
        }
    }
    return owner.find(property);
}

namespace {

// A string member's RAW text -- the bytes between the quotes, escapes not
// decoded. No document in the 145-file corpus carries an escape, so nothing
// here has ever exercised decoding; a name that needed one would come back
// with its backslashes intact, and that is stated rather than pretended.
std::string_view stringMember(const json::Value& node, std::string_view key) {
    const json::Value* v = node.find(key);
    if (!v || v->kind() != json::Value::Kind::String) return {};
    return v->rawString();
}

// The object elements of an array member, in document order.
std::vector<const json::Value*> objectsIn(const json::Value& node, std::string_view key) {
    std::vector<const json::Value*> out;
    const json::Value* list = node.find(key);
    if (!list || list->kind() != json::Value::Kind::Array) return out;
    for (const auto& e : list->elements()) {
        if (e.kind() == json::Value::Kind::Object) out.push_back(&e);
    }
    return out;
}

}  // namespace

std::string_view Layer::textOf(std::string_view key) const { return stringMember(*node_, key); }

const json::Value* Layer::resolve(std::string_view property, Context ctx) const {
    return icf::resolve(*node_, property, ctx);
}

std::string_view Group::name() const { return stringMember(*node_, "name"); }

std::vector<Layer> Group::layers() const {
    std::vector<Layer> out;
    for (const json::Value* n : objectsIn(*node_, "layers")) out.emplace_back(*n);
    return out;
}

const json::Value* Group::resolve(std::string_view property, Context ctx) const {
    return icf::resolve(*node_, property, ctx);
}

std::optional<IconDocument> IconDocument::open(const json::Value& root) {
    if (root.kind() != json::Value::Kind::Object) return std::nullopt;
    const json::Value* groups = root.find("groups");
    if (!groups || groups->kind() != json::Value::Kind::Array) return std::nullopt;
    return IconDocument(root);
}

std::vector<Group> IconDocument::groups() const {
    std::vector<Group> out;
    for (const json::Value* n : objectsIn(*root_, "groups")) out.emplace_back(*n);
    return out;
}

}  // namespace icf
