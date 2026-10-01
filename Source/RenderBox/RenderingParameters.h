#pragma once
// `ICRRenderingParameters`, BOTH GENERATIONS OF IT, IN ONE PLACE.
//
// Source: `References/2.0-125/out/slices/IconRendering.arm64` (VA == file
// offset). Generation 27 is what the aggregate constructor `0x5E844` writes;
// generation 26 is what `0x76FC0` leaves after rewriting a copy of that
// (`DesignGeneration.h`). Every value below was taken from the bytes of the two
// blocks as the two functions leave them, field by field, and each field carries
// the address of the instruction that writes it in `0x76FC0` when generation 26
// differs -- a field with no such address is the SAME in both.
//
// WHY ONE STRUCT
// --------------
// Until 2026-10-01 each module carried the defaults it had read as a global of
// its own -- `kShadow`, `kGlyphTranslucency`, a static table of highlight slots,
// the default member initialisers of `AutomaticGradientParameters` -- and the
// renderer reached for them wherever it needed one. That is one generation, and
// a second one could not be selected without editing five files. The module
// structs are unchanged and are MEMBERS here; what changed is who hands them
// out. `renderIconOn` asks `renderingParameters(options.generation)` once and
// passes what each step needs.
//
// WHAT IS HERE AND NOT CONSUMED YET
// ---------------------------------
// The block is whole so that a reader finds every generation-dependent number
// in it, and so that the front that wires a consumer does not have to re-read a
// value. Carried and not consumed by the renderer on the day this file was
// written, and marked where they stand: the NUMBERS of `clearMode` (its
// presence is read) and `outlineUsesDynamicOpacity`; `shouldClampPlusLBlending`
// is wired behind a gate of the target's that is not read.
#include <cstdint>
#include <optional>
#include <vector>

#include "Source/RenderBox/AutomaticGradient.h"
#include "Source/RenderBox/DesignGeneration.h"
#include "Source/RenderBox/GlassMaterial.h"
#include "Source/RenderBox/GlassShadow.h"
#include "Source/RenderBox/GlassSpecular.h"
#include "Source/RenderBox/GlassTranslucency.h"
#include "Source/RenderBox/SystemFill.h"

namespace rb {

// `[BIN]` `ICRRenderingParameters.thresholds`, three `Double`s at `+0x230`,
// `+0x238` and `+0x240`. Only the last one differs between the generations
// (`0x77098`): 256 in 27, 128 in 26.
struct SizeClassThresholds {
    double minMediumSize = 25.0;
    double minLargeSize = 60.0;
    double minDisplaySize = 256.0;
};

// `[BIN]` The ladder of `0x42D54`-`0x42DE4` (and again at `0x18D70` and
// `0x1C790`), over `m = min(width, height)` of the icon's rect in POINTS:
//
//     0x42D58  fcmp d15, d14 ; fcsel d0, d15, d14, mi      m = min(w, h)
//     0x42D60  fcmp d0, [minMediumSize]  ; b.pl             m <  25  -> 0 small
//     0x42D74  fcmp d0, [minLargeSize]   ; b.pl             m <  60  -> 1 medium
//     0x42DD4  fcmp d0, [minDisplaySize] ; csel 2, 3, mi    m < min  -> 2 large
//                                                           else     -> 3 display
//
// Every comparison is `b.pl`/`mi`, so a side EQUAL to a threshold belongs to the
// class above it.
IconSizeClass sizeClassFor(double minSidePoints, const SizeClassThresholds& thresholds);

// `[INF]` THE SIDE THIS RENDERER CLASSIFIES WHEN NOBODY SAYS OTHERWISE. The
// rect the target measures is the icon's own on screen, in points, and a render
// asked for by pixel size has no such rect: `IconRenderOptions::sizeClass`
// stays the caller's to give. What is used when the caller gives none is the
// one rect this project has a reference picture for -- the 206 pt chiclet of
// the 256 pt icon the oracle renders at 512 px (`GlassSpecular.h`). Under
// generation 27's ladder that is `large`, the default this renderer has always
// had; under generation 26's it is `display`.
inline constexpr double kReferenceIconSidePoints = 206.0;

// `[BIN]` `ICRRenderingParameters.Highlights.ChicletHighlightsAppearanceMode`
// (`fieldmd 0xA4FB0`), the byte at `Highlights+0x110` the chiclet selector
// `0x62588` tests first. 27 writes `1`; 26 writes `0` (`0x77298`).
enum class ChicletHighlightsAppearanceMode : std::uint8_t {
    SystemAppearance = 0,
    ChicletLuminance = 1,
};

// `[BIN]` `ICRRenderingParameters.Highlights` (`params+0x250`, a box of `0x3EF1`
// bytes): the preamble and the ten sets. `GlassSpecular.h` carries the layout.
struct HighlightParameters {
    // `+0x00` and `+0x08`. Two lights, and they are separate fields: the glyph
    // resolver is handed `defaultGlyphLight` (`ctx+0x5F8`, `0x4930C`) and the
    // chiclet one `defaultChicletLight` (`ctx+0x5F0`, `0x4761C`). Both `0` in
    // 27; both `-pi/4` in 26 (`0x771B8`, `0x771E0`).
    double defaultChicletLightLongitude = 0.0;
    double defaultGlyphLightLongitude = 0.0;

