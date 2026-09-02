// `sdf_glass_displacement`: the GPU against the oracle, and the oracle against
// the closed forms.
//
// The shader this runs is `SdfDisplacement.glsl` -- the SAME file the renderer
// will include -- so what draws and what is verified cannot drift apart.
//
// WHAT THIS GATE HAS TO CATCH, beyond "the two sides agree". This stage is small
// and almost every way of getting it wrong still produces a picture:
//
//   * `0.2929` is not `1 - sqrt(2)/2`. The two differ at the fifth decimal, so a
//     "correction" to the closed form draws a plausible glass and the wrong one.
//     The gate MEASURES the gap and asserts it as a band, so both the value and
//     the tidied-up version fail if they are swapped.
//   * The `-5.0` cutoff zeroes the ALPHA and nothing else. A version that also
//     zeroed `disp.xy` agrees everywhere the mask is consumed. So the band is
//     crossed from both sides and the displacement is required to survive it.
//   * The profile switch at `x < 1` is a branch, and an untested branch is not a
//     guard: both sides of it are exercised explicitly, and the sweep COUNTS how
//     many samples took each so a spread that quietly stopped covering one shows
//     up as a failure rather than as silence.
//   * The rotation multiplies the GRADIENT. AquaKit's scar
//     (`docs/re/2026-08-28-aberracao-a-matriz-nao-e-o-displacement.md`) is an
//     earlier version of that matrix eating the displacement instead, which left
//     a row of `(alpha, 1.0)` and an effect that stopped varying with the
//     geometry. Here the direction is pinned against the closed form at four
//     angles, its length is required to track `|g|`, and it is required NOT to
//     depend on any of the four params.
#include "check.h"
#include "Source/RenderBox/Compute.h"
#include "Source/RenderBox/PathVertexOracle.h"
#include "Source/RenderBox/SdfDisplacement.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

static const std::uint32_t kSdfDisplacementSpirv[] =
#include "sdf_displacement_probe.comp.inc"
    ;

using namespace rb;
namespace sd = rb::sdfdisp;

