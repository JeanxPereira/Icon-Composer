#include "check.h"
#include "Source/IconComposerFoundation/Json.h"

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace icf::json;

namespace {
std::string slurp(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}
void collectNumbers(const Value& v, std::vector<std::string>& out) {
    switch (v.kind()) {
        case Value::Kind::Number: out.push_back(v.number()); break;
        case Value::Kind::Array: for (const auto& e : v.elements()) collectNumbers(e, out); break;
        case Value::Kind::Object: for (const auto& m : v.members()) collectNumbers(m.second, out); break;
        default: break;
    }
}
}  // namespace

TEST_CASE(json_set_replaces_in_place_and_keeps_sibling_lexemes) {
    auto v = parse(R"({"a" : 0.10000000000000001, "b" : 2})");
    REQUIRE(v.has_value());
    v->set("b", Value::number(3.0));
    CHECK_EQ(v->find("b")->number(), std::string("3"));
    CHECK_EQ(v->find("a")->number(), std::string("0.10000000000000001"));  // untouched
    CHECK_EQ(v->members().size(), std::size_t(2));
}

TEST_CASE(json_set_appends_when_absent_and_erase_removes) {
    auto v = parse(R"({"a" : 1})");
    REQUIRE(v.has_value());
    v->set("z", Value::boolean(true));
    CHECK_EQ(v->members().size(), std::size_t(2));
    CHECK(v->find("z") != nullptr);
    CHECK(v->erase("a"));
    CHECK(!v->erase("a"));
    CHECK(v->find("a") == nullptr);
    CHECK_EQ(write(*v), std::string("{\n  \"z\" : true\n}"));
}

TEST_CASE(json_mutable_find_writes_through) {
    auto v = parse(R"({"groups" : [ { "name" : "x" } ]})");
    REQUIRE(v.has_value());
    Value* groups = v->find("groups");
    REQUIRE(groups != nullptr);
    groups->elements()[0].set("name", Value::string("y"));
    CHECK_EQ(v->find("groups")->elements()[0].find("name")->rawString(), std::string("y"));
}

TEST_CASE(json_number_from_double_spells_like_the_corpus) {
    // spec §2.2: 1.361 floats are the shortest round-trip, 1.152 integers have no ".0".
    CHECK_EQ(Value::number(1.0).number(), std::string("1"));
    CHECK_EQ(Value::number(0.5).number(), std::string("0.5"));
    CHECK_EQ(Value::number(-0.0010000010208841559).number(), std::string("-0.0010000010208841559"));
    CHECK_EQ(Value::number(3.000000106112566e-07).number(), std::string("3.000000106112566e-07"));
    CHECK_EQ(Value::number(1751189635.0).number(), std::string("1751189635"));
}

TEST_CASE(json_number_from_double_reproduces_every_corpus_lexeme) {
    const char* dir = std::getenv("IC_CORPUS_DIR");
    REQUIRE(dir != nullptr);
    std::size_t seen = 0, same = 0;
    std::string firstMiss;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
        if (e.path().filename() != "icon.json") continue;
        auto v = parse(slurp(e.path()));
        REQUIRE(v.has_value());
        std::vector<std::string> lexemes;
        collectNumbers(*v, lexemes);
        for (const auto& lx : lexemes) {
            double d = 0;
            auto r = std::from_chars(lx.data(), lx.data() + lx.size(), d);
            REQUIRE(r.ec == std::errc());
            ++seen;
            if (Value::number(d).number() == lx) ++same;
            else if (firstMiss.empty()) firstMiss = lx + " -> " + Value::number(d).number();
        }
    }
    std::printf("  %zu number lexemes, %zu re-spelled identically %s\n", seen, same,
                firstMiss.empty() ? "" : ("first miss: " + firstMiss).c_str());
    CHECK(seen >= 2500);
    CHECK_EQ(same, seen);
}
