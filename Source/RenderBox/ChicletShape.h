#pragma once
// The chiclet: the continuous-corner rounded square an icon is cut to.
//
// WHY THIS FILE EXISTS
// --------------------
// Until this file the background was painted over the whole canvas square and
// `kBackgroundShapeNote` said, truthfully, that the corner geometry had not been
// read. It has been now. `Docs/Laudos/2026-09-15-chiclet-curva.md` closes the
// one thing `Docs/Laudos/2026-09-15-chiclet.md` left open -- WHICH CURVE -- and
// everything below is a transcription of that reading, not a squircle taken
// from folklore.
//
// THE THREE READINGS THIS RESTS ON
// --------------------------------
//  1. `[BIN]` `RenderBox.arm64`, `set_rounded_rect` `0x21780`-`0x217A4`: a
//     continuous corner (`cornerStyle == 1`) is STORED multiplied by 1.275 and
//     tagged shape type 4.
//
//  2. `[BIN]` That multiply is a storage convention and EVERY reader undoes it,
//     guarded on the type byte being 4:
//        `Coverage::Primitive::add_path`   `0x96838` -- x 0.7843137
//        `Coverage::Primitive::encode`     `0x94550` -- x 0.7843137
//        `Coverage::Primitive::decode`     `0x94834` -- x 1.275 on the way back
//        `Coverage::Primitive::set_globals` `0x95A94` -- x 0.7843137
//     `add_path` is the encoder the older laudo asked for: it is what turns the
//     stored shape into `RBPathMakeRoundedRect` (`0x96880`), whose element is
//     `8 | continuous` (`0x118E2C`) -- the `RBPathElement` 9 that
//     `Mapper::apply_callback` (`0x80DCC`) hands to `add_rounded_rect`. It
//     DIVIDES. So the radius that reaches the curve is the radius the caller
//     asked for, and for the chiclet that is 266.24, not 339.456.
//
//  3. `[BIN]` `add_rounded_rect` `0x7F580`-`0x7FE58` then picks the curve by a
//     per-EDGE slack, `0x7F664`-`0x7F690`:
//        t = (|edge| - (r_near + r_far)) / ((r_near + r_far) * 0.528664947)
//     With r = 266.24 on a 1024 edge, t = 1.746 >= 1 -- the CANONICAL
//     continuous corner, not the blended one. Reading (1) alone would have given
//     t = 0.9615 and a visibly different corner; that is the fork this file
//     stands on the far side of.
//
// WHICH OF THE TARGET'S TWO PATHS THIS IS
// ---------------------------------------
// `[BIN]` The target draws an icon in one of two framings, and this file is the
// first one:
//
//  * FULL BLEED -- the chiclet fills the canvas square. This is what
//    `Configuration(icon:style:parametersOverride:)` (`IconRendering 0x19F4C`)
//    sets up, writing `useLegacyInsetting = 0` at `0x1A844`, and it is the ONLY
//    one Icon Composer itself ever asks for: `IconComposerKit` imports that init
//    and not the one that takes the flag.
//
//  * LEGACY INSET -- `0x4202C` shrinks the rect to
//    `side - 2 * floor(side * (relativeIconInset ?? 100/1024))`, the `100/1024`
//    being the immediate `0x3FB9000000000000` at `0x4224C` (the only one in the
//    bundle). That is the framing of the rendition Apple shipped inside
//    `Assets.car`, and of `AppIcon.icns`.
//
// So the 824/1024 of the oracle is NOT missing from this file: it belongs to a
// mode of the target that its own app never turns on, and
// `scripts/png-diff.py --legacy-inset` aligns the two framings for comparison
// instead.
//
// `[OBS]` In that legacy rendition the corner is the SAME curve with a SMALLER
// radius: fitted by `scripts/chiclet-profile.py`, Apple's 412 body wants
// `r = 0.225 * body` (RMS 0.088 px) where `0.26` costs 5.36 px, and our corner
// therefore starts 4.1 px earlier along the diagonal. No constant in the bundle
// produces 0.225 -- platform `main` has no radius override (`0x5EB38`), and the
// one mechanism that can replace the shape is a caller-supplied
// `GlobalConfiguration.iconShape` (`+0x58`, tested by `cbz` at `0x4296C`).
// `Docs/Laudos/2026-09-15-chiclet-geometria.md` has the measurement; the radius
// below stays 0.26 because that is what the reading of THIS path says.
#include <cstdint>
#include <vector>

#include "Source/CoreSVG/Path.h"
#include "Source/RenderBox/PixelGrid.h"

namespace rb {

// The three numbers that shape one END of one edge.
//
// `[BIN]` `0x7F694`-`0x7F6F8`. `extent` is how much of the edge the corner eats,
// measured in radii; `control` and `shoulder` place the two control points that
// leave the edge. At t >= 1 they are the canonical triple; below it they are a
// linear blend that reproduces the canonical triple exactly at t == 1 -- which
// is what validates both constant pools at once.
struct ContinuousCornerParams {
    double extent = 1.5286649465560913;    // `0x15EBC0` lane 0
    double control = 1.0884900093078613;   // `0x15EBC0` lane 1
    double shoulder = 0.8684070110321045;  // `0x15EC78`
};

// The slack of one edge, and the triple it selects. `edge` is the edge's length;
// `rNear` and `rFar` are the two corner radii MEASURED ALONG THAT EDGE.
//
// The division is done in float, as the target does it: the comparison against
// 1.0 is a cliff, and doing it in double would move the cliff.
ContinuousCornerParams continuousCornerParams(double edge, double rNear, double rFar);

// The outline of a continuous rounded rect: four lines and twelve cubics, the
// count `add_rounded_rect` emits for `RBPathElement` 9.
//
// The subpath starts at the middle of the right edge, as the target's does
// (`0x7F634`) -- a seam in the middle of an edge rather than inside a corner.
icf::svg::Path continuousRoundedRect(double x, double y, double w, double h,
                                     double rx, double ry);

// `[BIN]` `GlassRenderingParameters::defaultChicletCornerRadius` is 266.24 at
// `+0x228`, against the 1024 canvas of `Platform.extendedCanvasBounds`
// (`IconComposerFoundation 0x385D8`). 266.24 / 1024 is exactly 0.26, so the
// radius scales with the canvas without a second constant.
double chicletCornerRadius(std::uint32_t size);

// Coverage of the chiclet over the BUFFER of `g`, row major, one float per
// pixel in [0, 1]. Antialiased: a pixel the outline crosses gets the fraction
// of itself that is inside.
//
// O CONTORNO E ABSOLUTO -- ele e o retangulo continuo de `g.size` x `g.size`,
// com o raio de `g.size` -- e so a VARREDURA se restringe ao buffer, na
// coordenada absoluta `(x + g.originX) + 0.5` (spec 2026-09-16, "O invariante
// que governa o desenho"). Com `PixelGrid::full(size)` sai exatamente a
// cobertura de antes.
std::vector<float> chicletCoverage(const PixelGrid& g);

// A mesma cobertura sobre o canvas inteiro. Fica porque um chamador que so tem
// a resolucao nao precisa montar uma grade para dizer "tudo".
std::vector<float> chicletCoverage(std::uint32_t size);

// Multiplies a PREMULTIPLIED RGBA accumulator by that coverage, in place.
//
// Premultiplied is why all four channels are scaled and not just alpha: in that
// form the colour carries its own alpha and a partial pixel has to dim in step.
void clipToChiclet(std::vector<float>& acc, const PixelGrid& g);
void clipToChiclet(std::vector<float>& acc, std::uint32_t size);

}  // namespace rb