namespace {

Device& gpu() {
    static Device* d = [] {
        auto made = Device::create();
        if (!made) {
            std::printf("  FAIL no Vulkan device: %s\n", made.error().c_str());
            ++ictest::failures();
            return static_cast<Device*>(nullptr);
        }
        return new Device(std::move(*made));
    }();
    static Device dead;
    return d ? *d : dead;
}

// Mirrors the push-constant block in `sdf_displacement_probe.comp`.
struct Control {
    std::uint32_t count = 0;
    std::uint32_t stage = 0;
    std::uint32_t stride = 11;
    std::uint32_t probeBase = 0;
};

constexpr std::uint32_t kStageWhole = 0;
constexpr std::uint32_t kStageGeometry = 1;
constexpr std::uint32_t kStageProfile = 2;
constexpr std::uint32_t kStageDirection = 3;
constexpr std::uint32_t kStageMask = 4;

// One record: the three arguments of the target's function, plus the screen
// derivative the rasteriser would have produced. `fwidthT` is data here for the
// same reason `dpdx` is data in `test_rb_displacement.cpp` -- a compute dispatch
// has no fragment quad -- and making it data is also what lets a test drive it
// below the `1e-4` floor on purpose.
struct Probe {
    sd::Field field{};
    sd::Params params{};
    sd::Rot rot{};
    float fwidthT = 1.0f;
};

std::vector<float> onGpu(Device& d, const std::vector<Probe>& probes, std::uint32_t stage) {
    if (probes.empty()) return {};

    Control c;
    c.count = static_cast<std::uint32_t>(probes.size());
    c.stage = stage;

    std::vector<float> data;
    data.reserve(probes.size() * c.stride);
    for (const Probe& p : probes) {
        data.push_back(p.field.d);
        data.push_back(p.field.gx);
        data.push_back(p.field.gy);
        data.push_back(p.field.coverage);
        data.push_back(p.params.height);
        data.push_back(p.params.curvature);
        data.push_back(p.params.offset);
        data.push_back(p.params.maskOffset);
        data.push_back(p.rot.cos);
        data.push_back(p.rot.sin);
        data.push_back(p.fwidthT);
    }

    auto input = Buffer::create(d, data.size() * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!input) return {};
    std::memcpy(input->mapped(), data.data(), data.size() * sizeof(float));

    const std::size_t outBytes = probes.size() * 4 * sizeof(float);
    auto out = Buffer::create(d, outBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!out) return {};
    std::memset(out->mapped(), 0, outBytes);

    auto pass = ComputePass::create(d, kSdfDisplacementSpirv, sizeof kSdfDisplacementSpirv,
                                    sizeof(Control));
    if (!pass) {
        std::printf("  FAIL compute pass: %s\n", pass.error().c_str());
        ++ictest::failures();
        return {};
    }
    auto ran = pass->run(d, *input, *out, &c, sizeof c, c.count);
    if (!ran) {
        std::printf("  FAIL dispatch: %s\n", ran.error().c_str());
        ++ictest::failures();
        return {};
    }
    std::vector<float> raw(probes.size() * 4, 0.0f);
    std::memcpy(raw.data(), out->mapped(), raw.size() * sizeof(float));
    return raw;
}

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

// WHERE BIT-FOR-BIT IS DEMANDED, AND WHERE IT IS NOT.
//
// The depth, the mask distance, the flat profile and the direction are multiplies
// and adds, and `precise` has taken contraction off the table on the GLSL side
// (`GlassOracle.h` records why `fma()` could not be used instead: glslc does not
// decorate `OpExtInst Fma` with `NoContraction`, so the driver decomposes it).
// Those are compared BIT FOR BIT, and an allowance on them would be a place for a
// transcription error to hide.
//
// The mask ramp, the band coordinate and the fade each carry a DIVISION, which
// Vulkan specifies to 2.5 ULP; the circular profile carries a SQUARE ROOT, which
// it specifies to 3 ULP. Neither has a SPIR-V control that makes it exact. So
// those stages are compared with an allowance derived from those two numbers --
// and the worst disagreement actually measured is PRINTED, so that a driver
// parting from the oracle reads as a number moving rather than only as a
// pass/fail flip.
//
// `[ART]` MEASURED on this adapter, and every sweep PRINTS its own number so
// these stay measurements rather than claims: the band coordinate and the two
// profiles come back at 1 ULP, the mask at 2, and the whole stage at 3 with 1239
// of its 1284 components bit-identical. The divergence is real and it is confined
// to the four expressions that divide or take a root; every multiply-and-add in
// the stage agrees to the last bit, which is what makes the bit-exact half of
// this gate worth having.
constexpr std::uint32_t kDivUlps = 3;    // 2.5, rounded up to an integer of ULP
constexpr std::uint32_t kSqrtUlps = 3;

float ulpSizeAt(float magnitude) {
    const float m = std::fabs(magnitude);
    return std::nextafter(m, std::numeric_limits<float>::infinity()) - m;
}

// The worst ULP distance seen in a sweep, tracked so the print is a measurement.
struct Worst {
    std::uint32_t ulps = 0;
    void see(float got, float want) {
        if (sameBits(got, want)) return;
        if (std::isnan(got) || std::isnan(want)) return;
        ulps = std::max(ulps, ulpsApart(got, want));
    }
};

// Not called `near`: `windef.h` reaches this translation unit through Vulkan's
// Win32 platform header and defines `near` as an empty macro.
bool closeEnough(float got, float want, float bound) {
    if (sameBits(got, want)) return true;
    if (std::isnan(got) && std::isnan(want)) return true;
    return std::fabs(got - want) <= bound;
}

// AND WHY THE ALLOWANCE IS SIZED WHERE THE ERROR IS MADE.
//
// The whole stage is `(1 - mag) * dir * fade`. `mag` comes out of a square root
// and `fade` out of a division, both of them values in [0, 1]; the error they can
// carry is therefore ULP of ONE, and it arrives at the result multiplied by
// `|dir|`. Counting ULP at the result instead would reject a correct
// transcription wherever the product is small and the error is not -- the same
// lesson `test_rb_glass.cpp` records for `bandAmount`.
float dispBound(float dirComponent) {
    const float perFactor = static_cast<float>(std::max(kDivUlps, kSqrtUlps)) * ulpSizeAt(1.0f);
    return 2.0f * perFactor * std::fabs(dirComponent);
}

sd::Result oracleAt(const Probe& p) {
    return sd::glassDisplacement(p.field, p.params, p.rot, p.fwidthT);
}

// THE SPREAD, and it is built to hit every branch rather than to look thorough.
//
// `height` includes 0 (no guard in the target) and the decoded default of 20;
// `curvature` takes both endpoints and a middle; the distances put `s/height`
// both below and above 1; `mask-offset` moves `t` across the `-5.0` cutoff from
// both sides; the gradient includes the zero vector; `fwidth` sits both above and
// below the `1e-4` floor; and the rotation includes a NON-UNIT `rot`, because
// nothing read says the caller hands this function a unit vector -- the scar's
// laudo says the matrix is texel-scaled.
//
// `withNaN == false` drops the single input that makes the target produce a NaN
// (`height == 0` at exactly `s == 0`, where `0/0` is the target's own unguarded
// divide). GLSL leaves `clamp` of a NaN undefined, so that one case is pinned on
// the CPU alone -- see `a_zero_height_is_unguarded_in_the_target`.
std::vector<Probe> spread(bool withNaN) {
    std::vector<Probe> out;
    const float heights[] = {20.0f, 8.0f, 1.5f, 0.0f};
    const float curvatures[] = {0.0f, 0.35f, 1.0f};
    const float distances[] = {12.0f, 4.0f, 0.5f, 0.0f, -0.75f, -3.0f, -4.9f, -5.1f, -9.0f};
    const float maskOffsets[] = {0.0f, 2.5f, -1.25f};
    const float gradients[][2] = {{1.0f, 0.0f},   {0.0f, -1.0f}, {0.70710678f, 0.70710678f},
                                  {0.0f, 0.0f},   {-0.3f, 0.9f}, {2.5f, -1.75f}};
    const float rots[][2] = {{1.0f, 0.0f}, {0.0f, 1.0f}, {0.70710678f, 0.70710678f},
                             {-0.6f, 0.8f}, {1.7f, -0.4f}};
    const float coverages[] = {1.0f, 0.4f, 0.0f};
    // THE WIDE ONE IS NOT DECORATION. With a narrow derivative the mask ramp is
    // already zero everywhere the cutoff fires, so the cutoff and the ramp
    // produce the same zero and a differential cannot tell them apart -- the
    // mutation that DELETED the cutoff passed this sweep until 60 was added. It
    // is the only width at which `t < -5` and `ramp > 0` hold at once.
    const float widths[] = {1.0f, 0.25f, 1e-6f, 60.0f};

    int k = 0;
    for (float height : heights) {
        for (float curvature : curvatures) {
            for (float d : distances) {
                for (float maskOffset : maskOffsets) {
                    Probe p;
                    p.field.d = d;
                    p.field.gx = gradients[k % 6][0];
                    p.field.gy = gradients[k % 6][1];
                    p.field.coverage = coverages[k % 3];
                    p.params.height = height;
                    p.params.curvature = curvature;
                    // The offset shifts `s`, so it is varied too: `s` is
                    // `-(d + offset)` and a transcription that dropped the
                    // offset would still pass a sweep that left it at zero.
                    p.params.offset = (k % 4 == 0) ? 0.0f : (k % 4 == 1 ? 1.75f : -2.5f);
                    p.params.maskOffset = maskOffset;
                    p.rot.cos = rots[k % 5][0];
                    p.rot.sin = rots[k % 5][1];
                    // Deliberately NOT `k % 3`, which is the coverage's cycle:
                    // sharing it would tie every zero coverage to the same
                    // derivative and leave a corner of the product unvisited.
                    p.fwidthT = widths[(k / 5) % 4];
                    ++k;
                    const float s = sd::depth(p.field, p.params);
                    if (!withNaN && p.params.height == 0.0f && s == 0.0f) continue;
                    out.push_back(p);
                }
            }
        }
    }
    return out;
}

}  // namespace

// ===========================================================================
// 1. the vocabulary, and the default that does not exist
// ===========================================================================

// `[BIN]` The four scalars are the four fields RenderBox's own XML serialiser
// names at `0xEDB24` -- `height`, `curvature`, `angle`, `mask-offset` -- and the
// first three defaults are QuartzCore's, out of the frozen dictionary at
// `0x1e8970978`, count THREE.
//
// The count is the point of this test. Four properties and three keys is a gap,
// and the gap is carried rather than filled: there is no `kDefaultMaskOffset`
// here, and this case exists so that adding one is a deliberate act with a
// measurement behind it rather than a tidy-up.
TEST_CASE(the_decoded_defaults_are_three_and_the_fourth_is_not_read) {
    CHECK_EQ(sd::kDefaultAngle, 0.0f);
    CHECK_EQ(sd::kDefaultCurvature, 1.0f);
    CHECK_EQ(sd::kDefaultHeight, 20.0f);

    // The angle reaches the shader already decomposed, so the default angle is
    // the identity rotation.
    const sd::Rot fromDefault{std::cos(sd::kDefaultAngle), std::sin(sd::kDefaultAngle)};
    CHECK_EQ(fromDefault.cos, 1.0f);
    CHECK_EQ(fromDefault.sin, 0.0f);

    // `[OBS]` `maskOffset` has no decoded default. The struct's member
    // initialiser is C++'s zero, and it is NOT a measurement -- nothing was read
    // that says the ivar is born at zero.
    const sd::Params fresh{};
    CHECK_EQ(fresh.maskOffset, 0.0f);
    std::printf("  [OBS] mask-offset has no decoded default: the frozen dictionary has 3 keys "
                "for 4 properties\n");
}

