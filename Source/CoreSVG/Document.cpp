#include "Source/CoreSVG/Document.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace icf::svg {
namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// The numbers inside a transform function or a `viewBox`: whitespace- or
// comma-separated, and the same scientific notation the path grammar allows.
std::vector<double> numbers(std::string_view text) {
    std::vector<double> out;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (isSpace(text[i]) || text[i] == ',')) ++i;
        if (i >= text.size()) break;
        const size_t start = i;
        if (text[i] == '+' || text[i] == '-') ++i;
        while (i < text.size() && ((text[i] >= '0' && text[i] <= '9') || text[i] == '.')) ++i;
        if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
            const size_t e = i++;
            if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
            const size_t d = i;
            while (i < text.size() && text[i] >= '0' && text[i] <= '9') ++i;
            if (i == d) i = e;
        }
        if (i == start) return {};  // something that is not a number at all
        double v = 0;
        auto r = std::from_chars(text.data() + start, text.data() + i, v);
        if (r.ec != std::errc()) return {};
        out.push_back(v);
    }
    return out;
}

std::optional<double> number(const Element& e, std::string_view attr, double fallback) {
    const std::string* v = e.attribute(attr);
    if (!v) return fallback;
    auto n = numbers(*v);
    if (n.size() != 1) return std::nullopt;
    return n[0];
}

Transform translate(double x, double y) { return {1, 0, 0, 1, x, y}; }

// A quarter turn of a circle as a cubic. The constant is the usual one --
// 4/3 * (sqrt(2) - 1) -- and the error against a true arc is under 0.02% of the
// radius, which is far below anything a rasteriser at icon sizes can show.
constexpr double kKappa = 0.5522847498307936;

void appendEllipse(Path& p, double cx, double cy, double rx, double ry) {
    const double ox = rx * kKappa;
    const double oy = ry * kKappa;
    p.segments.push_back({SegmentKind::Move, {{cx + rx, cy}}});
    p.segments.push_back({SegmentKind::Cubic,
                          {{cx + rx, cy + oy}, {cx + ox, cy + ry}, {cx, cy + ry}}});
    p.segments.push_back({SegmentKind::Cubic,
                          {{cx - ox, cy + ry}, {cx - rx, cy + oy}, {cx - rx, cy}}});
    p.segments.push_back({SegmentKind::Cubic,
                          {{cx - rx, cy - oy}, {cx - ox, cy - ry}, {cx, cy - ry}}});
    p.segments.push_back({SegmentKind::Cubic,
                          {{cx + ox, cy - ry}, {cx + rx, cy - oy}, {cx + rx, cy}}});
    p.segments.push_back({SegmentKind::Close, {}});
}

void appendPolygon(Path& p, const std::vector<double>& pts, bool close) {
    for (size_t i = 0; i + 1 < pts.size(); i += 2) {
        p.segments.push_back({i == 0 ? SegmentKind::Move : SegmentKind::Line,
                              {{pts[i], pts[i + 1]}}});
    }
    if (close && !p.segments.empty()) p.segments.push_back({SegmentKind::Close, {}});
}

// A property, looked up the way CSS orders it: the `style` attribute outranks
// the presentation attribute of the same name. 67 of 149 files put their paint
// in `style`, so a reader that consults only attributes misses most of it.
std::optional<std::string> property(const Element& e, std::string_view name,
                                    const std::map<std::string, std::string>& style,
                                    const std::map<std::string, std::string>& fromClass) {
    // CSS's order, and it is not obvious: a PRESENTATION ATTRIBUTE is weaker
    // than any stylesheet rule, and the `style` attribute is stronger than both.
    // Reversing the first two paints 35 corpus files with the colour they were
    // overridden away from.
    auto it = style.find(std::string(name));
    if (it != style.end()) return it->second;
    auto cls = fromClass.find(std::string(name));
    if (cls != fromClass.end()) return cls->second;
    if (const std::string* a = e.attribute(name)) return *a;
    return std::nullopt;
}

std::optional<std::string> property(const Element& e, std::string_view name,
                                    const std::map<std::string, std::string>& style) {
    static const std::map<std::string, std::string> none;
    return property(e, name, style, none);
}

