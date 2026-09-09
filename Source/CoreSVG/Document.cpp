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

// What a shape inherits from its ancestry -- plus the two things that do not
// inherit in SVG's sense and are carried here anyway, each for its own reason:
// `opacityChain` multiplies, and `clips` accumulate.
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

    // The `clipPath` ids in force, outermost first. This one DOES inherit in
    // the ordinary sense -- a group's clip applies to everything under it --
    // and it accumulates rather than replaces, because two nested clips
    // intersect.
    std::vector<std::string> clips;

    // Same shape as `clips`, and same reason: a group's mask applies to
    // everything under it, and two nested masks multiply.
    std::vector<std::string> masks;

    // The innermost `<filter>` in force, and the instance of the element that
    // named it. Unlike `clips` and `masks` this REPLACES rather than
    // accumulates: SVG applies a filter to the element's own rendering, so a
    // filtered group inside a filtered group is the inner one's result being
    // filtered again -- which the corpus never does, and which is named rather
    // than silently flattened (`filtro dentro de filtro`).
    std::string filterId;
    std::size_t filterInstance = 0;
};

// The alpha the paint ends up with. `fill-opacity` is a separate multiplier
// from the colour's own alpha, and both apply.
Paint withOpacity(Paint p, double opacity) {
    if (p.kind == PaintKind::Color) p.color.a *= opacity;
    return p;
}

// BASE64, because a `data:` URI is how the corpus's raster art actually arrives.
//
// `[ART]` Delta's three files carry a 1024x1024 RGBA PNG apiece as 2.376.512
// characters of base64 inside an `xlink:href`. Decoding it is arithmetic and
// belongs here; decoding the PNG that comes out is a dependency and belongs to
// the renderer (architecture spec, rule 1).
//
// STRICT ON PURPOSE. A stray character is a corrupt payload, and a decoder that
// skipped it would hand the PNG reader a shifted stream and blame it for the
// mess. Whitespace IS skipped -- XML is free to wrap a long attribute -- and
// nothing else is.
bool decodeBase64(std::string_view text, std::vector<std::uint8_t>& out) {
    auto sextet = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::uint32_t acc = 0;
    int bits = 0;
    std::size_t padding = 0;
    for (char c : text) {
        if (isSpace(c)) continue;
        if (c == '=') {
            ++padding;
            continue;
        }
        // Padding is the END. A character after it means the stream is not what
        // it claims to be.
        if (padding) return false;
        const int v = sextet(c);
        if (v < 0) return false;
        acc = (acc << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xFF));
        }
    }
    if (padding > 2) return false;
    // Whatever is left in `acc` is fewer than eight bits and has to be zero:
    // base64 pads with zero bits, so anything else is a truncated stream rather
    // than a short one.
    return (acc & ((1u << bits) - 1)) == 0;
}

// A quarter of a rounded corner, as a cubic. Same constant as the ellipse.
void appendCorner(Path& p, Point from, Point to, Point corner, double kx, double ky) {
    p.segments.push_back({SegmentKind::Cubic,
                          {{from.x + kx, from.y + ky},
                           {to.x - kx, to.y - ky},
                           to}});
    (void)corner;
}

