#include "Source/RenderBox/GlassSpecular.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Source/RenderBox/Parallel.h"
#include "Source/RenderBox/BlendFormula.h"

namespace rb {
namespace {

constexpr double kPi = 3.14159265358979323846;
// `[BIN]` `2^-10`, the floor the shader clamps `fwidth` and the two
// denominators to (`default_mod1.ll`, `0x3AA00000` as a half / `0.0009765625`).
constexpr double kEps = 0.0009765625;
// `[BIN]` The half `0xH3AAA` of `default_mod1.ll` `%15`, decoded exactly.
constexpr double kBandWidth = 0.8330078125;

double sat(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

HighlightSizeValue sbv(double display, double large, double medium, double small,
                       bool present = true) {
    HighlightSizeValue v;
    v.slots[0] = display;
    v.slots[1] = large;
    v.slots[2] = medium;
    v.slots[3] = small;
    v.present = present;
    return v;
}

HighlightSizeValue flat(double all, bool present = true) {
    return sbv(all, all, all, all, present);
}

// `[BIN]` The three `SizeBasedValue`s `0x00063AEC`-`0x00063B34` hands the glyph
// factory `0x00064604`. All five `glyphs*` sets get these same three, so there
// is one table here and not five.
//
//   A = {1.0, 1.0, 1.0, 0.3}   sp+0x120: (1.0,1.0) from sp+0xB0, (1.0,0.3) from 0x98540
//   B = {1.0, 1.0, 0.2, 0.2}   sp+0x100: (1.0,1.0) from sp+0xB0, (0.2,0.2) from sp+0xD0
//   C = {0.9, 0.9, 0.2, 0.2}   sp+0x0E0: (0.9,0.9) from sp+0xA0, (0.2,0.2) from sp+0xD0
const HighlightSlot* buildGlyphSlots(std::size_t& count) {
    static HighlightSlot slots[5];
    static bool built = false;
    if (!built) {
        const HighlightSizeValue A = sbv(1.0, 1.0, 1.0, 0.3);
        const HighlightSizeValue B = sbv(1.0, 1.0, 0.2, 0.2);
        const HighlightSizeValue C = sbv(0.9, 0.9, 0.2, 0.2);
        // `[BIN]` `Highlights+0x10` and `+0x30`: `fmov v0.2d, #0.75` stored four
        // times over each (`0x00062AD0`-`0x00062AE0`). Highlight and darklight
        // curvature are the same number in this version, so the `w22` select of
        // `0x000311C4` cannot be seen in a pixel.
        const HighlightSizeValue curvature = flat(0.75);
        const HighlightSizeValue one = flat(1.0);
        const HighlightSizeValue nil = flat(0.0, false);

        // keySharp -- `0x00064648`-`0x000646F8`, written to set+0x000.
        HighlightSettings keySharp;
        keySharp.brightness = 1.0;
        keySharp.opacity = A;
        keySharp.outsetOpacity = nil;
        keySharp.distance = sbv(4.0, 6.0, 6.0, 6.0);
        keySharp.minDistancePixels = one;
        keySharp.inset = flat(0.0);
        keySharp.minInsetPixels = nil;
        keySharp.spread = flat(kPi / 2.0);
        keySharp.bias = 0.5;

        // keyDiffuse -- `0x000646FC`-`0x00064788`, set+0x108.
        HighlightSettings keyDiffuse;
        keyDiffuse.brightness = 1.0;
        keyDiffuse.opacity = one;
        keyDiffuse.outsetOpacity = nil;
        keyDiffuse.distance = sbv(16.0, 24.0, 24.0, 24.0);
        keyDiffuse.minDistancePixels = flat(4.0);
        keyDiffuse.inset = flat(0.0);
        keyDiffuse.minInsetPixels = nil;
        keyDiffuse.spread = flat(kPi / 3.0);
        keyDiffuse.bias = 0.08;

        // fillSharp -- `0x0006478C`-`0x00064854`, set+0x210. Same as keySharp
        // except the cone, which is the narrower one.
        HighlightSettings fillSharp = keySharp;
        fillSharp.spread = flat(kPi / 3.0);

        // dark -- `0x00064888`-`0x00064910`, set+0x420. The ONLY one of the six
        // with an `outsetOpacity`, which is exactly the one that `0x000494F8`
        // needs before it will consider drawing outside.
        HighlightSettings dark;
        dark.brightness = 0.0;
        dark.opacity = B;
        dark.outsetOpacity = C;
        dark.distance = sbv(4.0, 6.0, 6.0, 6.0);
        dark.minDistancePixels = one;
        dark.inset = flat(0.0);
        dark.minInsetPixels = nil;
        dark.spread = sbv(kPi / 2.0, kPi / 2.0, kPi, kPi);
        dark.bias = 0.2;

        // `[BIN]` `fillDiffuse` is `FillHighlights? == nil` (`0x00033FA4` writes
        // the byte 20) and `rim` is `HighlightSettings? == nil` (`0x00033DE4`
        // writes 19), so slots 3 and 6 of `0x00030E88` are dropped by the filter
        // at `0x00031384`. Five survive.
        slots[0] = HighlightSlot{keySharp, 0.0, curvature, false};
        slots[1] = HighlightSlot{keyDiffuse, 0.0, one, false};
        slots[2] = HighlightSlot{fillSharp, kPi, curvature, false};
        slots[3] = HighlightSlot{dark, kPi / 2.0, curvature, true};
        slots[4] = HighlightSlot{dark, -kPi / 2.0, curvature, true};
        built = true;
    }
    count = 5;
    return slots;
}

}  // namespace

double highlightSizeValue(const HighlightSizeValue& v, IconSizeClass sizeClass) {
    // `[BIN]` `0x0004BEB0`-`0x0004BEF4`: a four-way ladder that lands `k == 0`
    // on the LAST slot. The same inversion `GlassShadow.h` carries, read out of
    // a second function on a second struct.
    const int k = static_cast<int>(sizeClass);
    return v.slots[3 - (k < 0 ? 0 : (k > 3 ? 3 : k))];
}

const HighlightSlot* glyphHighlightSlots(std::size_t& count) { return buildGlyphSlots(count); }

GlassHighlightSettings resolveHighlight(const HighlightSlot& slot, const SpecularArguments& args) {
    const HighlightSettings& hs = slot.settings;
    GlassHighlightSettings out;

    // `[BIN]` `0x0004BEFC`-`0x0004BF04` and `0x0004BF28`-`0x0004BF30`: both are
    // `max`, and the second one feeds `-INFINITY` when the Optional is `nil`
    // (`mov x8, #-0x10000000000000`, `0x0004BF20`) so that the `max` collapses
    // to the plain value instead of being branched around.
    const double ppp = args.pixelsPerPoint;
    const double heightPx = std::max(highlightSizeValue(hs.distance, args.sizeClass) * ppp,
                                     highlightSizeValue(hs.minDistancePixels, args.sizeClass));
    const double minInset = hs.minInsetPixels.present
                                ? highlightSizeValue(hs.minInsetPixels, args.sizeClass)
                                : -std::numeric_limits<double>::infinity();
    double insetPx = std::max(highlightSizeValue(hs.inset, args.sizeClass) * ppp, minInset);
    double curvature = highlightSizeValue(slot.curvature, args.sizeClass);
    double alpha = highlightSizeValue(hs.opacity, args.sizeClass);

    // `[BIN]` `0x000495D8`-`0x000495F0`. The bit only ever ASKS for outside; the
    // two refusals live in `specularDrawsInside`.
    const bool inside = specularDrawsInside(args.placement, args.identityRecolour,
                                            slot.isDarklight, hs.outsetOpacity.present);
    if (!inside) {
        insetPx -= heightPx;
        curvature = 0.0;
        alpha = highlightSizeValue(hs.outsetOpacity, args.sizeClass);
    }

    // `[BIN]` `0x0004BEF8` then `0x0004C014`-`0x0004C030`: two `__sincos_stret`
    // calls, `theta` for the plane and `phi` for the tilt out of it, folded as
    // `(cos phi sin theta, cos phi cos theta, sin phi)`.
    //
    // `[BIN]` `phi == 0` here because `0x0004BE74` takes that branch when the
    // `customLightDirection` Optional tag `ctx[0x20]` is `1` (`nil`). And it
    // would not matter if it were not: `spatialHighlight()` below overwrites
    // the direction with the `phi = 0` form on every path.
    const double theta = slot.angleFromKey + args.lightLongitude;
    const double cosPhi = std::cos(args.lightLatitude);
    out.directionX = cosPhi * std::sin(theta);
    out.directionY = cosPhi * std::cos(theta);
    out.directionZ = std::sin(args.lightLatitude);

    out.spread = highlightSizeValue(hs.spread, args.sizeClass);
    out.bias = hs.bias;
    out.height = heightPx;
    out.inset = insetPx;
    out.curvature = curvature;
    // `[BIN]` `ctx[0]` (`0x0004C010`) is `GlobalConfiguration.lightIntensity`
    // and `0x0004C0A8` (`fmul d12, d13, d12`) multiplies the size-resolved
    // opacity by it. It is no longer a silent identity: it is a named field
    // whose shipped value is `1.0`. `layerOpacity` is the second multiplier
    // (`[descriptor+0x38]`, `0x000495F0`).
    out.opacity = args.lightIntensity * alpha * args.layerOpacity;

    // `[BIN]` `0x0004C078`-`0x0004C0B0`: `brightness` broadcast into rgb with
    // alpha forced to `1.0`, and the mode picked off the same number.
    out.colour[0] = hs.brightness;
    out.colour[1] = hs.brightness;
    out.colour[2] = hs.brightness;
    out.colour[3] = 1.0;
    out.blendMode = hs.hasBlendModeOverride
                        ? hs.blendModeOverride
                        : (hs.brightness < 0.5 ? BlendMode::PlusDarker : BlendMode::PlusLighter);

    // `[BIN]` `0x0004C0F8` runs the spatial post-pass over the block it has
    // just written, and `0x0004C0FC` (`ldur d8, [x29, #-0x100]`) reloads the
    // opacity from it -- which is how we know it writes in place.
    //
    // With the six parameters unread we may only run it where it is provably
    // the identity, and `spatialHighlight()` states where that is. Running it
    // anyway with placeholder numbers would be inventing a magnitude; NOT
    // running it at `phi == 0` invents nothing, because there it does nothing.
    if (args.spatial.read) {
        spatialHighlight(out, args.spatial);
    } else {
        // `[BIN]` `0x0001263C`-`0x00012650`, the one rewrite that is
        // unconditional and parameter-free: the direction is re-derived from
        // its own azimuth and `z` is zeroed. At `phi == 0` this is already
        // where `resolveHighlight` left it, so it is written out rather than
        // skipped, to keep the two branches the same function.
        const double az = std::atan2(out.directionX, out.directionY);
        out.directionX = std::sin(az);
        out.directionY = std::cos(az);
        out.directionZ = 0.0;
    }
    return out;
}

void spatialHighlight(GlassHighlightSettings& s, const SpatialHighlighting& p) {
    // `[BIN]` `0x00012588`-`0x000125B8`. `hypot` of the two PLANAR components,
    // divided by the SINE of `alignmentRange` -- not by the angle itself.
    const double planar = std::hypot(s.directionX, s.directionY);
    const double t = std::max(0.0, 1.0 - planar / std::sin(p.alignmentRange));

    // `[BIN]` `0x000125BC`-`0x000125E0`. Aligned (`t -> 1`) sinks the opacity
    // to `minIntensity`; unaligned (`t == 0`) leaves it alone.
    s.opacity *= p.minIntensity + (1.0 - p.minIntensity) * (1.0 - std::pow(t, p.intensityPower));

    // `[BIN]` `0x000125E4`-`0x00012614`. The cone opens TOWARDS pi, which is
    // the sentinel the shader boundary reads as "no cone at all".
    s.spread += std::pow(t, p.spreadPower) * (kPi - s.spread);

    // `[BIN]` `0x00012618`-`0x00012638`.
    s.height *= 1.0 + p.maxExtraHeight * std::pow(t, p.heightPower);

    // `[BIN]` `0x0001263C`-`0x00012650`. `atan2` then `__sincos_stret`, and
    // `str xzr, [x20, #0x18]`: the latitude is gone by the time the shader
    // sees the direction.
    const double az = std::atan2(s.directionX, s.directionY);
    s.directionX = std::sin(az);
    s.directionY = std::cos(az);
    s.directionZ = 0.0;
}

double glassHighlightFragment(const GlassHighlightSettings& s, double sd, double nx, double ny,
                              double fwidthSd) {
    // `[BIN]` `glassHighlight_v1` line 48: the inset is subtracted from the
    // distance BEFORE the band, which is why `inset` is an anchor and not a
    // second thickness.
    const double d = sd - s.inset;
    if (s.height <= 0.0) return 0.0;

    // `[BIN]` `%15 = fmul fast half %14, 0xH3AAA` (`default_mod1.ll`). The
    // multiply is in HALF, so the constant the hardware uses is the half that
    // `0xH3AAA` decodes to and not the decimal it was written from:
    // exponent `0xE` and mantissa `0x2AA` give `2^-1 * (1 + 682/1024)` =
    // `0.8330078125`. This file carried `0.83349` -- the source decimal, five
    // parts in ten thousand away from the value the band edge is actually
    // divided by.
    const double w = std::min(std::max(fwidthSd, kEps), 2.0) * kBandWidth;
    const double band = sat(d / w + 0.5) * sat((s.height - d) / w + 0.5);
    if (band <= 0.0) return 0.0;

    const double k = sat((s.height - 1.0) * 0.5);
    const double t = k * k * s.curvature * (3.0 - 2.0 * k);
    const double shade = 1.0 + t * ((1.0 - sat(d / s.height)) - 1.0);

    // `[BIN]` `0x0000EE3C`-`0x0000EF5C`: the cone reaches the shader as a
    // COSINE, with `-1000` standing for "no cone at all".
    const double cone = (s.spread > kPi) ? -1000.0 : std::cos(s.spread);
    // `[BIN]` `fneg s6`, `0x0000EF8C`: the y of the direction is negated on the
    // way in, so the light's `+y` is the image's `-y`.
    const double dot = s.directionX * nx + (-s.directionY) * ny;
    const double lit = sat((dot - cone) / std::max(1.0 - cone, kEps));

    const double a = lit * shade;
    // `[BIN]` `bias' = 1/bias - 2`, `0x0000E9DC`-`0x0000E9EC`.
    const double bias = 1.0 / s.bias - 2.0;
    return band * a / std::max(1.0 + (1.0 - a) * bias, kEps);
}

namespace {

// `[BIN]` The two `CAColorMatrix` bases, 4 rows x 5 columns of `float`, copied
// byte for byte from `IconRendering.arm64 __const`: the `swift_once` bodies
// `0x00006948` (-> `0xE2960`, from `0x938E0`) and `0x00006988` (-> `0xE2910`,
// from `0x93920`), whose last `stp q1, q2, [x8, #0x30]` writes the shared
// alpha row `[0, 0, 0, 1, 0]` from `0x938D0`. Kept as `float` because that is
// the precision the target composes them in.
constexpr float kRgbToYcbcr709[4][5] = {
    {0.2126f, 0.7152f, 0.0722f, 0.0f, 0.0f},
    {-0.1146f, -0.3854f, 0.5f, 0.0f, 0.5f},
    {0.5f, -0.4542f, -0.0458f, 0.0f, 0.5f},
    {0.0f, 0.0f, 0.0f, 1.0f, 0.0f},
};
constexpr float kYcbcrToRgb709[4][5] = {
    {1.0f, 0.0f, 1.5748f, 0.0f, -0.7874f},
    {1.0f, -0.1873f, -0.4681f, 0.0f, 0.3277f},
    {1.0f, 1.8556f, 0.0f, 0.0f, -0.9278f},
    {0.0f, 0.0f, 0.0f, 1.0f, 0.0f},
};

}  // namespace

const GlyphVCM& glyphHighlightVCM() {
    // `[BIN]` `Highlights+0xB0`..`+0xD0`.
    static const GlyphVCM v{0.2, 1.2, 1.25, 0.0, true};
    return v;
}

const GlyphVCM& glyphDarklightVCM() {
    // `[BIN]` `Highlights+0xD8`..`+0xF8`.
    static const GlyphVCM v{-0.15, 0.7, 1.25, 0.0, true};
    return v;
}

// THE LEG THAT WAS MISSING, READ -- `Docs/Laudos/2026-09-15-realce-vcm-fechado.md`
// -----------------------------------------------------------------------------
// `[BIN]` `-[RBDisplayList beginLayerWithFlags:]` (`RenderBox 0x0003BCA0`) masks
// its argument (`0x7B`/`0x77`, `|0x80` when `0xA0` is set) and for `1` passes
// `1` unchanged to `Builder::begin_layer(State, OptionSet<Layer::Flag>)`
// (`0x000C9A28`) -> `make_layer` (`0x000C9940`) -> `Layer::Layer(uint,
// OptionSet<Flag>)` (`0x0014DB74`), whose `stp w1, w2, [x0, #0x40]` puts the
// flags at `Layer+0x44`.
//
// `[BIN]` `Builder::null_style_draw` (`0x000CDD80`) is where a finished layer
// item is placed, and at `0x000CE028` it reads that word:
//
//     0x000CE028  ldr  w8, [x22, #0x44]      ; Layer::Flag
//     0x000CE02C  tbz  w8, #0, 0xCE050       ; bit 0 clear -> ordinary append
//     ...                                    ; (parent bit 7 clear, +0x38 == 0,
//     0x000CE04C  b.eq 0xCE160               ;  effect & 3 == 0)
//     0x000CE160  ldr  x0, [x22, #0x18]      ; the layer's filter list
//     0x000CE174  ldr  x8, [x8, #0x70]       ; vtable slot 14
//     0x000CE17C  blr  x8                    ; -> make_backdrop_item(Builder&)
//     0x000CE1A4  bl   Heap::alloc<BackdropFilterItem<Filter::ColorMatrix>>
//                                            ; (no filter: an identity copy)
//     0x000CE20C  bl   Builder::append       ; into the PARENT layer
//     0x000CE210  str  xzr, [x22, #0x18]     ; the filter is consumed
//
// Slot `+0x70` is `make_backdrop_item` by the vtable, not by guess:
// `GenericFilter<Filter::ColorMatrix>`'s vtable (`0x0018B658`) lists entry 16
// = `0x0007E850` = `GenericFilter<ColorMatrix>::make_backdrop_item`, and
// entries start at `+0x10`, so the call offset is `(16 - 2) * 8 = 0x70`. The
// vtable the fallback stores (`0x0018B0A0`) starts with `0x0007D798`, the
// `BackdropFilterItem<Filter::ColorMatrix>` the previous laudo named. The same
// bit also blocks the inline merge at `0x000CDEF0` (`tbnz w11, #0`).
//
// So bit 0 of `Layer::Flag` is the BACKDROP bit: the `addColorMatrixFilter`
// installed on that layer is turned into an item that filters what is already
// in the parent -- the matrix reads the backdrop.
void applyGlyphVCM(const GlyphVCM& vcm, double rgb[3]) {
    double ycc[3];
    for (int r = 0; r < 3; ++r) {
        ycc[r] = kRgbToYcbcr709[r][0] * rgb[0] + kRgbToYcbcr709[r][1] * rgb[1] +
                 kRgbToYcbcr709[r][2] * rgb[2] + kRgbToYcbcr709[r][4];
    }
    // `[BIN]` The levels matrix, `0x00049A64`-`0x00049AA8`: Y row only.
    ycc[0] = (vcm.lumaCeiling - vcm.lumaFloor) * ycc[0] + vcm.lumaFloor;
    // `[BIN]` The chroma matrix, `0x00049B18`-`0x00049B68`, skipped by
    // `0x00049AE4` when `VCM[2] == 1.0` -- where it would be the identity anyway.
    const double k = 0.5 - 0.5 * vcm.saturation;
    ycc[1] = vcm.saturation * ycc[1] + k;
    ycc[2] = vcm.saturation * ycc[2] + k;
    for (int r = 0; r < 3; ++r) {
        rgb[r] = kYcbcrToRgb709[r][0] * ycc[0] + kYcbcrToRgb709[r][1] * ycc[1] +
                 kYcbcrToRgb709[r][2] * ycc[2] + kYcbcrToRgb709[r][4];
    }
}

std::size_t drawSpecular(std::vector<float>& rgba, const FieldImage& field,
                         const SpecularArguments& args) {
    const std::size_t n = static_cast<std::size_t>(field.width) * field.height;
    if (rgba.size() < n * 4) return 0;

    std::size_t count = 0;
    const HighlightSlot* slots = glyphHighlightSlots(count);
    // `[BIN]` The highlight draws are the ones the `shouldClampPlusLBlending`
    // gate covers -- see `SpecularArguments::clampPlusLighter`.
    BlendOptions blendOptions;
    blendOptions.clampPlusLighter = args.clampPlusLighter;
    // Distinct pixels, not pixel-passes: five highlights over the same rim
    // would otherwise report five times the area they cover.
    std::vector<char> hit(n, 0);

    for (std::size_t s = 0; s < count; ++s) {
        const GlassHighlightSettings g = resolveHighlight(slots[s], args);
        if (g.opacity <= 0.0 || g.height <= 0.0) continue;

        // One row per worker, and a join before the next highlight: a pixel
        // still sees the highlights in slot order, and no pixel reads another.
        parallelRanges(field.height, n * 24, [&](std::size_t y0, std::size_t y1) {
        for (std::uint32_t y = static_cast<std::uint32_t>(y0); y < static_cast<std::uint32_t>(y1); ++y) {
            for (std::uint32_t x = 0; x < field.width; ++x) {
                const float* p = field.at(x, y);
                // The field is NEGATIVE INSIDE and the shader's `sd` is positive
                // inside -- the same flip `glassOpacityMask` already makes.
                const double sd = -static_cast<double>(p[0]);
                const double nx = p[1];
                const double ny = p[2];
                if (nx == 0.0 && ny == 0.0) continue;

                // `fwidth(sd)` for a field whose slope is one per pixel.
                const double f = glassHighlightFragment(g, sd, nx, ny, 1.0);
                if (f <= 0.0) continue;

                const double alpha = f * g.opacity;
                const std::size_t px = static_cast<std::size_t>(y) * field.width + x;
                const std::size_t i = px * 4;

                if (args.useVCM) {
                    // `[BIN]` The highlight shape is drawn into a layer and
                    // closed with `clipLayerWithAlpha:1.0 mode:0` (`0x000497BC`),
                    // so what survives of it is its coverage; the colour and
                    // blend mode of the shape draw do not reach the pixel.
                    // `[INF]` `mode:0` is the alpha clip: a luminance clip would
                    // erase both darklights, whose colour is black.
                    const double cov = alpha > 1.0 ? 1.0 : alpha;
                    const double a = rgba[i + 3];
                    // An empty backdrop has nothing for the matrix to lift; its
                    // alpha row is `[0,0,0,1,0]`, so the result stays invisible.
                    if (a <= 0.0) continue;
                    double straight[3];
                    for (int c = 0; c < 3; ++c) straight[c] = rgba[i + c] / a;
                    double lifted[3] = {straight[0], straight[1], straight[2]};
                    applyGlyphVCM(slots[s].isDarklight ? glyphDarklightVCM() : glyphHighlightVCM(),
                                  lifted);
                    // `[INF]` `lerp(backdrop, VCM(backdrop), coverage)` with the
                    // backdrop's alpha kept. Where the backdrop is opaque this is
                    // exactly `drawLayerWithAlpha:1.0 blendMode:0` of the filtered
                    // copy through the clip; on a fringe with alpha < 1, whether
                    // the backdrop item REPLACES or composites over is `[OBS]`.
                    for (int c = 0; c < 3; ++c) {
                        double v = (straight[c] + cov * (lifted[c] - straight[c])) * a;
                        if (v < 0.0) v = 0.0;
                        if (static_cast<float>(v) != rgba[i + c]) hit[px] = 1;
                        rgba[i + c] = static_cast<float>(v);
                    }
                    continue;
                }

                BlendColour src;
                src.rgba[0] = g.colour[0] * alpha;
                src.rgba[1] = g.colour[1] * alpha;
                src.rgba[2] = g.colour[2] * alpha;
                src.rgba[3] = alpha;

                BlendColour dst;
                for (int c = 0; c < 4; ++c) dst.rgba[c] = rgba[i + c];
                const BlendColour outc = blend(g.blendMode, src, dst, blendOptions);
                for (int c = 0; c < 4; ++c) {
                    const double v = outc.rgba[c] < 0.0 ? 0.0 : outc.rgba[c];
                    if (static_cast<float>(v) != rgba[i + c]) hit[px] = 1;
                    rgba[i + c] = static_cast<float>(v);
                }
            }
        }
        });
    }
    std::size_t touched = 0;
    for (std::size_t i = 0; i < n; ++i) touched += static_cast<std::size_t>(hit[i]);
    return touched;
}

SpecularBand specularBand(const SpecularBand& declared, bool inside) {
    if (inside) return declared;
    // `[BIN]` `fsub d0, d2, d1` at `0x000495E0` is `inset - height`, with
    // `d1 == height` (`+0x30`) and `d2 == inset` (`+0x38`) loaded together by
    // the `ldp d1, d2, [x21, #-0x10]` at `0x000495C0`. The subtraction is
    // written in that order, not as `-(height - inset)`, because that is the
    // order it was read and this project's differentials are bit-for-bit.
    SpecularBand out = declared;
    out.inset = declared.inset - declared.height;
    out.curvature = 0.0;
    return out;
}

bool specularPlacementBit(SpecularPlacement placement, bool identityRecolour) {
    switch (placement) {
        case SpecularPlacement::Inside:
            return true;
        case SpecularPlacement::Outside:
            return false;
        case SpecularPlacement::Automatic:
            break;
    }
    // `[BIN]` `0x00049280`-`0x000492C0`. The three tests are ANDed, and the
    // middle one is an OR of five 64-bit loads compared against zero -- so any
    // one of them non-zero takes the automatic case to `outside`.
    return identityRecolour;
}

bool specularDrawsInside(SpecularPlacement placement, bool identityRecolour, bool isDarklight,
                         bool hasOutsetOpacity) {
    // `[BIN]` The `csinc` at `0x000494F8` tests the Optional tag FIRST in
    // effect: tag == 1 (nil) forces the result to 1 regardless of `w10`.
    if (!hasOutsetOpacity) return true;
    // `[BIN]` `eor w10, w12, #1` then `orr w10, w11, w10` at `0x000494E4` /
    // `0x000494EC`: a bright highlight contributes a literal 1 to the OR.
    if (!isDarklight) return true;
    return specularPlacementBit(placement, identityRecolour);
}

bool documentAsksForSpecular(const GlassMaterial& material) { return material.hasSpecular; }

bool documentAsksForSpecular(const DenormalisedGlass& glass) { return glass.hasSpecular; }

const char* specularDoesNotDrawNote() {
    // ARTE RASTER SAIU DESTA FRASE em 2026-09-15. Ela dizia que um raster nao
    // tem contorno e por isso nao tem campo; `[BIN]` o alvo tambem constroi o
    // campo dele a partir do alfa rasterizado (os enderecos estao em
    // DistanceField.h, PARTE TRES), e `generateFieldFromAlpha` faz o mesmo aqui.
    // O que sobra e o unico jeito que ainda resta de nao haver campo: uma arte
    // cujo contorno nao da para assinar.
    return "este documento pede um especular e ele NAO desenha NESTA CAMADA: nao ha campo de "
           "distancia para esta arte -- ou ela mistura non-zero e even-odd (uma regra so "
           "inverteria parte da forma), ou nao fecha contorno pintado nenhum, ou o alfa dela "
           "nunca chega a 0.5. Sem `sd` e sem normal o shader `glassHighlight` nao tem o que "
           "amostrar. Os numeros existem (Highlights, 16113 bytes, lidos em "
           "Docs/Laudos/2026-09-15-highlights.md); a nota de cada camada diz qual dos tres casos e";
}

const char* specularDrawnNote() {
    return "especular desenhado: `[BIN]` CINCO realces do conjunto `glyphs*` de "
           "ICRRenderingParameters.Highlights (0x2008, 0x629 bytes, identico aos outros quatro "
           "conjuntos de glifo nesta versao) -- keySharp (distance 4/6 pt, cone pi/2, bias 0.5), "
           "keyDiffuse (16/24 pt, pi/3, 0.08), fillSharp a pi de distancia angular, e o `dark` "
           "duas vezes a +-pi/2 com brightness 0 e portanto plusDarker. As tres identidades que "
           "este bloco tomava por fe foram MEDIDAS e as tres sao identidades de verdade: `[BIN]` "
           "ctx[0] (0x4C010) e GlobalConfiguration.lightIntensity, que vale 1.0; `[BIN]` a "
           "latitude phi da luz e 0 porque customLightDirection e nil (a tag ctx+0x20 de 0x4BE74) "
           "e, mesmo que nao fosse, 0x12550 reescreve a direcao como (sin theta, cos theta, 0) e "
           "apaga a latitude; `[BIN]` a pos-passagem espacial 0x12550 e ICRRenderingParameters."
           "spatialHighlighting e, com phi = 0, o fator dela t = max(0, 1 - hypot(dir.xy)/"
           "sin(alignmentRange)) e exatamente 0 para QUALQUER valor dos seis parametros, entao as "
           "quatro reescritas colapsam. `[BIN]` E o grampo de plusLighter, que parecia ser a causa "
           "barata do excesso, foi MEDIDO e NAO e daqui: "
           "ICRRenderingParameters.shouldClampPlusLBlending (params+0x220) e TRUE (0x5EAE8, "
           "com w22 = 1 de 0x5E8BC, imediatamente antes do 266.24 de defaultChicletCornerRadius "
           "em +0x228), e onde ele vale o alvo troca o composite por um setBlendShader: cujo nome "
           "-- soletrado por mov/movk em 0xD88C/0xD89C, dentro do corpo de swift_once 0xD848 que "
           "monta o global de 0xCE8D0 -- e `clampedPlusL`, um entry point Metal deste proprio "
           "bundle (metallib-iconrendering/default_mod8.ll:37): "
           "max(dest, (min(1, source+dest).rgb, saturate(source.a+dest.a))), com `source` e `dest` "
           "NOMEADOS pelo metadado AIR. So que os quatro sitios de troca (0x44654, 0x44908, "
           "0x4B57C, 0x4B7F4) vivem todos dentro de 0x435A0 e 0x4B4EC, e o desenho do especular "
           "do glifo (0x491C0-0x49DBC) nao chama nenhuma das duas: ele entrega o blendMode direto "
           "ao drawShape:fill:alpha:blendMode: de 0x0000ED00. `[OBS]` Quais desenhos o grampo "
           "cobre nao foi lido, entao ele esta transcrito em BlendFormula.h e DESLIGADO aqui -- "
           "liga-lo sem saber move pixel por conta de quem mede, nao do alvo. "
           "`[BIN]` A COR E PINTADA COMO O ALVO PINTA: o realce NAO soma branco, ele FILTRA o "
           "que esta embaixo. Por realce o alvo faz save/beginLayer (0x4977C/0x49784), desenha o "
           "glassHighlight dentro da camada, fecha com clipLayerWithAlpha:1.0 mode:0 (0x497BC), "
           "instala addColorMatrixFilterWithArray:flags:0 (0x49C48) e entao beginLayerWithFlags:1 + "
           "drawLayerWithAlpha:1.0 blendMode:0 (0x49C54/0x49C64). A matriz e a CAColorMatrix 4x5 "
           "BT.709 RGB->YCbCr de 0xE2960 (swift_once 0x6948, __const 0x938E0), os niveis "
           "Y <- (VCM[1]-VCM[0])*Y + VCM[0] (0x49A64), a croma Cb,Cr <- VCM[2]*c + (0.5-0.5*VCM[2]) "
           "(0x49B18) e a inversa de 0xE2910 (0x6988, 0x93920), com glyphHighlightVCM = "
           "[0.2, 1.2, 1.25, 0.0, true] (Highlights+0xB0) nos tres claros e glyphDarklightVCM = "
           "[-0.15, 0.7, 1.25, 0.0] (+0xD8) nos dois escuros; o ColorClamp do addStyle:9 e PULADO "
           "porque VCM[4] == 1. `[BIN]` E a matriz le o FUNDO: beginLayerWithFlags: (RenderBox "
           "0x3BCA0) passa o 1 intacto a Builder::begin_layer (0xC9A28), o Layer guarda-o em +0x44 "
           "(0x14DB74), e Builder::null_style_draw testa o bit 0 em 0xCE02C e, ligado, chama "
           "make_backdrop_item pela vtable +0x70 (0xCE174; entrada 16 de 0x18B658 = 0x7E850) ou "
           "aloca um BackdropFilterItem<Filter::ColorMatrix> (0xCE1A4, vtable 0x18B0A0 -> 0x7D798) "
           "e o anexa a camada PAI. Aqui: lerp(fundo, VCM(fundo), cobertura) sobre o composto, com a "
           "cobertura do glassHighlight. `[OBS]` Na franja com alfa < 1, se o item de fundo substitui "
           "ou compoe por cima nao foi lido; e dentro de um grupo com blend proprio o fundo e so o do "
           "grupo";
}

}  // namespace rb