// ===========================================================================
// 2. the constant that must not be tidied
// ===========================================================================

// `0.2929` IS NOT `1 - sqrt(2)/2`, AND THIS IS THE TEST THAT SAYS SO.
//
// AquaKit seals the literal as exact and hand-rounded in Apple's source. The
// closed form differs at the fifth digit, which is far above float noise, so the
// two are separable and the difference is asserted as a BAND -- it must be the
// measured gap, not merely "different" and not zero. A transcription that
// derived the constant would fail the upper bound; one that used some other
// rounding would fail the lower.
TEST_CASE(the_flat_profile_is_not_one_minus_root_two_over_two) {
    const float tidy = 1.0f - std::sqrt(2.0f) / 2.0f;
    const float gap = std::fabs(sd::kFlatDrop - tidy);
    std::printf("  [ART] 0.2929 - (1 - sqrt(2)/2) = %.9g, %u ULP apart\n", double(gap),
                ulpsApart(sd::kFlatDrop, tidy));
    CHECK(gap > 6.0e-6f);
    CHECK(gap < 8.0e-6f);
    // Tens of ULP: nothing about this is a rounding difference.
    CHECK(ulpsApart(sd::kFlatDrop, tidy) > 20u);

    // Inside the band the profile is the constant. Outside it, it is exactly 1 --
    // and "exactly" is the word: `0.2929 * 0.0` is a true zero, so the
    // displacement past the band is a true zero too.
    CHECK_EQ(sd::flatProfile(0.0f), 1.0f - sd::kFlatDrop);
    CHECK_EQ(sd::flatProfile(0.999f), 1.0f - sd::kFlatDrop);
    CHECK_EQ(sd::flatProfile(1.0f), 1.0f);
    CHECK(!sameBits(sd::flatProfile(0.5f), 1.0f - tidy));

    // The switch is `x < 1`, so the boundary itself takes the OUTSIDE branch.
    // One ULP below 1 is inside; 1 is not.
    CHECK_EQ(sd::flatProfile(std::nextafterf(1.0f, 0.0f)), 1.0f - sd::kFlatDrop);
    CHECK_EQ(sd::flatProfile(std::nextafterf(1.0f, 2.0f)), 1.0f);
}

// ===========================================================================
// 3. the circular profile, against its closed form
// ===========================================================================

// The quarter circle is analytic, so this half of the transcription can be
// checked without a GPU and without a second implementation: `circ(x)` is
// `sqrt(1 - (1-x)^2)`, i.e. the unit circle's ordinate at abscissa `1-x`.
//
// AND THE ALGEBRA IS PART OF THE TRANSCRIPTION. `sqrt(x * (2 - x))` is the same
// function on paper and a different one in float32 -- AquaKit's OTHER band
// profile (`BandAmount`, the compositor filter's, a different program) is
// genuinely written that second way, which is exactly why this one must not be
// quietly rewritten to match it. The second half of this case measures how far
// apart the two forms actually land.
TEST_CASE(the_circular_profile_is_the_quarter_circle_in_the_targets_algebra) {
    // The endpoints are exact on both sides: at the edge of the band there is no
    // displacement to remove, at the end of it there is none left.
    CHECK_EQ(sd::circularProfile(0.0f), 0.0f);
    CHECK_EQ(sd::circularProfile(1.0f), 1.0f);

    double worst = 0.0, worstAt = 0.0;
    double worstForm = 0.0, worstFormAt = 0.0;
    for (int i = 0; i <= 100000; ++i) {
        const float x = static_cast<float>(i) / 100000.0f;
        const double want = std::sqrt(1.0 - (1.0 - double(x)) * (1.0 - double(x)));
        const double err = std::fabs(double(sd::circularProfile(x)) - want);
        if (err > worst) {
            worst = err;
            worstAt = x;
        }
        const float other = std::sqrt(x * (2.0f - x));
        const double formErr = std::fabs(double(sd::circularProfile(x)) - double(other));
        if (formErr > worstForm) {
            worstForm = formErr;
            worstFormAt = x;
        }
    }
    std::printf("  [ART] |circ(x) - sqrt(1-(1-x)^2) in double| worst %.3e at x=%.5f\n", worst,
                worstAt);
    std::printf("  [ART] the two algebras part by up to %.3e (at x=%.5f) -- the form is not "
                "cosmetic\n",
                worstForm, worstFormAt);
    // `[ART]` MEASURED: 4.3e-6, at x = 2e-5, and it is the SAME number both
    // lines print. That is not two coincidences -- it is one fact seen twice.
    // Near the edge of the band `(1-x)^2` is just under 1 and `1 - (1-x)^2`
    // cancels, so the target's own algebra loses most of its significant bits
    // exactly where the displacement is largest. `sqrt(x * (2 - x))` does not
    // cancel and is the more accurate expression, which is precisely why it is
    // NOT what this file computes: the transcription owes the target its
    // arithmetic, including the part of it that is numerically worse.
    //
    // The bound is a band. Too large means the expression drifted; too small
    // means somebody rewrote it into the stable form and the error vanished.
    CHECK(worst > 1.0e-6);
    CHECK(worst < 1.0e-5);
    CHECK(worstForm > 1.0e-6);
    // Away from the cancellation the two sides agree to the last bits, so the
    // measurement above is a statement about x near 0 and nowhere else.
    for (float x : {0.25f, 0.5f, 0.75f, 1.0f}) {
        const double want = std::sqrt(1.0 - (1.0 - double(x)) * (1.0 - double(x)));
        CHECK(std::fabs(double(sd::circularProfile(x)) - want) < 1e-7);
    }
}

