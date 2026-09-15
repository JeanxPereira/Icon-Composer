#include "check.h"
#include "Source/CoreSVG/Document.h"
#include "Source/CoreSVG/Paint.h"

#include <cmath>

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

// NO `viewBox` FALLS BACK TO THE VIEWPORT, and this test used to assert the
// opposite. Its reason was that all 149 corpus files declare one -- true, and
// not a reason: SVG 1.1 §7.7 says the viewport establishes the user coordinate
// system when `viewBox` is absent, so the box is `0 0 width height`.
//
// `[OBS]` What Apple's CoreSVG does was never measured; this follows the
// specification and says so. The file that exposed it came from outside the
// corpus -- an ordinary `<svg width="1000px" height="1000px">` that this reader
// could not open at all.
TEST_CASE(an_svg_with_no_view_box_takes_its_viewport) {
    auto d = SvgDocument::parse("<svg width='16' height='16'/>");
    REQUIRE(d.has_value());
    CHECK(d->viewBox.x == 0.0);
    CHECK(d->viewBox.y == 0.0);
    CHECK(d->viewBox.width == 16.0);
    CHECK(d->viewBox.height == 16.0);

    // `px` IS the user unit, and it is the spelling real files use.
    auto p = SvgDocument::parse("<svg width='1000px' height='500px'/>");
    REQUIRE(p.has_value());
    CHECK(p->viewBox.width == 1000.0);
    CHECK(p->viewBox.height == 500.0);
}

// What is still refused, and each for its own reason.
TEST_CASE(document_refuses_what_it_cannot_place) {
    // Neither a box nor a viewport: there really is no user space to infer.
    CHECK(!SvgDocument::parse("<svg/>").has_value());
    CHECK(!SvgDocument::parse("<svg width='16'/>").has_value());
    // A percentage is a fraction of a viewport this reader does not have.
    CHECK(!SvgDocument::parse("<svg width='100%' height='100%'/>").has_value());
    // A unit that is not the user unit is refused rather than read as one --
    // and the guard that does it is `numbers()` returning EMPTY at the `c`,
    // not the size check below. A gate mutation proved that by surviving.
    CHECK(!SvgDocument::parse("<svg width='10cm' height='10cm'/>").has_value());
    // What the size check actually guards: a `width` that is not ONE number.
    // `10 20` is not a length, and taking the first of the two would place the
    // art in a viewport nobody wrote.
    CHECK(!SvgDocument::parse("<svg width='10 20' height='16'/>").has_value());
    // Zero and negative are not viewports.
    CHECK(!SvgDocument::parse("<svg width='0' height='16'/>").has_value());
    CHECK(!SvgDocument::parse("<svg width='-4' height='16'/>").has_value());
    // And the root still has to be an `svg`.
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
        <path d="M1 1"/><text>hi</text><symbol id="s"/></svg>)");
    REQUIRE(d.has_value());
    const auto un = d->unsupported();
    CHECK(un.count("text") == 1);
    CHECK(un.count("symbol") == 1);
    CHECK(un.count("path") == 0);
    CHECK(un.count("svg") == 0);

    // `filter` LEFT THIS LIST on 2026-09-09. It is READ now -- collected in a
    // pass of its own, wherever it sits -- so naming it here would be the
    // defect this list exists to prevent, pointed the other way: a report that
    // accuses the reader of a gap it no longer has.
    auto f = SvgDocument::parse(R"(<svg viewBox="0 0 10 10">
        <path d="M1 1"/><filter id="f"><feBlend/></filter></svg>)");
    REQUIRE(f.has_value());
    CHECK(f->unsupported().count("filter") == 0);
    CHECK(f->filters.count("f") == 1);
}

