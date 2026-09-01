#include "check.h"
#include "Source/IconComposerFoundation/Json.h"

using icf::json::parse;
using icf::json::write;

// The corpus gate is byte-exactness against 135 documents Apple's own encoder
// wrote (doc 01). A reader that turns every number into a double cannot pass it:
// `1` comes back `1.0` and `0.70000000000000007` comes back rounded. So the
// number's SPELLING is the thing that must survive, not its value.
TEST_CASE(json_integer_keeps_its_spelling) {
    auto v = parse("1");
    REQUIRE(v.has_value());
    CHECK_EQ(write(*v), std::string("1"));
}

// Measured over the 145-document corpus: numbers run to twenty decimal places.
// Re-emitting a double gives the shortest representation, which is a DIFFERENT
// string, and the gate would fail on every one of them.
TEST_CASE(json_long_fraction_keeps_every_digit) {
    auto v = parse("0.70000000000000007");
    REQUIRE(v.has_value());
    CHECK_EQ(write(*v), std::string("0.70000000000000007"));
}

TEST_CASE(json_string_round_trips) {
    auto v = parse("\"squircle-light.svg\"");
    REQUIRE(v.has_value());
    CHECK_EQ(write(*v), std::string("\"squircle-light.svg\""));
}

// `null` is not decoration here: 55 of the 145 documents carry one, and
// `blur-material` uses it to say "no material" -- which is not the same as
// leaving the key out (doc 01 §7).
TEST_CASE(json_null_round_trips) {
    auto v = parse("null");
    REQUIRE(v.has_value());
    CHECK_EQ(write(*v), std::string("null"));
}

TEST_CASE(json_booleans_round_trip) {
    auto t = parse("true");
    auto f = parse("false");
    REQUIRE(t.has_value());
    REQUIRE(f.has_value());
    CHECK_EQ(write(*t), std::string("true"));
    CHECK_EQ(write(*f), std::string("false"));
}

// Apple's encoder puts every element on its own line, indented two spaces per
// level, and closes on a line of its own.
TEST_CASE(json_array_writes_one_element_per_line) {
    auto v = parse("[1,2]");
    REQUIRE(v.has_value());
    CHECK_EQ(write(*v), std::string("[\n  1,\n  2\n]"));
}

// Two things at once, and both are Apple's: a SPACE on each side of the colon,
// which no other encoder emits, and keys in sorted order -- `JSONEncoder`'s
// `.sortedKeys`. All 145 documents have their root keys sorted.
TEST_CASE(json_object_writes_apple_separator_and_sorts_keys) {
    auto v = parse("{\"groups\":1,\"fill\":2}");
    REQUIRE(v.has_value());
    CHECK_EQ(write(*v), std::string("{\n  \"fill\" : 2,\n  \"groups\" : 1\n}"));
}

// A reader that stops at the first complete value would accept a truncated file
// followed by anything and report success. The document is the WHOLE text.
TEST_CASE(json_rejects_trailing_garbage) {
    CHECK(!parse("1 2").has_value());
    CHECK(!parse("{\"a\":1} junk").has_value());
}