// THE BLEND, AND THE ASYMMETRY THAT IDENTIFIES WHICH `mix` THIS IS.
//
// `air.mix(x, y, a)` is `x + (y - x) * a`. At a = 0 that is EXACTLY `x`. At
// a = 1 it is `y` only up to the rounding of the subtraction and the add back --
// and `[ART]` MEASURED, it misses by up to 4 ULP over this sweep.
//
// That miss is evidence, not noise. GLSL's own `mix` is free to expand as
// `x*(1-a) + y*a`, which at a = 1 lands on `y` EXACTLY, every time. So "the
// blend does not quite reach `circ` at curvature 1" is the fingerprint of the
// target's form, and a transcription that used the builtin would show a clean
// zero here. Both bounds are therefore asserted: no more than the measured 4
// ULP, and not zero either.
TEST_CASE(the_curvature_blend_reaches_both_profiles) {
    std::uint32_t worstAtOne = 0;
    for (int i = 0; i <= 1000; ++i) {
        const float x = static_cast<float>(i) / 1000.0f;
        const float flat = sd::flatProfile(x);
        const float circ = sd::circularProfile(x);
        CHECK_EQ(sd::magnitude(flat, circ, 0.0f), flat);
        worstAtOne = std::max(worstAtOne, ulpsApart(sd::magnitude(flat, circ, 1.0f), circ));
        // Monotone between the two, which is what "blends two surfaces" means.
        const float mid = sd::magnitude(flat, circ, 0.5f);
        CHECK(mid >= std::min(flat, circ) - 1e-7f);
        CHECK(mid <= std::max(flat, circ) + 1e-7f);
    }
    std::printf("  [ART] mix(flat, circ, 1) lands on circ to within %u ULP -- not on it\n",
                worstAtOne);
    CHECK(worstAtOne <= 4u);
    // The lower bound is the discriminator: `x*(1-a) + y*a` would give 0 here.
    CHECK(worstAtOne > 0u);

    // Curvature 0 inside the band is the flat 45-degree bevel: a CONSTANT
    // magnitude of 0.7071 across the whole band, which is the thing that makes
    // the two profiles "two concrete optical surfaces" rather than a vague blend.
    for (float x : {0.05f, 0.5f, 0.95f}) {
        CHECK_EQ(sd::magnitude(sd::flatProfile(x), sd::circularProfile(x), 0.0f),
                 1.0f - sd::kFlatDrop);
    }
}

// ===========================================================================
// 4. the guards -- and an untested guard is not a guard
// ===========================================================================

// THE `-5.0` CUTOFF, CROSSED FROM BOTH SIDES.
//
// It zeroes the ALPHA and leaves `disp.xy` alone. Both halves are asserted: that
// the alpha really does drop to an exact zero one ULP below the boundary, and
// that the displacement does NOT -- it is continuous across it, because nothing
// in the target's arithmetic touches it there.
TEST_CASE(the_band_cutoff_zeroes_the_alpha_and_leaves_the_displacement) {
    sd::Params params;
    params.height = 20.0f;
    params.curvature = 1.0f;
    params.offset = 0.0f;
    params.maskOffset = 0.0f;

    sd::Field field;
    field.gx = 0.6f;
    field.gy = -0.8f;
    field.coverage = 1.0f;

    const sd::Rot rot{1.0f, 0.0f};

    // `t = s = -d`, so `d = 5` puts `t` exactly at the boundary and `d` one ULP
    // larger puts it below. The comparison is `t < -5.0`, so the boundary itself
    // is NOT cut off.
    //
    // The derivative is deliberately WIDE (100). With a narrow one the ramp is
    // already zero at `t = -5` and the two sides of the boundary agree for a
    // reason that has nothing to do with the cutoff -- the test would pass
    // against a transcription that had no cutoff in it at all.
    const float wide = 100.0f;
    const float atBoundary = 5.0f;
    const float belowBoundary = std::nextafterf(5.0f, 10.0f);

    field.d = atBoundary;
    const sd::Result on = sd::glassDisplacement(field, params, rot, wide);
    field.d = belowBoundary;
    const sd::Result off = sd::glassDisplacement(field, params, rot, wide);

    CHECK_EQ(sd::maskDistance(sd::Field{atBoundary, 0, 0, 1}, params), sd::kBandCutoff);
    CHECK(on.alpha > 0.4f);
    CHECK_EQ(off.alpha, 0.0f);
    CHECK(!std::signbit(off.alpha));

    // The displacement is untouched by the cutoff: one ULP of `d` cannot move it,
    // and it is emphatically not zero down here.
    CHECK(std::fabs(off.x) > 0.1f);
    CHECK(std::fabs(on.x - off.x) < 1e-5f);
    CHECK(std::fabs(on.y - off.y) < 1e-5f);

    // Far below the cutoff, with a derivative wide enough that the ramp is still
    // well away from zero -- so the alpha's zero is the cutoff's doing and not
    // the ramp's.
    field.d = 50.0f;
    CHECK(sd::maskRamp(sd::maskDistance(field, params), 1000.0f) > 0.4f);
    const sd::Result deep = sd::glassDisplacement(field, params, rot, 1000.0f);
    CHECK_EQ(deep.alpha, 0.0f);

    // And the cutoff is on `t`, which the mask-offset moves. With
    // `mask-offset = -10` the same `d` is no longer cut off: a transcription that
    // compared `s` instead of `t` agrees on every probe where the offset is zero.
    params.maskOffset = -10.0f;
    field.d = 8.0f;
    const sd::Result shifted = sd::glassDisplacement(field, params, rot, 1.0f);
    CHECK(shifted.alpha > 0.0f);
    params.maskOffset = 0.0f;
    const sd::Result unshifted = sd::glassDisplacement(field, params, rot, 1.0f);
    CHECK_EQ(unshifted.alpha, 0.0f);
}

// The alpha's three inputs produce a zero in three different ways, and the gate
// has to be able to tell them apart -- otherwise "alpha is 0" is not evidence of
// anything.
TEST_CASE(a_zero_alpha_has_three_causes_and_they_are_distinguishable) {
    sd::Params params;
    params.height = 20.0f;

    // 1. the ramp: `t` far above the band's edge, in the negative direction.
    CHECK_EQ(sd::alpha(-1.0f, 1.0f, 1.0f), 0.0f);
    // 2. the coverage: a full ramp times nothing.
    CHECK_EQ(sd::alpha(10.0f, 1.0f, 0.0f), 0.0f);
    CHECK_EQ(sd::alpha(10.0f, 1.0f, 1.0f), 1.0f);
    // 3. the cutoff: a FULL ramp, a full coverage, and still zero.
    CHECK_EQ(sd::maskRamp(-6.0f, 1.0f), 0.0f);
    CHECK_EQ(sd::alpha(-6.0f, 1.0f, 1.0f), 0.0f);
    // The cutoff's fingerprint: make `w` large enough that the ramp is NOT zero
    // at `t = -6`, and the alpha still is.
    CHECK(sd::maskRamp(-6.0f, 100.0f) > 0.4f);
    CHECK_EQ(sd::alpha(-6.0f, 100.0f, 1.0f), 0.0f);
    // ... while just above the cutoff, with the same wide `w`, it is not.
    CHECK(sd::alpha(-4.9f, 100.0f, 1.0f) > 0.4f);
}

