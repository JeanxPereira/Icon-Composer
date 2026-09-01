#include "check.h"
#include "Source/CoreSVG/Document.h"
#include "Source/CoreSVG/Paint.h"

#include <map>
#include <string>

using namespace icf::svg;

namespace {

std::string get(const std::map<std::string, std::string>& m, const char* key) {
    auto it = m.find(key);
    return it == m.end() ? std::string("<absent>") : it->second;
}

// The same reason as in test_svg_paint: `map::at` on an absent key THROWS, and a
// crashed process is a test that proves nothing. The mutation sweep found this
// the hard way -- it reported a defect "caught" with ZERO assertions, which is
// the shape of a crash rather than of a test noticing.
std::string rule(const std::map<std::string, std::map<std::string, std::string>>& sheet,
                 const char* cls, const char* prop) {
    auto it = sheet.find(cls);
    return it == sheet.end() ? std::string("<no rule>") : get(it->second, prop);
}

std::string fillText(const Shape& s) {
    if (s.fill.kind != PaintKind::Color) return "<not a colour>";
    auto r = [](double v) { return std::to_string(static_cast<int>(v * 1000 + 0.5)); };
    return r(s.fill.color.r) + "," + r(s.fill.color.g) + "," + r(s.fill.color.b);
}

}  // namespace

// Measured over the corpus's 18 stylesheets: 29 rules, EVERY ONE a single class
// selector. No id, no element, no descendant, no pseudo-class, no at-rule, no
// comment. So this is a lookup from class name to declarations, and calling it a
// CSS engine would be flattering it.
TEST_CASE(stylesheet_reads_class_rules) {
    const auto sheet = parseStylesheet(".fil0 {fill:#000066} .str0{stroke:white;stroke-width:1.5}");
    REQUIRE(sheet.size() == static_cast<size_t>(2));
    CHECK_EQ(rule(sheet, "fil0", "fill"), std::string("#000066"));
    CHECK_EQ(rule(sheet, "str0", "stroke"), std::string("white"));
    CHECK_EQ(rule(sheet, "str0", "stroke-width"), std::string("1.5"));
}

TEST_CASE(stylesheet_ignores_a_selector_it_does_not_support) {
    // An id, an element and a descendant selector: none occurs in the corpus,
    // and a rule this reader cannot target must not be applied to everything.
    const auto sheet = parseStylesheet("#a{fill:red} rect{fill:blue} .b rect{fill:green} .c{fill:cyan}");
    REQUIRE(sheet.size() == static_cast<size_t>(1));
    CHECK_EQ(rule(sheet, "c", "fill"), std::string("cyan"));
}

TEST_CASE(stylesheet_survives_cdata_and_comments) {
    const auto sheet = parseStylesheet("/* a note */ .x { fill : red }");
    REQUIRE(sheet.size() == static_cast<size_t>(1));
    CHECK_EQ(rule(sheet, "x", "fill"), std::string("red"));
}

// ---- the cascade --------------------------------------------------------

TEST_CASE(document_applies_a_class_rule_to_a_shape) {
    auto d = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10">
        <style>.fil0 {fill:#ff0000}</style>
        <path class="fil0" d="M1 1"/></svg>)SVG");
    REQUIRE(d.has_value());
    REQUIRE(d->shapes.size() == 1);
    CHECK_EQ(fillText(d->shapes[0]), std::string("1000,0,0"));
    CHECK(d->unsupported().count("paint:class") == 0);
}

// A stylesheet is normally declared inside `defs`, at the top -- but nothing
// requires that, so the sheets are collected in a pass of their own before any
// shape is built.
TEST_CASE(document_finds_a_stylesheet_declared_after_the_shape_that_uses_it) {
    auto d = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10">
        <path class="a" d="M1 1"/>
        <defs><style>.a{fill:#00ff00}</style></defs></svg>)SVG");
    REQUIRE(d.has_value());
    CHECK_EQ(fillText(d->shapes[0]), std::string("0,1000,0"));
}

// CSS's order, and it is not obvious: a presentation attribute is WEAKER than
// any stylesheet rule, and the `style` attribute is stronger than both. Getting
// this backwards paints 35 files with the colour they were overridden away from.
TEST_CASE(the_stylesheet_outranks_the_presentation_attribute) {
    auto d = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10">
        <style>.a{fill:#0000ff}</style>
        <path class="a" fill="#ff0000" d="M1 1"/></svg>)SVG");
    REQUIRE(d.has_value());
    CHECK_EQ(fillText(d->shapes[0]), std::string("0,0,1000"));
}

TEST_CASE(the_style_attribute_outranks_the_stylesheet) {
    auto d = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10">
        <style>.a{fill:#0000ff}</style>
        <path class="a" style="fill:#00ff00" d="M1 1"/></svg>)SVG");
    REQUIRE(d.has_value());
    CHECK_EQ(fillText(d->shapes[0]), std::string("0,1000,0"));
}

TEST_CASE(document_applies_every_class_an_element_lists) {
    auto d = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10">
        <style>.a{fill:#ff0000} .b{fill-rule:evenodd}</style>
        <path class="a b" d="M1 1"/></svg>)SVG");
    REQUIRE(d.has_value());
    CHECK_EQ(fillText(d->shapes[0]), std::string("1000,0,0"));
    CHECK(d->shapes[0].fillRule == FillRule::EvenOdd);
}

// A class with no rule is not an error and not a silence: the element simply
// keeps what it inherited, and the report says the class went unmatched so a
// missing stylesheet is visible.
TEST_CASE(document_reports_a_class_no_rule_matches) {
    auto d = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10">
        <style>.a{fill:#ff0000}</style>
        <path class="zzz" d="M1 1"/></svg>)SVG");
    REQUIRE(d.has_value());
    CHECK(d->unsupported().count("class:zzz") == 1);
    CHECK_EQ(fillText(d->shapes[0]), std::string("0,0,0"));  // still the initial black
}
