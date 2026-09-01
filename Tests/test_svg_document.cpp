#include "check.h"
#include "Source/CoreSVG/Document.h"

using namespace icf::svg;

namespace {

std::string pointText(Point p) {
    auto r = [](double v) {
        return std::to_string(static_cast<long long>(v * 100 + (v < 0 ? -0.5 : 0.5)));
    };
    return r(p.x) + "," + r(p.y);
}

// The first point of the first segment -- enough to say a transform landed.
std::string firstPoint(const Shape& s) {
    if (s.path.segments.empty()) return "<empty>";
    return pointText(s.path.segments[0].p[0]);
}

}  // namespace

// ---- transforms ---------------------------------------------------------

TEST_CASE(transform_reads_translate_with_one_or_two_arguments) {
    auto a = parseTransform("translate(10 20)");
    REQUIRE(a.has_value());
    CHECK_EQ(pointText(a->apply({0, 0})), std::string("1000,2000"));
    auto b = parseTransform("translate(10)");
    REQUIRE(b.has_value());
    CHECK_EQ(pointText(b->apply({0, 0})), std::string("1000,0"));
}

TEST_CASE(transform_reads_scale_matrix_and_rotate) {
    auto s = parseTransform("scale(2 3)");
    REQUIRE(s.has_value());
    CHECK_EQ(pointText(s->apply({1, 1})), std::string("200,300"));

    auto m = parseTransform("matrix(1 0 0 1 5 6)");
    REQUIRE(m.has_value());
    CHECK_EQ(pointText(m->apply({0, 0})), std::string("500,600"));

    auto r = parseTransform("rotate(90)");
    REQUIRE(r.has_value());
    CHECK_EQ(pointText(r->apply({1, 0})), std::string("0,100"));
}

// 62 of 149 files carry a transform, and a list of them composes LEFT TO RIGHT:
// the leftmost is applied last. Composing the other way puts the artwork
// somewhere else entirely.
TEST_CASE(transform_composes_a_list_leftmost_last) {
    auto t = parseTransform("translate(10 0) scale(2)");
    REQUIRE(t.has_value());
    CHECK_EQ(pointText(t->apply({1, 1})), std::string("1200,200"));
}

TEST_CASE(transform_refuses_a_function_it_does_not_know) {
    CHECK(!parseTransform("wobble(3)").has_value());
}

// ---- the document -------------------------------------------------------

TEST_CASE(document_reads_the_view_box) {
    auto d = SvgDocument::parse(R"(<svg viewBox="0 0 1024 1024"/>)");
    REQUIRE(d.has_value());
    CHECK(d->viewBox.width == 1024.0);
    CHECK(d->viewBox.height == 1024.0);
    CHECK(d->shapes.empty());
}

// All 149 corpus files declare one (doc 04 §2). Without it there is no user
// space, and every coordinate in the file means nothing in particular.
TEST_CASE(document_refuses_an_svg_with_no_view_box) {
    CHECK(!SvgDocument::parse("<svg width='16' height='16'/>").has_value());
    CHECK(!SvgDocument::parse("<notsvg viewBox='0 0 1 1'/>").has_value());
}

TEST_CASE(document_flattens_the_tree_into_shapes_in_paint_order) {
    auto d = SvgDocument::parse(R"(<svg viewBox="0 0 10 10">
        <path d="M1 1"/><g><path d="M2 2"/><path d="M3 3"/></g></svg>)");
    REQUIRE(d.has_value());
    REQUIRE(d->shapes.size() == 3);
    CHECK_EQ(firstPoint(d->shapes[0]), std::string("100,100"));
    CHECK_EQ(firstPoint(d->shapes[1]), std::string("200,200"));
    CHECK_EQ(firstPoint(d->shapes[2]), std::string("300,300"));
}

// A group's transform multiplies down. A reader that applies only the element's
// own transform draws every nested group in the wrong place.
TEST_CASE(document_accumulates_the_transform_down_the_tree) {
    auto d = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10">
        <g transform="translate(10 0)"><g transform="translate(0 5)">
            <path d="M1 1"/></g></g></svg>)SVG");
    REQUIRE(d.has_value());
    REQUIRE(d->shapes.size() == 1);
    CHECK_EQ(firstPoint(d->shapes[0]), std::string("1100,600"));
}

