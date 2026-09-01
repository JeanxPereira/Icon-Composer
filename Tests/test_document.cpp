#include "check.h"
#include "Source/IconComposerFoundation/IconDocument.h"

using namespace icf;

// The vocabularies are closed -- doc 01 §6 read every case out of the binary.
// A value outside them is not a value this format can carry, and saying so is
// the whole point of having a typed layer over the JSON.
TEST_CASE(appearance_parses_its_four_cases) {
    CHECK(appearanceFromString("base") == Appearance::Base);
    CHECK(appearanceFromString("light") == Appearance::Light);
    CHECK(appearanceFromString("dark") == Appearance::Dark);
    CHECK(appearanceFromString("tinted") == Appearance::Tinted);
}

TEST_CASE(appearance_rejects_anything_else) {
    CHECK(!appearanceFromString("Dark").has_value());   // the format is lowercase
    CHECK(!appearanceFromString("").has_value());
    CHECK(!appearanceFromString("vibrant").has_value());
}

TEST_CASE(idiom_parses_its_five_cases) {
    CHECK(idiomFromString("base") == Idiom::Base);
    CHECK(idiomFromString("square") == Idiom::Square);
    CHECK(idiomFromString("iOS") == Idiom::IOS);
    CHECK(idiomFromString("macOS") == Idiom::MacOS);
    CHECK(idiomFromString("watchOS") == Idiom::WatchOS);
}

// ---- resolution ---------------------------------------------------------
//
// Every appearance property comes in a pair: the value, and a list of
// conditional overrides. The rule below is MEASURED over the corpus's 890 lists
// (doc 01 §5), not assumed: among the entries whose predicate matches, the most
// specific wins, and an entry with no predicate is the default.

namespace {
icf::json::Value obj(const char* text) {
    auto v = icf::json::parse(text);
    return *v;  // the snippets here are literals; a failure to parse is a bug in the test
}
std::string text(const icf::json::Value* v) {
    return v ? icf::json::write(*v) : std::string("<none>");
}
}  // namespace

TEST_CASE(resolve_falls_back_to_the_plain_property) {
    auto o = obj(R"({"opacity":0.5})");
    CHECK_EQ(text(resolve(o, "opacity", {Appearance::Dark, Idiom::Square})), std::string("0.5"));
}

TEST_CASE(resolve_uses_the_unconstrained_entry_as_the_default) {
    auto o = obj(R"({"opacity-specializations":[{"value":0.25}]})");
    CHECK_EQ(text(resolve(o, "opacity", {Appearance::Light, Idiom::Base})), std::string("0.25"));
}

TEST_CASE(resolve_prefers_a_matching_appearance_over_the_default) {
    auto o = obj(R"({"opacity-specializations":[{"value":0.25},{"appearance":"dark","value":1}]})");
    CHECK_EQ(text(resolve(o, "opacity", {Appearance::Dark, Idiom::Base})), std::string("1"));
}

TEST_CASE(resolve_ignores_a_specialization_whose_appearance_differs) {
    auto o = obj(R"({"opacity-specializations":[{"value":0.25},{"appearance":"dark","value":1}]})");
    CHECK_EQ(text(resolve(o, "opacity", {Appearance::Light, Idiom::Base})), std::string("0.25"));
}

// Two lists in the corpus carry an `appearance` entry beside an
// `appearance`+`idiom` one. That is the only place specificity decides, and it
// is the reason the rule is "most specific wins" rather than "first match wins".
TEST_CASE(resolve_prefers_the_more_specific_predicate) {
    auto o = obj(R"({"opacity-specializations":[
        {"appearance":"dark","value":1},
        {"appearance":"dark","idiom":"square","value":2}]})");
    CHECK_EQ(text(resolve(o, "opacity", {Appearance::Dark, Idiom::Square})), std::string("2"));
    CHECK_EQ(text(resolve(o, "opacity", {Appearance::Dark, Idiom::MacOS})), std::string("1"));
}

// 190 of the 890 lists carry no unconstrained entry. A context that matches none
// of them has no specialized value, and the plain property is what remains --
// which may itself be absent.
TEST_CASE(resolve_reports_nothing_when_neither_side_provides_a_value) {
    auto o = obj(R"({"opacity-specializations":[{"appearance":"dark","value":1}]})");
    CHECK(resolve(o, "opacity", {Appearance::Light, Idiom::Base}) == nullptr);
}

TEST_CASE(resolve_prefers_a_specialization_over_the_plain_property) {
    auto o = obj(R"({"opacity":0.5,"opacity-specializations":[{"appearance":"dark","value":1}]})");
    CHECK_EQ(text(resolve(o, "opacity", {Appearance::Dark, Idiom::Base})), std::string("1"));
    CHECK_EQ(text(resolve(o, "opacity", {Appearance::Light, Idiom::Base})), std::string("0.5"));
}

