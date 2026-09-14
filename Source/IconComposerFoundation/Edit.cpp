#include "Source/IconComposerFoundation/Edit.h"

#include <string>
#include <utility>
#include <vector>

namespace icf {
namespace {

std::string listKeyFor(std::string_view prop) {
    std::string k(prop);
    k += "-specializations";
    return k;
}

bool isBase(Context c) { return c.appearance == Appearance::Base && c.idiom == Idiom::Base; }

// The scope an entry's predicate NAMES, or nullopt when it names none -- a key
// present but not spelling a case this vocabulary knows, or a predicate family
// (`localization`, `language-direction`) the binary declares and the corpus never
// uses (0 of 1740).
std::optional<Context> scopeOf(const json::Value& entry) {
    if (entry.find("localization") || entry.find("language-direction")) return std::nullopt;
    Context c;
    if (const json::Value* a = entry.find("appearance")) {
        if (a->kind() != json::Value::Kind::String) return std::nullopt;
        auto parsed = appearanceFromString(a->rawString());
        if (!parsed) return std::nullopt;
        c.appearance = *parsed;
    }
    if (const json::Value* i = entry.find("idiom")) {
        if (i->kind() != json::Value::Kind::String) return std::nullopt;
        auto parsed = idiomFromString(i->rawString());
        if (!parsed) return std::nullopt;
        c.idiom = *parsed;
    }
    return c;
}

// Is this entry the one FOR `scope`? `resolve`'s `matches` asks a different
// question -- "does this entry APPLY here" -- and both are needed.
//
// A key spelled with the `base` case names the same scope as the key being absent.
// `[ART]` no corpus entry spells it (0 of 1740) and this file never writes it, but
// `resolve` accepts it AND `specificity` scores it above the unpredicated default --
// so treating it as a different scope would let a write at Base create a second
// entry that `resolve` prefers over it: a value written and then invisible.
bool predicateIs(const json::Value& entry, Context scope) {
    const std::optional<Context> named = scopeOf(entry);
    return named && named->appearance == scope.appearance && named->idiom == scope.idiom;
}

json::Value entryFor(Context scope, json::Value value) {
    std::vector<json::Value::Member> m;
    if (scope.appearance != Appearance::Base) {
        m.emplace_back("appearance", json::Value::string(std::string(appearanceToString(scope.appearance))));
    }
    if (scope.idiom != Idiom::Base) {
        m.emplace_back("idiom", json::Value::string(std::string(idiomToString(scope.idiom))));
    }
    m.emplace_back("value", std::move(value));
    return json::Value::object(std::move(m));
}

json::Value* entryIn(json::Value& list, Context scope) {
    for (auto& e : list.elements()) {
        if (e.kind() == json::Value::Kind::Object && predicateIs(e, scope)) return &e;
    }
    return nullptr;
}

}  // namespace

json::Value* nodeAt(json::Value& root, NodePath path) {
    if (!path.group) return &root;
    json::Value* groups = root.find("groups");
    if (!groups || groups->kind() != json::Value::Kind::Array) return nullptr;
    if (*path.group >= groups->elements().size()) return nullptr;
    json::Value* group = &groups->elements()[*path.group];
    if (!path.layer) return group;
    json::Value* layers = group->find("layers");
    if (!layers || layers->kind() != json::Value::Kind::Array) return nullptr;
    if (*path.layer >= layers->elements().size()) return nullptr;
    return &layers->elements()[*path.layer];
}

const json::Value* nodeAt(const json::Value& root, NodePath path) {
    return nodeAt(const_cast<json::Value&>(root), path);
}

void setProperty(json::Value& owner, std::string_view prop, Context scope,
                 std::optional<json::Value> value) {
    const std::string listKey = listKeyFor(prop);
    const std::string plainKey(prop);
    json::Value* list = owner.find(listKey);

    // A `<prop>-specializations` that is not an array is not a specialization list.
    // `resolve` and `hasOwnEntry` both refuse to walk one; this function must not
    // write into one either, or the edit disappears into storage `json::write` does
    // not emit for that kind. Dropping it keeps this function's whole contract --
    // that the two forms never coexist -- which leaving it in place would break.
    if (list && list->kind() != json::Value::Kind::Array) {
        owner.erase(listKey);
        list = nullptr;
    }

    // §4.3 step 1: no list and the Base scope -- the plain key is the whole story.
    if (!list && isBase(scope)) {
        if (value) owner.set(plainKey, std::move(*value));
        else owner.erase(plainKey);
        return;
    }

    // Removing from a list that does not exist removes nothing.
    if (!list && !value) return;

    // §4.3 step 2: the list is the target. Create it, moving the plain key into
    // index 0 as the unpredicated entry -- the two never coexist.
    if (!list) {
        std::vector<json::Value> entries;
        if (json::Value* plain = owner.find(plainKey)) {
            entries.push_back(entryFor(Context{}, *plain));
            owner.erase(plainKey);
        }
        owner.set(listKey, json::Value::array(std::move(entries)));
        list = owner.find(listKey);
    }

    // §4.3 step 3: the entry whose predicate IS the scope.
    if (value) {
        if (json::Value* entry = entryIn(*list, scope)) {
            entry->set("value", std::move(*value));
        } else if (isBase(scope)) {
            list->elements().insert(list->elements().begin(), entryFor(scope, std::move(*value)));
        } else {
            // `[INF]` at the end: the corpus has no observable order between
            // predicates -- no predicate repeats (doc 01 §5) -- so this is a choice,
            // and it is named. The consequence, which the corpus also cannot show
            // because no list there carries both an appearance-only and an
            // idiom-only entry: `resolve` breaks a specificity tie first-wins, so a
            // value written here at `{Base, watchOS}` resolves at `{Dark, watchOS}`
            // to a pre-existing `{appearance: dark}` instead. Appending keeps older
            // entries winning; prepending would only pick the other side of an
            // ambiguity that is the format's, not this function's.
            list->elements().push_back(entryFor(scope, std::move(*value)));
        }
        return;
    }

    // §4.3 step 4: remove, then collapse.
    auto& entries = list->elements();
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (it->kind() == json::Value::Kind::Object && predicateIs(*it, scope)) {
            entries.erase(it);
            break;
        }
    }
    if (entries.empty()) {
        owner.erase(listKey);
        return;
    }
    if (entries.size() == 1 && predicateIs(entries[0], Context{})) {
        if (const json::Value* v = entries[0].find("value")) {
            json::Value plain = *v;
            owner.erase(listKey);
            owner.set(plainKey, std::move(plain));
        }
    }
}

