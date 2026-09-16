// The SURFACE of `blur-material`: the clip arithmetic of `0x4A4A4`-`0x4A56C`
// and the composite of `beginLayerWithFlags:1` + `drawLayerWithAlpha:`.
//
// These pin a transcription that is deliberately SWITCHED OFF at its call site
// -- `IconRenderer.cpp` emits a note instead of calling it, because the layer
// frame at descriptor `+0x70` is still unread and the gabarito refused the only
// reading this front had for it. A transcription nothing calls is exactly the
// kind that rots, so it is clamped here instead of by a render.
#include "check.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "Source/RenderBox/BlurKernel.h"

namespace {
bool near(double a, double b, double eps = 1e-12) { return std::fabs(a - b) <= eps; }
}  // namespace

// `[BIN]` `radius = min(b,1) * 64` lives in the display list's own coordinates,
// which the clip rect proves are the 1024 canvas (`0x42940`). So a sigma in
// pixels is `radius * min(w,h) / 1024` -- and at `size == 1024` the two are the
// same number, which is the cheapest way to see that no scale was invented.
TEST_CASE(blur_material_sigma_is_canvas_units_per_pixel) {
    const rb::BlurMaterialSurface full =
        rb::blurMaterialSurface(35.84, 0.0, 0.0, 1.0, 1.0, 1024, 1024);
    CHECK(full.draws);
    CHECK(near(full.sigmaPixels, 35.84));

    const rb::BlurMaterialSurface half =
        rb::blurMaterialSurface(35.84, 0.0, 0.0, 1.0, 1.0, 512, 512);
    CHECK(half.draws);
    CHECK(near(half.sigmaPixels, 17.92));

    // `blurStrengthMax` is 64 and `min(b, 1.0)` has a ceiling but NO floor, so a
    // zero or negative radius is not a surface at all. This is the same gate
    // `shadowDraws` uses, one level up: 48 of the corpus's 123 `blur-material`
    // values are an explicit `null`, and a null must not read as "drawn".
    CHECK(!rb::blurMaterialSurface(0.0, 0.0, 0.0, 1.0, 1.0, 512, 512).draws);
    CHECK(!rb::blurMaterialSurface(-1.0, 0.0, 0.0, 1.0, 1.0, 512, 512).draws);
    CHECK(!rb::blurMaterialSurface(35.84, 0.0, 0.0, 1.0, 1.0, 0, 0).draws);
}

// `[BIN]` `CGRectInset(r, -[ctx+0x46A8], -[ctx+0x46A8])` at `0x4A558`-`0x4A564`,
// with `[ctx+0x46A8] = (1/escala)/contentsScale` = canvas units per PIXEL. The
// inset of a NEGATIVE is an OUTSET, and the outset is exactly one pixel -- so a
// frame that already covers the canvas clamps back to the canvas and a half
// frame grows by one pixel on each side.
TEST_CASE(blur_material_clip_is_the_frame_outset_by_one_pixel) {
    const rb::BlurMaterialSurface whole =
        rb::blurMaterialSurface(8.0, 0.0, 0.0, 1.0, 1.0, 400, 400);
    CHECK(whole.draws);
    CHECK_EQ(whole.x0, 0u);
    CHECK_EQ(whole.y0, 0u);
    CHECK_EQ(whole.x1, 400u);
    CHECK_EQ(whole.y1, 400u);

    // A quarter frame at the centre: `(0.25, 0.25, 0.5, 0.5)` of 400 pixels is
    // `[100, 300)`, grown by one pixel on every side.
    const rb::BlurMaterialSurface quarter =
        rb::blurMaterialSurface(8.0, 0.25, 0.25, 0.5, 0.5, 400, 400);
    CHECK(quarter.draws);
    CHECK_EQ(quarter.x0, 99u);
    CHECK_EQ(quarter.y0, 99u);
    CHECK_EQ(quarter.x1, 301u);
    CHECK_EQ(quarter.y1, 301u);

    // `CGRectGetMinX`/`GetWidth` standardise a negative size rather than
    // returning it, so a frame written backwards is the same rectangle.
    const rb::BlurMaterialSurface flipped =
        rb::blurMaterialSurface(8.0, 0.75, 0.75, -0.5, -0.5, 400, 400);
    CHECK(flipped.draws);
    CHECK_EQ(flipped.x0, quarter.x0);
    CHECK_EQ(flipped.x1, quarter.x1);
}

// The composite, and the one thing about it that a picture would hide: it runs
// on a PREMULTIPLIED accumulator and it is source-over, so a fully opaque
// backdrop stays fully opaque and only its colour moves.
TEST_CASE(blur_material_composite_is_source_over_on_premultiplied) {
    const std::uint32_t n = 64;
    std::vector<float> acc(static_cast<std::size_t>(n) * n * 4, 0.0f);
    for (std::uint32_t y = 0; y < n; ++y) {
        for (std::uint32_t x = 0; x < n; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * n + x) * 4;
            const float v = (x < n / 2) ? 1.0f : 0.0f;  // a hard vertical edge
            acc[i + 0] = v;
            acc[i + 1] = v;
            acc[i + 2] = v;
            acc[i + 3] = 1.0f;
        }
    }
    const std::vector<float> before = acc;

    const rb::BlurMaterialSurface s =
        rb::blurMaterialSurface(4.0 * 1024.0 / n, 0.0, 0.0, 1.0, 1.0, n, n);
    CHECK(s.draws);
    CHECK(near(s.sigmaPixels, 4.0));

    const std::size_t moved = rb::drawBlurMaterial(acc, n, n, s);
    CHECK(moved > 0);

    // Alpha was 1 everywhere and source-over with a source alpha of 1 leaves it
    // there: this is the reading that makes the composite a REPLACEMENT inside
    // an opaque backdrop.
    for (std::uint32_t y = 0; y < n; ++y) {
        const std::size_t i = (static_cast<std::size_t>(y) * n + n / 4) * 4;
        CHECK(near(acc[i + 3], 1.0f, 1e-5));
    }

    // The edge moved: a column that was flat 1.0 now sits below it, and one that
    // was flat 0.0 now sits above it.
    const std::size_t left = (static_cast<std::size_t>(n / 2) * n + (n / 2 - 1)) * 4;
    const std::size_t right = (static_cast<std::size_t>(n / 2) * n + (n / 2)) * 4;
    CHECK(before[left] == 1.0f && acc[left] < 0.95f);
    CHECK(before[right] == 0.0f && acc[right] > 0.05f);

    // Outside the clip nothing is touched. A zero-area clip draws nothing at
    // all, which is what keeps "refused" distinguishable from "drew nothing".
    std::vector<float> acc2 = before;
    rb::BlurMaterialSurface empty = s;
    empty.x1 = empty.x0;
    CHECK_EQ(rb::drawBlurMaterial(acc2, n, n, empty), std::size_t{0});
    CHECK(acc2 == before);

    std::vector<float> acc3 = before;
    rb::BlurMaterialSurface off;
    CHECK_EQ(rb::drawBlurMaterial(acc3, n, n, off), std::size_t{0});
    CHECK(acc3 == before);
}