// What a shape inherits from its ancestry. `opacity` is NOT here: it applies to
// a whole group as a composited unit, which is a different idea from paint and
// is still reported as unsupported.
struct Inherited {
    Paint fill = [] { Paint p; p.kind = PaintKind::Color; p.color = {0, 0, 0, 1, false}; return p; }();
    Paint stroke = [] { Paint p; p.kind = PaintKind::None; return p; }();
    FillRule fillRule = FillRule::NonZero;
    double fillOpacity = 1.0;
    double strokeOpacity = 1.0;
    double strokeWidth = 1.0;

    // `opacity` DOES NOT INHERIT -- it MULTIPLIES. Every other field here is
    // replaced by a descendant that names it; this one accumulates, because a
    // group at 0.5 inside a group at 0.5 composites at 0.25. That is why it
    // lives in this struct and is still not "inherited" in SVG's sense.
    double opacityChain = 1.0;
};

// The alpha the paint ends up with. `fill-opacity` is a separate multiplier
// from the colour's own alpha, and both apply.
Paint withOpacity(Paint p, double opacity) {
    if (p.kind == PaintKind::Color) p.color.a *= opacity;
    return p;
}

// A quarter of a rounded corner, as a cubic. Same constant as the ellipse.
void appendCorner(Path& p, Point from, Point to, Point corner, double kx, double ky) {
    p.segments.push_back({SegmentKind::Cubic,
                          {{from.x + kx, from.y + ky},
                           {to.x - kx, to.y - ky},
                           to}});
    (void)corner;
}

// The attributes that decide how a shape LOOKS. None is read by this layer, and
// each is named the moment it appears -- doc 04 §4's rule, applied to paint
// rather than to elements. Without this the report would call a file understood
// while dropping every colour in it.
// What is STILL not read. `fill`, `stroke`, `fill-rule`, `fill-opacity`,
// `stroke-opacity`, `stroke-width` and `style` left this list when they were
// implemented; `class` stays because the stylesheet that would give it meaning
// is not read, and `opacity` stays because group compositing is a different
// idea from paint.
// Presentation attributes this reader sees and does NOT act on. `opacity` left
// this list on 2026-09-05 when it started being read -- and leaving it here
// would have been its own defect: a property that IS applied and still reports
// itself as ignored teaches whoever reads the report to distrust it.
constexpr std::string_view kPaintAttributes[] = {
    "clip-path", "mask", "filter", "clip-rule",
    "stroke-linecap", "stroke-linejoin", "stroke-dasharray", "mix-blend-mode",
};

// Elements that carry nothing to draw. Reporting them as unsupported would bury
// the real findings under noise present in most files.
bool isIgnorable(std::string_view name) {
    return name == "title" || name == "desc" || name == "metadata" ||
           name == "sodipodi:namedview" || name == "inkscape:path-effect";
}

// The builder owns its own outputs; `SvgDocument::parse` is a member and moves
// them in. A friend declaration would have to name a type from this file's
// anonymous namespace, which a header cannot do.
struct Builder {
    std::vector<Shape> shapes;
    std::set<std::string> unsupported;
    std::map<std::string, Gradient> gradients;

    // One element's paint, resolved against what the ancestry set.
    // The stylesheets, collected before anything is drawn.
    std::map<std::string, std::map<std::string, std::string>> sheet;

    // Every `<style>` in the document, wherever it sits. A pass of its own:
    // nothing requires a stylesheet to be declared before the shapes that use
    // it, and 18 corpus files put theirs inside `defs`.
    void collectStyles(const Element& e) {
        if (e.name == "style") {
            for (const auto& [cls, decls] : parseStylesheet(e.text)) {
                for (const auto& d : decls) sheet[cls][d.first] = d.second;
            }
            return;
        }
        for (const auto& c : e.children) collectStyles(c);
    }

    // What this element's `class` list contributes, later classes winning.
    std::map<std::string, std::string> classDeclarations(const Element& e) {
        std::map<std::string, std::string> out;
        const std::string* value = e.attribute("class");
        if (!value) return out;
        size_t i = 0;
        while (i < value->size()) {
            while (i < value->size() && isSpace((*value)[i])) ++i;
            const size_t start = i;
            while (i < value->size() && !isSpace((*value)[i])) ++i;
            if (i == start) break;
            const std::string name = value->substr(start, i - start);
            auto it = sheet.find(name);
            if (it == sheet.end()) {
                // Not an error: the element keeps what it inherited. But a class
                // nothing matches usually means a stylesheet that was not read,
                // and that is worth seeing.
                unsupported.insert("class:" + name);
                continue;
            }
            for (const auto& d : it->second) out[d.first] = d.second;
        }
        return out;
    }