// Presentation attributes this reader SEES AND DOES NOT ACT ON, named the moment
// they appear -- doc 04 §4's rule, applied to paint rather than to elements.
// Without it the report would call a file understood while dropping every colour
// in it.
//
// Leaving an implemented property here is its own defect: one that IS applied
// and still reports itself as ignored teaches whoever reads the report to
// distrust it. `fill`, `stroke`, `fill-rule`, the opacities, `stroke-width` and
// `style` left as they were built; `opacity` left on 2026-09-05 and `clip-path`
// on the same day. `class` stays, because the stylesheet that would give it
// meaning is still not read.
constexpr std::string_view kPaintAttributes[] = {
    "clip-rule",
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
    std::map<std::string, std::vector<Path>> clipPaths;
    std::map<std::string, SvgDocument::Mask> masks;
    std::map<std::string, SvgDocument::Filter> filters;
    std::map<std::string, SvgDocument::Pattern> patterns;
    std::map<std::string, SvgDocument::EmbeddedImage> images;

    // THE DEFINITION GRAPH, for deciding what is actually a gap.
    //
    // A `<defs>` child this reader does not draw is only a LOSS if something
    // drawn can reach it. Until 2026-09-09 that question was answered by pairing
    // names -- `defs:filter` was dropped unless a `paint:filter` had also been
    // seen -- which works one level deep and no further. `[ART]` It failed on
    // Delta's `texture.svg`: its `<image>` is reached through a `<pattern>`,
    // and the only shape naming that pattern sits under a filter the target
    // collapses, so nothing is drawn and nothing is lost. The name pairing had
    // no way to see two links away.
    struct Definition {
        std::string kind;              // the element name, for the report
        std::string id;                // may be empty: then nothing can name it
        std::set<std::string> refs;    // the ids ITS subtree reaches
    };
    std::vector<Definition> definitions;
    // Ids reached from something DRAWN -- a shape's paint, its clips, its masks,
    // its filter -- and the seeds of the walk below.
    std::set<std::string> reachedIds;

    // Every `url(#id)` and `href="#id"` an element's subtree names.
    static void collectRefs(const Element& e, std::set<std::string>& into) {
        for (const auto& a : e.attributes) {
            const std::string& v = a.second;
            std::size_t at = 0;
            while ((at = v.find("url(#", at)) != std::string::npos) {
                const std::size_t close = v.find(')', at);
                if (close == std::string::npos) break;
                into.insert(v.substr(at + 5, close - at - 5));
                at = close + 1;
            }
            if ((a.first == "href" || a.first == "xlink:href") && v.size() > 1 && v[0] == '#') {
                into.insert(v.substr(1));
            }
        }
        for (const auto& c : e.children) collectRefs(c, into);
    }
    // Zero means "no filter", so the first group to name one is 1.
    std::size_t filterInstances = 0;
    bool sawStylesheet = false;

    // One element's paint, resolved against what the ancestry set.
    // The stylesheets, collected before anything is drawn.
    std::map<std::string, std::map<std::string, std::string>> sheet;

    // Every `<style>` in the document, wherever it sits. A pass of its own:
    // nothing requires a stylesheet to be declared before the shapes that use
    // it, and 18 corpus files put theirs inside `defs`.
    void collectStyles(const Element& e) {
        if (e.name == "style") {
            sawStylesheet = true;
            for (const auto& [cls, decls] : parseStylesheet(e.text)) {
                for (const auto& d : decls) sheet[cls][d.first] = d.second;
            }
            return;
        }
        for (const auto& c : e.children) collectStyles(c);
    }

    // Every `<filter>` in the document, wherever it sits, before anything is
    // walked. A pass of its own for the same reason `collectStyles` is one:
    // Figma writes `<defs>` at the BOTTOM of the file, after every group that
    // references it, and a group cannot be dropped for a chain that has not been
    // read yet.
    void collectFilters(const Element& e) {
        if (e.name == "filter") {
            const std::string* id = e.attribute("id");
            if (!id) return;  // unreferenceable, so it changes nothing
            SvgDocument::Filter f;
            if (const std::string* u = e.attribute("filterUnits")) f.userSpace = (*u == "userSpaceOnUse");
            const std::string* xs = e.attribute("x");
            const std::string* ys = e.attribute("y");
            const std::string* ws = e.attribute("width");
            const std::string* hs = e.attribute("height");
            if (xs && ys && ws && hs) {
                auto one = [](const std::string& v, double& slot) {
                    auto n = numbers(v);
                    if (n.size() != 1) return false;
                    slot = n[0];
                    return true;
                };
                f.hasRegion = one(*xs, f.x) && one(*ys, f.y) && one(*ws, f.width) &&
                              one(*hs, f.height);
            }
            for (const auto& c : e.children) {
                // THE TARGET'S TABLE, AND NOTHING ELSE, DECIDES THIS. A name
                // outside it is never constructed there (0x2A230), so keeping it
                // here would build a chain the target does not have.
                bool built = false;
                for (const auto& name : SvgDocument::targetFilterPrimitives()) {
                    if (c.name == name) { built = true; break; }
                }
                if (!built) {
                    f.dropped.push_back(c.name);
                    continue;
                }
                SvgDocument::FilterPrimitive p;
                p.name = c.name;
                for (const auto& a : c.attributes) p.attributes[a.first] = a.second;
                f.primitives.push_back(std::move(p));
            }
            filters[*id] = std::move(f);
            return;
        }
        for (const auto& c : e.children) collectFilters(c);
    }

    // Every `<pattern>` and every `<image>` with a `data:` URI, before the walk
    // and for the same reason the filters are: Figma writes `<defs>` at the
    // BOTTOM, after everything that references it.
    //
    // WHAT IS REFUSED BY NAME, because the corpus does not exercise it and
    // guessing would be inventing geometry:
    //
    //   patternUnits="userSpaceOnUse"   none of the 11 use it, and it is a
    //                                   different mapping, not a different default
    //   patternTransform                none of the 11 carry one
    //   content that is not exactly one `<use>` naming an `<image>`
    //   an `href` that is not `#id`, or a `data:` URI this cannot read
    void collectPatterns(const Element& e) {
        if (e.name == "pattern") {
            const std::string* id = e.attribute("id");
            if (!id) return;  // unreferenceable, so it changes nothing
            SvgDocument::Pattern p;
            if (const std::string* u = e.attribute("patternUnits")) {
                if (*u == "userSpaceOnUse") {
                    unsupported.insert("patternUnits:userSpaceOnUse");
                    return;
                }
                if (*u != "objectBoundingBox") {
                    unsupported.insert("patternUnits:" + *u);
                    return;
                }
            }
            if (const std::string* c = e.attribute("patternContentUnits")) {
                if (*c == "objectBoundingBox") p.contentUserSpace = false;
                else if (*c == "userSpaceOnUse") p.contentUserSpace = true;
                else {
                    unsupported.insert("patternContentUnits:" + *c);
                    return;
                }
            }
            if (e.attribute("patternTransform")) {
                unsupported.insert("patternTransform");
                return;
            }
            auto one = [&](const char* name, double& slot, bool required) {
                const std::string* v = e.attribute(name);
                if (!v) return !required;
                auto n = numbers(*v);
                if (n.size() != 1) return false;
                slot = n[0];
                return true;
            };
            if (!one("x", p.x, false) || !one("y", p.y, false) ||
                !one("width", p.width, true) || !one("height", p.height, true) ||
                p.width <= 0.0 || p.height <= 0.0) {
                unsupported.insert("pattern sem width/height legiveis");
                return;
            }
            // Exactly one `<use>`, and nothing else that draws.
            const Element* use = nullptr;
            for (const auto& c : e.children) {
                if (isIgnorable(c.name)) continue;
                if (c.name == "use" && !use) { use = &c; continue; }
                unsupported.insert("pattern com conteudo que nao e um <use>: " + c.name);
                return;
            }
            if (!use) {
                unsupported.insert("pattern sem <use>");
                return;
            }
            const std::string* href = use->attribute("xlink:href");
            if (!href) href = use->attribute("href");
            if (!href || href->size() < 2 || (*href)[0] != '#') {
                unsupported.insert("use sem href para um id do documento");
                return;
            }
            p.imageId = href->substr(1);
            if (const std::string* t = use->attribute("transform")) {
                auto parsed = parseTransform(*t);
                if (!parsed) {
                    unsupported.insert("transform:" + *t);
                    return;
                }
                p.contentTransform = *parsed;
            }
            patterns[*id] = std::move(p);
            return;
        }
        if (e.name == "image") {
            const std::string* id = e.attribute("id");
            const std::string* href = e.attribute("xlink:href");
            if (!href) href = e.attribute("href");
            if (!id || !href) return;
            // `data:<media type>;base64,<payload>` and nothing else. A URL that
            // points outside the file is art this reader cannot see at all, and
            // saying so is better than a silent blank.
            constexpr std::string_view kPrefix = "data:";
            if (href->compare(0, kPrefix.size(), kPrefix) != 0) {
                unsupported.insert("image com href que nao e data:");
                return;
            }
            const std::size_t comma = href->find(',');
            const std::size_t semi = href->find(';');
            if (comma == std::string::npos || semi == std::string::npos || semi > comma ||
                href->compare(semi, comma - semi, ";base64") != 0) {
                unsupported.insert("image data: que nao e base64");
                return;
            }
            SvgDocument::EmbeddedImage img;
            img.mediaType = href->substr(kPrefix.size(), semi - kPrefix.size());
            if (!decodeBase64(std::string_view(*href).substr(comma + 1), img.bytes)) {
                unsupported.insert("image com base64 corrompido");
                return;
            }
            auto dim = [&](const char* name, double& slot) {
                if (const std::string* v = e.attribute(name)) {
                    auto n = numbers(*v);
                    if (n.size() == 1) slot = n[0];
                }
            };
            dim("width", img.width);
            dim("height", img.height);
            images[*id] = std::move(img);
            return;
        }
        for (const auto& c : e.children) collectPatterns(c);
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
                // Not an error: the element keeps what it inherited. A class no
                // RULE matches usually means a stylesheet that was not read, and
                // that is worth seeing.
                //
                // BUT ONLY IF THERE WAS A STYLESHEET AT ALL. With no `<style>`
                // anywhere in the file there is no rule that could have matched,
                // and the class is the exporter's decoration -- naming it accuses
                // the file of a gap it does not have. `[ART]` The corpus splits
                // cleanly on this: every file that carries classes AND a
                // stylesheet is Apollo's or OneKey's, where a missing rule is
                // real; the only file with classes and NO stylesheet at all is
                // PDF-Archiver, where all four were false alarms.
                if (sawStylesheet) unsupported.insert("class:" + name);
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
        if (auto v = property(e, "mask", style, fromClass)) {
            const std::string& t = *v;
            if (t.size() > 6 && t.compare(0, 5, "url(#") == 0 && t.back() == ')') {
                in.masks.push_back(t.substr(5, t.size() - 6));
            } else if (t != "none") {
                unsupported.insert("paint:mask=" + t);
            }
        }
        if (auto v = property(e, "clip-path", style, fromClass)) {
            // Only `url(#id)` is a reference this reader can follow. The CSS
            // basic shapes (`inset()`, `circle()`, ...) are a different
            // language and are named rather than half-read.
            const std::string& t = *v;
            if (t.size() > 6 && t.compare(0, 5, "url(#") == 0 && t.back() == ')') {
                in.clips.push_back(t.substr(5, t.size() - 6));
            } else if (t != "none") {
                unsupported.insert("paint:clip-path=" + t);
            }
        }
        if (auto v = property(e, "opacity", style, fromClass)) {
            auto n = numbers(*v);
            if (n.size() == 1) {
                in.opacityChain *= std::clamp(n[0], 0.0, 1.0);
            } else {
                unsupported.insert("paint:opacity=" + *v);
            }
        }
        if (auto v = property(e, "filter", style, fromClass)) {
            const std::string& t = *v;
            if (t == "none") {
                // The instruction NOT to filter, and the one value here that
                // means the element is ordinary.
            } else if (t.size() > 6 && t.compare(0, 5, "url(#") == 0 && t.back() == ')') {
                const std::string id = t.substr(5, t.size() - 6);
                auto f = filters.find(id);
                if (f == filters.end()) {
                    // A DANGLING FILTER REFERENCE IS A QUESTION, NOT A
                    // REPRODUCTION. What the target does with one was not
                    // measured -- `SVGAttribute::resolveAsFilter` was not read
                    // -- and no corpus file has one, so it is named instead of
                    // guessed in either direction.
                    unsupported.insert("paint:filter=" + t);
                } else if (!in.filterId.empty()) {
                    // A filter applied to the result of a filter. The corpus has
                    // none, and flattening the two would be a guess.
                    unsupported.insert("filtro dentro de filtro");
                } else {
                    in.filterId = id;
                    in.filterInstance = ++filterInstances;
                }
            } else {
                unsupported.insert("paint:filter=" + t);
            }
        }
        return in;
    }

    // A `<clipPath>`, by id. Its children are geometry and nothing else: their
    // paint is ignored by SVG, which is why this collects paths rather than
    // shapes.
    //
    // `[ART]` The corpus has exactly one -- quick-push's `<rect>` -- so what is
    // exercised is the single-child case. The union of several is still built,
    // because refusing a second child would be a limit this reader has no reason
    // to have.
    void collectClipPath(const Element& e, const Transform& parent) {
        collectLiveRefs(e);
        const std::string* id = e.attribute("id");
        if (!id) return;
        // `objectBoundingBox` re-scales the clip to each USER's bounding box, so
        // one definition means different geometry per referrer. That is a real
        // difference and it is named rather than silently read as user space.
        if (const std::string* u = e.attribute("clipPathUnits")) {
            if (*u != "userSpaceOnUse") {
                unsupported.insert("clipPathUnits:" + *u);
                return;
            }
        }
        std::vector<Path>& into = clipPaths[*id];
        const std::size_t before = shapes.size();
        // THE CHILDREN, not the element. Walking `e` itself lands back on the
        // `clipPath` branch that called this, which recurses until the stack is
        // gone -- the first build of this crashed the whole suite with no
        // output, exit 253.
        for (const auto& c : e.children) walk(c, parent, Inherited{});
        // `walk` appends to `shapes`; a clip's children are not drawn, so they
        // are moved out of that list and into the clip.
        for (std::size_t i = before; i < shapes.size(); ++i) {
            into.push_back(std::move(shapes[i].path));
        }
        shapes.resize(before);
    }

    // A `<mask>`, by id. Its children keep their PAINT, because a mask's value
    // is the luminance of what it draws -- a black rect and a white one are not
    // the same mask, while for a clip they would be the same region.
    void collectMask(const Element& e, const Transform& parent) {
        collectLiveRefs(e);
        const std::string* id = e.attribute("id");
        if (!id) return;
        // `maskUnits` DEFAULTS TO `objectBoundingBox`, which re-scales the
        // region per referrer. `[ART]` All three corpus masks say
        // `userSpaceOnUse` outright, so refusing the other reading costs
        // nothing and keeps a guess out of the picture.
        const std::string* u = e.attribute("maskUnits");
        if (!u || *u != "userSpaceOnUse") {
            unsupported.insert(std::string("maskUnits:") + (u ? *u : "objectBoundingBox"));
            return;
        }
        if (const std::string* c = e.attribute("maskContentUnits")) {
            if (*c != "userSpaceOnUse") {
                unsupported.insert("maskContentUnits:" + *c);
                return;
            }
        }
        SvgDocument::Mask m;
        auto num = [&](const char* n, double& slot) {
            if (const std::string* v = e.attribute(n)) {
                auto k = numbers(*v);
                if (k.size() == 1) { slot = k[0]; return true; }
            }
            return false;
        };
        const bool hx = num("x", m.x), hy = num("y", m.y);
        const bool hw = num("width", m.width), hh = num("height", m.height);
        m.hasRegion = hx && hy && hw && hh;
        const std::size_t before = shapes.size();
        // The CHILDREN, for the reason `collectClipPath` records: walking the
        // element itself lands back on the branch that called this.
        for (const auto& c : e.children) walk(c, parent, Inherited{});
        for (std::size_t i = before; i < shapes.size(); ++i) {
            // A mask inside a mask would recurse at render time; the corpus has
            // none, and naming it costs one line.
            if (!shapes[i].masks.empty()) unsupported.insert("mask dentro de mask");
            m.shapes.push_back(std::move(shapes[i]));
        }
        shapes.resize(before);
        masks[*id] = std::move(m);
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

    // A definition this reader DOES draw -- a gradient, a clip path, a mask --
    // is live wherever it is used, and so is everything its own content names.
    // Folding those in here is deliberately CONSERVATIVE: it can keep a gap
    // named that closer reading would clear, and the opposite mistake -- hiding
    // art that really is lost -- is the one that matters.
    void collectLiveRefs(const Element& e) { collectRefs(e, reachedIds); }

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

        // THE ELEMENT DRAWS NOTHING, AND THAT IS THE TARGET BEING REPRODUCED.
        // A chain holding a primitive the target never constructs leaves a hole
        // that nulls its way to `SVGFilter::draw`'s cleanup path -- see
        // `SvgDocument::Filter` for the six addresses. Returning here is the
        // whole of it: no shape, no descent, and NOTHING REPORTED, because a
        // report would accuse the file of losing art the target does not draw
        // either.
        if (!inherited.filterId.empty()) {
            auto f = filters.find(inherited.filterId);
            if (f != filters.end() && f->second.collapses()) return;
        }

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
        } else if (e.name == "mask") {
            collectMask(e, here);
            return;
        } else if (e.name == "filter" || e.name == "pattern" || e.name == "image") {
            // All three are collected before the walk. `<image>` is the odd one:
            // SVG DOES draw it where it stands, and this reader only reads the
            // ones a `<pattern>` names -- so a bare `<image>` on the canvas is
            // still a gap, and says so.
            if (e.name == "image" && !e.attribute("id")) unsupported.insert("image");
            return;
        } else if (e.name == "clipPath") {
            // Outside `<defs>` as well: `<clipPath>` is a definition wherever it
            // sits, and SVG does not draw it either way.
            collectClipPath(e, here);
            return;
        } else if (e.name == "defs") {
            // Definitions are referenced, not drawn -- walking in to paint them
            // would put gradient stops on the canvas. But what is DEFINED in
            // there is exactly what a later round has to implement, so each kind
            // is named once.
            for (const auto& c : e.children) {
                if (c.name == "linearGradient" || c.name == "radialGradient") {
                    collectGradient(c);
                } else if (c.name == "clipPath") {
                    collectClipPath(c, here);
                } else if (c.name == "mask") {
                    collectMask(c, here);
                } else if (c.name == "pattern" || c.name == "image") {
                    // Collected before the walk -- but only the ones that were
                    // ACCEPTED are in the maps. A `<pattern>` refused for naming
                    // `patternTransform`, or an `<image>` whose `href` points
                    // outside the file, is art that really is lost, so it stays a
                    // definition like any other and the reachability walk decides
                    // whether anything drawn can reach it.
                    const std::string* cid = c.attribute("id");
                    const bool collected =
                        cid && (c.name == "pattern" ? patterns.count(*cid) > 0
                                                    : images.count(*cid) > 0);
                    if (!collected) {
                        Definition d;
                        d.kind = c.name;
                        if (cid) d.id = *cid;
                        collectRefs(c, d.refs);
                        definitions.push_back(std::move(d));
                    }
                } else if (c.name != "style" && c.name != "filter") {
                    // `style` and `filter` are collected in passes of their own,
                    // before the walk.
                    //
                    // AND THERE IS NO `isIgnorable` TEST HERE, though one was
                    // written on 2026-09-09 and taken out the same day. It was
                    // meant for `inkscape:path-effect`, which used to be named a
                    // gap purely for sitting in `<defs>` -- but the walk below
                    // already answers that: `[ART]` all 27 of Jellify's
                    // definitions carry an id and NOTHING references one, so
                    // reachability prunes them without help. The mutation sweep
                    // said so by surviving, and a guard no input can distinguish
                    // is dead weight that makes the sweep report a defect nobody
                    // can fix.
                    //
                    // RECORDED, NOT ACCUSED: whether this is a gap depends on
                    // whether anything drawn can reach it, and that is not known
                    // until the walk is over.
                    Definition d;
                    d.kind = c.name;
                    if (const std::string* id = c.attribute("id")) d.id = *id;
                    collectRefs(c, d.refs);
                    definitions.push_back(std::move(d));
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
        shape.clipPaths = inherited.clips;
        shape.masks = inherited.masks;
        shape.filterId = inherited.filterId;
        shape.filterInstance = inherited.filterInstance;
        // THE SEEDS. Everything this shape can reach is reached, and a shape
        // that was never emitted -- because its group's filter collapses --
        // seeds nothing, which is the whole point.
        if (inherited.fill.kind == PaintKind::Reference) reachedIds.insert(inherited.fill.reference);
        if (inherited.stroke.kind == PaintKind::Reference) {
            reachedIds.insert(inherited.stroke.reference);
        }
        for (const auto& c : inherited.clips) reachedIds.insert(c);
        for (const auto& m : inherited.masks) reachedIds.insert(m);
        if (!inherited.filterId.empty()) reachedIds.insert(inherited.filterId);
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

// `[BIN]` The six-entry table at `CoreSVG.arm64 __const:0x327F0`, in ITS OWN
// ORDER: {0x67, 0x5f, 0x5b, 0x72, 0x65, 0x78}. `SVGFilter::filterPrimitive`
// (0x2A230) scans it linearly and constructs nothing whose atom is absent.
//
// THE ORDER IS KEPT BECAUSE IT IS EVIDENCE. Sorting it would make the
// transcription unfalsifiable against the bytes it came from.
const std::vector<std::string>& SvgDocument::targetFilterPrimitives() {
    static const std::vector<std::string> six = {
        "feGaussianBlur",    // 0x67
        "feOffset",          // 0x5f
        "feFlood",           // 0x5b
        "feComposite",       // 0x72
        "feBlend",           // 0x65
        "feConvolveMatrix",  // 0x78
    };
    return six;
}

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
    b.collectFilters(xml->root);
    b.collectPatterns(xml->root);
    b.walk(xml->root, Transform{}, Inherited{});
    doc.shapes = std::move(b.shapes);
    doc.gradients = std::move(b.gradients);
    doc.clipPaths = std::move(b.clipPaths);
    doc.masks = std::move(b.masks);
    doc.filters = std::move(b.filters);
    doc.patterns = std::move(b.patterns);
    doc.images = std::move(b.images);
    doc.unsupported_ = std::move(b.unsupported);

    // A DEFINITION NOTHING DRAWN CAN REACH IS NOT A GAP.
    //
    // `[ART]` PDF-Archiver has EIGHT `<filter>` definitions and ZERO references
    // -- the Pixodesk exporter emitted them and never wired them up -- so all
    // eight lines were false alarms. The ruler carried the same defect and
    // blocked the whole document over them; fixing both moved it from 179/48 to
    // 183/49 with no change to a single pixel.
    //
    // THAT FIX PAIRED NAMES, AND A PAIR ONLY SEES ONE LINK. It dropped
    // `defs:filter` unless a `paint:filter` had also been seen, which cannot
    // answer for a definition reached through ANOTHER definition. `[ART]` Delta's
    // `texture.svg` is that case: its `<image>` is named by a `<use>` inside a
    // `<pattern>`, and the only shape naming that pattern sits under a filter the
    // target collapses. Nothing is drawn, nothing is lost, and the report said
    // otherwise.
    //
    // So the reachability is walked instead of guessed. The seeds are what
    // EMITTED SHAPES name; the edges are what each definition's own subtree
    // names; and a definition the walk never arrives at is not reported.
    {
        std::set<std::string> reached = b.reachedIds;
        // An id nothing can name is unreachable by construction; so is one the
        // walk never arrives at. One predicate, used by both passes below, so
        // that what expands the walk and what gets reported cannot drift apart.
        auto arrived = [&reached](const auto& d) {
            return !d.id.empty() && reached.count(d.id) != 0;
        };
        bool grew = true;
        while (grew) {
            grew = false;
            for (const auto& d : b.definitions) {
                if (!arrived(d)) continue;
                for (const auto& r : d.refs) {
                    if (reached.insert(r).second) grew = true;
                }
            }
        }
        for (const auto& d : b.definitions) {
            if (!arrived(d)) continue;
            doc.unsupported_.insert("defs:" + d.kind);
        }
    }
    return doc;
}

}  // namespace icf::svg
