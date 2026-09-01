#pragma once
// How a shape is painted: the value of `fill`, `stroke` and `stop-color`.
//
// The vocabulary is not SVG's, it is the corpus's. Measured over 149 files:
// `url(...)` 165 times, six-digit hex 162, `none` 148, `white` 41, `black` 29,
// `color(display-p3 ...)` a dozen, three-digit hex once. `rgb()`, `rgba()`,
// `currentColor`, `transparent` and the other 145 CSS colour names occur ZERO
// times -- so this reader carries a two-name table, and anything else is
// `Unreadable` and gets reported rather than turned into a plausible black.
#include <map>
#include <string>
#include <string_view>

namespace icf::svg {

struct SvgColor {
    double r = 0, g = 0, b = 0, a = 1;
    // `color(display-p3 ...)` is not sRGB, and reading it as sRGB shifts every
    // component. The space travels with the colour rather than being assumed.
    bool displayP3 = false;
};

enum class PaintKind {
    None,        // `none` -- this shape is not painted, which is not an error
    Color,
    Reference,   // `url(#id)`
    Unreadable,  // a value this reader does not know, named by the caller
};

struct Paint {
    PaintKind kind = PaintKind::Unreadable;
    SvgColor color;
    std::string reference;  // the id, without the `#`
};

Paint parsePaint(std::string_view value);

// `style="fill:red;stroke:none"` as properties. Not CSS: no selectors, no
// cascade, no `!important` -- just the declarations of one element.
std::map<std::string, std::string> parseStyle(std::string_view style);

// A `<style>` element, as a lookup from CLASS NAME (without the dot) to the
// declarations that class carries.
//
// Measured over the corpus's 18 stylesheets: 29 rules, and every one of them a
// single class selector. No id, no element, no descendant, no pseudo-class, no
// at-rule. A rule whose selector is anything else is DROPPED rather than applied
// to everything -- and a first survey of this corpus reported descendant
// selectors that did not exist, an artifact of a regex that captured the newline
// before the dot.
std::map<std::string, std::map<std::string, std::string>> parseStylesheet(
    std::string_view css);

}  // namespace icf::svg
