#include "check.h"
#include "Source/CoreSVG/Document.h"
#include "Source/CoreSVG/Paint.h"

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

// Paint IS read now -- `fill`, `stroke`, `fill-rule`, the opacities, the widths
// and `style`. What is still not read is named, and the list is short and
// deliberate: a class nothing matches is named with the class, and `opacity` is
// group compositing rather than paint.
TEST_CASE(document_reports_the_paint_it_still_does_not_read) {
    auto d = SvgDocument::parse(
        R"(<svg viewBox="0 0 10 10"><path d="M1 1" fill="#ff0000" stroke="black"
            class="a" opacity="0.5" mix-blend-mode="multiply"/></svg>)");
    REQUIRE(d.has_value());
    CHECK(d->unsupported().count("paint:fill") == 0);     // read
    CHECK(d->unsupported().count("paint:stroke") == 0);   // read
    CHECK(d->unsupported().count("class:a") == 1);        // no rule matches it
    CHECK(d->unsupported().count("paint:opacity") == 1);  // group compositing
    CHECK(d->unsupported().count("paint:mix-blend-mode") == 1);
}

// A colour this reader cannot read is named WITH ITS VALUE. `currentColor`
// occurs zero times in the corpus and needs an inherited `color` property that
// nothing here tracks -- so a file that carries one says which value, rather
// than being painted a plausible black.
TEST_CASE(document_names_the_paint_value_it_could_not_read) {
    auto d = SvgDocument::parse(
        R"SVG(<svg viewBox="0 0 10 10"><path d="M1 1" fill="currentColor"/></svg>)SVG");
    REQUIRE(d.has_value());
    CHECK(d->unsupported().count("paint:fill=currentColor") == 1);
    // And the shape keeps the INHERITED fill rather than an invented one.
    CHECK(d->shapes[0].fill.kind == PaintKind::Color);
}

// Gradients are collected now. A filter is not -- and `defs` still names what it
// holds that nobody reads.
TEST_CASE(document_reports_what_is_defined_but_not_understood) {
    auto d = SvgDocument::parse(R"(<svg viewBox="0 0 10 10"><defs>
        <linearGradient id="g"><stop offset="0"/></linearGradient>
        <filter id="f"><feBlend/></filter></defs><path d="M1 1"/></svg>)");
    REQUIRE(d.has_value());
    CHECK(d->unsupported().count("defs:linearGradient") == 0);  // collected
    CHECK(d->unsupported().count("defs:filter") == 1);
    CHECK(d->gradients.count("g") == 1);
}

TEST_CASE(document_does_not_report_what_it_deliberately_ignores) {
    auto d = SvgDocument::parse(R"(<svg viewBox="0 0 10 10"><title>x</title><metadata/></svg>)");
    REQUIRE(d.has_value());
    // `title` and `metadata` carry nothing to draw. Reporting them as
    // unsupported would drown the real findings in noise.
    CHECK(d->unsupported().empty());
}

// ---- paint on a shape ---------------------------------------------------

namespace {
std::string paintText(const Paint& p) {
    switch (p.kind) {
        case PaintKind::None: return "none";
        case PaintKind::Reference: return "url:" + p.reference;
        case PaintKind::Unreadable: return "unreadable";
        case PaintKind::Color: {
            auto r = [](double v) { return std::to_string(static_cast<int>(v * 1000 + 0.5)); };
            return std::string(p.color.displayP3 ? "p3 " : "srgb ") + r(p.color.r) + "," +
                   r(p.color.g) + "," + r(p.color.b) + "," + r(p.color.a);
        }
    }
    return "?";
}
}  // namespace

// SVG's initial values, and they are not symmetric: a shape with no `fill`
// attribute is BLACK, and one with no `stroke` is not stroked at all.
TEST_CASE(shape_defaults_to_black_fill_and_no_stroke) {
    auto d = SvgDocument::parse(R"(<svg viewBox="0 0 10 10"><path d="M1 1"/></svg>)");
    REQUIRE(d.has_value());
    REQUIRE(d->shapes.size() == 1);
    CHECK_EQ(paintText(d->shapes[0].fill), std::string("srgb 0,0,0,1000"));
    CHECK_EQ(paintText(d->shapes[0].stroke), std::string("none"));
}

// Paint inherits. A group that sets `fill` sets it for every shape under it
// that does not say otherwise -- and 201 groups in the corpus rely on that.
TEST_CASE(shape_inherits_paint_from_its_group) {
    auto d = SvgDocument::parse(R"(<svg viewBox="0 0 10 10"><g fill="#ff0000">
        <path d="M1 1"/><path d="M2 2" fill="#00ff00"/></g></svg>)");
    REQUIRE(d.has_value());
    REQUIRE(d->shapes.size() == 2);
    CHECK_EQ(paintText(d->shapes[0].fill), std::string("srgb 1000,0,0,1000"));
    CHECK_EQ(paintText(d->shapes[1].fill), std::string("srgb 0,1000,0,1000"));
}