// `[BIN]` The `1e-4` floor under the derivative. It is the function's, not the
// caller's, so a caller that hands in a zero derivative -- which is what a flat
// region of a fragment quad produces -- gets a hard edge and not a division by
// zero.
TEST_CASE(the_filter_width_has_a_floor_and_it_is_the_targets) {
    CHECK_EQ(sd::filterWidth(0.0f), sd::kMinFilterWidth);
    CHECK_EQ(sd::filterWidth(1e-9f), sd::kMinFilterWidth);
    CHECK_EQ(sd::filterWidth(sd::kMinFilterWidth), sd::kMinFilterWidth);
    CHECK_EQ(sd::filterWidth(0.5f), 0.5f);
    // A zero derivative must not produce an infinity or a NaN downstream.
    const sd::Result r = sd::glassDisplacement(sd::Field{-1.0f, 1.0f, 0.0f, 1.0f},
                                               sd::Params{20.0f, 1.0f, 0.0f, 0.0f},
                                               sd::Rot{1.0f, 0.0f}, 0.0f);
    CHECK(r.alpha == r.alpha);
    CHECK_EQ(r.alpha, 1.0f);
    CHECK(r.x == r.x);
}

// A ZERO GRADIENT DISPLACES NOWHERE, and the mask does not care.
//
// The two halves of the return come from different inputs -- `disp.xy` from the
// gradient, `alpha` from the distance and the coverage -- and this is the case
// that proves they are not entangled.
TEST_CASE(a_zero_gradient_displaces_nowhere_but_still_masks) {
    sd::Field field;
    field.d = -4.0f;   // inside the band
    field.gx = 0.0f;
    field.gy = 0.0f;
    field.coverage = 1.0f;
    const sd::Params params{20.0f, 1.0f, 0.0f, 0.0f};

    for (float angle : {0.0f, 1.0f, 2.5f}) {
        const sd::Rot rot{std::cos(angle), std::sin(angle)};
        const sd::Result r = sd::glassDisplacement(field, params, rot, 1.0f);
        CHECK_EQ(r.x, 0.0f);
        CHECK_EQ(r.y, 0.0f);
        CHECK_EQ(r.alpha, 1.0f);
        CHECK_EQ(r.one, 1.0f);
    }
}

// A ZERO HEIGHT IS UNGUARDED IN THE TARGET, and this is what that costs.
//
// `[OBS]` `x = saturate(s / height)` divides unconditionally. With `height == 0`
// the quotient is an infinity for any non-zero `s` -- saturating to 1 inside the
// shape (so `mag` is 1 and the displacement is exactly zero) and to 0 outside it.
// At exactly `s == 0` it is `0/0`, a NaN, and the NaN survives the saturate here
// because the clamp is written as two comparisons, both of which a NaN fails.
//
// This case is CPU-only on purpose: GLSL leaves `clamp` of a NaN undefined, so
// gating it against the GPU would measure the driver rather than the
// transcription. It is pinned here so that the absence of a guard is a recorded
// fact rather than an accident nobody looked at.
TEST_CASE(a_zero_height_is_unguarded_in_the_target) {
    const sd::Params zero{0.0f, 1.0f, 0.0f, 0.0f};

    CHECK_EQ(sd::bandCoordinate(3.0f, 0.0f), 1.0f);
    CHECK_EQ(sd::bandCoordinate(-3.0f, 0.0f), 0.0f);
    const float nan = sd::bandCoordinate(0.0f, 0.0f);
    CHECK(nan != nan);
    std::printf("  [OBS] height == 0 at s == 0 is the target's own 0/0: x is a NaN\n");

    // Inside the shape a zero height means no band at all, so no displacement.
    const sd::Result inside =
        sd::glassDisplacement(sd::Field{-4.0f, 1.0f, 0.0f, 1.0f}, zero, sd::Rot{1.0f, 0.0f}, 1.0f);
    CHECK_EQ(inside.x, 0.0f);
    CHECK_EQ(inside.y, 0.0f);
    CHECK_EQ(inside.alpha, 1.0f);

    // And the mask is untouched by it: the height reaches only the profile and
    // the fade, never the alpha.
    const sd::Result masked =
        sd::glassDisplacement(sd::Field{-4.0f, 1.0f, 0.0f, 0.25f}, zero, sd::Rot{1.0f, 0.0f}, 1.0f);
    CHECK_EQ(masked.alpha, 0.25f);
}

// ===========================================================================
// 5. the rotation, and the scar it carries
// ===========================================================================

