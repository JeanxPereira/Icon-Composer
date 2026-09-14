#include "check.h"
#include "Source/IconComposerFoundation/Edit.h"
#include "Source/IconComposerFoundation/IconBundle.h"

#include <cstdlib>
#include <filesystem>

using namespace icf;

namespace {
json::Value obj(const char* text) {
    auto v = json::parse(text);
    if (!v) std::abort();
    return *v;
}
const Context kBase{Appearance::Base, Idiom::Base};
const Context kDark{Appearance::Dark, Idiom::Base};
const Context kDarkWatch{Appearance::Dark, Idiom::WatchOS};
}  // namespace

TEST_CASE(edit_node_at_walks_groups_and_layers) {
    json::Value root = obj(R"({"groups" : [ { "name" : "g0", "layers" : [ { "name" : "l0" } ] } ]})");
    CHECK(nodeAt(root, NodePath{}) == &root);
    CHECK(nodeAt(root, NodePath{0, std::nullopt})->find("name")->rawString() == "g0");
    CHECK(nodeAt(root, NodePath{0, 0})->find("name")->rawString() == "l0");
    CHECK(nodeAt(root, NodePath{1, std::nullopt}) == nullptr);
    CHECK(nodeAt(root, NodePath{0, 3}) == nullptr);
}

TEST_CASE(edit_base_scope_without_a_list_writes_the_plain_key) {
    json::Value layer = obj(R"({"name" : "l"})");
    setProperty(layer, "glass", kBase, json::Value::boolean(true));
    CHECK_EQ(json::write(layer), std::string("{\n  \"glass\" : true,\n  \"name\" : \"l\"\n}"));
    CHECK(hasOwnEntry(layer, "glass", kBase));
    CHECK(!hasOwnEntry(layer, "glass", kDark));
    setProperty(layer, "glass", kBase, std::nullopt);
    CHECK(layer.find("glass") == nullptr);
}

TEST_CASE(edit_predicated_scope_moves_the_plain_key_into_index_zero) {
    // spec §4.3 step 2: the plain key becomes the unpredicated entry, and the two
    // never coexist (890 of 890 lists, spec §2.3).
    json::Value layer = obj(R"({"glass" : false})");
    setProperty(layer, "glass", kDark, json::Value::boolean(true));
    CHECK(layer.find("glass") == nullptr);
    const json::Value* list = layer.find("glass-specializations");
    REQUIRE(list != nullptr);
    REQUIRE(list->elements().size() == 2);
    CHECK(list->elements()[0].find("appearance") == nullptr);
    CHECK(list->elements()[0].find("value")->boolean() == false);
    CHECK(list->elements()[1].find("appearance")->rawString() == "dark");
    CHECK(list->elements()[1].find("value")->boolean() == true);
    CHECK(resolve(layer, "glass", kDark)->boolean() == true);
    CHECK(resolve(layer, "glass", kBase)->boolean() == false);
}

TEST_CASE(edit_predicated_scope_with_no_plain_key_creates_a_list_without_a_default) {
    // 274 corpus lists carry no unpredicated entry; writing Dark first must give one of those.
    json::Value layer = obj(R"({"name" : "l"})");
    setProperty(layer, "hidden", kDark, json::Value::boolean(true));
    const json::Value* list = layer.find("hidden-specializations");
    REQUIRE(list != nullptr);
    CHECK_EQ(list->elements().size(), std::size_t(1));
    CHECK(list->elements()[0].find("appearance")->rawString() == "dark");
    CHECK(!hasOwnEntry(layer, "hidden", kBase));
    CHECK(hasOwnEntry(layer, "hidden", kDark));
}

TEST_CASE(edit_base_scope_with_a_list_writes_index_zero) {
    json::Value layer = obj(R"({"glass-specializations" : [ { "appearance" : "dark", "value" : true } ]})");
    setProperty(layer, "glass", kBase, json::Value::boolean(false));
    const json::Value* list = layer.find("glass-specializations");
    REQUIRE(list->elements().size() == 2);
    CHECK(list->elements()[0].find("appearance") == nullptr);
    CHECK(list->elements()[0].find("value")->boolean() == false);
    CHECK(layer.find("glass") == nullptr);
}

TEST_CASE(edit_two_predicates_match_only_their_own_entry) {
    json::Value layer = obj(R"({"name" : "l"})");
    setProperty(layer, "opacity", kDark, json::Value::number(0.5));
    setProperty(layer, "opacity", kDarkWatch, json::Value::number(0.25));
    setProperty(layer, "opacity", kDark, json::Value::number(0.75));  // replaces, not appends
    const json::Value* list = layer.find("opacity-specializations");
    REQUIRE(list->elements().size() == 2);
    CHECK(resolve(layer, "opacity", kDark)->number() == "0.75");
    CHECK(resolve(layer, "opacity", kDarkWatch)->number() == "0.25");
    CHECK(list->elements()[1].find("idiom")->rawString() == "watchOS");
}