// The `style` attribute outranks the presentation attribute -- that is CSS's
// rule, and 67 of 149 files put their paint there.
TEST_CASE(style_attribute_outranks_the_presentation_attribute) {
    auto d = SvgDocument::parse(
        R"(<svg viewBox="0 0 10 10"><path d="M1 1" fill="#ff0000" style="fill:#0000ff"/></svg>)");
    REQUIRE(d.has_value());
    CHECK_EQ(paintText(d->shapes[0].fill), std::string("srgb 0,0,1000,1000"));
}

TEST_CASE(shape_reads_its_fill_rule_and_stroke_width) {
    auto d = SvgDocument::parse(
        R"(<svg viewBox="0 0 10 10"><path d="M1 1" fill-rule="evenodd" stroke="black" stroke-width="2.5"/></svg>)");
    REQUIRE(d.has_value());
    CHECK(d->shapes[0].fillRule == FillRule::EvenOdd);
    CHECK(d->shapes[0].strokeWidth == 2.5);
}

TEST_CASE(shape_multiplies_its_opacity_into_the_paint) {
    auto d = SvgDocument::parse(
        R"(<svg viewBox="0 0 10 10"><path d="M1 1" fill="#ffffff" fill-opacity="0.5"/></svg>)");
    REQUIRE(d.has_value());
    CHECK_EQ(paintText(d->shapes[0].fill), std::string("srgb 1000,1000,1000,500"));
}

// ---- gradients ----------------------------------------------------------

// 165 paint values are `url(...)`, and 161 gradients are defined to answer
// them. A document that collects the shapes and not the gradients answers the
// most common question in the corpus with nothing.
TEST_CASE(document_collects_the_gradients_defined_in_defs) {
    auto d = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10"><defs>
        <linearGradient id="g" x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stop-color="#ff0000"/>
          <stop offset="1" stop-color="#0000ff" stop-opacity="0.5"/>
        </linearGradient></defs>
        <path d="M1 1" fill="url(#g)"/></svg>)SVG");
    REQUIRE(d.has_value());
    REQUIRE(d->gradients.count("g") == 1);
    const auto& g = d->gradients.at("g");
    CHECK(g.kind == GradientKind::Linear);
    REQUIRE(g.stops.size() == 2);
    CHECK(g.stops[0].offset == 0.0);
    CHECK_EQ(paintText(Paint{PaintKind::Color, g.stops[1].color, ""}),
             std::string("srgb 0,0,1000,500"));
    CHECK_EQ(paintText(d->shapes[0].fill), std::string("url:g"));
}

TEST_CASE(document_reads_a_radial_gradient) {
    auto d = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10"><defs>
        <radialGradient id="r" cx="5" cy="5" r="4"><stop offset="0" stop-color="black"/>
        </radialGradient></defs><path d="M1 1"/></svg>)SVG");
    REQUIRE(d.has_value());
    REQUIRE(d->gradients.count("r") == 1);
    CHECK(d->gradients.at("r").kind == GradientKind::Radial);
    CHECK(d->gradients.at("r").radius == 4.0);
}

// A stop can put its colour in `style` instead of in `stop-color`, and 34 of
// them do.
TEST_CASE(document_reads_a_stop_whose_colour_is_in_its_style) {
    auto d = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10"><defs>
        <linearGradient id="g"><stop offset="0" style="stop-color:#00ff00;stop-opacity:1"/>
        </linearGradient></defs><path d="M1 1"/></svg>)SVG");
    REQUIRE(d.has_value());
    REQUIRE(d->gradients.count("g") == 1);
    REQUIRE(d->gradients.at("g").stops.size() == 1);
    CHECK_EQ(paintText(Paint{PaintKind::Color, d->gradients.at("g").stops[0].color, ""}),
             std::string("srgb 0,1000,0,1000"));
}

// ---- rounded rectangles -------------------------------------------------

// 40 rects in 15 files carry `rx`. They were drawn with sharp corners and
// reported; now they are drawn.
TEST_CASE(rect_with_rx_gets_rounded_corners) {
    auto d = SvgDocument::parse(
        R"(<svg viewBox="0 0 10 10"><rect x="0" y="0" width="10" height="10" rx="2"/></svg>)");
    REQUIRE(d.has_value());
    REQUIRE(d->shapes.size() == 1);
    // Move, then four sides and four corner arcs, then close.
    CHECK(d->shapes[0].path.segments.size() == 10);
    CHECK(d->unsupported().count("rect:rounded") == 0);
}