    Inherited resolve(const Element& e, const std::map<std::string, std::string>& style,
                      const std::map<std::string, std::string>& fromClass, Inherited in) {
        if (auto v = property(e, "fill", style, fromClass)) {
            Paint p = parsePaint(*v);
            if (p.kind == PaintKind::Unreadable) unsupported.insert("paint:fill=" + *v);
            else in.fill = p;
        }
        if (auto v = property(e, "stroke", style, fromClass)) {
            Paint p = parsePaint(*v);
            if (p.kind == PaintKind::Unreadable) unsupported.insert("paint:stroke=" + *v);
            else in.stroke = p;
        }
        if (auto v = property(e, "fill-rule", style, fromClass)) {
            if (*v == "evenodd") in.fillRule = FillRule::EvenOdd;
            else if (*v == "nonzero") in.fillRule = FillRule::NonZero;
            else unsupported.insert("paint:fill-rule=" + *v);
        }
        auto scalar = [&](const char* name, double& slot) {
            if (auto v = property(e, name, style, fromClass)) {
                auto n = numbers(*v);
                if (n.size() == 1) slot = n[0];
                else unsupported.insert(std::string("paint:") + name + "=" + *v);
            }
        };
        scalar("fill-opacity", in.fillOpacity);
        scalar("stroke-opacity", in.strokeOpacity);
        scalar("stroke-width", in.strokeWidth);
        // `opacity` multiplies rather than replaces, so it cannot go through
        // `scalar`. Out-of-range values are CLAMPED, which is what SVG 1.1 §14
        // says for this property -- not refused, because a refusal here would
        // drop a shape over a number the specification tells us how to read.
        if (auto v = property(e, "opacity", style, fromClass)) {
            auto n = numbers(*v);
            if (n.size() == 1) {
                in.opacityChain *= std::clamp(n[0], 0.0, 1.0);
            } else {
                unsupported.insert("paint:opacity=" + *v);
            }
        }
        return in;
    }

    // A gradient definition, by id. Stops in document order.
    void collectGradient(const Element& e) {
        const std::string* id = e.attribute("id");
        if (!id) return;
        Gradient g;
        g.kind = (e.name == "radialGradient") ? GradientKind::Radial : GradientKind::Linear;
        auto num = [&](const char* name, double& slot) {
            if (const std::string* v = e.attribute(name)) {
                auto n = numbers(*v);
                if (n.size() == 1) slot = n[0];
            }
        };
        num("x1", g.x1); num("y1", g.y1); num("x2", g.x2); num("y2", g.y2);
        num("cx", g.cx); num("cy", g.cy); num("r", g.radius);
        if (const std::string* u = e.attribute("gradientUnits")) {
            g.userSpace = (*u == "userSpaceOnUse");
        }
        if (const std::string* t = e.attribute("gradientTransform")) {
            if (auto tr = parseTransform(*t)) g.transform = *tr;
            else unsupported.insert("gradientTransform:" + *t);
        }
        if (e.attribute("xlink:href") || e.attribute("href")) {
            // A gradient that inherits another's stops. One occurrence in the
            // corpus, and following it is a second pass this does not make.
            unsupported.insert("gradient:href");
        }
        for (const auto& c : e.children) {
            if (c.name != "stop") continue;
            GradientStop stop;
            const auto style = c.attribute("style")
                                   ? parseStyle(*c.attribute("style"))
                                   : std::map<std::string, std::string>{};
            if (auto v = property(c, "offset", style)) {
                auto n = numbers(*v);
                if (n.size() == 1) stop.offset = n[0];
            }
            if (auto v = property(c, "stop-color", style)) {
                Paint pnt = parsePaint(*v);
                if (pnt.kind == PaintKind::Color) stop.color = pnt.color;
                else unsupported.insert("stop-color=" + *v);
            }
            if (auto v = property(c, "stop-opacity", style)) {
                auto n = numbers(*v);
                if (n.size() == 1) stop.color.a *= n[0];
            }
            g.stops.push_back(stop);
        }
        gradients[*id] = std::move(g);
    }

    // What a shape's paint attributes would have said, had anything read them.
    void notePaint(const Element& e) {
        for (const auto& a : e.attributes) {
            for (auto p : kPaintAttributes) {
                if (a.first == p) unsupported.insert("paint:" + a.first);
            }
        }
    }