TEST_CASE(edit_removing_the_last_override_collapses_back_to_the_plain_key) {
    json::Value layer = obj(R"({"glass" : false})");
    setProperty(layer, "glass", kDark, json::Value::boolean(true));
    setProperty(layer, "glass", kDark, std::nullopt);
    CHECK(layer.find("glass-specializations") == nullptr);
    REQUIRE(layer.find("glass") != nullptr);
    CHECK(layer.find("glass")->boolean() == false);
    // and removing a list that had no default deletes the list entirely
    json::Value other = obj(R"({"name" : "l"})");
    setProperty(other, "hidden", kDark, json::Value::boolean(true));
    setProperty(other, "hidden", kDark, std::nullopt);
    CHECK(other.find("hidden-specializations") == nullptr);
    CHECK(other.find("hidden") == nullptr);
}

TEST_CASE(edit_a_specialization_list_that_is_not_an_array_is_dropped_not_written_into) {
    // `resolve` and `hasOwnEntry` both refuse to walk a `<prop>-specializations` that
    // is not an array. Before the guard, `setProperty` walked it anyway: the write
    // landed in an `elements_` vector that `json::write` never emits for that kind,
    // and the edit disappeared with nothing to say so. No corpus document carries one.

    // At Base scope the malformed sibling goes and the plain key is written.
    json::Value layer = obj(R"({"name" : "l", "glass-specializations" : { "value" : true }})");
    setProperty(layer, "glass", kBase, json::Value::boolean(false));
    CHECK(layer.find("glass-specializations") == nullptr);
    REQUIRE(layer.find("glass") != nullptr);
    CHECK(layer.find("glass")->boolean() == false);
    CHECK(hasOwnEntry(layer, "glass", kBase));
    REQUIRE(resolve(layer, "glass", kBase) != nullptr);
    CHECK(resolve(layer, "glass", kBase)->boolean() == false);

    // At a predicated scope it is replaced by a real list holding that one entry.
    json::Value other = obj(R"({"hidden-specializations" : "not a list"})");
    setProperty(other, "hidden", kDark, json::Value::boolean(true));
    const json::Value* list = other.find("hidden-specializations");
    REQUIRE(list != nullptr);
    REQUIRE(list->kind() == json::Value::Kind::Array);
    CHECK_EQ(list->elements().size(), std::size_t(1));
    REQUIRE(resolve(other, "hidden", kDark) != nullptr);
    CHECK(resolve(other, "hidden", kDark)->boolean() == true);
}

namespace {
constexpr Appearance kA[] = {Appearance::Base, Appearance::Light, Appearance::Dark, Appearance::Tinted};
constexpr Idiom kI[] = {Idiom::Base, Idiom::Square, Idiom::IOS, Idiom::MacOS, Idiom::WatchOS};
const char* kProps[] = {"glass", "hidden", "opacity", "blend-mode", "fill", "shadow", "position",
                        "translucency", "specular", "image-name", "name",
                        "blur-material", "lighting"};

bool coexists(const json::Value& owner) {
    for (const char* p : kProps) {
        if (owner.find(p) && owner.find(std::string(p) + "-specializations")) return true;
    }
    return false;
}
}  // namespace

