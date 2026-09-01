#pragma once
// `automatic-gradient`: one colour in, a two-stop ramp out.
//
// WHAT THIS CLOSES
// ----------------
// Doc 01's question 4 -- "what does `automatic-gradient` do with a single
// colour" -- has been open since the format was surveyed. Doc 03 §19.4 read the
// six PARAMETERS from the binary and left the RULE unread, and the spec for this
// work named it as a step that might not close.
//
// It closed. `[BIN]` The derivation is one function in `IconRendering`, and it
// is read end to end (doc 03 §24). This is a transcription of it, not a rule
// invented to fit six numbers.
//
// THE PART THAT WOULD HAVE BEEN GUESSED WRONG
// -------------------------------------------
// The four "lightening" parameters are named for brightness bands, and the
// obvious guess is that the six numbers describe the bands. They do not: the
// BAND BOUNDARIES ARE HARD-CODED IN THE CODE at 0.25, 0.5 and 0.75 -- `fmov d2,
// #0.25` / `#0.50` / `#0.75` as immediates, not loads from the parameter block.
// A ramp built by inventing boundaries from the six numbers would have been
// plausible and wrong.
#include <vector>

#include "Source/IconComposerFoundation/Values.h"

namespace rb {

// `[BIN]` `ICRRenderingParameters.Fills.AutomaticGradient`, in declaration
// order, with the defaults read from the global at 0xcdfd0+0x50 (doc 03 §19.4).
struct AutomaticGradientParameters {
    double basePosition = 0.0;
    double saturationBoost = 0.2;
    double dimLightening = 0.04;
    double midDimLightening = 0.08;
    double midBrightLightening = 0.15;
    double brightLightening = -0.05;
};

// A stop of the ramp this produces: a colour and where it sits.
struct RampStop {
    double rgba[4]{0, 0, 0, 0};
    double location = 0.0;
};

// `[BIN]` Rec.709 luminance -- the coefficients are 0.2126 and 0.7152 from
// `__TEXT.__const` at 0x938a0 and 0.0722 built by a `movk` chain, summing to
// exactly 1.0. Applied to the components AS STORED: the function does no colour
// space conversion of any kind.
//
// `[OBS]` Which means whether those components are linear-light or
// sRGB-encoded is decided upstream, and was not measured. A gradient derived
// from a display-p3 colour therefore inherits the space question of §20.4.
double luminance709(double r, double g, double b);

// The two stops `automatic-gradient` derives, sorted ascending by location --
// the target sorts them, and the order is not incidental: which stop comes
// first flips with the sign of the lightening.
std::vector<RampStop> automaticGradient(const icf::Color& base,
                                        const AutomaticGradientParameters& p =
                                            AutomaticGradientParameters{});

}  // namespace rb