    void walk(const Element& e, const Transform& parent, Inherited inherited) {
        const auto style = e.attribute("style")
                               ? parseStyle(*e.attribute("style"))
                               : std::map<std::string, std::string>{};
        inherited = resolve(e, style, classDeclarations(e), inherited);
        Transform here = parent;
        if (const std::string* t = e.attribute("transform")) {
            auto local = parseTransform(*t);
            if (!local) {
                unsupported.insert("transform:" + *t);
                return;
            }
            here = local->then(parent);
        }

        Path path;
        bool drew = false;

        if (e.name == "path") {
            if (const std::string* d = e.attribute("d")) {
                auto parsed = parsePath(*d);
                if (!parsed) {
                    unsupported.insert(
                        parsed.error().unsupportedCommand
                            ? std::string("path:") + parsed.error().unsupportedCommand
                            : std::string("path:malformed"));
                    return;
                }
                path = *parsed;
            }
            drew = true;
        } else if (e.name == "rect") {
            auto x = number(e, "x", 0), y = number(e, "y", 0);
            auto w = number(e, "width", 0), h = number(e, "height", 0);
            if (!x || !y || !w || !h) return;
            // `rx` alone implies `ry`, and the other way round: SVG says an
            // omitted one takes the value of the other, and both clamp to half
            // the side they run along.
            double rx = number(e, "rx", -1).value_or(-1);
            double ry = number(e, "ry", -1).value_or(-1);
            if (rx < 0) rx = ry;
            if (ry < 0) ry = rx;
            if (rx < 0) rx = 0;
            if (ry < 0) ry = 0;
            rx = std::min(rx, *w / 2);
            ry = std::min(ry, *h / 2);
            const double x0 = *x, y0 = *y, x1 = *x + *w, y1 = *y + *h;
            if (rx <= 0 || ry <= 0) {
                path.segments.push_back({SegmentKind::Move, {{x0, y0}}});
                path.segments.push_back({SegmentKind::Line, {{x1, y0}}});
                path.segments.push_back({SegmentKind::Line, {{x1, y1}}});
                path.segments.push_back({SegmentKind::Line, {{x0, y1}}});
                path.segments.push_back({SegmentKind::Close, {}});
            } else {
                const double kx = rx * kKappa, ky = ry * kKappa;
                path.segments.push_back({SegmentKind::Move, {{x0 + rx, y0}}});
                path.segments.push_back({SegmentKind::Line, {{x1 - rx, y0}}});
                path.segments.push_back({SegmentKind::Cubic,
                    {{x1 - rx + kx, y0}, {x1, y0 + ry - ky}, {x1, y0 + ry}}});
                path.segments.push_back({SegmentKind::Line, {{x1, y1 - ry}}});
                path.segments.push_back({SegmentKind::Cubic,
                    {{x1, y1 - ry + ky}, {x1 - rx + kx, y1}, {x1 - rx, y1}}});
                path.segments.push_back({SegmentKind::Line, {{x0 + rx, y1}}});
                path.segments.push_back({SegmentKind::Cubic,
                    {{x0 + rx - kx, y1}, {x0, y1 - ry + ky}, {x0, y1 - ry}}});
                path.segments.push_back({SegmentKind::Line, {{x0, y0 + ry}}});
                path.segments.push_back({SegmentKind::Cubic,
                    {{x0, y0 + ry - ky}, {x0 + rx - kx, y0}, {x0 + rx, y0}}});
                path.segments.push_back({SegmentKind::Close, {}});
            }
            drew = true;
        } else if (e.name == "circle" || e.name == "ellipse") {
            auto cx = number(e, "cx", 0), cy = number(e, "cy", 0);
            std::optional<double> rx, ry;
            if (e.name == "circle") {
                rx = ry = number(e, "r", 0);
            } else {
                rx = number(e, "rx", 0);
                ry = number(e, "ry", 0);
            }
            if (!cx || !cy || !rx || !ry) return;
            appendEllipse(path, *cx, *cy, *rx, *ry);
            drew = true;
        } else if (e.name == "line") {
            auto x1 = number(e, "x1", 0), y1 = number(e, "y1", 0);
            auto x2 = number(e, "x2", 0), y2 = number(e, "y2", 0);
            if (!x1 || !y1 || !x2 || !y2) return;
            path.segments.push_back({SegmentKind::Move, {{*x1, *y1}}});
            path.segments.push_back({SegmentKind::Line, {{*x2, *y2}}});
            drew = true;
        } else if (e.name == "polyline" || e.name == "polygon") {
            const std::string* pts = e.attribute("points");
            if (!pts) return;
            appendPolygon(path, numbers(*pts), e.name == "polygon");
            drew = true;
        } else if (e.name == "svg" || e.name == "g" || e.name == "a") {
            notePaint(e);  // paint inherits, so a group's is a group's shapes'
            // A GROUP'S `opacity` IS NOT ITS SHAPES' OPACITY, and the two only
            // agree when the group holds ONE shape. SVG composites the group's
            // whole rendering once, at that alpha; folding it into each shape
            // composites each one separately, and where two of them overlap the
            // two results differ.
            //
            // `[ART]` In the corpus this is not academic and not universal:
            // SAP's six `<g opacity>` hold exactly one shape each -- identity,
            // not approximation -- while Delta's hold 44, 2, 2, 2, 2 and 2. So
            // the fold happens, and a group that can actually differ is NAMED.
            const std::size_t before = shapes.size();
            for (const auto& c : e.children) walk(c, here, inherited);
            if (inherited.opacityChain < 1.0 && shapes.size() - before > 1) {
                unsupported.insert("opacity de grupo sobre mais de uma forma");
            }
            return;
        } else if (e.name == "defs") {
            // Definitions are referenced, not drawn -- walking in to paint them
            // would put gradient stops on the canvas. But what is DEFINED in
            // there is exactly what a later round has to implement, so each kind
            // is named once.
            for (const auto& c : e.children) {
                if (c.name == "linearGradient" || c.name == "radialGradient") {
                    collectGradient(c);
                } else if (c.name != "style") {  // collected in its own pass
                    unsupported.insert("defs:" + c.name);
                }
            }
            return;
        } else if (e.name == "style") {
            return;  // already collected, and it draws nothing
        } else if (isIgnorable(e.name)) {
            return;  // nothing to draw, and nothing to report
        } else {
            // Everything else is named and not walked into: filters, masks,
            // patterns, clip paths, use, symbol, switch, text, image, style, and
            // whatever an editor invented. Doc 04 §4 is the scope; this is how a
            // file that leaves it says so.
            unsupported.insert(e.name);
            return;
        }

        if (!drew) return;
        notePaint(e);
        for (auto& s : path.segments) {
            const int n = (s.kind == SegmentKind::Cubic) ? 3 : (s.kind == SegmentKind::Close ? 0 : 1);
            for (int i = 0; i < n; ++i) s.p[i] = here.apply(s.p[i]);
        }
        Shape shape;
        shape.path = std::move(path);
        shape.element = e.name;
        shape.fill = withOpacity(inherited.fill, inherited.fillOpacity);
        shape.stroke = withOpacity(inherited.stroke, inherited.strokeOpacity);
        shape.fillRule = inherited.fillRule;
        shape.strokeWidth = inherited.strokeWidth;
        shape.opacity = inherited.opacityChain;
        // A shape composited at less than 1 that has BOTH a fill and a stroke
        // is the other place where folding differs from compositing once: the
        // stroke overlaps the fill along the edge, and two separate composites
        // darken that edge where one would not. `[ART]` No corpus shape does
        // this -- all 24 with `opacity < 1` are unstroked -- so it is named
        // rather than built.
        if (shape.opacity < 1.0 && shape.stroke.kind != PaintKind::None &&
            shape.strokeWidth > 0.0) {
            unsupported.insert("paint:opacity-com-fill-e-stroke");
        }
        shapes.push_back(std::move(shape));
    }
};

}  // namespace

