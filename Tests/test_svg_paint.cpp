#include "check.h"
#include "Source/CoreSVG/Paint.h"

#include <map>
#include <string>

using namespace icf::svg;

namespace {
std::string colorText(const SvgColor& c) {
    auto r = [](double v) { return std::to_string(static_cast<int>(v * 1000 + 0.5)); };
    return std::string(c.displayP3 ? "p3 " : "srgb ") + r(c.r) + "," + r(c.g) + "," + r(c.b) +
           "," + r(c.a);
}
// `map::at` on an absent key THROWS, which turns a red test into a crashed
// process -- and a crashed process reports nothing about the other cases.
std::string get(const std::map<std::string, std::string>& m, const char* key) {
    auto it = m.find(key);
    return it == m.end() ? std::string("<absent>") : it->second;
}

}  // namespace

// 162 of the corpus's fill/stroke values are six-digit hex, and exactly one is
// three-digit. Both are sRGB.
TEST_CASE(paint_reads_six_and_three_digit_hex) {
    auto a = parsePaint("#ff8000");
    REQUIRE(a.kind == PaintKind::Color);
    CHECK_EQ(colorText(a.color), std::string("srgb 1000,502,0,1000"));

    auto b = parsePaint("#f80");
    REQUIRE(b.kind == PaintKind::Color);
    CHECK_EQ(colorText(b.color), std::string("srgb 1000,533,0,1000"));
}

// `none` is 148 occurrences -- the third most common value there is. It is not
// a colour and not a failure: it means this shape has no fill.
TEST_CASE(paint_reads_none_as_its_own_answer) {
    auto p = parsePaint("none");
    CHECK(p.kind == PaintKind::None);
}

// Only `white` and `black` occur, 41 and 29 times. The other 145 CSS colour
// names appear zero times, so the table this reader carries is two entries long
// -- and a third name is a refusal that gets reported, not a silent black.
TEST_CASE(paint_reads_the_two_colour_names_that_occur_and_no_others) {
    CHECK(parsePaint("white").kind == PaintKind::Color);
    CHECK_EQ(colorText(parsePaint("white").color), std::string("srgb 1000,1000,1000,1000"));
    CHECK_EQ(colorText(parsePaint("black").color), std::string("srgb 0,0,0,1000"));
    CHECK(parsePaint("rebeccapurple").kind == PaintKind::Unreadable);
}

// CSS Color 4, and the same space the `.icon` format writes its colours in
// (doc 01 §8). Reading it as sRGB would shift every one of them.
TEST_CASE(paint_reads_a_display_p3_colour) {
    auto p = parsePaint("color(display-p3 0.0848 0.2611 0.5)");
    REQUIRE(p.kind == PaintKind::Color);
    CHECK_EQ(colorText(p.color), std::string("p3 85,261,500,1000"));
}

TEST_CASE(paint_reads_the_alpha_of_a_display_p3_colour) {
    auto p = parsePaint("color(display-p3 1 0 0 / 0.5)");
    REQUIRE(p.kind == PaintKind::Color);
    CHECK_EQ(colorText(p.color), std::string("p3 1000,0,0,500"));
}

// The single most common paint value in the corpus: 165 of them. A reader that
// cannot follow the reference draws the gradient's fallback, which is nothing.
TEST_CASE(paint_reads_a_reference_to_a_gradient) {
    auto p = parsePaint("url(#grad1)");
    REQUIRE(p.kind == PaintKind::Reference);
    CHECK_EQ(p.reference, std::string("grad1"));

    auto q = parsePaint("url('#g2')");
    CHECK_EQ(q.reference, std::string("g2"));
}

// The corpus report found these AFTER the first probe missed them: several
// `stop-color` values and one `fill` are `rgb()`, with components 0-255. The
// instrument naming the exact value is what turned an invisible gap into a
// twenty-line function.
TEST_CASE(paint_reads_an_rgb_function) {
    auto p = parsePaint("rgb(9,111,124)");
    REQUIRE(p.kind == PaintKind::Color);
    CHECK_EQ(colorText(p.color), std::string("srgb 35,435,486,1000"));
    CHECK(parsePaint("rgb(255, 0, 0)").kind == PaintKind::Color);
    CHECK(parsePaint("rgb(1,2)").kind == PaintKind::Unreadable);
}

TEST_CASE(paint_reports_a_value_it_cannot_read) {
    CHECK(parsePaint("currentColor").kind == PaintKind::Unreadable);
    CHECK(parsePaint("").kind == PaintKind::Unreadable);
    CHECK(parsePaint("#12345").kind == PaintKind::Unreadable);
}

// ---- style="…" ----------------------------------------------------------

// 67 of 149 files put paint in a `style` attribute instead of in attributes of
// its own, and `stop-color` is there 54 times. A reader that looks only at
// attributes misses half the colour in the corpus.
TEST_CASE(style_declarations_are_read_as_properties) {
    const auto s = parseStyle("fill:#ff0000; fill-rule : evenodd ;stop-opacity:0.5");
    CHECK_EQ(s.size(), static_cast<size_t>(3));
    CHECK_EQ(get(s, "fill"), std::string("#ff0000"));
    CHECK_EQ(get(s, "fill-rule"), std::string("evenodd"));
    CHECK_EQ(get(s, "stop-opacity"), std::string("0.5"));
}

TEST_CASE(style_ignores_an_empty_or_malformed_declaration) {
    const auto s = parseStyle(";;fill:red;;nonsense;");
    CHECK_EQ(s.size(), static_cast<size_t>(1));
    CHECK_EQ(get(s, "fill"), std::string("red"));
}
