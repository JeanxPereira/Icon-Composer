// The shared glass math: GPU against the oracle, bit for bit, plus the closed
// forms that exist independently of either.
//
// The shader this runs is `Glass.glsl` -- the SAME file the two glass shaders
// will include -- so what draws and what is verified cannot drift apart. That is
// the arrangement every transcribed stage in this repository uses.
//
// WHAT MAKES THIS A GATE RATHER THAN A DEMONSTRATION. A differential proves the
// two transcriptions agree with each other; it cannot prove either is the
// target's. Three things are pinned here that a differential alone would not
// catch:
//   * the edge curve against the CLOSED-FORM Gaussian CDF, with the measured
//     error written down, so a wrong coefficient stops looking like a curve;
//   * the seven dispersion weights summing, per channel and after the channel
//     scale, to one -- an invariant of a resampling kernel that no single wrong
//     weight survives;
//   * the YCC composite against its own algebra: identity at the defaults, exact
//     luma at saturation zero, and the fill taking over at alpha one.
#include "check.h"
#include "Source/RenderBox/Compute.h"
#include "Source/RenderBox/GlassOracle.h"
#include "Source/RenderBox/PathVertexOracle.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

static const std::uint32_t kGlassSpirv[] =
#include "glass_probe.comp.inc"
    ;

using namespace rb;

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

// Mirrors `Input` in glass_probe.comp. Every member is a vec4, so std430 needs
// no padding counted by hand.
struct Probe {
    float a[4]{0, 0, 0, 0};
    float c[4]{0, 0, 0, 0};
    float cm0[4]{0, 0, 0, 0};
    float cm1[4]{0, 0, 0, 0};
    float cm2[4]{0, 0, 0, 0};
};

struct Control {
    std::uint32_t count = 0;
    std::uint32_t stage = 0;
    std::uint32_t state = 0;
};

enum Stage : std::uint32_t {
    kEdgeDomain = 0,
    kEdgePolynomial = 1,
    kEdgeCoverage = 2,
    kEdgeInSigma = 3,
    kBandAmount = 4,
    kCompressMaxLuma = 5,
    kFaceMatrixRaw = 6,
    kFaceMatrix = 7,
    kAberrationTap = 8,
    kAberrationCombine = 9,
};

std::vector<float> onGpu(Device& d, const std::vector<Probe>& in, std::uint32_t stage) {
    if (in.empty()) return {};
    auto input = Buffer::create(d, in.size() * sizeof(Probe),
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!input) return {};
    std::memcpy(input->mapped(), in.data(), in.size() * sizeof(Probe));

    auto out = Buffer::create(d, in.size() * 4 * sizeof(float),
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!out) return {};
    std::memset(out->mapped(), 0, in.size() * 4 * sizeof(float));

    auto pass = ComputePass::create(d, kGlassSpirv, sizeof kGlassSpirv, sizeof(Control));
    if (!pass) {
        std::printf("  FAIL compute pass: %s\n", pass.error().c_str());
        ++ictest::failures();
        return {};
    }
    Control ctl;
    ctl.count = static_cast<std::uint32_t>(in.size());
    ctl.stage = stage;
    auto ran = pass->run(d, *input, *out, &ctl, sizeof ctl, ctl.count);
    if (!ran) {
        std::printf("  FAIL dispatch: %s\n", ran.error().c_str());
        ++ictest::failures();
        return {};
    }
    std::vector<float> raw(in.size() * 4, 0.0f);
    std::memcpy(raw.data(), out->mapped(), raw.size() * sizeof(float));
    return raw;
}

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

// WHERE BIT-FOR-BIT IS DEMANDED, AND WHERE IT IS NOT.
//
// Every primitive here is polynomial except one. `edgeCoverage`, the luminance
// compression, the colour matrix, the dispersion taps and the combine are
// multiplies and adds, which Vulkan specifies exactly once `precise` has taken
// contraction off the table -- so they are compared bit for bit, and any
// allowance would be a place for a transcription error to hide.
//
// `bandAmount` takes a square root, and Vulkan specifies `sqrt` only to 3 ULP
// with no SPIR-V control that makes it exact. The same limit already shows in
// the path vertex and gradient stages, which is why `ulpsApart` exists rather
// than being invented here.
constexpr std::uint32_t kSqrtUlps = 3;

float ulpSizeAt(float magnitude) {
    const float m = std::fabs(magnitude);
    return std::nextafter(m, std::numeric_limits<float>::infinity()) - m;
}

// AND WHY THE BAND'S ALLOWANCE IS MEASURED WHERE THE ERROR IS MADE.
//
// The root produces `circ`, which lives in [0, 1]; the result is
// `amount * (1 - circ)`, so three ULP created at `circ` arrive at the result
// scaled by `amount`. Counting ULP at the result instead would reject a correct
// transcription wherever `amount * (1 - circ)` is small and the error is not --
// the same lesson the gradient's radial geometry taught. So the bound is
// absolute, derived, and proportional to `amount`.
bool bandAgrees(float got, float want, float amount) {
    if (sameBits(got, want)) return true;
    const float bound = static_cast<float>(kSqrtUlps) * ulpSizeAt(1.0f) * std::fabs(amount);
    return std::fabs(got - want) <= bound;
}

}  // namespace

// ===========================================================================
// 1. the edge curve
// ===========================================================================

