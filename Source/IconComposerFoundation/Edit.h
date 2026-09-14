#pragma once
// Editing a `.icon` document IN the JSON tree it was read from.
//
// The model is a view (IconDocument.h), and editing keeps it one: an edit is a
// change to a node of the tree, spelled the way Apple's encoder would spell it,
// and `json::write` of the tree is the save. That is what keeps the 135
// byte-exact documents byte-exact through an edit and its undo.
//
// THE ONE RULE, MEASURED (spec 13/09 §2.3, §4.3; doc 01 §5)
// -----------------------------------------------------------
// A property lives in exactly one of two places: the plain key `<prop>`, or the
// list `<prop>-specializations`. Over the corpus's 890 lists the plain key is
// absent 890 times. In a list, the entry with no predicate -- the default -- is
// at index 0 (616 of 616), and no predicate repeats. So "write under a scope"
// is one operation, `setProperty`, and both `resolve` and this file agree on
// which entry a scope names.
#include "Source/IconComposerFoundation/IconDocument.h"

#include <optional>
#include <string>
#include <string_view>

namespace icf {

// Where a node is. Empty is the root; `group` alone is a group; both is a layer.
struct NodePath {
    std::optional<std::size_t> group;
    std::optional<std::size_t> layer;
    bool operator==(const NodePath&) const = default;
};

json::Value* nodeAt(json::Value& root, NodePath path);
const json::Value* nodeAt(const json::Value& root, NodePath path);

// Writes `value` for `prop` under `scope`, or removes that scope's own entry when
// `value` is nullopt. See the header note for the rule; the steps are §4.3 of
// the spec, numbered in the implementation.
void setProperty(json::Value& owner, std::string_view prop, Context scope,
                 std::optional<json::Value> value);

// True when `scope` has an entry of its OWN -- as opposed to resolving to
// something more general. This is what the inspector marks as "inherited".
bool hasOwnEntry(const json::Value& owner, std::string_view prop, Context scope);

// ---- structure -------------------------------------------------------------
// A new node carries the minimum a fresh node carries in the corpus: `name`, plus
// `image-name` on a layer, plus an empty `layers` on a group. Everything else is
// the renderer's default until the inspector writes it.
std::size_t addGroup(json::Value& root, std::string name);
std::size_t addLayer(json::Value& group, std::string name, std::string imageName);
bool removeNode(json::Value& root, NodePath path);     // false for the root or out of range
bool moveNode(json::Value& root, NodePath path, int delta);   // -1 up, +1 down; false at an edge
bool setName(json::Value& root, NodePath path, std::string name);

}  // namespace icf
