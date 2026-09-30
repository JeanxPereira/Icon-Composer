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

#include <cstdint>
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

// A CAIXA DE USUÁRIO DE UM SVG, LIDA SOZINHA.
//
// `SvgDocument::parse` estabelece essa caixa para depois construir as formas,
// os gradientes, os clips e os filtros. Quem só precisa saber ONDE a arte fica
// -- o hit-test do canvas, que pergunta isso de cada asset -- não precisa de
// nada disso, e pagar a construção inteira para ler quatro números seria pagar
// o documento todo por um atributo.
//
// Esta é a MESMA regra, EXTRAÍDA do corpo de `parse`, que passou a chamá-la --
// e não uma segunda leitura escrita ao lado. A regra tem um caso que não é
// óbvio (a falta de `viewBox` NÃO é a falta de espaço de usuário: SVG 1.1 §7.7
// manda cair em `0 0 width height`), e duas cópias dela seriam duas que param
// de concordar no dia em que esse fallback mudar.
//
// Nada (`nullopt`) quando a raiz não é um `<svg>`, quando o `viewBox` não traz
// quatro números, ou quando, na falta dele, não há um `width`/`height` em
// unidades de usuário de onde inferir a caixa. Nesses casos não há caixa a
// afirmar, e quem pergunta tem de ter um caminho para isso.
std::optional<ViewBox> readViewBox(const Element& root);
// O mesmo a partir dos bytes do arquivo: lê o XML e PARA aí.
std::optional<ViewBox> readViewBox(std::string_view svg);

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

    // The transform from the element's OWN user space to the document's: every
    // `transform` of the element and of its ancestors. `path` is already baked
    // through it, but a `userSpaceOnUse` gradient is measured in the element's
    // user space (SVG 1.1 §13.2.2), so its coordinates have to go through it
    // too. `[ART]` Without it the ImHex background -- 114 hex digits under a
    // `<g transform>` of scale 5.79, each with a radial gradient -- put the
    // spotlight in the corner at 1/5.79 of its size (Edge, as reference, puts
    // it in the middle).
    Transform ctm;

    // SVG's initial values, and they are not symmetric: an unpainted shape is
    // BLACK, and an unstroked one is not stroked at all.
    Paint fill;
    Paint stroke;
    FillRule fillRule = FillRule::NonZero;
    double strokeWidth = 1.0;

    // `opacity`, folded down from every ancestor group and from the element
    // itself. It is NOT `fill-opacity`: SVG composites the shape's whole
    // rendering -- fill and stroke together -- against the backdrop with this
    // alpha, and `fill-opacity` multiplies only the fill's paint.
    //
    // `[ART]` The two agree on every corpus shape that carries it: all 24
    // elements with `opacity < 1` have NO stroke, so there is nothing for the
    // two rules to disagree about. A shape that had both is named rather than
    // approximated (`paint:opacity-com-fill-e-stroke`).
    double opacity = 1.0;

    // The `clipPath` ids this shape is inside, outermost first. Clipping is a
    // per-pixel INTERSECTION, so applying a group's clip to each of its shapes
    // gives exactly what clipping the composed group gives -- unlike `opacity`
    // above, there is no approximation to declare here.
    std::vector<std::string> clipPaths;

    // The `mask` ids this shape is inside, outermost first. A mask is a
    // LUMINANCE mask: unlike a clip it carries paint, so its children are kept
    // as shapes rather than as bare paths.
    std::vector<std::string> masks;

    // The `<filter>` this shape's group carries, and WHICH group carried it.
    //
    // A filter is not a per-shape property, and that is what separates it from
    // the two above. A clip intersects per pixel and a mask multiplies per
    // pixel, so applying either to each shape of a group gives exactly what
    // applying it to the composed group gives. A filter does not: a blur of the
    // sum is not the sum of the blurs. So the group has to be composed FIRST,
    // in a target of its own, and the chain applied to that.
    //
    // `filterInstance` is what makes that possible after the tree is flattened:
    // every shape under one filtered `<g>` gets the same number, and a second
    // `<g>` naming the SAME id gets a different one. Zero means unfiltered.
    std::string filterId;
    std::size_t filterInstance = 0;
};

class SvgDocument {
public:
    static std::optional<SvgDocument> parse(std::string_view svg);

    ViewBox viewBox;
    std::vector<Shape> shapes;
    // By id, so a `url(#g)` paint can be answered. 161 gradients answer 165
    // references in the corpus.
    std::map<std::string, Gradient> gradients;

    // By id, the geometry each `<clipPath>` encloses, already in the document's
    // user space. The clip region is the UNION of these, which for one path --
    // the only shape the corpus uses -- is just the path.
    std::map<std::string, std::vector<Path>> clipPaths;

    // By id, the shapes each `<mask>` encloses -- WITH their paint, because the
    // mask's value at a pixel is the luminance of what it draws times its alpha.
    // The region (`x`/`y`/`width`/`height` under `maskUnits`) rides along, since
    // content outside it does not mask.
    struct Mask {
        std::vector<Shape> shapes;
        double x = 0, y = 0, width = 0, height = 0;
        bool hasRegion = false;
    };
    std::map<std::string, Mask> masks;

