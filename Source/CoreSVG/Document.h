#pragma once
// An SVG read as geometry: a flat list of shapes in paint order, each with the
// transform of its whole ancestry already applied.
//
// Scope is a decision with a number behind it (doc 04 §4): what this draws is
// what the 149 corpus files actually weigh on. What it does NOT draw it names,
// through `unsupported()` -- the same rule as `IconDocument::unknownKeys`, and
// for the same reason.
#include "Source/CoreSVG/Paint.h"
#include "Source/CoreSVG/Path.h"
#include "Source/CoreSVG/Xml.h"

#include <optional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace icf::svg {

// An affine transform, row-major: | a c e |
//                                 | b d f |
struct Transform {
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;

    Point apply(Point p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }

    // `this` then `t`, so composing a list left to right leaves the leftmost
    // applied last -- which is what SVG means by `translate(...) scale(...)`.
    Transform then(const Transform& t) const {
        return {a * t.a + b * t.c,
                a * t.b + b * t.d,
                c * t.a + d * t.c,
                c * t.b + d * t.d,
                e * t.a + f * t.c + t.e,
                e * t.b + f * t.d + t.f};
    }
};

std::optional<Transform> parseTransform(std::string_view text);

struct ViewBox {
    double x = 0, y = 0, width = 0, height = 0;
};

enum class FillRule { NonZero, EvenOdd };

enum class GradientKind { Linear, Radial };

struct GradientStop {
    double offset = 0;
    SvgColor color;
};

struct Gradient {
    GradientKind kind = GradientKind::Linear;
    // Linear: the axis. Radial: centre and radius. Defaults are SVG's own.
    double x1 = 0, y1 = 0, x2 = 1, y2 = 0;
    double cx = 0.5, cy = 0.5, radius = 0.5;
    // `gradientUnits`: false is `objectBoundingBox`, the default, where the
    // coordinates above are fractions of the shape's box rather than lengths.
    bool userSpace = false;
    Transform transform;
    std::vector<GradientStop> stops;
};

struct Shape {
    Path path;                 // already in the document's user space
    std::string element;       // the element it came from: path, rect, circle...

    // SVG's initial values, and they are not symmetric: an unpainted shape is
    // BLACK, and an unstroked one is not stroked at all.
    Paint fill;
    Paint stroke;
    FillRule fillRule = FillRule::NonZero;
    double strokeWidth = 1.0;
};

class SvgDocument {
public:
    static std::optional<SvgDocument> parse(std::string_view svg);

    ViewBox viewBox;
    std::vector<Shape> shapes;
    // By id, so a `url(#g)` paint can be answered. 161 gradients answer 165
    // references in the corpus.
    std::map<std::string, Gradient> gradients;

    // Element names seen and not drawn. Empty means every element in the file is
    // either drawn or deliberately ignored (`title`, `desc`, `metadata`).
    const std::set<std::string>& unsupported() const { return unsupported_; }

private:
    std::set<std::string> unsupported_;
// `parse` is a member, so it can fill the private set directly.
};

}  // namespace icf::svg