std::optional<Transform> parseTransform(std::string_view text) {
    Transform result;
    size_t i = 0;
    bool any = false;
    while (i < text.size()) {
        while (i < text.size() && (isSpace(text[i]) || text[i] == ',')) ++i;
        if (i >= text.size()) break;
        const size_t nameStart = i;
        while (i < text.size() && text[i] != '(' && !isSpace(text[i])) ++i;
        const std::string_view name = text.substr(nameStart, i - nameStart);
        while (i < text.size() && isSpace(text[i])) ++i;
        if (i >= text.size() || text[i] != '(') return std::nullopt;
        const size_t argStart = ++i;
        while (i < text.size() && text[i] != ')') ++i;
        if (i >= text.size()) return std::nullopt;
        const auto n = numbers(text.substr(argStart, i - argStart));
        ++i;  // ')'

        Transform t;
        if (name == "matrix" && n.size() == 6) {
            t = {n[0], n[1], n[2], n[3], n[4], n[5]};
        } else if (name == "translate" && (n.size() == 1 || n.size() == 2)) {
            t = translate(n[0], n.size() > 1 ? n[1] : 0.0);
        } else if (name == "scale" && (n.size() == 1 || n.size() == 2)) {
            t = {n[0], 0, 0, n.size() > 1 ? n[1] : n[0], 0, 0};
        } else if (name == "rotate" && (n.size() == 1 || n.size() == 3)) {
            const double r = n[0] * 3.14159265358979323846 / 180.0;
            Transform rot{std::cos(r), std::sin(r), -std::sin(r), std::cos(r), 0, 0};
            t = (n.size() == 3)
                    ? translate(-n[1], -n[2]).then(rot).then(translate(n[1], n[2]))
                    : rot;
        } else if (name == "skewX" && n.size() == 1) {
            t = {1, 0, std::tan(n[0] * 3.14159265358979323846 / 180.0), 1, 0, 0};
        } else if (name == "skewY" && n.size() == 1) {
            t = {1, std::tan(n[0] * 3.14159265358979323846 / 180.0), 0, 1, 0, 0};
        } else {
            return std::nullopt;
        }
        // Leftmost applied last: each new function goes UNDER what came before.
        result = t.then(result);
        any = true;
    }
    return any ? std::optional<Transform>(result) : std::nullopt;
}

