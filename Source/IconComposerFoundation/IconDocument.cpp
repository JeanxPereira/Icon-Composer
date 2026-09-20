#include "Source/IconComposerFoundation/IconDocument.h"

#include <compare>
#include <optional>
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

std::string_view appearanceToString(Appearance a) {
    switch (a) {
        case Appearance::Base: return "base";
        case Appearance::Light: return "light";
        case Appearance::Dark: return "dark";
        case Appearance::Tinted: return "tinted";
    }
    return "base";
}

std::string_view idiomToString(Idiom i) {
    switch (i) {
        case Idiom::Base: return "base";
        case Idiom::Square: return "square";
        case Idiom::IOS: return "iOS";
        case Idiom::MacOS: return "macOS";
        case Idiom::WatchOS: return "watchOS";
    }
    return "base";
}

std::optional<Idiom> idiomParent(Idiom i) {
    switch (i) {
        case Idiom::Base: return std::nullopt;
        case Idiom::Square: return Idiom::Base;
        case Idiom::WatchOS: return Idiom::Base;
        case Idiom::IOS: return Idiom::Square;
        case Idiom::MacOS: return Idiom::Square;
    }
    return std::nullopt;
}

std::optional<Appearance> appearanceParent(Appearance a) {
    if (a == Appearance::Base) return std::nullopt;
    return Appearance::Base;
}

int idiomPrecedent(Idiom i) { return static_cast<int>(i); }
int appearancePrecedent(Appearance a) { return static_cast<int>(a); }

bool idiomCovers(Idiom slot, Idiom ctx) {
    for (std::optional<Idiom> at = ctx; at; at = idiomParent(*at)) {
        if (*at == slot) return true;
    }
    return false;
}

bool appearanceCovers(Appearance slot, Appearance ctx) {
    for (std::optional<Appearance> at = ctx; at; at = appearanceParent(*at)) {
        if (*at == slot) return true;
    }
    return false;
}

namespace {

// O par que `SpecializationSlot.precedent` (`0xD55B8`) devolve, na ordem em que
// `Precedent.<` (`0xD5A40`) compara: idiom primeiro, aparência desempatando.
struct Precedent {
    int idiom = 0;
    int appearance = 0;

    auto operator<=>(const Precedent&) const = default;
};

// O slot de uma entrada: o par `(idiom, appearance)` com `base` no lugar da
// chave ausente, ou nada quando a entrada não pode casar contexto nenhum.
//
// `localization` and `language-direction` are declared by the binary and occur
// in ZERO of the corpus's 1,740 specialization entries, so no context here can
// satisfy one. An entry that carries either never matches -- which is the
// conservative answer, and it is visible rather than silent: such an entry
// simply never wins, and the property falls back. O mesmo vale para uma grafia
// que `*FromString` não reconhece: um `idiom` que este leitor não sabe ler não
// pode ser tratado como "sem idiom", porque isso o promoveria ao default.
std::optional<Context> slotOf(const json::Value& entry) {
    if (entry.find("localization") || entry.find("language-direction")) return std::nullopt;
    Context slot;
    if (const json::Value* a = entry.find("appearance")) {
        if (a->kind() != json::Value::Kind::String) return std::nullopt;
        auto want = appearanceFromString(a->rawString());
        if (!want) return std::nullopt;
        slot.appearance = *want;
    }
    if (const json::Value* i = entry.find("idiom")) {
        if (i->kind() != json::Value::Kind::String) return std::nullopt;
        auto want = idiomFromString(i->rawString());
        if (!want) return std::nullopt;
        slot.idiom = *want;
    }
    return slot;
}

}  // namespace

const json::Value* resolve(const json::Value& owner, std::string_view property, Context ctx) {
    std::string listKey(property);
    listKey += "-specializations";
    if (const json::Value* list = owner.find(listKey)) {
        if (list->kind() == json::Value::Kind::Array) {
            const json::Value* best = nullptr;
            Precedent bestPrecedent;
            for (const auto& entry : list->elements()) {
                if (entry.kind() != json::Value::Kind::Object) continue;
                const std::optional<Context> slot = slotOf(entry);
                if (!slot) continue;
                if (!idiomCovers(slot->idiom, ctx.idiom)) continue;
                if (!appearanceCovers(slot->appearance, ctx.appearance)) continue;
                const json::Value* value = entry.find("value");
                if (!value) continue;  // an override with nothing to override with
                const Precedent precedent{idiomPrecedent(slot->idiom),
                                          appearancePrecedent(slot->appearance)};
                if (!best || precedent > bestPrecedent) {
                    bestPrecedent = precedent;
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
