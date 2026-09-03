#pragma once
// The engine's blend modes, and the two-step translation that takes one to the
// case number the RenderBox shader switches on.
//
// TWO ENUMS, NOT ONE -- AND THIS FILE IS THE SECOND
// -------------------------------------------------
// `[BIN]` The FORMAT has ten cases: `IconComposition.BlendMode` in
// `IconComposerFoundation` (`fieldmd` 0x12C790). That is `icf::BlendMode` in
// this project, and doc 01 §6 records it -- seven of the modes the previous
// effort implemented are not reachable from a `.icon` because the format cannot
// spell them.
//
// `[BIN]` The ENGINE has eighteen: `IconRendering.Icon.BlendMode` (doc 03
// §17.3), and its tag is what indexes the translation table below. **This enum
// is that one**, because the tables are indexed by it and renumbering them to
// the format's ten would break the one property that makes them checkable.
//
// The two orders are different, so the bridge between them is by NAME. Only
// the ten the format has are given a key here. The other eight have no
// `icon.json` spelling in evidence, and inventing a kebab-case one would be a
// guess in the one place a guess is indistinguishable from a reading.
//
// WHY THE TRANSLATION IS TWO STEPS
// --------------------------------
// This project looked for a table mapping the engine's 18 onto the shader's 56
// and could not find one, because **it does not exist**. `[BIN]` An exhaustive
// literal byte search for the composed sequence
// `[2,27,25,30,44,28,12,29,43,26,31,32,33,14,36,37,38,39]` -- widths 1, 2, 4
// and 8 little-endian, over all seven slices -- returns zero occurrences. The
// method is purely literal, with no decoder in the path, so the negative is
// real rather than a truncated sweep.
//
// The reason is that **the bridge speaks CoreGraphics.** `Icon.BlendMode` is
// translated to `CGBlendMode` first, and the RenderBox translates *that* to its
// own case. Two tables in two binaries, neither aware of the other's numbering.
//
// WHAT MAKES THIS CHECKABLE FROM OUTSIDE THE BINARY
// --------------------------------------------------
// `[BIN]` The first table, `IconRendering 0x94AC0` (identical copy at
// `0x978F4`), is 18 x uint32, read by `ldr w2, [x8, x23, lsl #2]` at `0x25578`
// and handed straight to `setBlendMode:`. Its values are
//
//     0, 4, 1, 7, 26, 5, 2, 6, 27, 3, 8, 9, 10, 11, 12, 13, 14, 15
//
// and **those are the public `CGBlendMode` constants** -- normal 0, multiply 1,
// screen 2, overlay 3, darken 4, lighten 5, colorDodge 6, colorBurn 7,
// softLight 8, hardLight 9, difference 10, exclusion 11, hue 12, saturation 13,
// color 14, luminosity 15, plusDarker 26, plusLighter 27.
//
// Eighteen positions agreeing with a PUBLISHED Apple header is a check from
// outside this binary, against a numbering nobody here chose. It is a different
// kind of evidence from matching a formula to a shader block, and it is why the
// two modes formula-matching left ambiguous are ambiguous no longer:
// **`PlusLighter` is case 43 and `PlusDarker` is case 44.**
//
// `[BIN]` The second table, `RenderBox 0x15ED18` (`cg_table`, 28 x uint32),
// carries the same property: all 28 entries agree with the CoreGraphics order
// when read against `RB::blend_name` -- CG 16 `clear` lands on case 1 `clear`,
// CG 17 `copy` on case 0 `copy`, CG 25 `xor` on case 10 `exclusive_or`. Two
// tables found independently, agreeing through a third source that is not a
// binary at all.
//
// `[BIN]` And `RB::blend_name` (`0x110E2C`, pointer table at `0x18E718`) names
// all 56 cases, which retires the "17 blocks with no match" of doc 03 §26.5:
// they have labels now even where the formula was not transcribed.
//
// WHAT IS HERE AND WHAT IS NOT
// ----------------------------
// Vocabulary and translation only: enums, tables, names. No blend arithmetic,
// and nothing wired into the renderer. Those are separate tasks, and doing them
// here would bury a table anyone can check under a compositor nobody can.
#include <cstdint>
#include <optional>
#include <string_view>

#include "Source/IconComposerFoundation/Values.h"

namespace rb {

// `[BIN]` `Icon.BlendMode.CodingKeys`, in declaration order -- which for a
// Swift enum IS the case order. Doc 03 §17.3.
enum class BlendMode : std::uint8_t {
    Normal = 0,
    Darken = 1,
    Multiply = 2,
    ColorBurn = 3,
    PlusDarker = 4,
    Lighten = 5,
    Screen = 6,
    ColorDodge = 7,
    PlusLighter = 8,
    Overlay = 9,
    SoftLight = 10,
    HardLight = 11,
    Difference = 12,
    Exclusion = 13,
    Hue = 14,
    Saturation = 15,
    Color = 16,
    Luminosity = 17,
};

inline constexpr int kBlendModeCount = 18;
inline constexpr int kRenderBoxBlendCaseCount = 56;

// The document's ten, lifted to the engine's eighteen by name. Total: every
// case of the format's enum has an engine counterpart, which is what makes the
// document path complete without the other eight ever being spelled.
BlendMode blendModeOf(icf::BlendMode formatMode);

// The `icon.json` spelling, for the ten the format can name. **Empty for the
// other eight**, and that emptiness is the measurement: no evidence of a
// spelling was found, so none is invented.
std::string_view blendModeKey(BlendMode mode);

// The reverse. `std::nullopt` for anything the format cannot spell -- an
// unknown blend is a gap to report, never a `normal` to fall back to.
std::optional<BlendMode> blendModeFromKey(std::string_view key);

// `[BIN]` First hop: `IconRendering 0x94AC0`. The value is a `CGBlendMode`, and
// this project deliberately does not redeclare that enum -- the number IS the
// contract with the second table, and a local name for it would only invite
// someone to renumber it.
std::uint32_t cgBlendModeOf(BlendMode mode);

// `[BIN]` Second hop: `RenderBox 0x15ED18`, indexed by the `CGBlendMode`.
// `std::nullopt` above 27, where the table ends -- `rb_blend_mode` at `0x8B4D4`
// does not read past it either.
std::optional<std::uint32_t> renderBoxCaseOfCg(std::uint32_t cgMode);

// The composition, which is what a caller actually wants. Total over the 18.
std::uint32_t renderBoxCaseOf(BlendMode mode);

// `[BIN]` `RB::blend_name`, `RenderBox 0x110E2C`, over the pointer table at
// `0x18E718`. Empty for a case the table does not hold: it is guarded by
// `cmp w0, #0x37` (55), while the shader's own switch lists a case 56 with no
// entry here. That discrepancy is `[OBS]` and is not papered over with a
// made-up label.
std::string_view renderBoxCaseName(std::uint32_t rbCase);

}  // namespace rb