// THE MATRIX MULTIPLIES THE GRADIENT.
//
// AquaKit's laudo of 2026-08-28 is a record of getting this wrong in a
// neighbouring function: the displacement was fed into the matrix instead of the
// gradient, one row became `(alpha, 1.0)`, and the effect stopped varying with
// the geometry. Three properties separate the right operand from that one, and
// all three are asserted:
//
//   * the closed form at four angles, where `(cos, -sin) / (sin, cos)` is a
//     rotation with a known answer;
//   * the LENGTH: for a unit `rot`, `|dir| == |g|` -- a matrix fed anything but
//     the gradient does not preserve the gradient's length;
//   * INDEPENDENCE: `dir` does not move when height, curvature, offset or
//     mask-offset move. A `dir` built out of the displacement would.
TEST_CASE(the_rotation_turns_the_gradient_and_nothing_else) {
    sd::Field field;
    field.gx = 0.6f;
    field.gy = -0.8f;
    float dir[2];

    // angle 0 -- the identity.
    sd::direction(field, sd::Rot{1.0f, 0.0f}, dir);
    CHECK_EQ(dir[0], 0.6f);
    CHECK_EQ(dir[1], -0.8f);
    // angle +90 -- (gx, gy) -> (-gy, gx).
    sd::direction(field, sd::Rot{0.0f, 1.0f}, dir);
    CHECK_EQ(dir[0], 0.8f);
    CHECK_EQ(dir[1], 0.6f);
    // angle 180.
    sd::direction(field, sd::Rot{-1.0f, 0.0f}, dir);
    CHECK_EQ(dir[0], -0.6f);
    CHECK_EQ(dir[1], 0.8f);
    // angle -90 -- (gx, gy) -> (gy, -gx). This is the one a transposed matrix
    // gets wrong while agreeing at 0 and 180.
    sd::direction(field, sd::Rot{0.0f, -1.0f}, dir);
    CHECK_EQ(dir[0], -0.8f);
    CHECK_EQ(dir[1], -0.6f);

    // The length, over a full turn of a UNIT rot.
    double worstLen = 0.0;
    for (int i = 0; i < 360; ++i) {
        const float a = static_cast<float>(i) * 3.14159265358979f / 180.0f;
        sd::direction(field, sd::Rot{std::cos(a), std::sin(a)}, dir);
        const double len = std::sqrt(double(dir[0]) * dir[0] + double(dir[1]) * dir[1]);
        worstLen = std::max(worstLen, std::fabs(len - 1.0));
    }
    std::printf("  [ART] |dir| over a full turn departs from |g| by at most %.3e\n", worstLen);
    CHECK(worstLen < 1e-6);

    // `[OBS]` A NON-UNIT rot is a similarity, not a rotation: the length scales
    // with it. Nothing read says the caller normalises -- the scar's laudo says
    // the matrix is TEXEL-SCALED -- so the function is required to be agnostic
    // rather than to assume a unit vector.
    sd::direction(field, sd::Rot{2.0f, 0.0f}, dir);
    CHECK_EQ(dir[0], 1.2f);
    CHECK_EQ(dir[1], -1.6f);

    // INDEPENDENCE from the four params, shown through the whole function
    // because that is where a wrong operand would hide: with the same gradient
    // and two very different parameter sets, the four params change the
    // displacement's LENGTH and leave its direction on the same ray. A `dir`
    // built out of the displacement -- the scar's mistake -- would not be
    // collinear with itself across a change of height and curvature.
    const sd::Rot rot{0.28f, 0.96f};
    const sd::Params a{20.0f, 1.0f, 0.0f, 0.0f};
    const sd::Params b{3.0f, 0.0f, 7.5f, -2.25f};
    const sd::Result resA =
        sd::glassDisplacement(sd::Field{-2.0f, field.gx, field.gy, 1.0f}, a, rot, 1.0f);
    const sd::Result resB =
        sd::glassDisplacement(sd::Field{-2.0f, field.gx, field.gy, 1.0f}, b, rot, 1.0f);
    CHECK(std::fabs(resA.x) > 1e-4f);
    CHECK(std::fabs(resB.x) > 1e-4f);
    const float crossed = resA.x * resB.y - resA.y * resB.x;
    CHECK(std::fabs(crossed) < 1e-6f);
}

// ===========================================================================
// 6. the differential
// ===========================================================================

// The spread itself, before anything is compared: an untested guard is not a
// guard, and a guard the sweep never reaches is not tested. So the coverage of
// the two branches is COUNTED and asserted rather than assumed from the shape of
// the loops above.
TEST_CASE(the_spread_reaches_both_sides_of_every_branch) {
    const std::vector<Probe> probes = spread(true);
    int insideBand = 0, outsideBand = 0, cut = 0, uncut = 0, floored = 0, zeroGradient = 0,
        zeroHeight = 0, zeroCoverage = 0, separable = 0;
    for (const Probe& p : probes) {
        const float s = sd::depth(p.field, p.params);
        const float t = sd::maskDistance(p.field, p.params);
        const float x = sd::bandCoordinate(s, p.params.height);
        if (x < 1.0f) ++insideBand; else if (x == x) ++outsideBand;
        if (t < sd::kBandCutoff) ++cut; else ++uncut;
        // A cutoff that fires where the ramp was going to be zero anyway is not
        // being tested by anything -- the two produce the same zero. THIS is the
        // count that says the guard is observable, and it was 0 until the wide
        // derivative went into `widths` above.
        if (t < sd::kBandCutoff && sd::maskRamp(t, sd::filterWidth(p.fwidthT)) > 0.0f &&
            p.field.coverage > 0.0f) {
            ++separable;
        }
        if (p.fwidthT < sd::kMinFilterWidth) ++floored;
        if (p.field.gx == 0.0f && p.field.gy == 0.0f) ++zeroGradient;
        if (p.params.height == 0.0f) ++zeroHeight;
        if (p.field.coverage == 0.0f) ++zeroCoverage;
    }
    std::printf("  spread: %d probes, x<1 on %d, x>=1 on %d, cutoff fires on %d / holds on %d\n"
                "          (%d of those where the ramp alone would NOT have been zero),\n"
                "          fwidth floored on %d, zero gradient %d, zero height %d, zero "
                "coverage %d\n",
                int(probes.size()), insideBand, outsideBand, cut, uncut, separable, floored,
                zeroGradient, zeroHeight, zeroCoverage);
    CHECK(separable > 5);
    CHECK(insideBand > 20);
    CHECK(outsideBand > 20);
    CHECK(cut > 20);
    CHECK(uncut > 20);
    CHECK(floored > 20);
    CHECK(zeroGradient > 10);
    CHECK(zeroHeight > 10);
    CHECK(zeroCoverage > 10);
}

// THE STAGE THAT IS ALL MULTIPLIES AND ADDS -- bit for bit, no allowance.
TEST_CASE(the_gpu_computes_the_same_direction_bit_for_bit) {
    Device& d = gpu();
    if (!d.valid()) return;
    const std::vector<Probe> probes = spread(false);
    const std::vector<float> got = onGpu(d, probes, kStageDirection);
    REQUIRE(got.size() == probes.size() * 4);

    int bad = 0;
    for (std::size_t i = 0; i < probes.size(); ++i) {
        float dir[2];
        sd::direction(probes[i].field, probes[i].rot, dir);
        if (!sameBits(got[i * 4 + 0], dir[0]) || !sameBits(got[i * 4 + 1], dir[1])) {
            if (bad < 3) {
                std::printf("  FAIL dir g=(%.4f,%.4f) rot=(%.4f,%.4f)  gpu (%.9g,%.9g)  "
                            "cpu (%.9g,%.9g)\n",
                            probes[i].field.gx, probes[i].field.gy, probes[i].rot.cos,
                            probes[i].rot.sin, got[i * 4 + 0], got[i * 4 + 1], dir[0], dir[1]);
            }
            ++bad;
        }
    }
    CHECK_EQ(bad, 0);
}