// Paint IS read now -- `fill`, `stroke`, `fill-rule`, the opacities, the widths,
// `opacity` and `style`. What is still not read is named, and the list is short
// and deliberate.
//
// `opacity` LEFT THIS LIST on 2026-09-05. It used to be named here with the
// reason "group compositing rather than paint", which was true of the reader
// and is no longer: the value is read, folded down the tree and composited. A
// property that IS applied and still reports itself as ignored is worse than
// noise -- it teaches whoever reads the report to distrust the ones that are
// real. `[ART]` It survived the change once: the four new tests measured the
// pixel and the folded value and passed either way, and only running the corpus
// documents showed SAP, LaunchNext and PDF-Archiver still naming it.
TEST_CASE(document_reports_the_paint_it_still_does_not_read) {
    auto d = SvgDocument::parse(
        R"(<svg viewBox="0 0 10 10"><path d="M1 1" fill="#ff0000" stroke="black"
            class="a" opacity="0.5" mix-blend-mode="multiply"/></svg>)");
    REQUIRE(d.has_value());
    CHECK(d->unsupported().count("paint:fill") == 0);     // read
    CHECK(d->unsupported().count("paint:stroke") == 0);   // read
    // No `<style>` in this document, so there is no rule that COULD have
    // matched and the class is decoration -- see the test below.
    CHECK(d->unsupported().count("class:a") == 0);
    CHECK(d->unsupported().count("paint:opacity") == 0);  // read, and applied
    CHECK(d->unsupported().count("paint:mix-blend-mode") == 1);
    // Read means REACHED THE SHAPE, not merely swallowed.
    REQUIRE(d->shapes.size() == 1);
    CHECK(std::fabs(d->shapes[0].opacity - 0.5) < 1e-9);
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

// A CLASS IS ONLY ACCUSED WHEN THERE WAS A STYLESHEET TO MATCH IT.
//
// The distinction is measured, not invented: across the corpus the only classes
// that match no rule are PDF-Archiver's four, and that file has no `<style>` at
// all -- they are the exporter's decoration and every one of the four lines was
// a false alarm. Every file that carries BOTH classes and a stylesheet (Apollo's
// four, OneKey's) resolves all of them.
//
// So the corpus cannot exercise the case that MUST stay reported: a stylesheet
// that exists and does not carry the class. This test is the only thing holding
// it, which is why both halves are here.
TEST_CASE(a_class_is_named_only_when_a_stylesheet_could_have_matched_it) {
    auto bare = SvgDocument::parse(
        R"(<svg viewBox="0 0 10 10"><path d="M1 1" class="zz"/></svg>)");
    REQUIRE(bare.has_value());
    CHECK(bare->unsupported().count("class:zz") == 0);

    auto sheeted = SvgDocument::parse(
        R"(<svg viewBox="0 0 10 10"><style>.other { fill: red }</style>
           <path d="M1 1" class="zz"/></svg>)");
    REQUIRE(sheeted.has_value());
    CHECK(sheeted->unsupported().count("class:zz") == 1);
}

// A DEFINITION NOTHING REFERENCES IS NOT A GAP, and one that IS referenced is.
//
// `[ART]` PDF-Archiver carries eight `<filter>` elements and references none of
// them: the Pixodesk exporter emitted them and never wired them up. Naming those
// accused the file of losing something it never used -- and the ruler had the
// same defect, blocking the whole document over it.
TEST_CASE(a_definition_is_named_only_when_something_references_it) {
    auto unused = SvgDocument::parse(R"(<svg viewBox="0 0 10 10"><defs>
        <linearGradient id="g"><stop offset="0"/></linearGradient>
        <filter id="f"><feBlend/></filter></defs><path d="M1 1"/></svg>)");
    REQUIRE(unused.has_value());
    CHECK(unused->unsupported().count("defs:linearGradient") == 0);  // collected
    CHECK(unused->unsupported().count("defs:filter") == 0);          // never used
    CHECK(unused->gradients.count("g") == 1);

    // A custom delimiter, because `url(#f)"` carries the `)"` that would close
    // a bare `R"(`.
    //
    // A REFERENCED FILTER IS NO LONGER EITHER HALF OF THAT PAIR. Both lines
    // used to assert a gap; the chain here is `feBlend` alone, which the target
    // DOES construct, so there is nothing missing to report -- the shape is
    // kept and carries the filter it is under.
    auto used = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10"><defs>
        <filter id="f"><feBlend/></filter></defs>
        <path d="M1 1" filter="url(#f)"/></svg>)SVG");
    REQUIRE(used.has_value());
    CHECK(used->unsupported().count("defs:filter") == 0);
    CHECK(used->unsupported().count("paint:filter") == 0);
    REQUIRE(used->shapes.size() == 1);
    CHECK_EQ(used->shapes[0].filterId, std::string("f"));
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

// ---- filters -------------------------------------------------------------
//
// The rule under all of these is one measurement, not a preference: the target
// builds six primitives and drops the rest at construction time, and the hole
// the drop leaves collapses the chain all the way to "nothing is drawn". See
// `SvgDocument::Filter` for the addresses.

TEST_CASE(the_six_primitives_the_target_builds_are_the_ones_transcribed) {
    const auto& six = SvgDocument::targetFilterPrimitives();
    CHECK_EQ(six.size(), std::size_t(6));
    // The table's own order, `__const:0x327F0` = {0x67,0x5f,0x5b,0x72,0x65,0x78}.
    CHECK_EQ(six[0], std::string("feGaussianBlur"));
    CHECK_EQ(six[1], std::string("feOffset"));
    CHECK_EQ(six[2], std::string("feFlood"));
    CHECK_EQ(six[3], std::string("feComposite"));
    CHECK_EQ(six[4], std::string("feBlend"));
    // NEVER LISTED BY THIS DOCUMENTATION UNTIL 2026-09-09, and it is in the
    // table and has a `drawFeConvolveMatrix` at 0x24F74.
    CHECK_EQ(six[5], std::string("feConvolveMatrix"));
    // And the three the old reading listed that the table does NOT hold.
    for (const char* absent : {"feImage", "feMerge", "feTile"}) {
        bool found = false;
        for (const auto& n : six) found = found || n == absent;
        CHECK(!found);
    }
}

TEST_CASE(a_chain_with_a_primitive_the_target_drops_makes_the_group_draw_nothing) {
    // Figma's inner shadow, which is 17 of the corpus's 18 chains.
    auto doc = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10">
      <g filter="url(#f)"><rect x="0" y="0" width="4" height="4" fill="#f00"/></g>
      <rect x="5" y="5" width="4" height="4" fill="#0f0"/>
      <defs><filter id="f">
        <feFlood flood-opacity="0" result="BackgroundImageFix"/>
        <feColorMatrix in="SourceAlpha" type="matrix" values="0 0 0 0 0" result="hardAlpha"/>
        <feComposite in2="hardAlpha" operator="arithmetic"/>
      </filter></defs></svg>)SVG");
    REQUIRE(doc.has_value());
    // The filtered group is gone; the unfiltered sibling is untouched.
    CHECK_EQ(doc->shapes.size(), std::size_t(1));
    if (!doc->shapes.empty()) CHECK_EQ(firstPoint(doc->shapes[0]), std::string("500,500"));

    auto f = doc->filters.find("f");
    REQUIRE(f != doc->filters.end());
    CHECK(f->second.collapses());
    CHECK_EQ(f->second.dropped.size(), std::size_t(1));
    if (!f->second.dropped.empty()) CHECK_EQ(f->second.dropped[0], std::string("feColorMatrix"));
    // The two the target DOES build survive into the chain.
    CHECK_EQ(f->second.primitives.size(), std::size_t(2));

    // AND IT IS NOT A GAP. Reporting this would accuse the file of losing
    // something the target draws -- the target does not draw it either.
    CHECK_EQ(doc->unsupported().count("paint:filter"), std::size_t(0));
}