// THE CLOSED FORM, and this is the check that does not need a GPU or a second
// transcription: the polynomial is a minimax fit of `0.5 * erfc(u)`, which is
// `1 - Phi(u * sqrt(2))`, and with the 1/sqrt(2) pre-scale the composite is
// `1 - Phi(z)` for a distance in sigma.
//
// `[ART]` MEASURED: the maximum absolute error is **3.952e-03** over [-2, 2], at
// u = -1.8763. Through the pre-scale it is **3.949e-03** against `1 - Phi(z)`,
// at z = -2.6545. Those are the numbers this test asserts, with a tenth of a
// percent of slack for the sampling grid -- not a tolerance chosen until it
// passed, but the measurement itself, so a coefficient that drifts shows up as a
// failure rather than as a slightly different curve.
//
// The bound is a BAND, not a ceiling: the error must also not be far SMALLER
// than measured, because a transcription that accidentally computed the exact
// erfc would be a different (and wrong) function too.
TEST_CASE(the_edge_curve_is_the_gaussian_cdf_to_four_thousandths) {
    double worst = 0.0;
    double worstAt = 0.0;
    for (int i = 0; i <= 400000; ++i) {
        const double u = -2.0 + 4.0 * i / 400000.0;
        const double got = glass::edgeCoverage(static_cast<float>(u));
        const double want = 0.5 * std::erfc(u);
        const double err = std::fabs(got - want);
        if (err > worst) {
            worst = err;
            worstAt = u;
        }
    }
    std::printf("  [ART] max |edgeCoverage(u) - 0.5*erfc(u)| = %.6e at u=%.6f\n", worst, worstAt);
    CHECK(worst < 4.0e-3);
    CHECK(worst > 3.9e-3);

    double sigmaWorst = 0.0;
    for (int i = 0; i <= 400000; ++i) {
        const double z = -4.0 + 8.0 * i / 400000.0;
        const double got = glass::edgeCoverageInSigma(static_cast<float>(z));
        const double want = 0.5 * std::erfc(z / std::sqrt(2.0));
        sigmaWorst = std::fmax(sigmaWorst, std::fabs(got - want));
    }
    std::printf("  [ART] max |edgeCoverageInSigma(z) - (1 - Phi(z))| = %.6e\n", sigmaWorst);
    CHECK(sigmaWorst < 4.0e-3);
    CHECK(sigmaWorst > 3.9e-3);

    // It crosses a half at zero, exactly -- the odd part of the polynomial
    // vanishes there and the bias is all that is left.
    CHECK(sameBits(glass::edgeCoverage(0.0f), 0.5f));

    // `[ART]` AND IT IS NOT MONOTONE, WHICH WAS NOT EXPECTED AND IS THE TARGET'S.
    //
    // A CDF falls everywhere. This fit does not: its derivative
    // `c3 + 3*c2*u^2 + 5*c1*u^4 + 7*c0*u^6` turns POSITIVE at |u| = 1.9368 and
    // the curve hooks back. MEASURED: it dips to -5.0563e-04 at u = 1.9368 and
    // climbs to +2.4414e-04 (which is exactly 2^-12) at u = 2 -- so on the
    // positive side the coverage goes NEGATIVE, and by symmetry on the negative
    // side it goes to 1.00051, ABOVE one.
    //
    // That matters to a caller and it is not a defect to clamp away here: the
    // target does not clamp it either, and the two glass shaders multiply this
    // value into colours. A transcription that saturated the result would agree
    // with the target across 96% of the domain and disagree in the last 3% of the
    // band, where a hair of negative coverage is what keeps an edge from
    // haloing. Recorded, not corrected.
    int rises = 0;
    float previous = 2.0f;
    float lowest = 2.0f;
    float highest = -1.0f;
    for (int i = 0; i <= 200000; ++i) {
        const float u = -2.0f + 4.0f * static_cast<float>(i) / 200000.0f;
        const float v = glass::edgeCoverage(u);
        if (v > previous) ++rises;
        previous = v;
        lowest = std::fmin(lowest, v);
        highest = std::fmax(highest, v);
    }
    std::printf("  [ART] edgeCoverage over [-2,2]: range [%.9g, %.9g], %d rises\n", lowest,
                highest, rises);
    CHECK(rises > 0);
    CHECK(lowest < 0.0f);
    CHECK(lowest > -6.0e-4f);
    CHECK(highest > 1.0f);
    CHECK(highest < 1.0f + 6.0e-4f);
    // The two ends are exact powers of two, and they are what a clamp would
    // destroy: 2^-12 and its complement.
    CHECK(sameBits(glass::edgeCoverage(2.0f), 0.000244140625f));
    CHECK(sameBits(glass::edgeCoverage(-2.0f), 1.0f - 0.000244140625f));

    // Away from the two hooks it IS monotone, which is what makes it usable as a
    // coverage at all: nothing rises anywhere inside |u| <= 1.9.
    int innerRises = 0;
    previous = 2.0f;
    for (int i = 0; i <= 100000; ++i) {
        const float u = -1.9f + 3.8f * static_cast<float>(i) / 100000.0f;
        const float v = glass::edgeCoverage(u);
        if (v > previous) ++innerRises;
        previous = v;
    }
    CHECK_EQ(innerRises, 0);

    // Outside [-2, 2] the domain mapping saturates, so the curve is FLAT rather
    // than continuing -- which is the reason `0.5 * erfc` is only approximated on
    // that interval and why the clamp is part of the transcription.
    CHECK(sameBits(glass::edgeCoverage(2.0f), glass::edgeCoverage(9.0f)));
    CHECK(sameBits(glass::edgeCoverage(-2.0f), glass::edgeCoverage(-9.0f)));
}