// The depth and the mask distance, also exact; the filter width, also exact; and
// the band coordinate, which divides.
TEST_CASE(the_gpu_computes_the_same_geometry) {
    Device& d = gpu();
    if (!d.valid()) return;
    const std::vector<Probe> probes = spread(false);
    const std::vector<float> got = onGpu(d, probes, kStageGeometry);
    REQUIRE(got.size() == probes.size() * 4);

    int bad = 0;
    Worst worst;
    for (std::size_t i = 0; i < probes.size(); ++i) {
        const Probe& p = probes[i];
        const float s = sd::depth(p.field, p.params);
        const float t = sd::maskDistance(p.field, p.params);
        const float w = sd::filterWidth(p.fwidthT);
        const float x = sd::bandCoordinate(s, p.params.height);
        // The first three carry no division and no root: bit for bit.
        if (!sameBits(got[i * 4 + 0], s) || !sameBits(got[i * 4 + 1], t) ||
            !sameBits(got[i * 4 + 2], w)) {
            if (bad < 3) {
                std::printf("  FAIL geometry d=%.4f  gpu (s=%.9g t=%.9g w=%.9g)  "
                            "cpu (s=%.9g t=%.9g w=%.9g)\n",
                            p.field.d, got[i * 4 + 0], got[i * 4 + 1], got[i * 4 + 2], s, t, w);
            }
            ++bad;
        }
        // The band coordinate divides, so it takes the specification's allowance
        // -- sized on the quotient's own magnitude, which the saturate bounds
        // at 1.
        worst.see(got[i * 4 + 3], x);
        if (!closeEnough(got[i * 4 + 3], x, float(kDivUlps) * ulpSizeAt(1.0f))) {
            if (bad < 3) {
                std::printf("  FAIL band coordinate s=%.4f height=%.4f  gpu %.9g  cpu %.9g\n", s,
                            p.params.height, got[i * 4 + 3], x);
            }
            ++bad;
        }
    }
    std::printf("  sdf displacement geometry: worst %u ULP\n", worst.ulps);
    CHECK_EQ(bad, 0);
}

// The two profiles and their blend. The root is the only inexact operation here
// and the allowance is the specification's three ULP of a value in [0, 1].
TEST_CASE(the_gpu_computes_the_same_profiles) {
    Device& d = gpu();
    if (!d.valid()) return;
    const std::vector<Probe> probes = spread(false);
    const std::vector<float> got = onGpu(d, probes, kStageProfile);
    REQUIRE(got.size() == probes.size() * 4);

    int bad = 0;
    Worst worst;
    const float bound = float(std::max(kDivUlps, kSqrtUlps)) * ulpSizeAt(1.0f);
    for (std::size_t i = 0; i < probes.size(); ++i) {
        const Probe& p = probes[i];
        const float x = sd::bandCoordinate(sd::depth(p.field, p.params), p.params.height);
        const float flat = sd::flatProfile(x);
        const float circ = sd::circularProfile(x);
        const float mag = sd::magnitude(flat, circ, p.params.curvature);
        // The flat profile is a branch and a multiply: no allowance at all, and
        // a driver that took the other side of `x < 1` shows up here rather than
        // as a small number.
        if (!sameBits(got[i * 4 + 0], flat)) {
            if (bad < 3) {
                std::printf("  FAIL flat x=%.9g  gpu %.9g  cpu %.9g\n", x, got[i * 4 + 0], flat);
            }
            ++bad;
        }
        worst.see(got[i * 4 + 1], circ);
        worst.see(got[i * 4 + 2], mag);
        if (!closeEnough(got[i * 4 + 1], circ, bound) || !closeEnough(got[i * 4 + 2], mag, bound)) {
            if (bad < 3) {
                std::printf("  FAIL profile x=%.9g curvature=%.3f  gpu (circ=%.9g mag=%.9g)  "
                            "cpu (circ=%.9g mag=%.9g)\n",
                            x, p.params.curvature, got[i * 4 + 1], got[i * 4 + 2], circ, mag);
            }
            ++bad;
        }
    }
    std::printf("  sdf displacement profiles: worst %u ULP\n", worst.ulps);
    CHECK_EQ(bad, 0);
}

// The mask, taken apart on both sides: the ramp before the coverage and the
// cutoff, the alpha after both, and -- the point of the stage -- whether the
// GPU's cutoff fired on the same probes as the oracle's.
TEST_CASE(the_gpu_masks_and_cuts_off_at_the_same_places) {
    Device& d = gpu();
    if (!d.valid()) return;
    const std::vector<Probe> probes = spread(false);
    const std::vector<float> got = onGpu(d, probes, kStageMask);
    REQUIRE(got.size() == probes.size() * 4);

    int bad = 0, fired = 0, separable = 0;
    Worst worst;
    for (std::size_t i = 0; i < probes.size(); ++i) {
        const Probe& p = probes[i];
        const float t = sd::maskDistance(p.field, p.params);
        const float w = sd::filterWidth(p.fwidthT);
        const float ramp = sd::maskRamp(t, w);
        const float a = sd::alpha(t, w, p.field.coverage);
        const float cut = t < sd::kBandCutoff ? 1.0f : 0.0f;
        if (cut != 0.0f) ++fired;
        if (cut != 0.0f && ramp > 0.0f && p.field.coverage > 0.0f) ++separable;

        worst.see(got[i * 4 + 0], ramp);
        worst.see(got[i * 4 + 1], a);
        const float bound = float(kDivUlps) * ulpSizeAt(1.0f);
        if (!closeEnough(got[i * 4 + 0], ramp, bound) ||
            !closeEnough(got[i * 4 + 1], a, bound * std::max(p.field.coverage, 1.0f)) ||
            !sameBits(got[i * 4 + 2], cut) || !sameBits(got[i * 4 + 3], p.field.coverage)) {
            if (bad < 3) {
                std::printf("  FAIL mask t=%.5f w=%.5f cov=%.3f  gpu (ramp=%.9g a=%.9g cut=%.1f)  "
                            "cpu (ramp=%.9g a=%.9g cut=%.1f)\n",
                            t, w, p.field.coverage, got[i * 4 + 0], got[i * 4 + 1],
                            got[i * 4 + 2], ramp, a, cut);
            }
            ++bad;
        }
    }
    std::printf("  sdf displacement mask: cutoff fired on %d of %d probes (%d of them where the "
                "ramp alone would not have zeroed it), worst %u ULP\n",
                fired, int(probes.size()), separable, worst.ulps);
    CHECK_EQ(bad, 0);
    // The GPU has to have taken the cut path too -- a sweep where it never fires
    // proves nothing about the guard. And it has to have taken it somewhere the
    // ramp was NOT already zero, or the differential is comparing two zeroes
    // that arrived for different reasons and cannot tell the guard from its
    // absence. `[ART]` Deleting the cutoff from the oracle left this sweep GREEN
    // until that second count was made non-zero.
    CHECK(fired > 20);
    CHECK(separable > 5);
}