TEST_CASE(a_chain_of_only_target_primitives_is_kept_and_the_group_survives) {
    // Delta's `00_stripes.svg`, the corpus's one fully-supported chain.
    auto doc = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10">
      <g filter="url(#b)"><rect x="1" y="1" width="2" height="8" fill="#ccc"/></g>
      <defs><filter id="b" x="0" y="0" width="10" height="10" filterUnits="userSpaceOnUse">
        <feFlood flood-opacity="0" result="BackgroundImageFix"/>
        <feBlend mode="normal" in="SourceGraphic" in2="BackgroundImageFix" result="shape"/>
        <feGaussianBlur stdDeviation="2" result="blurred"/>
      </filter></defs></svg>)SVG");
    REQUIRE(doc.has_value());
    REQUIRE(doc->shapes.size() == 1);
    CHECK_EQ(doc->shapes[0].filterId, std::string("b"));
    CHECK(doc->shapes[0].filterInstance != 0);

    auto f = doc->filters.find("b");
    REQUIRE(f != doc->filters.end());
    CHECK(!f->second.collapses());
    REQUIRE(f->second.primitives.size() == 3);
    CHECK_EQ(f->second.primitives[2].name, std::string("feGaussianBlur"));
    CHECK_EQ(f->second.primitives[2].attribute("stdDeviation"), std::string("2"));
    CHECK_EQ(f->second.primitives[1].attribute("in2"), std::string("BackgroundImageFix"));
    CHECK(f->second.hasRegion);
    CHECK(f->second.userSpace);
    CHECK_EQ(f->second.width, 10.0);
}