std::optional<SvgDocument> SvgDocument::parse(std::string_view svg) {
    auto xml = parseXml(svg);
    if (!xml) return std::nullopt;
    if (xml->root.name != "svg") return std::nullopt;

    // NO `viewBox` IS NOT NO USER SPACE. This line used to refuse such a
    // document, with the comment "every coordinate is meaningless" -- and that
    // is wrong: SVG 1.1 §7.7 says the viewport establishes the user coordinate
    // system when `viewBox` is absent, so the effective box is
    // `0 0 width height`.
    //
    // `[ART]` It went unnoticed because ALL 149 corpus SVGs carry a `viewBox`;
    // the first file to hit it came from outside the corpus, and a valid,
    // ordinary SVG could not be opened.
    //
    // `[OBS]` What Apple's CoreSVG does here was NOT measured. The rule below
    // is the SVG specification's, not a transcription, and it is the reason
    // this comment says which of the two it is. A document with neither a
    // `viewBox` nor a `width`/`height` is still refused: then there really is
    // no user space to infer.
    SvgDocument doc;
    const std::string* vb = xml->root.attribute("viewBox");
    if (vb) {
        const auto n = numbers(*vb);
        if (n.size() != 4) return std::nullopt;
        doc.viewBox = {n[0], n[1], n[2], n[3]};
    } else {
        const std::string* ws = xml->root.attribute("width");
        const std::string* hs = xml->root.attribute("height");
        if (!ws || !hs) return std::nullopt;
        // `width` is a LENGTH, not a bare number: `1000px` is the ordinary
        // spelling and `numbers()` returns nothing for it, because the `p`
        // makes it stop with no digits consumed. `px` is the user unit in SVG
        // 1.1 §4.2, so it is stripped; every other unit -- and a percentage,
        // which is a fraction of a viewport this reader does not have -- is
        // refused rather than read as if it were user units.
        auto lengthOf = [](const std::string& text) -> std::optional<double> {
            std::string_view v(text);
            while (!v.empty() && isSpace(v.back())) v.remove_suffix(1);
            if (v.size() > 2 && v.substr(v.size() - 2) == "px") v.remove_suffix(2);
            const auto n = numbers(v);
            if (n.size() != 1 || n[0] <= 0.0) return std::nullopt;
            return n[0];
        };
        const auto w = lengthOf(*ws);
        const auto h = lengthOf(*hs);
        if (!w || !h) return std::nullopt;
        doc.viewBox = {0.0, 0.0, *w, *h};
    }
    Builder b;
    b.collectStyles(xml->root);
    b.walk(xml->root, Transform{}, Inherited{});
    doc.shapes = std::move(b.shapes);
    doc.gradients = std::move(b.gradients);
    doc.unsupported_ = std::move(b.unsupported);
    return doc;
}

}  // namespace icf::svg