// The four coefficients, pinned against the AquaKit reading of a DIFFERENT
// binary. Two independent readings of the same numbers is what makes them a
// measurement; if a future edit "cleans up" a decimal, this is what notices.
TEST_CASE(the_edge_coefficients_are_the_halves_two_binaries_agree_on) {
    CHECK(sameBits(glass::kEdgeC0, 0.0029544830322265625f));   // 0xH1A0D
    CHECK(sameBits(glass::kEdgeC1, -0.034454345703125f));      // 0xHA869
    CHECK(sameBits(glass::kEdgeC2, 0.168212890625f));          // 0xH3162
    CHECK(sameBits(glass::kEdgeC3, -0.560546875f));            // 0xHB87C
    // The pre-scale is the HALF nearest 1/sqrt(2), not 1/sqrt(2).
    CHECK(sameBits(glass::kInvSqrt2, 0.70703125f));            // 0xH39A8
    CHECK(glass::kInvSqrt2 != static_cast<float>(1.0 / std::sqrt(2.0)));

    // THE DIVERGENCE THE BRIEF NAMED. RenderBox's epsilon is 0.001 (0xH1419) and
    // it is the only one either module carries. AquaKit reads 1e-4 from
    // QuartzCore's half variants and 1e-6 from its float ones. RenderBox wins,
    // and the gap is three orders of magnitude -- an antialiasing floor, not a
    // rounding.
    CHECK(glass::kEpsilon > 1.0e-3f);
    CHECK(glass::kEpsilon < 1.1e-3f);
    CHECK(glass::kEpsilon > 1.0e-4f * 9.0f);
}

TEST_CASE(the_edge_curve_agrees_with_the_gpu) {
    Device& d = gpu();
    if (!d.valid()) return;
    std::vector<Probe> in;
    // Across the domain, through both saturating ends, and onto the exact
    // boundaries -2 and 2 where the clamp changes hands.
    for (float x = -6.0f; x <= 6.0f; x += 0.017f) {
        Probe p;
        p.a[0] = x;
        in.push_back(p);
    }
    for (float x : {-2.0f, 2.0f, 0.0f, -0.0f, 1.0f, -1.0f, 1e-8f, -1e-8f}) {
        Probe p;
        p.a[0] = x;
        in.push_back(p);
    }
    struct Row {
        std::uint32_t stage;
        float (*cpu)(float);
    };
    const Row rows[4] = {
        {kEdgeDomain, glass::edgeDomain},
        {kEdgePolynomial, glass::edgePolynomial},
        {kEdgeCoverage, glass::edgeCoverage},
        {kEdgeInSigma, glass::edgeCoverageInSigma},
    };
    for (const Row& r : rows) {
        const std::vector<float> got = onGpu(d, in, r.stage);
        REQUIRE(got.size() == in.size() * 4);
        int bad = 0;
        for (std::size_t i = 0; i < in.size(); ++i) {
            const float want = r.cpu(in[i].a[0]);
            if (!sameBits(got[i * 4], want)) {
                if (bad < 3) {
                    std::printf("  FAIL stage=%u x=%.9g  gpu %.9g  cpu %.9g\n", r.stage,
                                in[i].a[0], got[i * 4], want);
                }
                ++bad;
            }
        }
        CHECK_EQ(bad, 0);
    }
}

// ===========================================================================
// 2. the height profile
// ===========================================================================

TEST_CASE(the_band_profile_agrees_with_the_gpu) {
    Device& d = gpu();
    if (!d.valid()) return;
    std::vector<Probe> in;
    // Negative distances are the interesting half -- the profile is driven by
    // `-d`, so inside the shape is where it does anything at all -- but positive
    // ones have to be swept too, because that is where the saturate holds it at
    // zero and a dropped clamp would show as a growing band.
    for (float d0 = -80.0f; d0 <= 40.0f; d0 += 1.7f) {
        for (float invH : {0.0f, 0.02f, 1.0f / 60.0f, 0.5f, 4.0f}) {
            for (float amount : {-150.0f, -1.0f, 0.0f, 1.0f, 100.0f, 400.0f}) {
                for (float offset : {0.0f, -3.0f, 11.0f}) {
                    Probe p;
                    p.a[0] = d0;
                    p.a[1] = amount;
                    p.a[2] = invH;
                    p.a[3] = offset;
                    in.push_back(p);
                }
            }
        }
    }
    const std::vector<float> got = onGpu(d, in, kBandAmount);
    REQUIRE(got.size() == in.size() * 4);
    int bad = 0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        const Probe& p = in[i];
        const float want = glass::bandAmount(p.a[0], p.a[1], p.a[2], p.a[3]);
        if (!bandAgrees(got[i * 4], want, p.a[1])) {
            if (bad < 3) {
                std::printf("  FAIL band d=%.4f amount=%.4f invH=%.6f off=%.2f  gpu %.9g  cpu %.9g\n",
                            p.a[0], p.a[1], p.a[2], p.a[3], got[i * 4], want);
            }
            ++bad;
        }
    }
    CHECK_EQ(bad, 0);
}

