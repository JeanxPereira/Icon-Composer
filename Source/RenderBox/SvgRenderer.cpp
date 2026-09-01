#include "Source/RenderBox/SvgRenderer.h"

#include <algorithm>
#include <cmath>

#include "Source/RenderBox/PathBuffer.h"
#include "Source/RenderBox/PathCompositeOracle.h"
#include "Source/RenderBox/PathResolveOracle.h"

namespace rb {
namespace {

// `[BIN]` Mode 1 of `accumulator_shape` is the plain fill: with the even-odd bit
// clear it is `saturate(|coverage|)`, the non-zero rule; with it set it is the
// triangle wave that alternates on each whole winding. Doc 03 §14.
constexpr std::uint32_t kPlainFill = 1u << kShapeModeShift;

std::uint32_t stateFor(icf::svg::FillRule rule) {
    return kPlainFill | (rule == icf::svg::FillRule::EvenOdd ? kEvenOdd : 0u);
}

}  // namespace

PathGlobals fitViewBox(const icf::svg::ViewBox& box, std::uint32_t width,
                       std::uint32_t height) {
    PathGlobals g;
    const double w = box.width > 0 ? box.width : 1.0;
    const double h = box.height > 0 ? box.height : 1.0;
    const double s = std::min(static_cast<double>(width) / w,
                              static_cast<double>(height) / h);

    g.m0[0] = static_cast<float>(s);
    g.m0[1] = 0.0f;
    g.m1[0] = 0.0f;
    g.m1[1] = static_cast<float>(s);
    g.m2[0] = static_cast<float>((width - s * w) * 0.5 - s * box.x);
    g.m2[1] = static_cast<float>((height - s * h) * 0.5 - s * box.y);

    g.twoOverSize[0] = 2.0f / static_cast<float>(width);
    g.twoOverSize[1] = 2.0f / static_cast<float>(height);
    // Every pixel to the right of an edge has to be reachable by that edge's
    // quad, because that is how the winding is carried across the row. The
    // bound is the image's own right-hand side.
    g.urx = static_cast<float>(width);
    return g;
}

Result<RenderedImage> renderSvg(Device& device, const icf::svg::SvgDocument& doc,
                                RenderOptions options) {
    if (options.width == 0 || options.height == 0) {
        return std::unexpected("a render target of zero area was asked for");
    }

    RenderedImage out;
    out.width = options.width;
    out.height = options.height;
    const std::size_t texels = static_cast<std::size_t>(options.width) * options.height;

    // The accumulator is PREMULTIPLIED while compositing -- `over` is only
    // associative in premultiplied form -- and is un-multiplied once at the end.
    std::vector<float> acc(texels * 4, 0.0f);

    auto image = Image::create(device, options.width, options.height);
    if (!image) return std::unexpected(image.error());
    auto pass = CoveragePass::create(device);
    if (!pass) return std::unexpected(pass.error());

    const PathGlobals globals = fitViewBox(doc.viewBox, options.width, options.height);

    for (std::size_t i = 0; i < doc.shapes.size(); ++i) {
        const icf::svg::Shape& shape = doc.shapes[i];

        // `none` is not an error and not a gap: the shape is deliberately
        // unpainted, and reporting it as skipped would bury the real gaps in
        // noise. It is simply not drawn.
        if (shape.fill.kind == icf::svg::PaintKind::None) continue;
        if (shape.fill.kind != icf::svg::PaintKind::Color) {
            out.skipped.push_back(
                {i, shape.element,
                 shape.fill.kind == icf::svg::PaintKind::Reference
                     ? "preenchimento por referencia (url(#" + shape.fill.reference +
                           ")) -- o caminho de gradiente nao foi transcrito"
                     : "valor de fill que este leitor nao le"});
            continue;
        }
        if (shape.path.segments.empty()) continue;

        auto buffer = buildPathBuffer(shape.path, BuildOptions{options.subdivisions});
        if (!buffer) {
            out.skipped.push_back({i, shape.element, buffer.error()});
            continue;
        }
        auto drew = pass->draw(device, *image, *buffer, globals);
        if (!drew) return std::unexpected(drew.error());
        auto coverage = readBack(device, *image);
        if (!coverage) return std::unexpected(coverage.error());
        if (coverage->size() != texels) {
            return std::unexpected("the coverage read back is not the target's size");
        }

        if (shape.fill.color.displayP3) out.unconvertedP3.push_back(i);
        const float colour[4] = {
            static_cast<float>(shape.fill.color.r), static_cast<float>(shape.fill.color.g),
            static_cast<float>(shape.fill.color.b), static_cast<float>(shape.fill.color.a)};
        const std::uint32_t state = stateFor(shape.fillRule);

        for (std::size_t t = 0; t < texels; ++t) {
            // `[BIN]` The signed area the edges accumulate lands in the SECOND
            // channel of the half2 target; the first carries the other half of
            // the pair. Doc 03 §12 and §13.
            const float alpha = accumulatorShape(state, (*coverage)[t].y, ShapeCurve{});
            if (alpha == 0.0f) continue;
            const CompositeOut c = compositeFlat(0u, 0u, colour, alpha, 0.0f);
            // `[BIN]` `c.coverage[0]` is the alpha the target REPORTS: the
            // paint's own alpha times the coverage, and the channel the invert
            // bit would act on. That number is measured.
            //
            // `[INF]` Stacking one shape on the next is a DIFFERENT operation
            // from `composite`, and it is not decoded: the target does it with a
            // framebuffer blend whose configuration lives in `RenderState` bits
            // that have no semantics yet (doc 03 §11). So the `over` below is
            // the standard premultiplied one and it is THIS renderer's choice,
            // not a transcription -- which is also why `c.colour` is not used
            // here: it is premultiplied by the coverage alone, not by the
            // paint's alpha, and it is the right value for the target's
            // attachment rather than for an accumulator.
            const float srcA = c.coverage[0];
            const float inv = 1.0f - srcA;
            float* dst = &acc[t * 4];
            for (int k = 0; k < 3; ++k) dst[k] = colour[k] * srcA + dst[k] * inv;
            dst[3] = srcA + dst[3] * inv;
        }
        ++out.drawn;
    }

    out.rgba.assign(texels * 4, 0.0f);
    for (std::size_t t = 0; t < texels; ++t) {
        const float a = acc[t * 4 + 3];
        for (int k = 0; k < 3; ++k) {
            out.rgba[t * 4 + k] = a > 0.0f ? acc[t * 4 + k] / a : 0.0f;
        }
        out.rgba[t * 4 + 3] = a;
    }
    return out;
}

}  // namespace rb
