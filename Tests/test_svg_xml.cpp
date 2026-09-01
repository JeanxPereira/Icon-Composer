#include "check.h"
#include "Source/CoreSVG/Xml.h"

using namespace icf::svg;

namespace {
std::string attr(const Element& e, const char* name) {
    const std::string* v = e.attribute(name);
    return v ? *v : std::string("<none>");
}
}  // namespace

TEST_CASE(xml_reads_an_element_with_attributes) {
    auto d = parseXml(R"(<svg width="16" height="16"/>)");
    REQUIRE(d.has_value());
    CHECK_EQ(d->root.name, std::string("svg"));
    CHECK_EQ(attr(d->root, "width"), std::string("16"));
    CHECK_EQ(attr(d->root, "height"), std::string("16"));
    CHECK(d->root.children.empty());
}

TEST_CASE(xml_reads_nested_children_in_order) {
    auto d = parseXml("<svg><g><path/><rect/></g><circle/></svg>");
    REQUIRE(d.has_value());
    REQUIRE(d->root.children.size() == 2);
    CHECK_EQ(d->root.children[0].name, std::string("g"));
    REQUIRE(d->root.children[0].children.size() == 2);
    CHECK_EQ(d->root.children[0].children[1].name, std::string("rect"));
    CHECK_EQ(d->root.children[1].name, std::string("circle"));
}

TEST_CASE(xml_accepts_single_quoted_attribute_values) {
    auto d = parseXml("<svg viewBox='0 0 16 16'/>");
    REQUIRE(d.has_value());
    CHECK_EQ(attr(d->root, "viewBox"), std::string("0 0 16 16"));
}

// Every corpus file opens with a declaration or a doctype or both, and 16 carry
// a `<metadata>` block full of RDF. None of it is drawing.
TEST_CASE(xml_skips_the_declaration_the_doctype_and_comments) {
    auto d = parseXml(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" \"http://www.w3.org/x.dtd\">\n"
        "<!-- a comment with <tags> and \"quotes\" -->\n"
        "<svg><!-- another --><path/></svg>");
    REQUIRE(d.has_value());
    CHECK_EQ(d->root.name, std::string("svg"));
    REQUIRE(d->root.children.size() == 1);
    CHECK_EQ(d->root.children[0].name, std::string("path"));
}

// `<style>` holds CSS, and 71 of the 149 files have one. Its text is not markup
// and must not be read as markup -- a `>` inside a selector would end the
// element early.
TEST_CASE(xml_keeps_the_text_of_an_element) {
    auto d = parseXml("<svg><style>.a > .b { fill: red }</style><title>Hi</title></svg>");
    REQUIRE(d.has_value());
    REQUIRE(d->root.children.size() == 2);
    CHECK_EQ(d->root.children[0].text, std::string(".a > .b { fill: red }"));
    CHECK_EQ(d->root.children[1].text, std::string("Hi"));
}

TEST_CASE(xml_resolves_the_five_predefined_entities) {
    auto d = parseXml(R"(<svg title="a &amp; b &lt;c&gt; &quot;d&quot; &apos;e&apos;"/>)");
    REQUIRE(d.has_value());
    CHECK_EQ(attr(d->root, "title"), std::string("a & b <c> \"d\" 'e'"));
}

TEST_CASE(xml_keeps_a_namespace_prefix_as_part_of_the_name) {
    auto d = parseXml(R"(<svg xmlns:xlink="http://x"><use xlink:href="#a"/></svg>)");
    REQUIRE(d.has_value());
    CHECK_EQ(attr(d->root.children[0], "xlink:href"), std::string("#a"));
}

TEST_CASE(xml_refuses_what_is_not_well_formed) {
    CHECK(!parseXml("<svg>").has_value());              // never closed
    CHECK(!parseXml("<svg></g>").has_value());          // closed by the wrong name
    CHECK(!parseXml("<svg attr=unquoted/>").has_value());
    CHECK(!parseXml("").has_value());                   // no root at all
    CHECK(!parseXml("<!-- only a comment -->").has_value());
}