// THE WHOLE FUNCTION. This is the differential the task asks for, over a spread
// that reaches both sides of both branches.
TEST_CASE(the_whole_stage_agrees_with_the_gpu) {
    Device& d = gpu();
    if (!d.valid()) return;
    const std::vector<Probe> probes = spread(false);
    const std::vector<float> got = onGpu(d, probes, kStageWhole);
    REQUIRE(got.size() == probes.size() * 4);

    int bad = 0, exact = 0;
    Worst worst;
    for (std::size_t i = 0; i < probes.size(); ++i) {
        const Probe& p = probes[i];
        const sd::Result want = oracleAt(p);
        float dir[2];
        sd::direction(p.field, p.rot, dir);

        const float wanted[4] = {want.x, want.y, want.alpha, want.one};
        const float bounds[4] = {dispBound(dir[0]), dispBound(dir[1]),
                                 float(kDivUlps) * ulpSizeAt(1.0f) * std::max(p.field.coverage, 1.0f),
                                 0.0f};
        for (int k = 0; k < 4; ++k) {
            worst.see(got[i * 4 + k], wanted[k]);
            if (sameBits(got[i * 4 + k], wanted[k])) ++exact;
            if (!closeEnough(got[i * 4 + k], wanted[k], bounds[k])) {
                if (bad < 4) {
                    std::printf("  FAIL whole d=%.3f h=%.2f c=%.2f off=%.2f mo=%.2f k=%d  "
                                "gpu %.9g  cpu %.9g\n",
                                p.field.d, p.params.height, p.params.curvature, p.params.offset,
                                p.params.maskOffset, k, got[i * 4 + k], wanted[k]);
                }
                ++bad;
            }
        }
    }
    // Printed so a regression reads as a NUMBER MOVING rather than only as a
    // pass/fail flip. `[ART]` It is zero on this adapter, and it is meant to
    // stay zero: nothing in this stage forces a divergence, the allowance exists
    // only because Vulkan's `fdiv` and `sqrt` are specified loosely.
    std::printf("  sdf displacement whole stage: %d probes, %d of %d components bit-identical, "
                "worst %u ULP\n",
                int(probes.size()), exact, int(probes.size()) * 4, worst.ulps);
    CHECK_EQ(bad, 0);
    // The `.w` is a literal 1.0, so it must be bit-identical everywhere with no
    // allowance whatsoever.
    for (std::size_t i = 0; i < probes.size(); ++i) CHECK_EQ(got[i * 4 + 3], 1.0f);
}

// ===========================================================================
// 7. the cross-check against AquaKit's independent CPU twin
// ===========================================================================

// `[ART]` MEASURED, 2026-09-02, OUT OF TREE. AquaKit's
// `tests/GlassShaderReference.h::ReferenceDisplacement` is a second CPU
// transcription of the same IR, written by the project that read it. This oracle
// was compiled against it (g++ -O2, the same toolchain this suite builds with)
// and driven through two sweeps:
//
//   * 200,000 randomised inputs -- 800,000 components, ALL 800,000 bit-identical,
//     worst 0 ULP, worst absolute difference 0.
//   * a deterministic edge grid over height {0, 1.5, 8, 20} x curvature
//     {0, 0.35, 1} x nine distances that put `t` on both sides of -5.0 and `x` on
//     both sides of 1 x four mask-offsets x four gradients (including the zero
//     one) x four rotations (including a non-unit one) x three coverages x four
//     derivatives (including 0) -- 331,776 components, ALL bit-identical,
//     4,608 of them a NaN on both sides at once (the unguarded `0/0` of
//     `height == 0` at `s == 0`, which both transcriptions propagate).
//
// 1,131,776 components, zero disagreements.
//
// That measurement is recorded here and is NOT run by this suite, and the reason
// is a rule rather than a convenience: AquaKit lives outside this repository, so
// wiring its header into this build would make `ic_tests` fail to compile on any
// machine that does not have the other project checked out beside this one. The
// measurement is reproducible by hand -- compile `SdfDisplacement.cpp` together
// with `D:/CodingProjects/AquaKit/tests/GlassShaderReference.h` and sweep.
//
// AND IT IS NOT INDEPENDENT EVIDENCE OF THE READING. Both sides descend from the
// same person reading the same IR once; agreeing to the bit proves the port did
// not introduce an error, and proves nothing about whether the reading was right.
// That is the whole difference between this stage and the rest of the tower, and
// `SdfDisplacement.h` says so at more length.
//
// What the local half of that cross-check CAN do without AquaKit is verify the
// worked numbers the decode's write-up publishes (`docs/shaders-glass-quartzcore.md`
// §3): inside the band at curvature 0 the magnitude is the constant `0.7071`, so
// the displacement is `(1 - 0.7071) * |g|` in the gradient's direction; at
// curvature 1 the profile is the quarter circle, so it is `1 - sqrt(1-(1-x)^2)`.
TEST_CASE(the_published_worked_values_come_out_of_this_oracle) {
    const sd::Rot identity{1.0f, 0.0f};
    sd::Field field;
    field.gx = 1.0f;   // a unit gradient pointing along +x
    field.gy = 0.0f;
    field.coverage = 1.0f;

    // Half way into a band of 20, at curvature 0: the flat bevel.
    sd::Params flatParams{20.0f, 0.0f, 0.0f, 0.0f};
    field.d = -10.0f;
    const sd::Result flat = sd::glassDisplacement(field, flatParams, identity, 1.0f);
    CHECK(std::fabs(flat.x - (1.0f - 0.7071f)) < 1e-4f);
    CHECK_EQ(flat.y, 0.0f);

    // The same point at curvature 1: the quarter circle at x = 0.5.
    sd::Params circParams{20.0f, 1.0f, 0.0f, 0.0f};
    const sd::Result circ = sd::glassDisplacement(field, circParams, identity, 1.0f);
    const float wantCirc = 1.0f - std::sqrt(1.0f - 0.5f * 0.5f);
    CHECK(std::fabs(circ.x - wantCirc) < 1e-6f);

    // At the very edge of the band the quarter circle is at its maximum: the
    // whole gradient, undiminished.
    field.d = 0.0f;
    const sd::Result edge = sd::glassDisplacement(field, circParams, identity, 1.0f);
    CHECK_EQ(edge.x, 1.0f);

    // At the end of the band both profiles are 1 and there is no displacement
    // left -- exactly zero, from both sides of the blend.
    field.d = -20.0f;
    for (float curvature : {0.0f, 0.5f, 1.0f}) {
        sd::Params p{20.0f, curvature, 0.0f, 0.0f};
        const sd::Result none = sd::glassDisplacement(field, p, identity, 1.0f);
        CHECK_EQ(none.x, 0.0f);
        CHECK_EQ(none.y, 0.0f);
    }
}