// ---- the document -------------------------------------------------------

// `groups` and `supported-platforms` are in 145 of 145 documents -- they are the
// obligatory core (doc 01 §2). Anything without them is not a `.icon`.
TEST_CASE(document_rejects_what_is_not_an_icon) {
    auto notAnObject = obj("[1,2]");
    CHECK(!IconDocument::open(notAnObject).has_value());
    auto noGroups = obj(R"({"fill":"none"})");
    CHECK(!IconDocument::open(noGroups).has_value());
}

TEST_CASE(document_exposes_its_groups_and_their_layers) {
    auto d = obj(R"({"groups":[
        {"name":"Bottom","layers":[{"name":"back","image-name":"1.svg"}]},
        {"name":"Top","layers":[{"name":"mid","image-name":"2.svg"},
                                {"name":"front","image-name":"3.svg"}]}]})");
    auto doc = IconDocument::open(d);
    REQUIRE(doc.has_value());
    auto groups = doc->groups();
    REQUIRE(groups.size() == 2);
    CHECK_EQ(std::string(groups[0].name()), std::string("Bottom"));
    CHECK_EQ(std::string(groups[1].name()), std::string("Top"));
    REQUIRE(groups[1].layers().size() == 2);
    CHECK_EQ(std::string(groups[1].layers()[1].name()), std::string("front"));
    CHECK_EQ(std::string(groups[1].layers()[1].imageName()), std::string("3.svg"));
}

// ---- coverage -----------------------------------------------------------
//
// A typed layer that silently ignores a key it does not know is worse than no
// typed layer: it reports success on a document it only half understood. So the
// model can be ASKED what it did not recognise, and the corpus gate turns that
// into the proof that it knows the format (Tests/test_corpus.cpp).

TEST_CASE(document_reports_a_key_it_does_not_know) {
    auto d = obj(R"({"groups":[{"name":"g","layers":[{"name":"l","wibble":1}]}],"frobnicate":2})");
    auto doc = IconDocument::open(d);
    REQUIRE(doc.has_value());
    auto unknown = doc->unknownKeys();
    REQUIRE(unknown.size() == 2);
    CHECK_EQ(unknown[0], std::string("frobnicate"));
    CHECK_EQ(unknown[1], std::string("groups[0]/layers[0]/wibble"));
}

TEST_CASE(document_knows_the_keys_the_format_uses) {
    auto d = obj(R"({"supported-platforms":{"squares":"shared"},
                     "groups":[{"name":"g","hidden":false,"blur-material":null,
                                "shadow":{"kind":"neutral","opacity":0.5},
                                "layers":[{"name":"l","image-name":"a.svg","glass":true,
                                           "position":{"scale":1,"translation-in-points":[0,0]},
                                           "fill":{"solid":"srgb:1,1,1,1"}}]}]})");
    auto doc = IconDocument::open(d);
    REQUIRE(doc.has_value());
    auto unknown = doc->unknownKeys();
    for (const auto& k : unknown) std::printf("    unexpected: %s\n", k.c_str());
    CHECK(unknown.empty());
}

// The shape of a specialization's `value` is the shape of the property it
// specializes -- `fill-specializations[].value` is a fill. Walking it under any
// other assumption would either miss keys or invent them.
TEST_CASE(document_walks_a_specialization_value_as_its_own_property) {
    auto d = obj(R"({"groups":[{"layers":[
        {"fill-specializations":[{"appearance":"dark","value":{"solid":"srgb:1,1,1,1"}},
                                 {"value":{"nonsense":1}}]}]}]})");
    auto doc = IconDocument::open(d);
    REQUIRE(doc.has_value());
    auto unknown = doc->unknownKeys();
    REQUIRE(unknown.size() == 1);
    CHECK_EQ(unknown[0], std::string("groups[0]/layers[0]/fill-specializations[1]/value/nonsense"));
}

// A layer's own properties resolve through the same rule as everything else --
// the layer IS the owner. This is what the render layer will actually call.
TEST_CASE(layer_resolves_its_own_properties_for_a_context) {
    auto d = obj(R"({"groups":[{"layers":[
        {"name":"a","fill-specializations":[{"value":{"solid":"srgb:1,1,1,1"}},
                                            {"appearance":"dark","value":"none"}]}]}]})");
    auto doc = IconDocument::open(d);
    REQUIRE(doc.has_value());
    auto layer = doc->groups()[0].layers()[0];
    CHECK_EQ(text(layer.resolve("fill", {Appearance::Dark, Idiom::Base})), std::string("\"none\""));
    CHECK_EQ(text(layer.resolve("fill", {Appearance::Light, Idiom::Base})),
             std::string("{\n  \"solid\" : \"srgb:1,1,1,1\"\n}"));
}