TEST_CASE(document_turns_a_rect_into_a_path) {
    auto d = SvgDocument::parse(R"(<svg viewBox="0 0 10 10"><rect x="1" y="2" width="3" height="4"/></svg>)");
    REQUIRE(d.has_value());
    REQUIRE(d->shapes.size() == 1);
    // Move, three lines and a close: the four corners, clockwise from (x, y).
    CHECK(d->shapes[0].path.segments.size() == 5);
    CHECK_EQ(firstPoint(d->shapes[0]), std::string("100,200"));
}

// `defs` holds definitions, not drawing. A reader that walks into it paints
// gradient stops as if they were shapes.
TEST_CASE(document_does_not_paint_what_is_inside_defs) {
    auto d = SvgDocument::parse(R"(<svg viewBox="0 0 10 10">
        <defs><path d="M9 9"/></defs><path d="M1 1"/></svg>)");
    REQUIRE(d.has_value());
    REQUIRE(d->shapes.size() == 1);
    CHECK_EQ(firstPoint(d->shapes[0]), std::string("100,100"));
}

// ---- the coverage report ------------------------------------------------
//
// The same rule as `IconDocument::unknownKeys` (doc 01): a reader that ignores
// what it does not know reports success on a file it understood in part.

TEST_CASE(document_reports_an_element_it_does_not_draw) {
    auto d = SvgDocument::parse(R"(<svg viewBox="0 0 10 10">
        <path d="M1 1"/><text>hi</text><filter id="f"><feBlend/></filter></svg>)");
    REQUIRE(d.has_value());
    const auto un = d->unsupported();
    CHECK(un.count("text") == 1);
    CHECK(un.count("filter") == 1);
    CHECK(un.count("path") == 0);
    CHECK(un.count("svg") == 0);
}

// This layer reads GEOMETRY. It does not read paint -- no fill, no stroke, no
// gradient, no CSS. Staying quiet about that would make `unsupported()` claim a
// file was understood when its every colour was dropped, and the corpus gate
// would report a number that means less than it looks like it means.
TEST_CASE(document_reports_that_it_does_not_read_paint) {
    auto d = SvgDocument::parse(
        R"(<svg viewBox="0 0 10 10"><path d="M1 1" fill="#ff0000" stroke="blue"/></svg>)");
    REQUIRE(d.has_value());
    CHECK(d->unsupported().count("paint:fill") == 1);
    CHECK(d->unsupported().count("paint:stroke") == 1);
}

TEST_CASE(document_reports_a_style_attribute_and_a_class) {
    auto d = SvgDocument::parse(
        R"(<svg viewBox="0 0 10 10"><path d="M1 1" style="fill:red" class="a"/></svg>)");
    REQUIRE(d.has_value());
    CHECK(d->unsupported().count("paint:style") == 1);
    CHECK(d->unsupported().count("paint:class") == 1);
}

// A gradient or a filter inside `defs` is a definition something references. Not
// walking into `defs` to DRAW is right; staying silent about what is in there is
// not -- those are exactly the things a later round has to implement.
TEST_CASE(document_reports_what_is_defined_but_not_understood) {
    auto d = SvgDocument::parse(R"(<svg viewBox="0 0 10 10"><defs>
        <linearGradient id="g"><stop offset="0"/></linearGradient>
        <filter id="f"><feBlend/></filter></defs><path d="M1 1"/></svg>)");
    REQUIRE(d.has_value());
    CHECK(d->unsupported().count("defs:linearGradient") == 1);
    CHECK(d->unsupported().count("defs:filter") == 1);
}

TEST_CASE(document_does_not_report_what_it_deliberately_ignores) {
    auto d = SvgDocument::parse(R"(<svg viewBox="0 0 10 10"><title>x</title><metadata/></svg>)");
    REQUIRE(d.has_value());
    // `title` and `metadata` carry nothing to draw. Reporting them as
    // unsupported would drown the real findings in noise.
    CHECK(d->unsupported().empty());
}
