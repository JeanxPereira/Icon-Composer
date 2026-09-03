#include "Source/RenderBox/BlendMode.h"

namespace rb {
namespace {

// `[BIN]` `IconRendering 0x94AC0`, 18 x uint32, indexed by the engine tag.
// These are `CGBlendMode` values; see the header for why that is checkable.
constexpr std::uint32_t kToCg[kBlendModeCount] = {
    0,   // normal      -> CG normal
    4,   // darken      -> CG darken
    1,   // multiply    -> CG multiply
    7,   // colorBurn   -> CG colorBurn
    26,  // plusDarker  -> CG plusDarker
    5,   // lighten     -> CG lighten
    2,   // screen      -> CG screen
    6,   // colorDodge  -> CG colorDodge
    27,  // plusLighter -> CG plusLighter
    3,   // overlay     -> CG overlay
    8,   // softLight   -> CG softLight
    9,   // hardLight   -> CG hardLight
    10,  // difference  -> CG difference
    11,  // exclusion   -> CG exclusion
    12,  // hue         -> CG hue
    13,  // saturation  -> CG saturation
    14,  // color       -> CG color
    15,  // luminosity  -> CG luminosity
};

// `[BIN]` `RenderBox 0x15ED18` (`cg_table`), 28 x uint32, indexed by
// `CGBlendMode`. The comments are `RB::blend_name` of the value, and every one
// of the 28 agrees with what CoreGraphics calls that index -- including the
// twelve the format never reaches, which is why they are transcribed here
// rather than trimmed to the eighteen: a table that only holds the reachable
// half cannot be checked against the whole.
constexpr int kCgTableCount = 28;
constexpr std::uint32_t kCgToCase[kCgTableCount] = {
    2,   // CG  0 normal          -> source_over
    25,  // CG  1 multiply        -> multiply
    12,  // CG  2 screen          -> screen
    26,  // CG  3 overlay         -> overlay
    27,  // CG  4 darken          -> darken
    28,  // CG  5 lighten         -> lighten
    29,  // CG  6 colorDodge      -> color_dodge
    30,  // CG  7 colorBurn       -> color_burn
    31,  // CG  8 softLight       -> soft_light
    32,  // CG  9 hardLight       -> hard_light
    33,  // CG 10 difference      -> difference
    14,  // CG 11 exclusion       -> exclusion
    36,  // CG 12 hue             -> hue
    37,  // CG 13 saturation      -> saturation
    38,  // CG 14 color           -> color
    39,  // CG 15 luminosity      -> luminosity
    1,   // CG 16 clear           -> clear
    0,   // CG 17 copy            -> copy
    3,   // CG 18 sourceIn        -> source_in
    4,   // CG 19 sourceOut       -> source_out
    5,   // CG 20 sourceAtop      -> source_atop
    6,   // CG 21 destinationOver -> dest_over
    7,   // CG 22 destinationIn   -> dest_in
    8,   // CG 23 destinationOut  -> dest_out
    9,   // CG 24 destinationAtop -> dest_atop
    10,  // CG 25 xor             -> exclusive_or
    44,  // CG 26 plusDarker      -> plus_darker
    43,  // CG 27 plusLighter     -> plus_lighter
};

// `[BIN]` `RB::blend_name`, over the pointer table at `RenderBox 0x18E718`.
// Fifty-six entries, and the numbering IS the case value of the shader switch:
// case 0 is `copy`, which is also the switch's default.
constexpr std::string_view kCaseNames[kRenderBoxBlendCaseCount] = {
    "copy", "clear", "source_over", "source_in", "source_out", "source_atop",
    "dest_over", "dest_in", "dest_out", "dest_atop", "exclusive_or",
    "additive", "screen", "linear_dodge", "exclusion", "maximum", "minimum",
    "subtract_s", "subtract_d", "clip_copy", "clip_intersect",
    "clip_copy_inverse", "clip_intersect_inverse", "accum_copy",
    "pass_through", "multiply", "overlay", "darken", "lighten", "color_dodge",
    "color_burn", "soft_light", "hard_light", "difference", "subtract",
    "divide", "hue", "saturation", "color", "luminosity", "linear_burn",
    "linear_light", "pin_light", "plus_lighter", "plus_darker",
    "darken_source", "lighten_source", "minimum_inverse",
    "plus_lighter_ignore_alpha", "plus_darker_ignore_alpha",
    "subtract_s_ignore_alpha", "sdf_maximum", "sdf_minimum",
    "sdf_minimum_inverse", "custom_normal", "custom_complex",
};

// The ten spellings the format has, against the engine case each one names.
// `[ART]` All ten occur in the 145 documents, and no eleventh spelling does --
// doc 01 §6 measured zero values outside this set.
struct KeyRow {
    BlendMode mode;
    std::string_view key;
};
constexpr KeyRow kKeys[] = {
    {BlendMode::Normal, "normal"},
    {BlendMode::Darken, "darken"},
    {BlendMode::Multiply, "multiply"},
    {BlendMode::PlusDarker, "plus-darker"},
    {BlendMode::Lighten, "lighten"},
    {BlendMode::Screen, "screen"},
    {BlendMode::PlusLighter, "plus-lighter"},
    {BlendMode::Overlay, "overlay"},
    {BlendMode::SoftLight, "soft-light"},
    {BlendMode::HardLight, "hard-light"},
};

}  // namespace

BlendMode blendModeOf(icf::BlendMode formatMode) {
    // By NAME, because the two enums do not share an order: the format lists
    // `plusLighter` second and the engine lists it ninth. Mapping by tag would
    // compile, run, and be wrong -- which is the whole reason this function
    // exists instead of a cast.
    switch (formatMode) {
        case icf::BlendMode::Normal:      return BlendMode::Normal;
        case icf::BlendMode::PlusLighter: return BlendMode::PlusLighter;
        case icf::BlendMode::PlusDarker:  return BlendMode::PlusDarker;
        case icf::BlendMode::Overlay:     return BlendMode::Overlay;
        case icf::BlendMode::Multiply:    return BlendMode::Multiply;
        case icf::BlendMode::SoftLight:   return BlendMode::SoftLight;
        case icf::BlendMode::HardLight:   return BlendMode::HardLight;
        case icf::BlendMode::Darken:      return BlendMode::Darken;
        case icf::BlendMode::Lighten:     return BlendMode::Lighten;
        case icf::BlendMode::Screen:      return BlendMode::Screen;
    }
    return BlendMode::Normal;
}

std::string_view blendModeKey(BlendMode mode) {
    for (const KeyRow& r : kKeys) {
        if (r.mode == mode) return r.key;
    }
    return {};
}

std::optional<BlendMode> blendModeFromKey(std::string_view key) {
    for (const KeyRow& r : kKeys) {
        if (r.key == key) return r.mode;
    }
    return std::nullopt;
}

std::uint32_t cgBlendModeOf(BlendMode mode) {
    return kToCg[static_cast<int>(mode)];
}

std::optional<std::uint32_t> renderBoxCaseOfCg(std::uint32_t cgMode) {
    if (cgMode >= static_cast<std::uint32_t>(kCgTableCount)) return std::nullopt;
    return kCgToCase[cgMode];
}

std::uint32_t renderBoxCaseOf(BlendMode mode) {
    // Total by construction: every one of the 18 `CGBlendMode` values in
    // `kToCg` is below 28, so the second lookup cannot miss. The test pins that
    // rather than leaving it as a remark.
    return kCgToCase[cgBlendModeOf(mode)];
}

std::string_view renderBoxCaseName(std::uint32_t rbCase) {
    if (rbCase >= static_cast<std::uint32_t>(kRenderBoxBlendCaseCount)) return {};
    return kCaseNames[rbCase];
}

}  // namespace rb