    // `+0x10`, `+0x30`, `+0x50`, `+0x70`: the curvature the expander attaches
    // to the sharp and the rim slots (`highlight`) and to the two dark ones
    // (`darklight`), per family. All four `[0.75 x4]` in 27. In 26 the glyph
    // highlight one is `[0.8, 0, 0, 0]` (`0x77214`) -- curved on `display` only
    // -- and the two chiclet ones are `[1 x4]` (`0x77244`, `0x77270`).
    HighlightSizeValue glyphHighlightCurvature;
    HighlightSizeValue glyphDarklightCurvature;
    HighlightSizeValue chicletHighlightCurvature;
    HighlightSizeValue chicletDarklightCurvature;

    // `+0x90`. `true` in 27, `false` in 26 (`0x77820`).
    bool glyphHighlightsUseVCM = true;

    // `+0x98`, `+0xA0`, `+0xA8`: the three inputs of `iconBrightness`
    // (`classifyChicletAppearance`). The flag is `false` in 27 and `true` in 26
    // (`0x77848`).
    double maxDimChicletLuminance = 0.2;
    double minBrightChicletLuminance = 0.99;
    bool iconBrightnessOnlyUsesMax = false;

    // `+0xB0` and `+0xD8`, the same in both.
    GlyphVCM glyphHighlightVCM;
    GlyphVCM glyphDarklightVCM;

    // `+0x100` and `+0x108`, `1.0` in both. Read only by the Clear paint
    // branch of `0x491C0` (`0x49554`, `0x49A44`).
    double glyphHighlightNonVCMScale = 1.0;
    double glyphDarklightNonVCMScale = 1.0;

    // `+0x110`.
    ChicletHighlightsAppearanceMode chicletHighlightsAppearanceMode =
        ChicletHighlightsAppearanceMode::ChicletLuminance;

    // The ten sets, each family in the memory order of its five:
    // `default, bright, dim, clear, screened` -- which is `HighlightsSetKind`.
    //   chiclet  +0x0118 +0x0748 +0x0D78 +0x13A8 +0x19D8
    //   glyphs   +0x2008 +0x2638 +0x2C68 +0x3298 +0x38C8
    HighlightsSet chiclet[5];
    HighlightsSet glyphs[5];
};

// `[BIN]` `ICRRenderingParameters.ClearMode` (`params+0x120`, an Optional whose
// tag is the byte at `+0x169`: `2` is nil). Present in 27; nil in 26
// (`0x77064`). ITS PRESENCE IS CONSUMED: a generation without one draws no
// Clear mask (`IconRenderer.cpp`, `kClearModeNilNote`) and selects the
// `Screened` highlight sets. The NUMBERS are carried and not consumed: the Clear
// rendition is drawn from constants of its own in `IconRenderer.cpp`
// (`applyClear`, `kClearContentLightening`), which are these values.
struct ClearModeParameters {
    double totalLighteningStrength = 1.0;      // +0x120
    double totalDarkeningStrength = 0.3;       // +0x128
    double totalHighlightsStrength = 1.0;      // +0x130
    double contentLighteningStrength = 0.85;   // +0x138
    double contentDarkeningStrength = 0.0;     // +0x140
    // +0x148, +0x170, +0x198: brightness, white point, saturation, headroom;
    // the Optional tag of the headroom follows each (`skipHeadroomClamp` is
    // that tag saying nil).
    GlyphVCM highlightsVCM{0.2, 1.35, 1.4, 1.2, false};
    bool lighteningUsesVCM = true;             // +0x169
    GlyphVCM lighteningVCM{0.9, 2.5, 2.0, 1.2, false};
    bool darkeningUsesVCM = false;             // +0x191
    GlyphVCM darkeningVCM{-0.5, 0.5, 1.5, 0.0, true};
    std::uint8_t passOrder = 0;                // +0x1B9
    bool applyToLightTintToo = true;           // +0x1BA
    bool leaveChicletDarklightsToSystem = true;   // +0x1BB
    bool drawByReference = true;               // +0x1BC
    bool debugMode = false;                    // +0x1BD
};

// `[BIN]` `ICRRenderingParameters.glow: Glow?` (`params+0x290`, tag at
// `+0x2A8`: `1` is nil). Nil in 27; in 26 `0x77E5C`-`0x77E64` write the three.
// The pass is `0x48C20`-`0x48DA8`, after a group's content and before its
// highlights: `GlassGlow.h`.
struct GlowParameters {
    double bias = 0.5;
    double innerRadius = 42.38;    // canvas points
    double innerOpacity = 0.03;
};

struct RenderingParameters {
    DesignGeneration generation = DesignGeneration::G27;

