#include "Source/IconComposerFoundation/Edit.h"

#include <string>
#include <vector>

namespace icf {
namespace {

std::string listKeyFor(std::string_view prop) {
    std::string k(prop);
    k += "-specializations";
    return k;
}

bool isBase(Context c) { return c.appearance == Appearance::Base && c.idiom == Idiom::Base; }

// An entry's predicate EQUALS the scope: the keys present are exactly the
// non-Base parts of the scope, and each one names the scope's case. `resolve`
// asks "matches"; this asks "is the entry FOR this scope", which is stricter.
bool predicateIs(const json::Value& entry, Context scope) {
    if (entry.find("localization") || entry.find("language-direction")) return false;
    const json::Value* a = entry.find("appearance");
    const json::Value* i = entry.find("idiom");
    if ((scope.appearance != Appearance::Base) != (a != nullptr)) return false;
    if ((scope.idiom != Idiom::Base) != (i != nullptr)) return false;
    if (a && (a->kind() != json::Value::Kind::String ||
              appearanceFromString(a->rawString()) != scope.appearance)) return false;
    if (i && (i->kind() != json::Value::Kind::String ||
              idiomFromString(i->rawString()) != scope.idiom)) return false;
    return true;
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
            // predicates (spec §4.3), so this is a choice, and it is named.
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

}  // namespace icf