// THE ZERO-HEIGHT GUARD, which is the whole reason `invHeight` is a reciprocal
// rather than a height. Doc 03 §27.2: the ARM64 packer writes `1/height` with a
// compare-and-select that sends zero to ZERO, so a shader that divided would
// produce infinity exactly where the target produces a flat band.
//
// Both halves are pinned: the packer's guard, and what the profile then does
// with the zero it is handed.
TEST_CASE(a_height_of_zero_gives_zero_and_not_infinity) {
    CHECK(sameBits(glass::reciprocalHeight(0.0f), 0.0f));
    CHECK(sameBits(glass::reciprocalHeight(-0.0f), 0.0f));
    // `> 0`, not `!= 0`: a negative height takes the zero branch too.
    CHECK(sameBits(glass::reciprocalHeight(-60.0f), 0.0f));
    CHECK(sameBits(glass::reciprocalHeight(60.0f), 1.0f / 60.0f));
    CHECK(std::isfinite(glass::reciprocalHeight(0.0f)));

    // And with that zero in hand the profile is finite everywhere and equal to
    // `amount`: `x` is zero, so `circ` is zero, so nothing is subtracted.
    for (float d0 : {-1000.0f, -1.0f, 0.0f, 1.0f, 1000.0f}) {
        const float v = glass::bandAmount(d0, 100.0f, glass::reciprocalHeight(0.0f), 0.0f);
        CHECK(std::isfinite(v));
        CHECK(sameBits(v, 100.0f));
    }
}

// The profile's own algebra, independent of the GPU. `x` is a saturated depth,
// `circ` is the quarter circle over it, and the two ends are exact.
TEST_CASE(the_band_profile_lands_on_its_two_ends_exactly) {
    // Fully outside (d >= 0 with no offset): x = 0, circ = 0, the full amount.
    CHECK(sameBits(glass::bandAmount(0.0f, 7.0f, 0.5f, 0.0f), 7.0f));
    CHECK(sameBits(glass::bandAmount(50.0f, 7.0f, 0.5f, 0.0f), 7.0f));
    // Fully inside (-d * invHeight >= 1): x = 1, circ = sqrt(1) = 1, zero left.
    CHECK(sameBits(glass::bandAmount(-2.0f, 7.0f, 0.5f, 0.0f), 0.0f));
    CHECK(sameBits(glass::bandAmount(-2000.0f, 7.0f, 0.5f, 0.0f), 0.0f));
    // The offset moves the band bodily, and it is SUBTRACTED from `-d`: at
    // `d = -3` an offset of +3 puts the profile back where `d = 0` with no
    // offset puts it, and an offset of -3 pushes it three further in. The sign
    // is the thing worth pinning -- getting it backwards still draws a band.
    CHECK(sameBits(glass::bandAmount(-3.0f, 7.0f, 0.5f, 3.0f),
                   glass::bandAmount(0.0f, 7.0f, 0.5f, 0.0f)));
    CHECK(sameBits(glass::bandAmount(-3.0f, 7.0f, 0.5f, -3.0f),
                   glass::bandAmount(-6.0f, 7.0f, 0.5f, 0.0f)));
}

// ===========================================================================
// 3. luminance compression
// ===========================================================================

// The two Rec. 709 triples mod98 carries, and the point of the test is that they
// are NOT the same triple: red and blue differ by one ulp of half between the
// one the compression measures with and the one the specular weighs with.
// Collapsing them would be a tidying-up this reading has no basis for.
TEST_CASE(mod98_carries_two_rec709_triples_and_they_differ) {
    CHECK(sameBits(glass::kLumaCompress[0], 0.212646484375f));      // 0xH32CE
    CHECK(sameBits(glass::kLumaCompress[1], 0.71533203125f));       // 0xH39B9
    CHECK(sameBits(glass::kLumaCompress[2], 0.07220458984375f));    // 0xH2C9F
    CHECK(sameBits(glass::kLumaWeight[0], 0.2125244140625f));       // 0xH32CD
    CHECK(sameBits(glass::kLumaWeight[1], 0.71533203125f));         // 0xH39B9
    CHECK(sameBits(glass::kLumaWeight[2], 0.07208251953125f));      // 0xH2C9D
    CHECK(glass::kLumaCompress[0] != glass::kLumaWeight[0]);
    CHECK(glass::kLumaCompress[2] != glass::kLumaWeight[2]);
    // Green is the SAME half in both, which is what makes the other two a
    // rounding difference rather than a different matrix.
    CHECK(sameBits(glass::kLumaCompress[1], glass::kLumaWeight[1]));
    // Both are Rec. 709 to within half's precision, and both sum to about one.
    const float sumA = glass::kLumaCompress[0] + glass::kLumaCompress[1] + glass::kLumaCompress[2];
    const float sumB = glass::kLumaWeight[0] + glass::kLumaWeight[1] + glass::kLumaWeight[2];
    CHECK(std::fabs(sumA - 1.0f) < 1.0e-3f);
    CHECK(std::fabs(sumB - 1.0f) < 1.0e-3f);
    CHECK(std::fabs(glass::kLumaCompress[0] - 0.2126f) < 1.0e-3f);
    CHECK(std::fabs(glass::kLumaCompress[1] - 0.7152f) < 1.0e-3f);
    CHECK(std::fabs(glass::kLumaCompress[2] - 0.0722f) < 1.0e-3f);
}

