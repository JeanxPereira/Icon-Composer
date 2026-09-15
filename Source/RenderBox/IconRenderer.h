#pragma once
// A whole `.icon` bundle drawn: groups, layers, and the art each one names.
//
// WHAT THIS ADDS OVER `renderSvg`
// -------------------------------
// `renderSvg` draws ONE svg fitted to the target. A document is a stack: groups
// in order, layers in order inside them, each naming art that is PLACED on a
// canvas rather than fitted to it, each with its own scale, translation and
// opacity, and each resolved for a context (`appearance`, `idiom`).
//
// THE TWO THINGS THE DOCUMENT DOES NOT SAY, AND ARE THEREFORE ASSUMPTIONS
// -----------------------------------------------------------------------
// Measured over all 145 corpus documents: **no canvas-size key exists in any of
// them**. The size lives in the renderer -- `IconRendering.GlobalConfiguration`
// declares `canvasSize` (doc 03 §17.5) -- and that default has not been read.
//
//   1. `kCanvasPoints = 1024`. What supports it: the most common art viewBox is
//      `0 0 1024 1024` (36 of the corpus's 149 SVGs), and the largest layer
//      translation measured is 590 points, which sits inside a ±512 canvas plus
//      overhang and would be far outside a 512-point one. What does NOT support
//      it: nothing read it from the binary.
//
//   2. The origin is the canvas CENTRE and +y points DOWN. Centre because
//      translations are frequently negative, which a top-left origin would not
//      produce. The y direction was settled by rendering a composed icon whose
//      parts are recognisable and looking at it -- see doc 03 §22.
//
// Both are marked in the code where they are used. An assumption that is written
// down can be corrected; one that is merely coded becomes folklore.
//
// AND THE FILL, SINCE 2026-09-03
// ------------------------------
// The document's `fill` is no longer read by this file. `FillResolve.h` reads
// it -- twice, because the target reads it twice: a BACKGROUND converter and a
// LAYER converter that answer differently to the same word. This file consumes
// those answers and turns them into paint.
//
// Three consequences show in the pictures:
//
//   1. THE BACKGROUND IS DRAWN. Until now `renderIcon` drew layers over
//      nothing, and the root `fill` -- which every one of the 145 corpus
//      documents carries -- was not read at all.
//   2. `automatic` DRAWS. On the background it is a system chiclet ramp, or,
//      under `tinted`, `IconColor.clear`. On a LAYER it is not a colour: it is
//      what that same layer resolves at the `light` slot, which `[ART]` is nil
//      in 152 of its 156 corpus firings -- the art keeps its own colours.
//   3. `automatic-gradient` DRAWS. `2026-09-01-gradiente.md` §4.4 refused it
//      because THE AXIS HAD NEVER BEEN READ. It has been: all three converter
//      sites pass `placement: nil`, and nil is `GradientPlacement.default`,
//      `(0,0)->(0,1)`. The reason for that refusal is gone.
//
// AND THE ORIENTATION THE BACKGROUND THROWS AWAY
// ----------------------------------------------
// `[BIN]` Exactly one of the four gradient-constructing sites reads the
// document's `orientation`: the LAYER's `linear-gradient`. A BACKGROUND
// `linear-gradient` that names an orientation draws on the default vertical
// axis anyway. `[ART]` 61 background resolutions over the corpus name an
// orientation the target discards. Honouring it here would be more correct than
// the target and therefore a different pixel, so the background's axis comes
// from `resolveBackgroundFill`, which passes the nil as a literal.
//
// THREE THINGS DRAWN WITHOUT BEING READ, AND THEY SAY SO IN `notes`
// ------------------------------------------------------------------
// `kChicletRectNote`, `kGradientAxisDirectionNote` and `kBackgroundShapeNote`.
// Each names a gap that changes the pixels; none of them is a reason to refuse
// to draw, and none of them is allowed to be silent.
//
// AND THE GLASS, SINCE 2026-09-02
// -------------------------------
// A layer whose `glass` bit is set is no longer skipped. The material comes off
// the GROUP (`GlassMaterial.h`), the layer's art becomes contours, the contours
// become a field, the field becomes a displacement map, and the accumulator --
// which IS the backdrop -- is refracted through it before the layer's own art
// is drawn on top. `GlassLayer.h` carries the whole chain, the ordering
// decision, and the two gaps that are named rather than filled: the
// points-to-pixels ruler (which lands in `RenderedIcon::notes`) and raster art,
// which has no contour to flatten.
#include <cstdint>
#include <string>
#include <vector>

