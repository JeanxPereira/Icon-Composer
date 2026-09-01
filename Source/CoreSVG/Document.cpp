#include "Source/CoreSVG/Document.h"

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

// The attributes that decide how a shape LOOKS. None is read by this layer, and
// each is named the moment it appears -- doc 04 §4's rule, applied to paint
// rather than to elements. Without this the report would call a file understood
// while dropping every colour in it.
constexpr std::string_view kPaintAttributes[] = {
    "fill", "stroke", "style", "class", "opacity", "fill-opacity", "stroke-opacity",
    "stroke-width", "fill-rule", "clip-path", "mask", "filter", "clip-rule",
    "stroke-linecap", "stroke-linejoin", "stroke-dasharray",
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

    // What a shape's paint attributes would have said, had anything read them.
    void notePaint(const Element& e) {
        for (const auto& a : e.attributes) {
            for (auto p : kPaintAttributes) {
                if (a.first == p) unsupported.insert("paint:" + a.first);
            }
        }
    }

    void walk(const Element& e, const Transform& parent) {
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
            if (e.attribute("rx") || e.attribute("ry")) {
                // Sharp corners, and said so. The gate counts how many rects are
                // rounded, and that number is what decides whether the arc is
                // worth writing.
                unsupported.insert("rect:rounded");
            }
            path.segments.push_back({SegmentKind::Move, {{*x, *y}}});
            path.segments.push_back({SegmentKind::Line, {{*x + *w, *y}}});
            path.segments.push_back({SegmentKind::Line, {{*x + *w, *y + *h}}});
            path.segments.push_back({SegmentKind::Line, {{*x, *y + *h}}});
            path.segments.push_back({SegmentKind::Close, {}});
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
            for (const auto& c : e.children) walk(c, here);
            return;
        } else if (e.name == "defs") {
            // Definitions are referenced, not drawn -- walking in to paint them
            // would put gradient stops on the canvas. But what is DEFINED in
            // there is exactly what a later round has to implement, so each kind
            // is named once.
            for (const auto& c : e.children) unsupported.insert("defs:" + c.name);
            return;
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
        shapes.push_back({std::move(path), e.name});
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

    const std::string* vb = xml->root.attribute("viewBox");
    if (!vb) return std::nullopt;  // no user space; every coordinate is meaningless
    const auto n = numbers(*vb);
    if (n.size() != 4) return std::nullopt;

    SvgDocument doc;
    doc.viewBox = {n[0], n[1], n[2], n[3]};
    Builder b;
    b.walk(xml->root, Transform{});
    doc.shapes = std::move(b.shapes);
    doc.unsupported_ = std::move(b.unsupported);
    return doc;
}

}  // namespace icf::svg