TEST_CASE(two_groups_naming_one_filter_are_two_instances) {
    auto doc = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10">
      <g filter="url(#b)"><rect x="0" y="0" width="2" height="2" fill="#f00"/></g>
      <g filter="url(#b)"><rect x="5" y="5" width="2" height="2" fill="#0f0"/></g>
      <defs><filter id="b"><feGaussianBlur stdDeviation="1"/></filter></defs></svg>)SVG");
    REQUIRE(doc.has_value());
    REQUIRE(doc->shapes.size() == 2);
    CHECK_EQ(doc->shapes[0].filterId, std::string("b"));
    CHECK_EQ(doc->shapes[1].filterId, std::string("b"));
    // SAME id, DIFFERENT target: a blur of one group is not a blur of both.
    CHECK(doc->shapes[0].filterInstance != doc->shapes[1].filterInstance);
}

TEST_CASE(a_filter_reference_that_does_not_resolve_is_named_not_collapsed) {
    // Not the same thing as a dropped primitive: nothing was measured about a
    // dangling filter reference, so it is a question rather than a reproduction.
    auto doc = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10">
      <g filter="url(#missing)"><rect x="0" y="0" width="4" height="4" fill="#f00"/></g>
      </svg>)SVG");
    REQUIRE(doc.has_value());
    CHECK_EQ(doc->unsupported().count("paint:filter=url(#missing)"), std::size_t(1));
}

TEST_CASE(a_filter_definition_nothing_references_still_drops_nothing) {
    // The PDF-Archiver case, and the guard on it stays a guard.
    auto doc = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10">
      <rect x="0" y="0" width="4" height="4" fill="#f00"/>
      <defs><filter id="unused"><feColorMatrix values="0"/></filter></defs></svg>)SVG");
    REQUIRE(doc.has_value());
    CHECK_EQ(doc->shapes.size(), std::size_t(1));
    CHECK_EQ(doc->unsupported().count("defs:filter"), std::size_t(0));
    CHECK_EQ(doc->unsupported().count("paint:filter"), std::size_t(0));
}

TEST_CASE(filter_none_is_not_a_filter_at_all) {
    auto doc = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10">
      <g filter="none"><rect x="0" y="0" width="4" height="4" fill="#f00"/></g></svg>)SVG");
    REQUIRE(doc.has_value());
    CHECK_EQ(doc->shapes.size(), std::size_t(1));
    CHECK_EQ(doc->shapes[0].filterInstance, std::size_t(0));
    CHECK_EQ(doc->unsupported().count("paint:filter"), std::size_t(0));
}

// ---- what counts as a gap: reachability, not name pairing ----------------
//
// A `<defs>` child this reader does not draw is a LOSS only if something drawn
// can reach it. Until 2026-09-09 that was answered by pairing `defs:K` against
// `paint:K`, which sees exactly one link -- and got it wrong in BOTH directions.