#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/IconComposerFoundation/IconDocument.h"
#include "Source/RenderBox/Device.h"
#include "Source/RenderBox/FillResolve.h"
#include "Source/RenderBox/GlassTranslucency.h"
#include "Source/RenderBox/SvgRenderer.h"
#include "Source/RenderBox/SystemFill.h"

namespace rb {

// `[INF]` The canvas the document's points are measured in. See the header note.
inline constexpr double kCanvasPoints = 1024.0;

struct IconRenderOptions {
    std::uint32_t size = 512;
    icf::Context context;
    int subdivisions = 16;

    // Which of `SizeBasedValue`'s four slots `glyphTranslucency.strength` is read
    // from (`GlassTranslucency.h`).
    //
    // `[OBS]` It is an option because the origin of the byte the target switches
    // on -- `self+0x469F`, read at `0x0000FDA0` -- was NOT located (laudo §7.2).
    // What the byte means is read; who writes it is not, so this renderer does
    // not invent a mapping from `size` onto it.
    //
    // `[BIN]` It costs nothing today: the aggregate initialiser writes 1.0 into
    // all four slots, so every choice gives the same pixel. It is here so that
    // the inversion `slots[3 - sizeClass]` has something to be exercised with
    // the day a parameter file differentiates the classes.
    IconSizeClass sizeClass = IconSizeClass::Large;
};

// A layer that was not drawn, and why. Named, never dropped in silence.
struct SkippedLayer {
    std::size_t group = 0;
    std::string layer;
    std::string why;
};

struct RenderedIcon {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> rgba;   // straight RGBA, row major

    std::size_t drawn = 0;
    std::size_t total = 0;
    std::vector<SkippedLayer> skipped;
    // Shapes inside drawn layers that the SVG renderer itself could not draw.
    std::vector<std::string> shapeGaps;

    // Things that WERE drawn but whose provenance is short of a measurement,
    // said in words. A gap that changes the pixels and is not reported becomes
    // folklore the moment the picture looks plausible -- so the glass's
    // points-to-pixels ruler (`GlassLayer.h`, GAP ONE) announces itself here
    // instead of scaling quietly. Deduplicated: one sentence per render, not
    // one per layer.
    std::vector<std::string> notes;

    // The document's root `fill` -- the icon's background -- counted APART from
    // the layers. `drawn`, `total` and `skipped` are the layer ruler, and the
    // corpus gate asserts `drawn + skipped <= total` on it; a background folded
    // into those numbers would break that arithmetic and, worse, would make a
    // document with one layer and a background read as two layers.
    bool backgroundPainted = false;
    // Non-empty when the root `fill` is present and could not be turned into
    // paint. `[ART]` All 145 corpus documents carry a root `fill`, so an empty
    // `backgroundGap` with `backgroundPainted == false` means the document
    // named none -- which no corpus document does.
    std::string backgroundGap;

    // Layers whose art was multiplied by the `simplifiedShapeAwareGradientMask`
    // that `translucency` opens. Counted apart from `drawn` for the same reason
    // `glassRefracted` is: a group whose `translucency` is absent, zero or
    // switched off produces an identity mask, and "drew opaque because the
    // document said opaque" must stay distinguishable from "drew opaque because
    // the effect is not implemented" -- which is what this whole number is for.
    std::size_t glassTranslucent = 0;