bool hasOwnEntry(const json::Value& owner, std::string_view prop, Context scope) {
    if (const json::Value* list = owner.find(listKeyFor(prop))) {
        if (list->kind() == json::Value::Kind::Array) {
            for (const auto& e : list->elements()) {
                if (e.kind() == json::Value::Kind::Object && predicateIs(e, scope) && e.find("value")) {
                    return true;
                }
            }
        }
        return false;
    }
    return isBase(scope) && owner.find(prop) != nullptr;
}

namespace {
// The array a path's node lives in, and its index there. Null for the root.
json::Value* siblingsOf(json::Value& root, NodePath path, std::size_t& index) {
    if (!path.group) return nullptr;
    if (!path.layer) {
        json::Value* groups = root.find("groups");
        if (!groups || groups->kind() != json::Value::Kind::Array) return nullptr;
        index = *path.group;
        return index < groups->elements().size() ? groups : nullptr;
    }
    json::Value* group = nodeAt(root, NodePath{path.group, std::nullopt});
    if (!group) return nullptr;
    json::Value* layers = group->find("layers");
    if (!layers || layers->kind() != json::Value::Kind::Array) return nullptr;
    index = *path.layer;
    return index < layers->elements().size() ? layers : nullptr;
}
}  // namespace

std::size_t addGroup(json::Value& root, std::string name) {
    json::Value* groups = root.find("groups");
    if (!groups || groups->kind() != json::Value::Kind::Array) {
        root.set("groups", json::Value::array({}));
        groups = root.find("groups");
    }
    groups->elements().push_back(json::Value::object(
        {{"name", json::Value::string(std::move(name))}, {"layers", json::Value::array({})}}));
    return groups->elements().size() - 1;
}

std::size_t addLayer(json::Value& group, std::string name, std::string imageName) {
    json::Value* layers = group.find("layers");
    if (!layers || layers->kind() != json::Value::Kind::Array) {
        group.set("layers", json::Value::array({}));
        layers = group.find("layers");
    }
    layers->elements().push_back(json::Value::object({{"name", json::Value::string(std::move(name))},
                                                      {"image-name", json::Value::string(std::move(imageName))}}));
    return layers->elements().size() - 1;
}

bool removeNode(json::Value& root, NodePath path) {
    std::size_t index = 0;
    json::Value* siblings = siblingsOf(root, path, index);
    if (!siblings) return false;
    siblings->elements().erase(siblings->elements().begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool moveNode(json::Value& root, NodePath path, int delta) {
    std::size_t index = 0;
    json::Value* siblings = siblingsOf(root, path, index);
    if (!siblings || delta == 0) return false;
    auto& v = siblings->elements();
    if (delta < 0 && index == 0) return false;
    if (delta > 0 && index + 1 >= v.size()) return false;
    const std::size_t other = delta < 0 ? index - 1 : index + 1;
    std::swap(v[index], v[other]);
    return true;
}

bool setName(json::Value& root, NodePath path, std::string name) {
    json::Value* node = nodeAt(root, path);
    if (!node || node->kind() != json::Value::Kind::Object) return false;
    node->set("name", json::Value::string(std::move(name)));
    return true;
}

}  // namespace icf
