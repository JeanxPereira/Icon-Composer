#pragma once
// WHICH PARAMETER SET THE ICON IS DRAWN WITH.
//
// `[BIN]` `IconRendering.ICRDesignGeneration`, two cases, raw `1` and `2`. The
// renderer keeps ONE parameter block, `ICRRenderingParameters.default` (built by
// `0x5E844`), and that block is generation 27. Generation 26 is not a second
// block: `0x76FC0` takes a COPY of the default and rewrites it, and it does so
// exactly when the style's `designGeneration` (`style+0x38`) is `1`.
//
// `[BIN-1]` `ICRDesignGeneration.latest` answers `1` when the main bundle's
// identifier starts with "com.apple.IconComposer" and `2` otherwise -- so
// Apple's own editor draws generation 26, and the bitmaps it ships were drawn
// with it. In that editor the choice is view state (`EffectsRenderMode`), not a
// field of the document, which is why it is an option of the render here
// (`IconRenderOptions::generation`) and never read from `icon.json`.
//
// The whole block for each of the two is `RenderingParameters.h`.
#include <cstdint>

namespace rb {

// The raw values are the target's own.
enum class DesignGeneration : std::uint8_t { G26 = 1, G27 = 2 };

}  // namespace rb