TEST_CASE(edit_every_corpus_node_survives_a_write_under_every_scope) {
    // For each node and each property that resolves: write the resolved value back
    // under every one of the 20 scopes, read it back, then remove it, and the
    // "never coexist" invariant must hold at every step.
    //
    // Owners are named by PATH and re-resolved through `nodeAt` on every access,
    // never cached as pointers. Restoring a node assigns over its whole storage --
    // restoring a GROUP replaces the `layers` vector its layers live in -- so a
    // pointer captured before a restore refers to freed memory.
    const char* dir = std::getenv("IC_CORPUS_DIR");
    REQUIRE(dir != nullptr);
    std::size_t nodes = 0, writes = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        auto b = IconBundle::open(e.path());
        if (!b) continue;
        json::Value root = b->json();  // a copy to scribble on

        std::vector<NodePath> owners{NodePath{}};
        const json::Value* groups = root.find("groups");
        const std::size_t groupCount = groups ? groups->elements().size() : 0;
        for (std::size_t g = 0; g < groupCount; ++g) {
            owners.push_back(NodePath{g, std::nullopt});
            const json::Value* group = nodeAt(root, NodePath{g, std::nullopt});
            const json::Value* layers = group ? group->find("layers") : nullptr;
            const std::size_t layerCount =
                (layers && layers->kind() == json::Value::Kind::Array) ? layers->elements().size() : 0;
            for (std::size_t l = 0; l < layerCount; ++l) owners.push_back(NodePath{g, l});
        }

        for (const NodePath path : owners) {
            ++nodes;
            for (const char* p : kProps) {
                for (auto a : kA) {
                    for (auto i : kI) {
                        const Context ctx{a, i};
                        json::Value* owner = nodeAt(root, path);
                        REQUIRE(owner != nullptr);
                        if (!resolve(*owner, p, ctx)) continue;
                        // A SENTINEL, never the value already sitting there: writing
                        // back what already resolves cannot tell "wrote it" apart
                        // from "left it", so a setProperty that did nothing would
                        // pass. The sentinel makes the read-back falsifiable.
                        const json::Value sentinel = json::Value::string("ic-sentinel");
                        const std::string before = json::write(*owner);
                        setProperty(*owner, p, ctx, sentinel);
                        ++writes;
                        REQUIRE(!coexists(*owner));
                        REQUIRE(hasOwnEntry(*owner, p, ctx));
                        const json::Value* got = resolve(*owner, p, ctx);
                        REQUIRE(got != nullptr);
                        REQUIRE(json::write(*got) == json::write(sentinel));
                        setProperty(*owner, p, ctx, std::nullopt);
                        REQUIRE(!coexists(*owner));
                        REQUIRE(!hasOwnEntry(*owner, p, ctx));
                        // Put the original back so the next scope sees the corpus and
                        // not our previous write. This assigns over the node's whole
                        // storage, which is why `owner` is re-resolved at the top of
                        // the next iteration rather than reused.
                        auto restored = json::parse(before);
                        REQUIRE(restored.has_value());
                        *owner = std::move(*restored);
                    }
                }
            }
        }
    }
    std::printf("  %zu nodes, %zu scoped writes, invariant held\n", nodes, writes);
    CHECK(nodes >= 700);
    CHECK(writes >= 60000);
}

TEST_CASE(edit_add_group_and_layer_create_the_minimal_node) {
    json::Value root = obj(R"({"groups" : [ ]})");
    CHECK_EQ(addGroup(root, "Back"), std::size_t(0));
    CHECK_EQ(addGroup(root, "Front"), std::size_t(1));
    json::Value* front = nodeAt(root, NodePath{1, std::nullopt});
    REQUIRE(front != nullptr);
    CHECK_EQ(addLayer(*front, "mark", "mark.svg"), std::size_t(0));
    CHECK_EQ(json::write(*nodeAt(root, NodePath{1, 0})),
             std::string("{\n  \"image-name\" : \"mark.svg\",\n  \"name\" : \"mark\"\n}"));
    CHECK_EQ(json::write(*nodeAt(root, NodePath{0, std::nullopt})),
             std::string("{\n  \"layers\" : [\n\n  ],\n  \"name\" : \"Back\"\n}"));
}

TEST_CASE(edit_remove_and_move_act_on_siblings_only) {
    json::Value root = obj(R"({"groups" : [ { "name" : "g", "layers" : [ { "name" : "a" }, { "name" : "b" }, { "name" : "c" } ] } ]})");
    CHECK(!removeNode(root, NodePath{}));
    CHECK(!moveNode(root, NodePath{0, 0}, -1));   // already first
    CHECK(moveNode(root, NodePath{0, 0}, +1));
    CHECK(nodeAt(root, NodePath{0, 0})->find("name")->rawString() == "b");
    CHECK(nodeAt(root, NodePath{0, 1})->find("name")->rawString() == "a");
    CHECK(!moveNode(root, NodePath{0, 2}, +1));   // already last
    CHECK(removeNode(root, NodePath{0, 1}));
    CHECK_EQ(nodeAt(root, NodePath{0, std::nullopt})->find("layers")->elements().size(), std::size_t(2));
    CHECK(!removeNode(root, NodePath{0, 5}));
    CHECK(removeNode(root, NodePath{0, std::nullopt}));
    CHECK_EQ(root.find("groups")->elements().size(), std::size_t(0));
}

TEST_CASE(edit_set_name_writes_the_name_key) {
    json::Value root = obj(R"({"groups" : [ { "name" : "g" } ]})");
    CHECK(setName(root, NodePath{0, std::nullopt}, "renamed"));
    CHECK(nodeAt(root, NodePath{0, std::nullopt})->find("name")->rawString() == "renamed");
    CHECK(!setName(root, NodePath{3, std::nullopt}, "x"));
}