TEST_CASE(a_definition_reached_through_another_definition_is_still_a_gap) {
    // Figma's embedded raster: a shape fills with a `<pattern>`, the pattern
    // holds a `<use>`, and the `<use>` names an `<image>`. THE NAME PAIRING HID
    // BOTH -- `fill="url(#p)"` is an ordinary paint reference and never writes a
    // `paint:pattern` line, so `defs:pattern` was dropped as unused while the
    // picture really was losing it. `[ART]` Two corpus files, Delta's
    // `delta-text.svg` and `04_symbols.svg`.
    auto doc = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10">
      <rect x="0" y="0" width="4" height="4" fill="url(#p)"/>
      <defs>
        <pattern id="p" patternTransform="rotate(10)" width="1" height="1">
          <use xlink:href="#img"/></pattern>
        <image id="img" width="8" height="8" xlink:href="https://example/x.png"/>
      </defs></svg>)SVG");
    REQUIRE(doc.has_value());
    // BOTH ARE REFUSED, which is what keeps them definitions at all now that the
    // ordinary ones are READ: the pattern names `patternTransform`, which no
    // corpus pattern does and this reader will not invent, and the image points
    // OUTSIDE the file. Art that really is lost, two links from the shape that
    // asked for it.
    CHECK_EQ(doc->unsupported().count("defs:pattern"), std::size_t(1));
    CHECK_EQ(doc->unsupported().count("defs:image"), std::size_t(1));
}

TEST_CASE(a_definition_whose_only_consumer_is_a_collapsed_filter_is_not_a_gap) {
    // Delta's `texture.svg`, in miniature. The rect is the only thing that names
    // the pattern, and its group carries a chain the target collapses -- so the
    // rect is never emitted, the pattern is never reached, and the image behind
    // it is never reached either. Nothing is drawn and nothing is lost.
    auto doc = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10">
      <g filter="url(#f)">
        <rect x="0" y="0" width="4" height="4" fill="url(#p)"/>
      </g>
      <defs>
        <filter id="f"><feColorMatrix values="0"/></filter>
        <pattern id="p" width="1" height="1"><use xlink:href="#img"/></pattern>
        <image id="img" width="8" height="8" xlink:href="data:image/png;base64,AA=="/>
      </defs></svg>)SVG");
    REQUIRE(doc.has_value());
    CHECK_EQ(doc->shapes.size(), std::size_t(0));
    CHECK_EQ(doc->unsupported().count("defs:pattern"), std::size_t(0));
    CHECK_EQ(doc->unsupported().count("defs:image"), std::size_t(0));
}

TEST_CASE(a_definition_nothing_references_at_all_is_not_a_gap) {
    // The PDF-Archiver case, kept as a guard through the rewrite.
    auto doc = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10">
      <rect x="0" y="0" width="4" height="4" fill="#f00"/>
      <defs>
        <pattern id="p" width="1" height="1"><use xlink:href="#img"/></pattern>
        <image id="img" width="8" height="8" xlink:href="data:image/png;base64,AA=="/>
      </defs></svg>)SVG");
    REQUIRE(doc.has_value());
    CHECK_EQ(doc->shapes.size(), std::size_t(1));
    CHECK_EQ(doc->unsupported().count("defs:pattern"), std::size_t(0));
    CHECK_EQ(doc->unsupported().count("defs:image"), std::size_t(0));
}

TEST_CASE(an_unreferenced_definition_of_any_kind_is_not_a_gap) {
    // `[ART]` Jellify's file carries 27 `<inkscape:path-effect>` definitions,
    // every one with an id and NOT ONE referenced, and it used to report the
    // kind as a gap. The reachability walk clears it with no special case --
    // which is the point: an `isIgnorable` test was written here and taken out
    // the same day, because the sweep proved no input could distinguish it.
    auto doc = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10">
      <path d="M1 1" fill="#f00"/>
      <defs>
        <inkscape:path-effect id="pe1" effect="spiro"/>
        <inkscape:path-effect id="pe2" effect="spiro"/>
      </defs></svg>)SVG");
    REQUIRE(doc.has_value());
    CHECK_EQ(doc->unsupported().count("defs:inkscape:path-effect"), std::size_t(0));
}