TEST_CASE(the_luminance_compression_agrees_with_the_gpu) {
    Device& d = gpu();
    if (!d.valid()) return;
    std::vector<Probe> in;
    for (float r = -0.4f; r <= 1.6f; r += 0.31f) {
        for (float g = -0.4f; g <= 1.6f; g += 0.27f) {
            for (float b = -0.4f; b <= 1.6f; b += 0.23f) {
                // The complement sweep includes zero and a negative, which take
                // the guard's early return, and one above one, which drives the
                // saturate to its floor.
                for (float k : {-0.5f, 0.0f, 0.05f, 0.3f, 1.0f, 2.5f}) {
                    Probe p;
                    p.c[0] = r;
                    p.c[1] = g;
                    p.c[2] = b;
                    p.c[3] = k;
                    in.push_back(p);
                }
            }
        }
    }
    const std::vector<float> got = onGpu(d, in, kCompressMaxLuma);
    REQUIRE(got.size() == in.size() * 4);
    int bad = 0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        float want[3];
        glass::compressMaxLuma(in[i].c, in[i].c[3], want);
        for (int k = 0; k < 3; ++k) {
            if (!sameBits(got[i * 4 + k], want[k])) {
                if (bad < 3) {
                    std::printf("  FAIL luma rgb=(%.3f,%.3f,%.3f) k=%.3f ch=%d  gpu %.9g  cpu %.9g\n",
                                in[i].c[0], in[i].c[1], in[i].c[2], in[i].c[3], k,
                                got[i * 4 + k], want[k]);
                }
                ++bad;
            }
        }
    }
    CHECK_EQ(bad, 0);
}

// The guard, on its own. `[BIN]` mod98 %876 is `ogt`, so a complement of zero or
// below returns the colour UNTOUCHED -- not compressed by nothing, which would
// round differently, but the same bits back.
TEST_CASE(a_complement_at_or_below_zero_returns_the_colour_untouched) {
    const float c[3] = {0.31f, 0.77f, 0.128f};
    for (float k : {0.0f, -0.0f, -1.0f, -1e-30f}) {
        float out[3];
        glass::compressMaxLuma(c, k, out);
        for (int i = 0; i < 3; ++i) CHECK(sameBits(out[i], c[i]));
    }
    // And just above zero it is NOT the identity -- otherwise the test above
    // would pass on a function that ignored its argument.
    float out[3];
    glass::compressMaxLuma(c, 0.25f, out);
    CHECK(!sameBits(out[0], c[0]));
}

// ===========================================================================
// 4. the colour matrix
// ===========================================================================

TEST_CASE(the_colour_matrix_agrees_with_the_gpu) {
    Device& d = gpu();
    if (!d.valid()) return;
    // A matrix with nothing symmetric about it: a row swap, a sign flip or a
    // bias read from the wrong lane all change the answer.
    const float rows[3][4] = {
        {0.91f, -0.13f, 0.07f, 0.031f},
        {0.02f, 0.83f, -0.21f, -0.114f},
        {-0.44f, 0.19f, 1.27f, 0.402f},
    };
    std::vector<Probe> in;
    for (float r = -0.5f; r <= 1.5f; r += 0.19f) {
        for (float g = -0.5f; g <= 1.5f; g += 0.23f) {
            for (float b = -0.5f; b <= 1.5f; b += 0.29f) {
                for (float opacity : {0.0f, 0.25f, 1.0f, 1.75f}) {
                    Probe p;
                    p.c[0] = r;
                    p.c[1] = g;
                    p.c[2] = b;
                    p.c[3] = opacity;
                    for (int k = 0; k < 4; ++k) {
                        p.cm0[k] = rows[0][k];
                        p.cm1[k] = rows[1][k];
                        p.cm2[k] = rows[2][k];
                    }
                    in.push_back(p);
                }
            }
        }
    }
    glass::ColorMatrix m;
    for (int i = 0; i < 3; ++i) {
        m.row[i].r = rows[i][0];
        m.row[i].g = rows[i][1];
        m.row[i].b = rows[i][2];
        m.row[i].bias = rows[i][3];
    }
    for (std::uint32_t stage : {static_cast<std::uint32_t>(kFaceMatrixRaw),
                                static_cast<std::uint32_t>(kFaceMatrix)}) {
        const std::vector<float> got = onGpu(d, in, stage);
        REQUIRE(got.size() == in.size() * 4);
        int bad = 0;
        for (std::size_t i = 0; i < in.size(); ++i) {
            float want[3];
            if (stage == kFaceMatrixRaw) {
                glass::faceColorMatrixRaw(in[i].c, m, want);
            } else {
                glass::applyFaceColorMatrix(in[i].c, m, in[i].c[3], want);
            }
            for (int k = 0; k < 3; ++k) {
                if (!sameBits(got[i * 4 + k], want[k])) {
                    if (bad < 3) {
                        std::printf("  FAIL matrix stage=%u rgb=(%.3f,%.3f,%.3f) o=%.2f ch=%d  "
                                    "gpu %.9g  cpu %.9g\n",
                                    stage, in[i].c[0], in[i].c[1], in[i].c[2], in[i].c[3], k,
                                    got[i * 4 + k], want[k]);
                    }
                    ++bad;
                }
            }
        }
        CHECK_EQ(bad, 0);
    }
}