    // ---- filters ---------------------------------------------------------
    //
    // `[BIN]` THE TARGET CONSTRUCTS SIX PRIMITIVES AND DROPS THE REST AT BUILD
    // TIME. `SVGFilter::filterPrimitive` (CoreSVG.arm64 0x2A230) scans a
    // six-entry table at `__const:0x327F0` -- `{0x67, 0x5f, 0x5b, 0x72, 0x65,
    // 0x78}`, the atoms of `feGaussianBlur`, `feOffset`, `feFlood`,
    // `feComposite`, `feBlend` and `feConvolveMatrix`. A name outside it is
    // logged (`"Filter primitive: <%s> is currently not supported."`) and the
    // function returns null: the primitive is NEVER CONSTRUCTED and never
    // enters the chain. `SVGFilterPrimitive::selectPrimitive` (0x23B48)
    // dispatches those same six and returns null for anything else.
    //
    // `[BIN]` AND THE HOLE IT LEAVES COLLAPSES THE WHOLE CHAIN.
    // `inputImage` (0x2520C) returns null for an `in`/`in2` naming a result
    // that was never produced; `drawFeBlend` (0x24BFC) and `drawFeComposite`
    // (0x240C8) both start their return value at zero and bail on a null input;
    // `SVGFilter::draw` (0x29A34) sends a null final result to 0x29E58, which
    // is cleanup and nothing else; and its one caller, `PopSVGNodeAttributes`
    // (0xA85C), ignores the return and has no fallback.
    //
    // So an element whose chain names an unimplemented primitive draws NOTHING
    // in the target -- not "draws unfiltered". `dropped` is how this reader says
    // that happened, and it is a REPRODUCTION of the target rather than a gap in
    // this reader, which is why it is not reported through `unsupported()`.
    struct FilterPrimitive {
        std::string name;
        std::map<std::string, std::string> attributes;

        // Filter attributes are read late, by the renderer, so they ride along
        // as text rather than as fields.
        std::string attribute(const std::string& key) const {
            auto it = attributes.find(key);
            return it == attributes.end() ? std::string() : it->second;
        }
    };

    struct Filter {
        // In document order, and ONLY the six the target builds.
        std::vector<FilterPrimitive> primitives;
        // The names the target refuses to construct, in the order met. Non-empty
        // means every element referencing this filter draws nothing.
        std::vector<std::string> dropped;
        // `x`/`y`/`width`/`height`. SVG's default for `filterUnits` is
        // `objectBoundingBox`; `userSpace` records which one was actually named,
        // because the two are different geometry.
        double x = 0, y = 0, width = 0, height = 0;
        bool hasRegion = false;
        bool userSpace = false;

        bool collapses() const { return !dropped.empty(); }
    };
    std::map<std::string, Filter> filters;

    // The primitives the target constructs, in the table's own order. Exposed so
    // a test can hold the transcription against the binary.
    static const std::vector<std::string>& targetFilterPrimitives();

    // ---- patterns, and the raster the corpus hides inside them ------------
    //
    // `[ART]` Three files -- all Delta's -- carry 11 `<pattern>` definitions and
    // 11 references, and every one is the same shape: a shape fills with
    // `url(#p)`, the pattern holds ONE `<use>`, and the `<use>` names an
    // `<image>` whose `href` is a `data:image/png;base64` payload. That is how
    // Figma exports "this shape is filled with a picture", and it is why `use`,
    // `pattern` and embedded raster were three lines of the ruler and one
    // construct.
    //
    // `[BIN]` And the target really draws it, which is NOT what the same question
    // about `<filter>` returned: `SVGPattern` is a class with `draw` (0x12184),
    // `drawCells` (0x11D50) and `attributeIsUserSpace` (0x11FA8), reaching
    // `CGPatternCreate` and `CGContextSetFillPattern`. Nothing here contradicts
    // the specification and nothing asks for a refusal.
    struct Pattern {
        // The tile, in the units `unitsUserSpace` names.
        double x = 0, y = 0, width = 0, height = 0;
        // `patternUnits`: SVG's default is `objectBoundingBox`.
        bool unitsUserSpace = false;
        // `patternContentUnits`: SVG's default is `userSpaceOnUse`. The two
        // defaults differ, which is why they are separate fields rather than one
        // flag -- reading them as a pair is the mistake this spells out.
        bool contentUserSpace = true;
        // The `<use>`'s own transform, applied to the image before the units.
        Transform contentTransform;
        // What the `<use>` names.
        std::string imageId;
    };
    std::map<std::string, Pattern> patterns;

    // An `<image>` whose `href` is a `data:` URI, carried as the bytes the URI
    // encoded and NOT decoded further.
    //
    // The format stays undecoded HERE on purpose: `CoreSVG` links the standard
    // library and nothing else (architecture spec, rule 1), and the PNG reader
    // lives in `IconComposerFoundation`. Base64 is arithmetic; PNG is a
    // dependency. So the reader does the arithmetic and the renderer does the
    // rest.
    struct EmbeddedImage {
        // The element's own `width`/`height` attributes, which are what the
        // pattern's geometry is written against -- NOT necessarily what the
        // encoded file turns out to hold. A disagreement between the two is the
        // renderer's to notice.
        double width = 0, height = 0;
        std::vector<std::uint8_t> bytes;
        // The media type from the `data:` URI, e.g. `image/png`.
        std::string mediaType;
    };
    std::map<std::string, EmbeddedImage> images;

    // Element names seen and not drawn. Empty means every element in the file is
    // either drawn or deliberately ignored (`title`, `desc`, `metadata`).
    const std::set<std::string>& unsupported() const { return unsupported_; }

private:
    std::set<std::string> unsupported_;
// `parse` is a member, so it can fill the private set directly.
};

}  // namespace icf::svg