TEST_CASE(a_stroke_reference_seeds_the_reachability_walk_too) {
    // The fill is the obvious seed and the stroke is the one a rewrite forgets:
    // a shape painted only on its outline reaches its definition just as much.
    auto doc = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10">
      <path d="M1 1 L8 8" fill="none" stroke="url(#p)" stroke-width="2"/>
      <defs>        <pattern id="p" patternTransform="rotate(10)" width="1" height="1">
          <use xlink:href="#img"/></pattern>
        <image id="img" width="8" height="8" xlink:href="https://example/x.png"/>
      </defs></svg>)SVG");
    REQUIRE(doc.has_value());
    CHECK_EQ(doc->unsupported().count("defs:pattern"), std::size_t(1));
    CHECK_EQ(doc->unsupported().count("defs:image"), std::size_t(1));
}

// ---- definitions outside `<defs>` ---------------------------------------
//
// `[BIN]` `SVGReader::parseXMLNode` (CoreSVG.arm64 0x677C) sends a gradient,
// clip path, mask, pattern, filter or style to its collector WHEREVER it stands,
// and resolves references only after the whole tree is read (0x5910, then
// `resolveDefinitions` at 0x5924). Until 2026-09-15 this reader collected a
// gradient only as a direct child of `<defs>` -- and no corpus file noticed,
// because all 161 corpus gradients are one. Adobe Illustrator puts them inside
// the `<g>` of the shape that uses them, and the Icon Composer's own app icon
// came out `6 of 6 layer(s) drawn` and without colour.

// THE CASE NOBODY CAUGHT, in its hardest form: the gradient is outside `<defs>`
// AND after the shape that names it. A reader that collects on the way down in
// one pass fails the second half even once it fixes the first.
TEST_CASE(a_gradient_outside_defs_and_after_its_user_is_still_the_fill) {
    auto d = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10">
        <g id="art">
          <path d="M1 1 L9 1 L9 9 Z" fill="url(#late)"/>
          <linearGradient id="late" gradientUnits="userSpaceOnUse" x1="0" y1="0" x2="10" y2="0">
            <stop offset="0" style="stop-color:#FF0000"/>
            <stop offset="1" style="stop-color:#0000FF"/>
          </linearGradient>
        </g></svg>)SVG");
    REQUIRE(d.has_value());
    REQUIRE(d->gradients.count("late") == 1);
    CHECK_EQ(d->gradients.at("late").stops.size(), std::size_t(2));
    // A DEFINITION DRAWS NOTHING: one shape, the path, and no geometry made of
    // the gradient or its stops.
    REQUIRE(d->shapes.size() == 1);
    CHECK_EQ(d->shapes[0].element, std::string("path"));
    CHECK_EQ(paintText(d->shapes[0].fill), std::string("url:late"));
    // And the report no longer calls it an element the reader does not draw.
    CHECK_EQ(d->unsupported().count("linearGradient"), std::size_t(0));
    CHECK(d->unsupported().empty());
}

// Illustrator 29's exact arrangement, radial as well as linear, and one level
// deeper inside `<defs>` -- `<defs><g><linearGradient>` -- which the target's
// `<defs>` branch reaches through 0x6C38-0x6C78.
TEST_CASE(a_gradient_is_collected_wherever_the_target_parser_reaches) {
    auto d = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10">
        <g><g><radialGradient id="deep" cx="5" cy="5" r="4"><stop offset="0" stop-color="black"/>
        </radialGradient><path d="M1 1" fill="url(#deep)"/></g></g>
        <defs><g><linearGradient id="nested"><stop offset="0" stop-color="white"/></linearGradient></g></defs>
        <symbol id="s"><linearGradient id="insymbol"/></symbol>
        </svg>)SVG");
    REQUIRE(d.has_value());
    CHECK_EQ(d->gradients.count("deep"), std::size_t(1));
    CHECK_EQ(d->gradients.count("nested"), std::size_t(1));
    CHECK_EQ(d->gradients.count("insymbol"), std::size_t(1));
    CHECK_EQ(d->shapes.size(), std::size_t(1));
}