    // `+0x48`. `true` in 27, `false` in 26 (`0x77084`). Whether the chiclet's
    // highlights are drawn inside the tinted-dark layer -- read by generation
    // 27's root body only (`0x483D4`); `IconRenderer.cpp`, the chiclet pass.
    bool darkTintHighlightsBlendWithContent = true;

    // `+0x50`..`+0x78`, `fills.automaticGradient`. 26 rewrites five of the six
    // (`0x770B0`, `0x770BC`).
    AutomaticGradientParameters automaticGradient;
    // `+0x80` and `+0x88`, `fills.systemLightGradient` / `systemDarkGradient`:
    // two boxed stop arrays, replaced whole in 26 (`0x77124`, `0x77190`).
    SystemGradients systemGradients;

    // `+0x120`. Nil in 26; see `ClearModeParameters`.
    std::optional<ClearModeParameters> clearMode;

    // `+0x1D0`, `sdfGeneration.useAdvancedStacking`. `true` in 27, `false` in
    // 26 (`0x77F68`). It picks the blend the upper field of a stacked group is
    // drawn with (`DistanceField.h`, part four).
    bool useAdvancedStacking = true;

    // `+0x1E8`..`+0x228`. 26 zeroes `refractionStrengthMax` (`0x77F64`) and
    // touches nothing else in it. The renderer and the viewport plan
    // denormalise the material with this block.
    GlassRenderingParameters glass;

    // `+0x220`. `true` in 27, `false` in 26 (`0x77080`). Wired to the one draw
    // it reaches -- a plus-lighter group's image -- behind a second gate of the
    // target's that is not read, so it changes nothing unless the caller opens
    // that gate (`BlendFormula.h`,
    // `IconRenderOptions::drawingContextClampsPlusLighter`).
    bool shouldClampPlusLBlending = true;

    // `+0x230`.
    SizeClassThresholds thresholds;

    // `+0x250`.
    HighlightParameters highlights;

    // `+0x290`. Nil in 27.
    std::optional<GlowParameters> glow;

    // `+0x2B0`, the `Shadow` box. 26 rewrites twelve of its fifteen fields
    // (`0x77E80`-`0x77ED0`). Every shadow step of the renderer takes it from
    // here (`GlassShadow.h`).
    ShadowParameters shadow;

    // `+0x2B8`, `glyphTranslucency`. 26 rewrites four fields (`0x77F18`,
    // `0x77F20`, `0x77F3C`, `0x77F40`).
    TranslucencyEffect glyphTranslucency;

    // `_outlines+0x68`. `true` in 27, `false` in 26 (`0x77F54`). CARRIED, NOT
    // CONSUMED: the outline pass (`0x47AE8`, third in the root pass) only runs
    // in the mitigated export, which this renderer does not have --
    // `IconRenderer.cpp` says so where the root order is written.
    bool outlineUsesDynamicOpacity = true;

    // `+0x360`, `+0x361`, `+0x362`: the three trailing `Bool`s, written
    // together (`0x7707C`, `0x77074`). `SystemFill.h` and `SvgRenderer.h` carry
    // what the first two switch; the third picks the root body (`0x43140`) and
    // with it where the tinted-dark layer closes (`IconRenderer.cpp`, the
    // chiclet pass).
    bool supportsChicletAlignmentForSystemFills = true;
    bool recreateRadar153477135 = false;
    bool useOS26Compositing = false;
};

// The block of one generation. Built once, on first use.
const RenderingParameters& renderingParameters(DesignGeneration generation);

// `[BIN]` `0x00030E88` over one of the ten sets of a generation, with the two
// curvatures of its family: what the selector hands the resolver. The empty
// slots are already dropped (`0x00031338`-`0x000313EC`), so the size of the
// answer is the number of highlights that set draws.
enum class HighlightFamily : std::uint8_t { Chiclet = 0, Glyph = 1 };
const std::vector<HighlightSlot>& expandedHighlights(DesignGeneration generation,
                                                     HighlightFamily family,
                                                     HighlightsSetKind kind);

}  // namespace rb
