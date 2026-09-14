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

namespace {
constexpr Appearance kA[] = {Appearance::Base, Appearance::Light, Appearance::Dark, Appearance::Tinted};
constexpr Idiom kI[] = {Idiom::Base, Idiom::Square, Idiom::IOS, Idiom::MacOS, Idiom::WatchOS};
const char* kProps[] = {"glass", "hidden", "opacity", "blend-mode", "fill", "shadow", "position",
                        "translucency", "specular", "image-name", "name"};

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
                        const json::Value* was = resolve(*owner, p, ctx);
                        if (!was) continue;
                        const json::Value copy = *was;
                        const std::string before = json::write(*owner);
                        setProperty(*owner, p, ctx, copy);
                        ++writes;
                        REQUIRE(!coexists(*owner));
                        REQUIRE(hasOwnEntry(*owner, p, ctx));
                        REQUIRE(json::write(*resolve(*owner, p, ctx)) == json::write(copy));
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
}