// THE REACH IS THE TARGET'S, NOT THE DOCUMENT'S. `[BIN]` `a` is not an atom in
// `SVGAtom::initializeTable` (0x27D8) and falls to the default branch of
// `parseXMLNode` (0x6CE4), which builds a node WITHOUT reading its children --
// so a gradient in there is never filed, and a reference to it resolves to
// nothing in the target too.
TEST_CASE(a_gradient_where_the_target_parser_does_not_descend_is_not_collected) {
    auto d = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10">
        <a><linearGradient id="ina"/></a><path d="M1 1"/></svg>)SVG");
    REQUIRE(d.has_value());
    CHECK_EQ(d->gradients.count("ina"), std::size_t(0));
}

// THE LAST DEFINITION OF AN ID WINS. `[BIN]` `SVGNode::addDefinitionNode`
// (0x2D648) erases the id's earlier node (0x2D714-0x2D72C) and inserts the new
// one (0x2D768). For clip paths this reader used to APPEND the second to the
// first -- the union of both, which is neither.
TEST_CASE(a_repeated_definition_id_keeps_the_last_one_and_not_the_union) {
    auto d = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10">
        <defs>
          <linearGradient id="g"><stop offset="0" stop-color="#FF0000"/></linearGradient>
          <clipPath id="c"><rect x="0" y="0" width="2" height="2"/></clipPath>
        </defs>
        <g><linearGradient id="g"><stop offset="0" stop-color="#00FF00"/>
             <stop offset="1" stop-color="#0000FF"/></linearGradient>
           <clipPath id="c"><rect x="5" y="5" width="2" height="2"/></clipPath></g>
        <path d="M1 1" fill="url(#g)" clip-path="url(#c)"/></svg>)SVG");
    REQUIRE(d.has_value());
    REQUIRE(d->gradients.count("g") == 1);
    CHECK_EQ(d->gradients.at("g").stops.size(), std::size_t(2));
    REQUIRE(d->clipPaths.count("c") == 1);
    REQUIRE(d->clipPaths.at("c").size() == 1);
    CHECK_EQ(pointText(d->clipPaths.at("c")[0].segments[0].p[0]), std::string("500,500"));
}

// One id on two KINDS of definition is a question the per-kind maps cannot
// answer the target's way (its single map keeps only the later node), so it is
// named instead of quietly answered with the gradient.
TEST_CASE(an_id_shared_by_two_kinds_of_definition_is_named) {
    auto d = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10">
        <defs><linearGradient id="x"/><clipPath id="x"><rect width="1" height="1"/></clipPath></defs>
        <path d="M1 1"/></svg>)SVG");
    REQUIRE(d.has_value());
    CHECK_EQ(d->unsupported().count("id repetido entre definicoes de tipos diferentes: x"),
             std::size_t(1));
}

// The walk returns before the children of a group whose filter collapses --
// correctly, for DRAWING. But a clip path defined in there is a definition the
// target filed at parse time, and a shape outside the group may name it. The
// same for a mask one level inside a `<defs>` child that is not itself a
// definition.
TEST_CASE(a_clip_path_the_walk_never_draws_through_is_still_collected) {
    auto d = SvgDocument::parse(R"SVG(<svg viewBox="0 0 10 10">
        <g filter="url(#f)"><clipPath id="hidden"><rect width="3" height="3"/></clipPath>
           <rect width="4" height="4"/></g>
        <defs><g><mask id="m" maskUnits="userSpaceOnUse"><rect width="2" height="2" fill="white"/></mask></g>
          <filter id="f"><feColorMatrix/></filter></defs>
        <path d="M1 1 L2 2" clip-path="url(#hidden)" mask="url(#m)"/></svg>)SVG");
    REQUIRE(d.has_value());
    CHECK_EQ(d->clipPaths.count("hidden"), std::size_t(1));
    CHECK_EQ(d->masks.count("m"), std::size_t(1));
    // The collapsed group still draws nothing, and the clip's rect is not a shape.
    CHECK_EQ(d->shapes.size(), std::size_t(1));
}
