#pragma once
// The typed view over a parsed `.icon` document.
//
// It is a VIEW, not a copy: the JSON tree stays the storage. That keeps the
// round-trip total -- a key this layer does not model still survives a read and
// a write -- which matters for a format that ships a new build every few weeks.
#include "Source/IconComposerFoundation/Json.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace icf {

enum class Appearance { Base, Light, Dark, Tinted };
enum class Idiom { Base, Square, IOS, MacOS, WatchOS };

std::optional<Appearance> appearanceFromString(std::string_view s);
std::optional<Idiom> idiomFromString(std::string_view s);

// The disk spelling of each case. `Base` answers "base", which the format never
// writes (doc 01 §6: 0 of 1,740 entries) -- a writer tests for Base first.
std::string_view appearanceToString(Appearance a);
std::string_view idiomToString(Idiom i);

// What a property is resolved FOR.
struct Context {
    Appearance appearance = Appearance::Base;
    Idiom idiom = Idiom::Base;
};

// The value of `property` on `owner` under `ctx`, or nullptr when neither the
// plain property nor a matching specialization provides one.
//
// The rule is measured over the corpus's 890 specialization lists (doc 01 §5):
// among the entries whose predicate matches the context the most specific wins,
// an entry with no predicate is the default, and a specialization outranks the
// plain property.
const json::Value* resolve(const json::Value& owner, std::string_view property, Context ctx);


// A layer inside a group. A view: it borrows the JSON node, it does not own it.
class Layer {
public:
    explicit Layer(const json::Value& node) : node_(&node) {}

    const json::Value& json() const { return *node_; }
    std::string_view name() const { return textOf("name"); }
    std::string_view imageName() const { return textOf("image-name"); }
    const json::Value* resolve(std::string_view property, Context ctx) const;

private:
    std::string_view textOf(std::string_view key) const;
    const json::Value* node_;
};

// A group of layers.
class Group {
public:
    explicit Group(const json::Value& node) : node_(&node) {}

    const json::Value& json() const { return *node_; }
    std::string_view name() const;
    std::vector<Layer> layers() const;
    const json::Value* resolve(std::string_view property, Context ctx) const;

private:
    const json::Value* node_;
};

// The document. `open` refuses anything without the obligatory core -- an object
// carrying `groups` -- because a view over something that is not a `.icon` would
// answer questions with silence instead of an error.
class IconDocument {
public:
    static std::optional<IconDocument> open(const json::Value& root);

    const json::Value& json() const { return *root_; }
    std::vector<Group> groups() const;

    // Every key in the document this layer does not model, as a path
    // (`groups[0]/layers[1]/wibble`), in document order. Empty means the model
    // recognised everything -- which the corpus gate turns into the claim that
    // it knows the format.
    std::vector<std::string> unknownKeys() const;

private:
    explicit IconDocument(const json::Value& root) : root_(&root) {}
    const json::Value* root_;
};

}  // namespace icf