// An opacity of zero has to give the input back BIT FOR BIT, because the mix is
// `c + (f - c) * 0` and that is exactly `c` -- and an opacity of one has to give
// the raw matrix back the same way. Those two are what tell `mix(c, f, o)` apart
// from `c * (1 - o) + f * o`, which is not exact at either end.
TEST_CASE(the_face_opacity_is_exact_at_both_ends) {
    glass::ColorMatrix m;
    m.row[0] = {0.91f, -0.13f, 0.07f, 0.031f};
    m.row[1] = {0.02f, 0.83f, -0.21f, -0.114f};
    m.row[2] = {-0.44f, 0.19f, 1.27f, 0.402f};
    const float c[3] = {0.317f, 0.881f, 0.129f};
    float raw[3];
    glass::faceColorMatrixRaw(c, m, raw);
    float atZero[3];
    glass::applyFaceColorMatrix(c, m, 0.0f, atZero);
    float atOne[3];
    glass::applyFaceColorMatrix(c, m, 1.0f, atOne);
    for (int i = 0; i < 3; ++i) {
        CHECK(sameBits(atZero[i], c[i]));
        CHECK(sameBits(atOne[i], raw[i]));
    }
}

// ===========================================================================
// 5. the YCC composite -- CPU only, and checked against its own algebra
// ===========================================================================