    // Layers drawn with their glass refracting the backdrop underneath them.
    // Counted apart from `drawn` because a glass layer whose refraction is the
    // identity (`refractionStrength == 0`, the read default) draws its art and
    // refracts nothing, and the two outcomes must be distinguishable.
    std::size_t glassRefracted = 0;
};

// The placement of one layer's art on the canvas, in the target's own terms.
// Exposed so the transform can be checked without a GPU.
struct LayerPlacement {
    double scale = 1.0;
    double translateX = 0.0;   // canvas points, from the CENTRE
    double translateY = 0.0;
};

// `[INF]` A group's transform applies to the layer's, so the two compose:
// `G(L(p)) = gs*ls*p + gs*lt + gt`. The translation of the inner one is scaled
// by the outer, which is what composition means and is not something the
// document states.
LayerPlacement compose(const LayerPlacement& group, const LayerPlacement& layer);

// The globals that place `box` on the canvas under `p`, for a square target.
PathGlobals placeOnCanvas(const icf::svg::ViewBox& box, const LayerPlacement& p,
                          std::uint32_t size);

// ---- the fill, from a resolution to paint --------------------------------

// The rect a fill's gradient is placed against, in TARGET PIXELS.
//
// `[BIN]` The draw path resolves a placement against the `boundingRect` of the
// shape being filled (`0x1BC8C`), and the unit-to-rect mapping is
// `SystemFill.h`'s `placeUnitPoint`. So the rect is not decoration: it is where
// the axis gets its length and its offset.
//
// `[OBS]` WHICH rect an `Icon.Element` reports there -- the `bounds` it is
// constructed with, the art's viewBox, or the tight bounding box of the paths
// actually drawn -- was not read. This returns the art's viewBox as placed on
// the canvas, which is the box the element's own `contents` declare.
//
// This replaces an earlier assumption. Until now a layer `linear-gradient` took
// its `orientation`'s unit square to be THE CANVAS, marked `[OBS]` because the
// corpus cannot tell the two apart while every orientation runs corner to
// corner. The rect is now read, so the assumption is retired rather than kept
// beside the reading.
PlacementRect artPlacementRect(const icf::svg::ViewBox& box, const LayerPlacement& p,
                               std::uint32_t size);

// A resolved fill turned into what the compositor paints with, against
// `shapeRect`.
//
// `why` is set, and the returned paint is `Kind::None`, when a fill that
// resolved cannot be painted -- which is one case only: a gradient whose
// placement collapses to a point, where there is no axis to project onto.
// `[ART]` No corpus fill does that; the guard exists because a document could.
//
// Exposed so the whole corpus can be swept through it with no GPU: the
// interesting failures of this function are arithmetic, and a sweep that needed
// a device would not run in the places that most need it.
FillOverride fillPaint(const ResolvedFill& fill, const PlacementRect& shapeRect,
                       std::string& why);

// ---- what is drawn without having been read ------------------------------
//
// Each of these lands in `RenderedIcon::notes`, deduplicated, the way
// `glassRulerNote` does. They are not skip reasons: the pixel IS drawn. They
// exist because a gap that changes the picture and is not said out loud becomes
// folklore the moment the picture looks plausible.

// `[BIN]` `ICRRenderingParameters+0x360` --
// `supportsChicletAlignmentForSystemFills`, written `1` by the default
// initialiser -- makes a `.system` fill's rect origin `(0,0)` with a `CGSize`
// from the drawing context instead of the shape's bounding rect.
// `[OBS]` Whether that size is the canvas, the chiclet or the full-bleed frame
// was NOT read, and `SystemFill::systemFillRect` answers `nullopt` for it on
// purpose. So this renderer draws on the bounding rect -- the branch the target
// takes when alignment is off -- and says so here.
extern const char* const kChicletRectNote;

// `[OBS]` The default axis is `(0,0)->(0,1)` and that is read. Which END of the
// shape receives the first stop is not: the y-handedness of the RB display list
// was never established. On the light chiclet ramp the two answers differ by
// four parts in 255; on the dark one, 31 against 15, they do not.
extern const char* const kGradientAxisDirectionNote;

// `[BIN]` The background converter reads `primaryColor` and `secondaryColor`
// and never touches `orientation`. Said only when the document actually named
// one, because the note is about THIS document having asked for something the
// target throws away -- not about the rule.
extern const char* const kDiscardedBackgroundOrientationNote;

// `[OBS]` The background is painted over the whole canvas square. The chiclet's
// own geometry -- the corner radius that would clip it -- was not read, so
// there is no shape to cut it to.
extern const char* const kBackgroundShapeNote;

// The layer's fill reaches the VECTOR path only. `[ART]` 45 of the corpus's
// layers name `.png` art and some of those carry a fill; the raster is placed
// with its own colours. `[OBS]` Whether the target retints a raster element the
// way it retints a vector one was not read, so this is named rather than
// guessed in either direction.
extern const char* const kRasterFillNote;

// `[OBS]` display-p3 components drawn without a conversion matrix, the same gap
// `RenderedImage::unconvertedP3` reports for a shape's own paint.
extern const char* const kBackgroundP3Note;

// The two things the translucency mask is drawn WITHOUT having read, said out
// loud every time it draws: the rect its vertical ramp is measured in, and which
// end of that rect the ramp starts at.
extern const char* const kTranslucencyBoundsNote;

// A glass layer whose group asks for translucency but whose art is a raster: the
// mask is shaped by a distance field and a raster has no contour to build one
// from. The layer still draws -- opaque, as it did before -- and says so.
extern const char* const kTranslucencyRasterNote;

Result<RenderedIcon> renderIcon(Device& device, const icf::IconBundle& bundle,
                                IconRenderOptions options = IconRenderOptions{});

}  // namespace rb