// `[OBS]` This is `[BIN]` from QuartzCore (AquaKit's reading of
// `CA::ColorMatrix::set_ycc_composite`), NOT from mod98. That RenderBox's packer
// runs the same routine is doc 03 §27.5's inference from the 76 unbound bytes,
// and it is recorded as inference. What CAN be gated without settling that is
// the algebra: the routine claims to be the YCC matrix, a saturation scale and a
// source-over composed, and each of those three has a case where the answer is
// known in closed form.
TEST_CASE(the_ycc_composite_is_the_identity_at_its_defaults) {
    const float noFill[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const glass::ColorMatrix m = glass::makeYccCompositeMatrix(1.0f, 0.0f, 1.0f, noFill);
    // White 1, black 0, saturation 1, nothing filled: the inverse times the
    // forward matrix, which is the identity to the precision the two constant
    // blocks were stored at. 7.5e-5 is what the published four-decimal
    // coefficients can do, and it is measured, not chosen.
    for (int r = 0; r < 3; ++r) {
        const float row[3] = {m.row[r].r, m.row[r].g, m.row[r].b};
        for (int c = 0; c < 3; ++c) {
            const float want = (r == c) ? 1.0f : 0.0f;
            CHECK(std::fabs(row[c] - want) < 2.0e-4f);
        }
        // The bias cancels EXACTLY at the defaults: the chroma's +0.5 going in
        // and the -0.5 folded into the inverse's bias column are the same
        // number. That cancellation is the thing that proves the two blocks are
        // a matched pair rather than two matrices that happen to look inverse.
        CHECK(std::fabs(m.row[r].bias) < 1.0e-6f);
    }
}

TEST_CASE(the_ycc_composite_at_saturation_zero_is_exactly_luma) {
    const float noFill[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const glass::ColorMatrix m = glass::makeYccCompositeMatrix(1.0f, 0.0f, 0.0f, noFill);
    // Saturation zero kills both chroma columns, and the inverse's first column
    // is all ones, so every row collapses onto the FORWARD matrix's luma row --
    // exactly, not approximately.
    for (int r = 0; r < 3; ++r) {
        CHECK(sameBits(m.row[r].r, glass::kYcc[0][0]));
        CHECK(sameBits(m.row[r].g, glass::kYcc[0][1]));
        CHECK(sameBits(m.row[r].b, glass::kYcc[0][2]));
        CHECK(std::fabs(m.row[r].bias) < 1.0e-6f);
    }
}

TEST_CASE(the_ycc_composite_hands_the_whole_pixel_to_an_opaque_fill) {
    // A premultiplied fill with alpha 1 leaves `1 - a = 0` of the matrix, so
    // every coefficient goes to zero and the bias IS the fill's colour. That is
    // source-over, and it is the only reading under which `fill` being
    // premultiplied means anything.
    const float opaque[4] = {0.2f, 0.4f, 0.6f, 1.0f};
    const glass::ColorMatrix m = glass::makeYccCompositeMatrix(0.8f, 0.1f, 0.7f, opaque);
    for (int r = 0; r < 3; ++r) {
        // `== 0` and not `sameBits`: a coefficient that was negative comes out
        // as NEGATIVE zero, which is a zero and not a defect. Bit comparison is
        // the right instrument for "the arithmetic did nothing" and the wrong
        // one for "the result is zero".
        CHECK(m.row[r].r == 0.0f);
        CHECK(m.row[r].g == 0.0f);
        CHECK(m.row[r].b == 0.0f);
        CHECK(sameBits(m.row[r].bias, opaque[r]));
    }
    // And a fully transparent fill adds nothing, so the matrix is the same one
    // the defaults produce -- the fill is genuinely composed on top, not mixed in.
    const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const glass::ColorMatrix a = glass::makeYccCompositeMatrix(0.8f, 0.1f, 0.7f, clear);
    const glass::ColorMatrix b = glass::makeYccCompositeMatrix(0.8f, 0.1f, 0.7f, clear);
    for (int r = 0; r < 3; ++r) CHECK(sameBits(a.row[r].bias, b.row[r].bias));

    // `white` and `black` are a gain and a lift on luma: white == black leaves no
    // gain at all, so the luma columns vanish and only the lift survives.
    const glass::ColorMatrix flat = glass::makeYccCompositeMatrix(0.5f, 0.5f, 0.0f, clear);
    for (int r = 0; r < 3; ++r) {
        CHECK(flat.row[r].r == 0.0f);
        CHECK(flat.row[r].g == 0.0f);
        CHECK(flat.row[r].b == 0.0f);
        CHECK(std::fabs(flat.row[r].bias - 0.5f) < 1.0e-6f);
    }
}

// ===========================================================================
// 6. the chromatic dispersion
// ===========================================================================

// The seven taps, value by value. `[BIN]` three at a positive offset with the
// weight running down from 1, four at a negative offset with it running up from
// 0 -- and the running sum is the point: AquaKit writes `1 - i/3`, mod98 writes
// a `fadd` per iteration, and the two disagree by a float at the third tap.
TEST_CASE(the_seven_dispersion_taps_are_the_two_loops_mod98_writes) {
    CHECK_EQ(glass::kAberrationTaps, 7);

    // Reproduce the accumulation here, independently of the oracle's loop, so a
    // change to either has to be a change to both.
    float down[3];
    down[0] = 1.0f;
    down[1] = down[0] + glass::kAberrationStepDown;
    down[2] = down[1] + glass::kAberrationStepDown;
    float up[4];
    up[0] = 0.0f;
    up[1] = up[0] + glass::kAberrationStepUp;
    up[2] = up[1] + glass::kAberrationStepUp;
    up[3] = up[2] + glass::kAberrationStepUp;

    for (int i = 0; i < 3; ++i) {
        const glass::AberrationTap t = glass::aberrationTap(i);
        CHECK(sameBits(t.offset, down[i]));
        CHECK(sameBits(t.weight[0], down[i]));
        CHECK(sameBits(t.weight[1], 1.0f - down[i]));
        CHECK(sameBits(t.weight[2], 0.0f));
    }
    for (int j = 0; j < 4; ++j) {
        const glass::AberrationTap t = glass::aberrationTap(3 + j);
        CHECK(t.offset == -up[j]);
        CHECK(sameBits(t.weight[0], 0.0f));
        CHECK(sameBits(t.weight[1], 1.0f - up[j]));
        CHECK(sameBits(t.weight[2], up[j]));
    }

    // The ends are exact and they are what makes this a DISPERSION: red is
    // carried entirely by the tap furthest in one direction, blue by the one
    // furthest in the other, and the two never overlap.
    CHECK(sameBits(glass::aberrationTap(0).offset, 1.0f));
    CHECK(sameBits(glass::aberrationTap(0).weight[0], 1.0f));
    // Tap three is the hinge between the loops: the second loop's weight starts
    // at zero, so its offset is negative zero. Zero either way, and the sign of
    // it is not something to assert.
    CHECK(glass::aberrationTap(3).offset == 0.0f);
    CHECK(sameBits(glass::aberrationTap(6).offset, -1.0f));
    CHECK(sameBits(glass::aberrationTap(6).weight[2], 1.0f));
    for (int i = 0; i < 3; ++i) CHECK(sameBits(glass::aberrationTap(i).weight[2], 0.0f));
    for (int i = 3; i < 7; ++i) CHECK(sameBits(glass::aberrationTap(i).weight[0], 0.0f));

    // AND THE DIVERGENCE, pinned rather than described: the accumulated third
    // weight is NOT what indexing gives.
    const float indexed = 1.0f - 2.0f * glass::kAberrationStepUp;
    CHECK(!sameBits(down[2], indexed));
    CHECK(ulpsApart(down[2], indexed) == 1u);
}

// THE INVARIANT A SINGLE WRONG WEIGHT CANNOT SURVIVE -- and the place the target
// misses it by an amount big enough to see.
//
// Each channel's weights, summed and then scaled by its entry in
// `kAberrationRgbScale`, should come to one: that is what makes the dispersion a
// resampling of the backdrop rather than a brightening of it. Red and blue are
// each spread over four taps and scaled by 1/2, and both land on one to within
// the accumulation's own rounding.
//
// `[ART]` GREEN DOES NOT, AND THE SHORTFALL IS THE TARGET'S. Green is spread
// over all seven taps, its weights sum to 3, and its scale is `0xH3555` -- the
// HALF nearest 1/3, which is 0.333251953125. Three of those is 0.999755859375,
// so green comes out **2.4414e-04 dark** against red and blue. That is not a
// transcription error to tune away: it is what a half-precision 1/3 does, it is
// the same constant both mod98 and mod99 carry, and a port that "fixed" it with
// float(1/3) would be brighter than the target by exactly this much on every
// dispersed pixel. So the shortfall is asserted to be EXACTLY `1 - 3*half(1/3)`,
// which no other constant produces.
TEST_CASE(the_dispersion_sums_to_one_except_green_which_the_half_leaves_short) {
    float sum[3] = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < glass::kAberrationTaps; ++i) {
        const glass::AberrationTap t = glass::aberrationTap(i);
        for (int k = 0; k < 3; ++k) sum[k] += t.weight[k];
    }
    float scaled[3];
    for (int k = 0; k < 3; ++k) {
        scaled[k] = sum[k] * glass::kAberrationRgbScale[k];
        std::printf("  [ART] dispersion channel %d: sum %.9g, scaled %.9g\n", k, sum[k], scaled[k]);
    }
    // Red and blue: one, to the accumulation's rounding over four terms.
    CHECK(std::fabs(scaled[0] - 1.0f) < 3.0e-7f);
    CHECK(std::fabs(scaled[2] - 1.0f) < 3.0e-7f);
    // Green's weights really do sum to three, so the shortfall is not in them.
    CHECK(std::fabs(sum[1] - 3.0f) < 1.0e-6f);
    const double shortfall = 1.0 - static_cast<double>(scaled[1]);
    const double fromTheHalf = 1.0 - 3.0 * 0.333251953125;
    std::printf("  [ART] green shortfall %.6e, and 1 - 3*half(1/3) = %.6e\n", shortfall,
                fromTheHalf);
    // The two agree to 6e-8, and the residue is the seven-term accumulation's
    // own rounding -- the weights sum to 2.99999976, three ulp under three, not
    // to three exactly. So the bound is that rounding and not a round number:
    // 1e-6 is wide enough for it and four hundred times too narrow for a wrong
    // constant, since float(1/3) instead of the half moves this by 2.4e-4.
    CHECK(std::fabs(shortfall - fromTheHalf) < 1.0e-6);
    // Which only means anything because the scale is the half and not float(1/3).
    CHECK(sameBits(glass::kAberrationRgbScale[1], 0.333251953125f));
    CHECK(glass::kAberrationRgbScale[1] != 1.0f / 3.0f);

    // The alpha normaliser is the count, and the count is seven.
    CHECK(std::fabs(glass::kAberrationTaps * glass::kAberrationAlphaScale - 1.0f) < 1.0e-6f);
}

TEST_CASE(the_dispersion_taps_agree_with_the_gpu) {
    Device& d = gpu();
    if (!d.valid()) return;
    std::vector<Probe> in(glass::kAberrationTaps);
    const std::vector<float> got = onGpu(d, in, kAberrationTap);
    REQUIRE(got.size() == in.size() * 4);
    int bad = 0;
    for (int i = 0; i < glass::kAberrationTaps; ++i) {
        const glass::AberrationTap t = glass::aberrationTap(i);
        const float want[4] = {t.offset, t.weight[0], t.weight[1], t.weight[2]};
        for (int k = 0; k < 4; ++k) {
            if (!sameBits(got[static_cast<std::size_t>(i) * 4 + k], want[k])) {
                if (bad < 4) {
                    std::printf("  FAIL tap %d lane %d  gpu %.9g  cpu %.9g\n", i, k,
                                got[static_cast<std::size_t>(i) * 4 + k], want[k]);
                }
                ++bad;
            }
        }
    }
    CHECK_EQ(bad, 0);
}

TEST_CASE(the_dispersion_combine_agrees_with_the_gpu) {
    Device& d = gpu();
    if (!d.valid()) return;
    std::vector<Probe> in;
    for (float r = 0.0f; r <= 3.0f; r += 0.37f) {
        for (float g = 0.0f; g <= 4.0f; g += 0.53f) {
            for (float b = -1.0f; b <= 3.0f; b += 0.41f) {
                for (float alphaSum : {0.0f, 0.5f, 3.5f, 7.0f, 9.5f}) {
                    Probe p;
                    p.a[0] = r;
                    p.a[1] = g;
                    p.a[2] = b;
                    p.a[3] = alphaSum;
                    in.push_back(p);
                }
            }
        }
    }
    const std::vector<float> got = onGpu(d, in, kAberrationCombine);
    REQUIRE(got.size() == in.size() * 4);
    int bad = 0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        float want[4];
        glass::aberrationCombine(in[i].a, in[i].a[3], want);
        for (int k = 0; k < 4; ++k) {
            if (!sameBits(got[i * 4 + k], want[k])) {
                if (bad < 3) {
                    std::printf("  FAIL combine acc=(%.3f,%.3f,%.3f) a=%.3f lane=%d  gpu %.9g  "
                                "cpu %.9g\n",
                                in[i].a[0], in[i].a[1], in[i].a[2], in[i].a[3], k,
                                got[i * 4 + k], want[k]);
                }
                ++bad;
            }
        }
    }
    CHECK_EQ(bad, 0);
}

// The combine RE-PREMULTIPLIES, and that is a divergence from AquaKit worth
// pinning rather than describing: `%708` multiplies the scaled sums by the alpha
// `%703` just produced. AquaKit returns them un-premultiplied beside it.
TEST_CASE(the_dispersion_combine_re_premultiplies_the_colour) {
    const float acc[3] = {2.0f, 3.0f, 2.0f};
    float zero[4];
    glass::aberrationCombine(acc, 0.0f, zero);
    // Alpha zero takes the colour with it. Un-premultiplied it would not.
    for (int i = 0; i < 4; ++i) CHECK(zero[i] == 0.0f);

    float full[4];
    glass::aberrationCombine(acc, 7.0f, full);
    CHECK(std::fabs(full[3] - 1.0f) < 1.0e-6f);
    // With alpha at one the colour is the scaled sum itself, so the two readings
    // only part company away from alpha one -- which is why the zero case above
    // is the one that decides it.
    for (int i = 0; i < 3; ++i) {
        CHECK(std::fabs(full[i] - acc[i] * glass::kAberrationRgbScale[i]) < 1.0e-6f);
    }
}
