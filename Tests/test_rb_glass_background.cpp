// `glassBackground_v1`, block by block: the GPU against the oracle, and the
// control flow against itself.
//
// The shader this runs is `GlassBackground.glsl` -- the SAME file the glass pass
// will include -- so what draws and what is verified cannot drift apart.
//
// WHAT MAKES THIS A GATE RATHER THAN A DEMONSTRATION. A differential proves two
// transcriptions agree with each other; it cannot prove either is the target's.
// Four kinds of check are here that a differential alone would not catch:
//
//   * THE BYTE MAP. The 106 mirrored floats are checked against `offsetof` and
//     against the TBAA offsets read from mod98's `!40`, so a member inserted or
//     reordered fails rather than silently renaming a slot.
//   * EVERY BRANCH. mod98 has three variant bits, fourteen uniform-is-zero
//     early-outs and one short-circuit to zero. Each one is driven from both
//     sides here. An untested guard is not a guard.
//   * THE ARRANGEMENTS THAT A PLAUSIBLE TRANSCRIPTION LOSES. Three of them are
//     pinned by construction rather than by comparison: the face matrix mixing
//     from the UNCOMPRESSED colour, the blur fill putting darken on the min, and
//     the clamp re-premultiplying by the RAW alpha.
//   * THE PIECEWISE RAMP AT ITS KNOTS, where the closed form is exact and no
//     second transcription is involved.
//
// WHERE BIT-FOR-BIT IS DEMANDED AND WHERE IT IS NOT. Most of these blocks are
// multiplies, adds, mins, maxes and clamps, which Vulkan specifies exactly once
// `precise` has taken contraction off the table -- those are compared bit for
// bit and any allowance would be a place for a transcription error to hide.
//
// Two operations are not exact, and the allowance for each is DERIVED from the
// operation rather than chosen:
//
//   * `sqrt`, which Vulkan specifies to 3 ULP. It enters through
//     `rbGlassBandAmount`, and the bound is carried the way `test_rb_glass.cpp`
//     already carries it -- absolute, at the place the error is made, scaled by
//     `amount`.
//
//   * `OpFDiv`, which the Vulkan spec's precision table gives as 2.5 ULP for
//     32-bit floats. `[ART]` MEASURED on this adapter: it is NOT correctly
//     rounded. The differential below counts how many division-bearing
//     comparisons came back bit-identical and how many needed the allowance, and
//     prints both, so a silent slide into slack shows up as a changing number.
//
//     The SHAPE of that allowance is the thing worth getting right. A divide's
//     error is RELATIVE and it is made at the QUOTIENT. Where the quotient
//     reaches the result through multiplies and adds that do not cancel, a bound
//     relative to the result is the same bound moved, and `divAgrees` states it
//     that way. Where something downstream CANCELS it is not: `ringShadow`
//     subtracts two edge-curve evaluations that agree to four digits, and one
//     ULP in the quotient came back as **137 ULP** in that difference. A
//     result-relative bound there would be a bound on nothing, so that block
//     derives its own absolute one from the curve's measured slope. Same lesson
//     `bandAgrees` records -- state the allowance where the error is MADE --
//     applied to a second operation.
#include "check.h"
#include "Source/RenderBox/Compute.h"
#include "Source/RenderBox/GlassBackground.h"
#include "Source/RenderBox/GlassOracle.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

static const std::uint32_t kBgSpirv[] =
#include "glass_background_probe.comp.inc"
    ;

using namespace rb;
using namespace rb::glassbg;

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

// Mirrors one record of `glass_background_probe.comp`: 28 floats, laid out as
// four generic vec4s and a 3x4 colour matrix.
constexpr std::uint32_t kStride = 28;

struct Probe {
    float f[kStride]{};
    float& a(int i) { return f[i]; }
    float& b(int i) { return f[4 + i]; }
    float& c(int i) { return f[8 + i]; }
    float& e(int i) { return f[12 + i]; }
    void matrix(const float m[3][4]) {
        for (int r = 0; r < 3; ++r)
            for (int k = 0; k < 4; ++k) f[16 + r * 4 + k] = m[r][k];
    }
};

struct Control {
    std::uint32_t count = 0;
    std::uint32_t stage = 0;
    std::uint32_t stride = kStride;
    std::uint32_t probeBase = 0;
};

enum Stage : std::uint32_t {
    kDecodeDistance = 0,
    kDecodeGradient = 1,
    kCoverage = 2,
    kMask = 3,
    kShadowPoint = 4,
    kRingShadowPoint = 5,
    kKeyFillHighlight = 6,
    kRingShadow = 7,
    kShadowHeight = 8,
    kShadowCoverage = 9,
    kShadowColor = 10,
    kShadowPremultiplied = 11,
    kInnerHeight = 12,
    kBlurRadiusRamp = 13,
    kFlatBlurRadius = 14,
    kRefractPoint = 15,
    kAberrationHeight = 16,
    kAberrationStep = 17,
    kBlurFill = 18,
    kOuterHeight = 19,
    kOuterWeight = 20,
    kOuterMix = 21,
    kUnpremultiply = 22,
    kFaceColor = 23,
    kSpecularWeight = 24,
    kSpecular = 25,
    kIsZero = 26,
    kComposite = 27,
    kApplyRingShadow = 28,
    kHighlightFillColor = 29,
    kApplyKeyFillHighlight = 30,
    kFade = 31,
    kClampHeadroom = 32,
    kSpecularHeight = 33,
};

// Runs one stage over `in` and returns four floats per record.
std::vector<float> onGpu(Device& d, const std::vector<Probe>& in, std::uint32_t stage) {
    if (in.empty()) return {};
    const std::size_t floats = in.size() * kStride;
    auto input = Buffer::create(d, floats * sizeof(float),
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!input) return {};
    std::memcpy(input->mapped(), in.data(), floats * sizeof(float));

    auto out = Buffer::create(d, in.size() * 4 * sizeof(float),
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!out) return {};
    std::memset(out->mapped(), 0, in.size() * 4 * sizeof(float));

    auto pass = ComputePass::create(d, kBgSpirv, sizeof kBgSpirv, sizeof(Control));
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

float ulpSizeAt(float magnitude) {
    const float m = std::fabs(magnitude);
    return std::nextafter(m, std::numeric_limits<float>::infinity()) - m;
}

// The band's allowance, carried unchanged from `test_rb_glass.cpp`: Vulkan
// specifies `sqrt` to 3 ULP, the root produces a value in [0, 1], and the result
// scales that by `amount` -- so the bound is absolute, measured where the error
// is made, and proportional to `amount`.
constexpr std::uint32_t kSqrtUlps = 3;
bool bandAgrees(float got, float want, float amount) {
    if (sameBits(got, want)) return true;
    const float bound = static_cast<float>(kSqrtUlps) * ulpSizeAt(1.0f) * std::fabs(amount);
    return std::fabs(got - want) <= bound;
}

// `OpFDiv` is 2.5 ULP in the Vulkan precision table, as a RELATIVE error at the
// quotient. `divisions` is how many are on the path, and two more ULP cover the
// exact multiplies and adds around them. Legitimate only where nothing
// downstream cancels; where something does, the site derives its own absolute
// bound instead -- see `ringShadowBound`.
constexpr float kUlpRelative = 5.9604645e-8f;   // 2^-24
constexpr float kDivRelative = 2.5f * kUlpRelative;

int gDivisionExact = 0;
int gDivisionSlack = 0;

bool divAgrees(float got, float want, int divisions) {
    if (sameBits(got, want)) {
        ++gDivisionExact;
        return true;
    }
    const float bound = static_cast<float>(divisions) * kDivRelative * std::fabs(want) +
                        2.0f * ulpSizeAt(want);
    const bool ok = std::fabs(got - want) <= bound;
    if (ok) ++gDivisionSlack;
    return ok;
}

// The same accounting, for a bound the call site derived itself.
bool absAgrees(float got, float want, float bound) {
    if (sameBits(got, want)) {
        ++gDivisionExact;
        return true;
    }
    const bool ok = std::fabs(got - want) <= bound;
    if (ok) ++gDivisionSlack;
    return ok;
}

// The maximum slope of `edgeCoverageInSigma` -- what turns an error in its
// ARGUMENT into an error in its VALUE. It is `kInvSqrt2 * |c3|`, because the
// polynomial's derivative `c3 + 3*c2*u^2 + 5*c1*u^4 + 7*c0*u^6` peaks at u = 0
// where every even term vanishes. `[ART]` The ring shadow's own test sweeps the
// curve and confirms it numerically rather than taking it on the algebra.
const float kEdgeSlope = glass::kInvSqrt2 * (-glass::kEdgeC3);

// `[BIN]` mod98 %277..%308. The only inexact operation is the reciprocal at
// %278; everything after it is multiplies, adds and a polynomial -- except the
// SUBTRACTION at %302, which is where an upstream relative error becomes a large
// relative error in a small difference.
//
// Every term of this derivation is the operation's, not a fitted number:
//   * `inv` carries at most `kDivRelative` relative error, so `a = d2 * inv` and
//     `b = sw * inv + a` carry at most `2 * kDivRelative` of their own magnitude
//     -- the divide's, plus one rounding of the multiply and the add;
//   * the edge curve turns an argument error into a value error through its
//     slope;
//   * the band is `c0 - c1`, so the two value errors add;
//   * the result scales the band by `sdfAlpha2 * opacity * m`.
float ringShadowBound(float d2, float sdfAlpha2, float maskValue, float opacity,
                      float ringMask, float blurRadius, float strokeWidth, float want) {
    const float inv = 1.0f / std::fmax(blurRadius, glass::kEpsilon);
    const float a = d2 * inv;
    const float b = strokeWidth * inv + a;
    const float argError = 2.0f * kDivRelative * (std::fabs(a) + std::fabs(b));
    const float bandError = kEdgeSlope * argError;
    const float m = 1.0f + (maskValue - 1.0f) * ringMask;
    return std::fabs(sdfAlpha2 * opacity * m) * bandError + 4.0f * ulpSizeAt(want);
}

// A deterministic spread that visits both signs, both sides of every guard, and
// the exact boundaries. Not random: a differential that only fails one time in
// twenty is not a gate.
float spread(int i, float lo, float hi, int n) {
    return lo + (hi - lo) * static_cast<float>(i) / static_cast<float>(n - 1);
}

const float kIdentity[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
const float kMixer[3][4] = {{0.6f, 0.25f, 0.15f, 0.05f},
                            {0.2f, 0.7f, 0.1f, -0.03f},
                            {0.1f, 0.2f, 0.7f, 0.02f}};

}  // namespace

// ===========================================================================
// 1. the uniform byte map
// ===========================================================================

// `[BIN]` The 75 fields of `RB::Shader::Glass::BackgroundUniforms` and the byte
// of each, from mod98's TBAA node `!40`. This is what makes the names in
// `GlassBackground.h` checkable: a member inserted, removed or reordered moves
// the packed float index and this fails, rather than the whole map sliding by
// one and every uniform quietly meaning something else.
TEST_CASE(the_uniform_mirror_is_106_packed_floats_at_the_measured_bytes) {
    CHECK_EQ(sizeof(Uniforms), kUniformSlots * sizeof(float));

    // The byte table itself: strictly increasing, starting at 0, ending at 254,
    // and every entry either a 4-byte float slot or a 2-byte half slot.
    CHECK_EQ(int(kUniformByte[0]), 0);
    CHECK_EQ(int(kUniformByte[kUniformSlots - 1]), 254);
    bool increasing = true;
    for (std::size_t i = 1; i < kUniformSlots; ++i) {
        if (!(kUniformByte[i] > kUniformByte[i - 1])) increasing = false;
    }
    CHECK(increasing);

    // `[BIN]` The 76 bytes of the three colour matrices are @80..@151, and the
    // mirror puts exactly 36 floats there -- nine `[4 x half]` rows.
    int inMatrices = 0;
    for (std::size_t i = 0; i < kUniformSlots; ++i) {
        if (kUniformByte[i] >= 80 && kUniformByte[i] < 152) ++inMatrices;
    }
    CHECK_EQ(inMatrices, 36);

    // And the load-bearing members land on the bytes the IR reads them from.
    // Each of these is a value number in `GlassBackground.h`; a wrong one here
    // would be a uniform doing another uniform's job.
    const std::size_t base = offsetof(Uniforms, fieldScale);
    auto byteOf = [&](std::size_t off) {
        return int(kUniformByte[(off - base) / sizeof(float)]);
    };
    CHECK_EQ(byteOf(offsetof(Uniforms, fieldScale)), 0);
    CHECK_EQ(byteOf(offsetof(Uniforms, gradientScale)), 8);
    CHECK_EQ(byteOf(offsetof(Uniforms, innerRefractionAmount)), 16);
    CHECK_EQ(byteOf(offsetof(Uniforms, outerRefractionAmount)), 24);
    CHECK_EQ(byteOf(offsetof(Uniforms, refractionDistance0)), 32);
    CHECK_EQ(byteOf(offsetof(Uniforms, blurRadius)), 40);
    CHECK_EQ(byteOf(offsetof(Uniforms, bleedAmount)), 48);
    CHECK_EQ(byteOf(offsetof(Uniforms, shadowAmount)), 56);
    CHECK_EQ(byteOf(offsetof(Uniforms, shadowOffsetX)), 64);
    CHECK_EQ(byteOf(offsetof(Uniforms, shadowBlurRadius)), 72);
    CHECK_EQ(byteOf(offsetof(Uniforms, shadowRadius)), 76);
    CHECK_EQ(byteOf(offsetof(Uniforms, faceMatrix)), 80);
    CHECK_EQ(byteOf(offsetof(Uniforms, specularMatrix)), 104);
    CHECK_EQ(byteOf(offsetof(Uniforms, shadowMatrix)), 128);
    CHECK_EQ(byteOf(offsetof(Uniforms, shadowVibrancy)), 152);
    CHECK_EQ(byteOf(offsetof(Uniforms, shadowMatrixAlpha)), 156);
    CHECK_EQ(byteOf(offsetof(Uniforms, blurOpacity0)), 160);
    CHECK_EQ(byteOf(offsetof(Uniforms, blurDistance0)), 168);
    CHECK_EQ(byteOf(offsetof(Uniforms, faceOpacity)), 182);
    CHECK_EQ(byteOf(offsetof(Uniforms, shadowDistanceOffset)), 188);
    CHECK_EQ(byteOf(offsetof(Uniforms, refractionOpacity)), 192);
    CHECK_EQ(byteOf(offsetof(Uniforms, clampCeiling)), 200);
    CHECK_EQ(byteOf(offsetof(Uniforms, faceMatrixMaxLuma)), 204);
    CHECK_EQ(byteOf(offsetof(Uniforms, aberrationAmount)), 208);
    CHECK_EQ(byteOf(offsetof(Uniforms, ringShadowOffsetY)), 218);
    CHECK_EQ(byteOf(offsetof(Uniforms, ringShadowOpacity)), 224);
    CHECK_EQ(byteOf(offsetof(Uniforms, highlightHeight)), 228);
    CHECK_EQ(byteOf(offsetof(Uniforms, blurFillLighten)), 244);
    CHECK_EQ(byteOf(offsetof(Uniforms, faceMatrixClampMode)), 254);

    // `[BIN]` And the one hole: field #48, byte 202, is the only field of the
    // seventy-five that mod98 never reads. Resolving every `getelementptr` in
    // the module finds 74. The spec says "every byte from 0 to 255 is read";
    // this is where that is one field too generous, and the slot is named
    // `unreadAt202` so nobody wires a document value into it by accident.
    CHECK_EQ(byteOf(offsetof(Uniforms, unreadAt202)), 202);

    // `[BIN]` The three variant bits are distinct powers of two: `and 1`,
    // `and 2`, `and 4` at %128, %166 and %168.
    CHECK_EQ(int(kVariantRing), 1);
    CHECK_EQ(int(kVariantInnerRefraction), 2);
    CHECK_EQ(int(kVariantBackdrop), 4);
}

// ===========================================================================
// 2. blocks 1 and 2 -- the decode, the coverage, the two sample points
// ===========================================================================

TEST_CASE(block1_decode_coverage_and_the_sample_points_match_the_gpu) {
    Device& d = gpu();
    if (!d.valid()) return;

    std::vector<Probe> dist, gradient, cover, msk, sp, rsp;
    for (int i = 0; i < 41; ++i) {
        const float raw = spread(i, -1.0f, 1.0f, 41);
        Probe p{};
        p.a(0) = raw;
        p.a(1) = 240.0f;
        p.a(2) = -120.0f;
        dist.push_back(p);

        Probe g{};
        g.a(0) = raw;
        g.a(1) = -raw * 0.5f;
        g.a(2) = 2.0f;
        g.a(3) = -1.0f;
        gradient.push_back(g);

        // The coverage's guarded divisor: below, at and above the epsilon.
        Probe c{};
        c.a(0) = spread(i, -3.0f, 3.0f, 41);
        c.a(1) = (i % 3 == 0) ? 0.0f : ((i % 3 == 1) ? glass::kEpsilon : 0.25f);
        cover.push_back(c);

        Probe m{};
        m.a(0) = spread(i, 0.0f, 1.0f, 41);
        m.a(1) = spread(40 - i, 0.0f, 1.0f, 41);
        msk.push_back(m);

        Probe s{};
        s.a(0) = raw * 17.0f;
        s.a(1) = raw * -13.0f;
        s.a(2) = 3.5f;
        s.a(3) = -2.25f;
        sp.push_back(s);
        rsp.push_back(s);
    }

    const auto gd = onGpu(d, dist, kDecodeDistance);
    const auto gg = onGpu(d, gradient, kDecodeGradient);
    const auto gc = onGpu(d, cover, kCoverage);
    const auto gm = onGpu(d, msk, kMask);
    const auto gs = onGpu(d, sp, kShadowPoint);
    const auto gr = onGpu(d, rsp, kRingShadowPoint);
    REQUIRE(gd.size() == dist.size() * 4);

    int bad = 0;
    for (std::size_t i = 0; i < dist.size(); ++i) {
        if (!sameBits(gd[i * 4], decodeDistance(dist[i].f[0], dist[i].f[1], dist[i].f[2])))
            ++bad;

        float g[2];
        const float raw[2] = {gradient[i].f[0], gradient[i].f[1]};
        decodeGradient(raw, gradient[i].f[2], gradient[i].f[3], g);
        if (!sameBits(gg[i * 4], g[0]) || !sameBits(gg[i * 4 + 1], g[1])) ++bad;

        // One division, and the only one in the block.
        if (!divAgrees(gc[i * 4], coverage(cover[i].f[0], cover[i].f[1]), 1)) ++bad;
        if (!sameBits(gm[i * 4], mask(msk[i].f[0], msk[i].f[1]))) ++bad;

        float pt[2];
        const float p[2] = {sp[i].f[0], sp[i].f[1]};
        shadowSamplePoint(p, sp[i].f[2], sp[i].f[3], pt);
        if (!sameBits(gs[i * 4], pt[0]) || !sameBits(gs[i * 4 + 1], pt[1])) ++bad;

        ringShadowSamplePoint(p, rsp[i].f[2], pt);
        if (!sameBits(gr[i * 4], pt[0]) || !sameBits(gr[i * 4 + 1], pt[1])) ++bad;
    }
    CHECK_EQ(bad, 0);

    // `[BIN]` %92 -- the guarded divisor is `fmax(fwidth, 0xH1419)`, and 0xH1419
    // is 0.001, not the 1e-4 AquaKit reads on the QuartzCore side. That is the
    // divergence the spec names first, and it is a visible edge rather than a
    // bit: at a zero derivative the two epsilons put the coverage ramp a hundred
    // times apart. Stated here so a port that "tidied" the constant fails.
    CHECK(sameBits(coverage(0.0f, 0.0f), 0.5f));
    const float steep = coverage(-2.0e-4f, 0.0f);
    const float withAquaKitEpsilon = 0.5f + 2.0e-4f / 1.0e-4f;   // would saturate to 1
    CHECK(steep < 1.0f);
    CHECK(withAquaKitEpsilon >= 1.0f);
    CHECK(std::fabs(steep - (0.5f + 2.0e-4f / glass::kEpsilon)) < 1e-6f);

    // `[BIN]` %139/%140 -- the ring shadow's X offset is a LITERAL zero in the
    // `insertelement`, so a non-zero X in the uniform pair must not move it.
    const float p[2] = {5.0f, 7.0f};
    float pt[2];
    ringShadowSamplePoint(p, 3.0f, pt);
    CHECK(sameBits(pt[0], 5.0f));
    CHECK(sameBits(pt[1], 4.0f));
}

// ===========================================================================
// 3. block 3 -- the key-fill highlight, and all four of its early-outs
// ===========================================================================

TEST_CASE(block3_key_fill_highlight_matches_the_gpu_on_both_placements) {
    Device& d = gpu();
    if (!d.valid()) return;

    std::vector<Probe> in;
    for (int placement = 0; placement < 2; ++placement) {
        for (int i = 0; i < 24; ++i) {
            for (int j = 0; j < 3; ++j) {
                Probe p{};
                p.a(0) = spread(i, -6.0f, 2.0f, 24);        // d
                p.a(1) = 0.25f + 0.1f * float(j);           // fwidthMax
                p.a(2) = (i % 5 == 0) ? 0.0f : 1.0f;        // sdfAlpha, zero included
                p.a(3) = spread(j, 0.0f, 1.0f, 3);          // mask, 1.0 included
                p.b(0) = 0.6f;                              // grad.x
                p.b(1) = -0.8f;                             // grad.y
                p.b(2) = 2.0f;                              // height
                p.b(3) = -1.5f;                             // effectOffset
                p.c(0) = float(placement);                  // placement
                p.c(1) = 0.3f;                              // edge
                p.c(2) = 0.9f;                              // projX
                p.c(3) = 0.4f;                              // projY
                p.e(0) = 1.75f;                             // spread
                in.push_back(p);
            }
        }
    }
    // `[BIN]` %203..%205 -- the indicator at the heart of the softening factor is
    // a CONVERSION of a boolean, so it is 0 or 1 with nothing between, and the
    // mix at %206 pulls the soft value three quarters of the way toward it. That
    // makes the block DISCONTINUOUS at `saturate(-s / height) == 1`, and there is
    // exactly one place a sweep can see it: everywhere else the indicator is 1
    // and a plain `1` would agree. A mutation replacing it with the constant
    // SURVIVED the grid above, which is how this case came to be here.
    //
    // Three records: the exact crossing, and one step of the grid either side.
    for (int k = -1; k <= 1; ++k) {
        Probe p{};
        p.a(0) = -2.0f + 0.125f * float(k);   // s = d + effectOffset = -2 at k = 0
        p.a(1) = 0.5f;
        p.a(2) = 1.0f;
        p.a(3) = 0.5f;
        p.b(0) = 0.6f;
        p.b(1) = -0.8f;
        p.b(2) = 2.0f;                        // height, so -s / height reaches 1
        p.b(3) = 0.0f;
        p.c(0) = 0.0f;
        p.c(1) = 0.3f;
        p.c(2) = 0.9f;
        p.c(3) = 0.4f;
        p.e(0) = 1.75f;
        in.push_back(p);
    }

    // The height gate: zero and negative must both return zero.
    for (float h : {0.0f, -1.0f}) {
        Probe p{};
        p.a(1) = 0.5f;
        p.a(2) = 1.0f;
        p.b(2) = h;
        in.push_back(p);
    }

    const auto g = onGpu(d, in, kKeyFillHighlight);
    REQUIRE(g.size() == in.size() * 4);

    int bad = 0;
    int nonZero = 0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        Probe p = in[i];
        const float grad[2] = {p.f[4], p.f[5]};
        const float want = keyFillHighlight(p.f[0], p.f[1], p.f[2], p.f[3], grad, p.f[6],
                                            p.f[7], p.f[8], p.f[9], p.f[10], p.f[11],
                                            p.f[12]);
        // Three divisions on the deepest path: the reciprocal of the width, the
        // one inside the softening factor, and the two falloffs.
        if (!divAgrees(g[i * 4], want, 4)) ++bad;
        if (want != 0.0f) ++nonZero;
    }
    CHECK_EQ(bad, 0);
    // The sweep has to actually reach the interesting side of the guards.
    CHECK(nonZero > 20);

    // THE FOUR EARLY-OUTS, each driven on its own. `[BIN]` %177, %191, %192,
    // %195 -- and the fourth is `sdfAlpha == 0`, which is a comparison against
    // an exact zero, not against the epsilon.
    const float grad[2] = {1.0f, 0.0f};
    CHECK(keyFillHighlight(-2.0f, 0.5f, 1.0f, 0.5f, grad, 0.0f, 0.0f, 0.0f, 0.3f, 1.0f,
                           0.0f, 1.0f) == 0.0f);                       // height gate
    CHECK(keyFillHighlight(10.0f, 0.5f, 1.0f, 0.5f, grad, 2.0f, 0.0f, 0.0f, 0.3f, 1.0f,
                           0.0f, 1.0f) == 0.0f);                       // %191, s >= halfW
    CHECK(keyFillHighlight(-2.0f, 0.5f, 0.0f, 0.5f, grad, 2.0f, 0.0f, 0.0f, 0.3f, 1.0f,
                           0.0f, 1.0f) == 0.0f);                       // %195, alpha zero
    // `[BIN]` %190 with `placement > 0`: a saturated mask kills the block.
    CHECK(keyFillHighlight(-2.0f, 0.5f, 1.0f, 1.0f, grad, 2.0f, 0.0f, 1.0f, 0.3f, 1.0f,
                           0.0f, 1.0f) == 0.0f);
    // and the crossing itself, stated as a claim rather than left to the sweep:
    // at `-s == height` the softening factor collapses to ZERO, and a hair
    // either side it does not. `[BIN]` %205 is `air.convert.f.f16.s.i32` of a
    // zero-extended `i1`; nothing else in mod98 has that shape.
    const float atCrossing = keyFillHighlight(-2.0f, 0.5f, 1.0f, 0.5f, grad, 2.0f, 0.0f,
                                              0.0f, 0.3f, 0.9f, 0.4f, 1.75f);
    const float justInside = keyFillHighlight(-1.875f, 0.5f, 1.0f, 0.5f, grad, 2.0f, 0.0f,
                                              0.0f, 0.3f, 0.9f, 0.4f, 1.75f);
    CHECK(atCrossing == 0.0f);
    CHECK(justInside != 0.0f);

    // and with `placement <= 0` a saturated mask is NOT fatal -- which is what
    // makes the placement a fork and not a scale. The distance is -1 rather than
    // -2 because at -2 the softening factor at %199..%206 collapses to zero on
    // its own, and the case would then prove nothing about the fork.
    CHECK(keyFillHighlight(-1.0f, 0.5f, 1.0f, 1.0f, grad, 2.0f, 0.0f, 1.0f, 0.3f, 1.0f,
                           0.0f, 1.0f) == 0.0f);
    CHECK(keyFillHighlight(-1.0f, 0.5f, 1.0f, 1.0f, grad, 2.0f, 0.0f, 0.0f, 0.3f, 1.0f,
                           0.0f, 1.0f) != 0.0f);
}

// ===========================================================================
// 4. block 4 -- the ring shadow and its two gates
// ===========================================================================

TEST_CASE(block4_ring_shadow_matches_the_gpu_and_both_gates_bite) {
    Device& d = gpu();
    if (!d.valid()) return;

    std::vector<Probe> in;
    for (int i = 0; i < 30; ++i) {
        for (int j = 0; j < 4; ++j) {
            Probe p{};
            p.a(0) = spread(i, -8.0f, 8.0f, 30);      // d2
            p.a(1) = 0.25f + 0.25f * float(j);        // sdfAlpha2
            p.a(2) = spread(j, 0.0f, 1.0f, 4);        // mask
            p.a(3) = 0.8f;                            // opacity
            p.b(0) = spread(j, 0.0f, 1.5f, 4);        // ringMask, crossing 1.0
            p.b(1) = (j == 0) ? 0.0f : 1.5f;          // blurRadius, zero included
            p.b(2) = 2.25f;                           // strokeWidth
            in.push_back(p);
        }
    }
    const auto g = onGpu(d, in, kRingShadow);
    REQUIRE(g.size() == in.size() * 4);

    int bad = 0;
    int nonZero = 0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        const Probe& p = in[i];
        const float want = ringShadow(p.f[0], p.f[1], p.f[2], p.f[3], p.f[4], p.f[5],
                                      p.f[6]);
        const float bound = ringShadowBound(p.f[0], p.f[1], p.f[2], p.f[3], p.f[4],
                                            p.f[5], p.f[6], want);
        if (!absAgrees(g[i * 4], want, bound)) ++bad;
        if (want != 0.0f) ++nonZero;
    }
    CHECK_EQ(bad, 0);
    CHECK(nonZero > 20);

    // `[BIN]` %266 -- the opacity gate, and it is `ogt`, so exactly zero is out.
    CHECK(ringShadow(-1.0f, 1.0f, 0.5f, 0.0f, 0.5f, 1.0f, 1.0f) == 0.0f);
    // `[BIN]` %268..%272 -- the second gate is an OR: a zero mask alone does not
    // kill it if the ring mask is below one, and that is the arrangement a
    // careless reading would turn into an AND.
    CHECK(ringShadow(-1.0f, 1.0f, 0.0f, 0.8f, 1.0f, 1.0f, 1.0f) == 0.0f);
    CHECK(ringShadow(-1.0f, 1.0f, 0.0f, 0.8f, 0.5f, 1.0f, 1.0f) != 0.0f);
    CHECK(ringShadow(-1.0f, 1.0f, 0.5f, 0.8f, 1.0f, 1.0f, 1.0f) != 0.0f);

    // `[BIN]` %302/%303 -- the band is a DIFFERENCE of two edge evaluations, so
    // a zero stroke width makes the two identical and the band exactly zero.
    CHECK(ringShadow(-1.0f, 1.0f, 0.5f, 0.8f, 0.5f, 1.0f, 0.0f) == 0.0f);

    // `[ART]` And the slope the bound above rests on, measured rather than
    // assumed: nowhere does `edgeCoverageInSigma` change faster than
    // `kInvSqrt2 * |c3|`. If it did, the allowance would be too tight in one
    // place and too loose in another, and this is what says which.
    // First the algebra it rests on: `|P'|` really does peak at u = 0.
    double worstAnalytic = 0.0;
    for (int i = 0; i <= 200000; ++i) {
        const double u = -2.0 + 4.0 * i / 200000.0;
        const double u2 = u * u;
        const double dp = glass::kEdgeC3 + 3.0 * glass::kEdgeC2 * u2 +
                          5.0 * glass::kEdgeC1 * u2 * u2 +
                          7.0 * glass::kEdgeC0 * u2 * u2 * u2;
        worstAnalytic = std::fmax(worstAnalytic, std::fabs(dp));
    }
    CHECK(std::fabs(worstAnalytic - std::fabs(double(glass::kEdgeC3))) < 1e-9);

    // Then the transcription itself, by difference quotient. It overshoots, and
    // by exactly the amount a difference quotient must: the two samples are
    // float32, so each carries up to half an ULP, and dividing by `h` scales
    // that to `ulp(1) / h`. The bound below is the slope plus that, and nothing
    // else -- which is why the printed number sits just above `kEdgeSlope`
    // rather than being waved through by a round tolerance.
    const double h = 1e-3;
    double worstSlope = 0.0;
    for (int i = 0; i <= 200000; ++i) {
        const double x = -4.0 + 8.0 * i / 200000.0;
        const double s0 = glass::edgeCoverageInSigma(static_cast<float>(x));
        const double s1 = glass::edgeCoverageInSigma(static_cast<float>(x + h));
        worstSlope = std::fmax(worstSlope, std::fabs((s1 - s0) / h));
    }
    const double quotientError = double(ulpSizeAt(1.0f)) / h;
    std::printf("  [ART] max |d/dx edgeCoverageInSigma| = %.6f (bound %.6f + %.6f of "
                "difference-quotient error)\n",
                worstSlope, double(kEdgeSlope), quotientError);
    CHECK(worstSlope <= double(kEdgeSlope) + quotientError);
    CHECK(worstSlope > double(kEdgeSlope) * 0.99);
}

// ===========================================================================
// 5. block 5 -- the shadow lobe
// ===========================================================================

TEST_CASE(block5_shadow_lobe_matches_the_gpu_sampled_and_unsampled) {
    Device& d = gpu();
    if (!d.valid()) return;

    std::vector<Probe> height, cover, colour, premul;
    for (int i = 0; i < 33; ++i) {
        Probe h{};
        h.a(0) = spread(i, -6.0f, 3.0f, 33);   // d
        h.a(1) = 200.0f;                       // amount
        h.a(2) = 1.0f / 250.0f;                // invHeight, already a reciprocal
        h.a(3) = -50.0f;                       // distanceOffset
        height.push_back(h);

        Probe c{};
        c.a(0) = spread(i, -20.0f, 20.0f, 33);
        c.a(1) = spread(i, 0.0f, 1.0f, 33);
        c.a(2) = 1.0f / 25.0f;
        c.a(3) = 0.75f;
        cover.push_back(c);

        Probe m{};
        m.a(0) = spread(i, 0.0f, 1.0f, 33);
        m.a(1) = spread(32 - i, 0.0f, 1.0f, 33);
        m.a(2) = 0.5f;
        m.a(3) = (i % 2 == 0) ? 1.0f : 0.0f;   // sampled
        m.b(0) = spread(i, 0.0f, 1.0f, 33);    // vibrancy
        m.b(1) = 0.4f;                         // matrixAlpha
        m.matrix(kMixer);
        colour.push_back(m);

        Probe q{};
        q.a(0) = spread(i, -1.0f, 1.0f, 33);
        q.a(1) = 0.3f;
        q.a(2) = -0.2f;
        q.a(3) = 0.9f;
        q.b(0) = spread(i, 0.0f, 2.0f, 33);
        premul.push_back(q);
    }

    const auto gh = onGpu(d, height, kShadowHeight);
    const auto gc = onGpu(d, cover, kShadowCoverage);
    const auto gm = onGpu(d, colour, kShadowColor);
    const auto gq = onGpu(d, premul, kShadowPremultiplied);
    REQUIRE(gh.size() == height.size() * 4);

    int bad = 0;
    for (std::size_t i = 0; i < height.size(); ++i) {
        const float want = shadowHeight(height[i].f[0], height[i].f[1], height[i].f[2],
                                        height[i].f[3]);
        if (!bandAgrees(gh[i * 4], want, height[i].f[1])) ++bad;

        if (!sameBits(gc[i * 4], shadowCoverage(cover[i].f[0], cover[i].f[1],
                                                cover[i].f[2], cover[i].f[3])))
            ++bad;

        float col[4];
        const float bd[3] = {colour[i].f[0], colour[i].f[1], colour[i].f[2]};
        shadowColor(bd, colour[i].f[3] != 0.0f, colour[i].f[4], kMixer, colour[i].f[5],
                    col);
        for (int k = 0; k < 4; ++k)
            if (!sameBits(gm[i * 4 + k], col[k])) ++bad;

        float pre[4];
        shadowPremultiplied(premul[i].f, premul[i].f[4], pre);
        for (int k = 0; k < 4; ++k)
            if (!sameBits(gq[i * 4 + k], pre[k])) ++bad;
    }
    CHECK_EQ(bad, 0);

    // `[BIN]` %452..%464 -- the UNSAMPLED colour is the three matrix BIASES and
    // the matrix alpha, and nothing else. This pins the fall-through that the
    // `shaderVariant & 4` clear takes, without needing a GPU or a texture.
    float col[3 + 1];
    const float any[3] = {0.9f, 0.2f, 0.7f};
    shadowColor(any, false, 0.5f, kMixer, 0.4f, col);
    CHECK(sameBits(col[0], kMixer[0][3]));
    CHECK(sameBits(col[1], kMixer[1][3]));
    CHECK(sameBits(col[2], kMixer[2][3]));
    CHECK(sameBits(col[3], 0.4f));

    // `[BIN]` %434 then %444 -- vibrancy scales the matrix BEFORE the bias is
    // added, so vibrancy zero has to give the bias exactly, and NOT
    // `bias * 0`. A transcription that biased first would give zero here.
    shadowColor(any, true, 0.0f, kMixer, 0.4f, col);
    CHECK(sameBits(col[0], kMixer[0][3]));
    CHECK(sameBits(col[3], 0.4f));
}

// ===========================================================================
// 6. block 6 -- the inner refraction and the three-segment blur ramp
// ===========================================================================

TEST_CASE(block6_inner_refraction_and_the_blur_ramp_match_the_gpu) {
    Device& d = gpu();
    if (!d.valid()) return;

    const float distances[4] = {-450.0f, -3.0f, 4.0f, 9.0f};
    const float opacities[3] = {0.1f, 0.25f, 0.4f};

    std::vector<Probe> inner, ramp, flat, refr;
    for (int i = 0; i < 37; ++i) {
        Probe h{};
        h.a(0) = spread(i, -80.0f, 20.0f, 37);
        h.a(1) = -150.0f;
        h.a(2) = 1.0f / 60.0f;
        inner.push_back(h);

        Probe r{};
        for (int k = 0; k < 4; ++k) r.a(k) = distances[k];
        for (int k = 0; k < 3; ++k) r.b(k) = opacities[k];
        r.b(3) = 1.0f;                                  // opacity0
        r.c(0) = 30.0f;                                 // scale
        r.c(1) = spread(i, -500.0f, 20.0f, 37);         // x
        ramp.push_back(r);

        // A SECOND family with the four distances OUT OF ORDER, and it is here
        // for a reason a sweep would not have found on its own.
        //
        // `[BIN]` %538/%540 associate the three products as `p.z + (p.x + p.y)`,
        // and this file transcribes that. With the distances SORTED that
        // association is unobservable: the three ramps are consecutive
        // intervals, so at most one of them is ever strictly between zero and
        // one and the other two contribute an exact zero or an exact opacity --
        // and a sum with two exact terms reassociates without moving a bit. A
        // mutation that summed left to right SURVIVED the sorted sweep.
        //
        // Nothing in mod98 or in doc 03 §27.4 says the four `inputBlurDistance`
        // keys arrive sorted, so unsorted spans are a legal input and are what
        // makes the association a claim rather than a decoration. Here all three
        // ramps are fractional at once.
        Probe u{};
        u.a(0) = 0.0f;
        u.a(1) = 1.0f;
        u.a(2) = -1.0f;
        u.a(3) = 2.0f;
        u.b(0) = 0.1f;
        u.b(1) = 0.30000001f;
        u.b(2) = 0.70000011f;
        u.b(3) = 1.0f;
        u.c(0) = 30.0f;
        u.c(1) = spread(i, -0.5f, 1.5f, 37);
        ramp.push_back(u);

        // And a THIRD family, built to make the association itself observable.
        //
        // Reassociating a float sum only moves a bit when the magnitudes are
        // spread far enough that a term is lost in one grouping and kept in the
        // other. Two terms of 4e-8 and one of 1: `(4e-8 + 4e-8) + 1` keeps
        // 8e-8, which is above half an ULP of 1 and rounds up, while
        // `4e-8 + (4e-8 + 1)` loses each one separately and returns 1 exactly.
        // Every ramp is saturated here, so the three products are the three
        // opacities and nothing else is in play.
        Probe w{};
        w.a(0) = 0.0f;
        w.a(1) = 1.0f;
        w.a(2) = 2.0f;
        w.a(3) = 3.0f;
        w.b(0) = 4.0e-8f;
        w.b(1) = 4.0e-8f;
        w.b(2) = 1.0f;
        w.b(3) = 0.0f;              // opacity0, so the result is just -sum
        w.c(0) = 1.0f;              // scale
        w.c(1) = 10.0f + float(i);  // above the last distance: every ramp is 1
        ramp.push_back(w);

        Probe f{};
        f.a(0) = spread(i, 0.0f, 1.0f, 37);
        f.a(1) = 30.0f;
        flat.push_back(f);

        Probe q{};
        q.a(0) = spread(i, -3.0f, 3.0f, 37);
        q.a(1) = spread(36 - i, -3.0f, 3.0f, 37);
        q.a(2) = 0.6f;
        q.a(3) = -0.8f;
        q.b(0) = spread(i, -40.0f, 40.0f, 37);
        refr.push_back(q);
    }

    const auto gi = onGpu(d, inner, kInnerHeight);
    const auto gr = onGpu(d, ramp, kBlurRadiusRamp);
    const auto gf = onGpu(d, flat, kFlatBlurRadius);
    const auto gq = onGpu(d, refr, kRefractPoint);
    REQUIRE(gi.size() == inner.size() * 4);

    int bad = 0;
    for (std::size_t i = 0; i < inner.size(); ++i) {
        if (!bandAgrees(gi[i * 4],
                        innerRefractionHeight(inner[i].f[0], inner[i].f[1], inner[i].f[2]),
                        inner[i].f[1]))
            ++bad;
        // handled below, over the whole (interleaved) ramp vector
        if (!sameBits(gf[i * 4], flatBlurRadius(flat[i].f[0], flat[i].f[1]))) ++bad;

        float pt[2];
        const float p[2] = {refr[i].f[0], refr[i].f[1]};
        const float g[2] = {refr[i].f[2], refr[i].f[3]};
        refractPoint(p, g, refr[i].f[4], pt);
        if (!sameBits(gq[i * 4], pt[0]) || !sameBits(gq[i * 4 + 1], pt[1])) ++bad;
    }
    // The ramp carries two families and each record names its own distances and
    // opacities, so it is walked separately. Three reciprocals and three biases
    // are six divisions on the path.
    for (std::size_t i = 0; i < ramp.size(); ++i) {
        const float dists[4] = {ramp[i].f[0], ramp[i].f[1], ramp[i].f[2], ramp[i].f[3]};
        const float ops[3] = {ramp[i].f[4], ramp[i].f[5], ramp[i].f[6]};
        const float want = blurRadiusRamp(ramp[i].f[9], dists, ops, ramp[i].f[7],
                                          ramp[i].f[8]);
        if (!divAgrees(gr[i * 4], want, 6)) ++bad;
    }
    CHECK_EQ(bad, 0);

    // THE RAMP AT ITS KNOTS, where the closed form is exact and there is no
    // second transcription in the loop. `[BIN]` The value at or below distance0
    // is `opacity0 * scale`; at or above distance3 every segment is saturated,
    // so it is `(opacity0 - sum(opacities)) * scale`.
    const float below = blurRadiusRamp(-1000.0f, distances, opacities, 1.0f, 30.0f);
    CHECK(sameBits(below, 30.0f * 1.0f));
    const float above = blurRadiusRamp(1000.0f, distances, opacities, 1.0f, 30.0f);
    const float wantAbove = 30.0f * (1.0f - ((0.1f + 0.25f) + 0.4f));
    CHECK(std::fabs(above - wantAbove) < 1e-6f);

    // THE SUM'S ASSOCIATION, AND WHY IT IS PINNED HERE AND NOT BY THE
    // DIFFERENTIAL. `[BIN]` %538 then %540: the products are summed as
    // `p.z + (p.x + p.y)`, not left to right. Reassociating a float sum moves at
    // most one ULP, and one ULP is INSIDE any allowance a stage with six
    // divisions can honestly carry -- so a differential cannot see it, and a
    // mutation to the other grouping survived the sweep above twice before this
    // check existed.
    //
    // What CAN see it is a closed form with no tolerance at all. Two products of
    // 4e-8 and one of 1: `(4e-8 + 4e-8) + 1` keeps 8e-8, which is above half an
    // ULP of one and rounds up, while `4e-8 + (4e-8 + 1)` loses each term
    // separately and returns one exactly. Every ramp is saturated at x = 10, so
    // the three products are the three opacities and nothing else is in play.
    //
    // This gates the ORACLE, not the GPU. The arrangement is spelled the same
    // way in `GlassBackground.glsl`, and the differential above holds the two
    // together within the divisions' bound; what this adds is that the
    // arrangement itself is the target's and cannot be tidied.
    const float assocDist[4] = {0.0f, 1.0f, 2.0f, 3.0f};
    const float assocOps[3] = {4.0e-8f, 4.0e-8f, 1.0f};
    const float assoc = blurRadiusRamp(10.0f, assocDist, assocOps, 0.0f, 1.0f);
    CHECK(sameBits(assoc, -std::nextafterf(1.0f, 2.0f)));
    CHECK(!sameBits(assoc, -1.0f));

    // `[BIN]` %841..%846 -- with the ramp bypassed the radius is exactly
    // `opacity0 * scale`, which is the ramp's own value below distance0. The two
    // paths agree at that end and only there; that is the check that the flat
    // path is the SAME quantity and not a second, unrelated one.
    CHECK(sameBits(flatBlurRadius(1.0f, 30.0f), below));
}

// ===========================================================================
// 7. block 7 -- the dispersion's band and its rotated step
// ===========================================================================

TEST_CASE(block7_aberration_height_and_step_match_the_gpu) {
    Device& d = gpu();
    if (!d.valid()) return;

    std::vector<Probe> height, step;
    for (int i = 0; i < 33; ++i) {
        Probe h{};
        h.a(0) = spread(i, -40.0f, 10.0f, 33);
        h.a(1) = 1.0f;                          // amount
        h.a(2) = 1.0f / 20.0f;                  // invHeight
        h.a(3) = 1.0f;                          // offset
        height.push_back(h);

        const float th = spread(i, 0.0f, 6.28318f, 33);
        Probe s{};
        s.a(0) = std::cos(th * 0.5f);
        s.a(1) = std::sin(th * 0.5f);
        s.a(2) = spread(i, -2.0f, 2.0f, 33);    // height
        s.a(3) = std::cos(th);                  // cos
        s.b(0) = std::sin(th);                  // sin
        step.push_back(s);
    }
    const auto gh = onGpu(d, height, kAberrationHeight);
    const auto gs = onGpu(d, step, kAberrationStep);
    REQUIRE(gh.size() == height.size() * 4);

    int bad = 0;
    for (std::size_t i = 0; i < height.size(); ++i) {
        if (!bandAgrees(gh[i * 4],
                        aberrationHeight(height[i].f[0], height[i].f[1], height[i].f[2],
                                         height[i].f[3]),
                        height[i].f[1]))
            ++bad;
        float out[2];
        const float g[2] = {step[i].f[0], step[i].f[1]};
        aberrationStep(g, step[i].f[2], step[i].f[3], step[i].f[4], out);
        if (!sameBits(gs[i * 4], out[0]) || !sameBits(gs[i * 4 + 1], out[1])) ++bad;
    }
    CHECK_EQ(bad, 0);

    // `[BIN]` %604 is `<%587, %583>` -- the two projections land SWAPPED. At an
    // angle of zero the rotation is the identity, so a step that did NOT swap
    // would return `(gx, gy) * height` and this returns `(gy, gx) * height`.
    const float g[2] = {3.0f, -5.0f};
    float out[2];
    aberrationStep(g, 2.0f, 1.0f, 0.0f, out);
    CHECK(sameBits(out[0], -10.0f));
    CHECK(sameBits(out[1], 6.0f));

    // And the seven taps are `GlassOracle`'s, not a second copy: their weights
    // still sum to one per channel after the channel scale, which is the
    // invariant `test_rb_glass.cpp` pins and which this block depends on.
    float sum[3] = {0, 0, 0};
    for (int t = 0; t < glass::kAberrationTaps; ++t) {
        const glass::AberrationTap tap = glass::aberrationTap(t);
        for (int k = 0; k < 3; ++k) sum[k] += tap.weight[k];
    }
    CHECK(std::fabs(sum[0] * glass::kAberrationRgbScale[0] - 1.0f) < 1e-6f);
    CHECK(std::fabs(sum[2] * glass::kAberrationRgbScale[2] - 1.0f) < 1e-6f);
}

// ===========================================================================
// 8. block 8 -- the blur fill
// ===========================================================================

TEST_CASE(block8_blur_fill_matches_the_gpu_and_darken_is_on_the_min) {
    Device& d = gpu();
    if (!d.valid()) return;

    std::vector<Probe> in;
    for (int i = 0; i < 25; ++i) {
        for (int j = 0; j < 3; ++j) {
            Probe p{};
            p.a(0) = spread(i, 0.0f, 1.0f, 25);
            p.a(1) = spread(24 - i, 0.0f, 1.0f, 25);
            p.a(2) = 0.5f;
            p.b(0) = spread(24 - i, 0.0f, 1.0f, 25);
            p.b(1) = spread(i, 0.0f, 1.0f, 25);
            p.b(2) = 0.25f;
            p.c(0) = spread(j, 0.0f, 4.0f, 3);     // lighten
            p.c(1) = 0.3f;                         // darken
            p.c(2) = spread(j, 0.0f, 1.0f, 3);     // normal
            in.push_back(p);
        }
    }
    const auto g = onGpu(d, in, kBlurFill);
    REQUIRE(g.size() == in.size() * 4);

    int bad = 0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        const Probe& p = in[i];
        const float face[3] = {p.f[0], p.f[1], p.f[2]};
        const float back[3] = {p.f[4], p.f[5], p.f[6]};
        float out[3];
        blurFill(face, back, p.f[8], p.f[9], p.f[10], out);
        for (int k = 0; k < 3; ++k)
            if (!sameBits(g[i * 4 + k], out[k])) ++bad;
    }
    CHECK_EQ(bad, 0);

    // `[BIN]` %755/%756 -- LIGHTEN multiplies the max and DARKEN the min, which
    // is the opposite of the spec's block list. Pinned by construction: with
    // darken alone the result has to be the MINIMUM of the two, and with lighten
    // alone the MAXIMUM. If the two were swapped both of these would invert.
    const float face[3] = {0.8f, 0.2f, 0.5f};
    const float back[3] = {0.1f, 0.9f, 0.5f};
    float out[3];
    blurFill(face, back, 0.0f, 1.0f, 0.0f, out);
    CHECK(sameBits(out[0], 0.1f));
    CHECK(sameBits(out[1], 0.2f));
    blurFill(face, back, 1.0f, 0.0f, 0.0f, out);
    CHECK(sameBits(out[0], 0.8f));
    CHECK(sameBits(out[1], 0.9f));
    // `[BIN]` %764 -- normal at one replaces everything with the backdrop. This
    // is the one claim in the file carrying an allowance with no division behind
    // it: `air.mix` is `x + (y - x) * a`, and at `a == 1` that is `y` in algebra
    // but two roundings in floating point. The bound is those two roundings at
    // the magnitude of the operands, and nothing else.
    blurFill(face, back, 0.37f, 0.11f, 1.0f, out);
    for (int k = 0; k < 3; ++k)
        CHECK(std::fabs(out[k] - back[k]) <= 2.0f * ulpSizeAt(1.0f));
    // and all three at zero is the identity on the face.
    blurFill(face, back, 0.0f, 0.0f, 0.0f, out);
    for (int k = 0; k < 3; ++k) CHECK(sameBits(out[k], face[k]));
}

// ===========================================================================
// 9. block 9 -- the outer refraction
// ===========================================================================

TEST_CASE(block9_outer_refraction_matches_the_gpu) {
    Device& d = gpu();
    if (!d.valid()) return;

    std::vector<Probe> height, weight, mixIn;
    for (int i = 0; i < 33; ++i) {
        Probe h{};
        h.a(0) = spread(i, -70.0f, 20.0f, 33);
        h.a(1) = 100.0f;
        h.a(2) = 1.0f / 50.0f;
        height.push_back(h);

        Probe w{};
        w.a(0) = spread(i, -20.0f, 10.0f, 33);
        w.a(1) = -11.0f;
        w.a(2) = -3.0f;
        w.a(3) = 0.75f;
        weight.push_back(w);

        Probe m{};
        for (int k = 0; k < 4; ++k) m.a(k) = spread(i + k, -1.0f, 1.0f, 40);
        for (int k = 0; k < 4; ++k) m.b(k) = spread(33 - i + k, -1.0f, 1.0f, 40);
        m.c(0) = spread(i, 0.0f, 1.0f, 33);
        mixIn.push_back(m);
    }
    const auto gh = onGpu(d, height, kOuterHeight);
    const auto gw = onGpu(d, weight, kOuterWeight);
    const auto gm = onGpu(d, mixIn, kOuterMix);
    REQUIRE(gh.size() == height.size() * 4);

    int bad = 0;
    for (std::size_t i = 0; i < height.size(); ++i) {
        if (!bandAgrees(gh[i * 4],
                        outerRefractionHeight(height[i].f[0], height[i].f[1],
                                              height[i].f[2]),
                        height[i].f[1]))
            ++bad;
        if (!divAgrees(gw[i * 4],
                       outerRefractionWeight(weight[i].f[0], weight[i].f[1],
                                             weight[i].f[2], weight[i].f[3]), 2))
            ++bad;
        float out[4];
        outerRefractionMix(mixIn[i].f, mixIn[i].f + 4, mixIn[i].f[8], out);
        for (int k = 0; k < 4; ++k)
            if (!sameBits(gm[i * 4 + k], out[k])) ++bad;
    }
    CHECK_EQ(bad, 0);

    // `[BIN]` %838 is `air.mix.v4f16` -- FOUR channels. Alpha moves too, and a
    // three-channel mix would leave it behind.
    const float face[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    const float outer[4] = {0.9f, 0.8f, 0.7f, 0.6f};
    float out[4];
    outerRefractionMix(face, outer, 1.0f, out);
    for (int k = 0; k < 4; ++k) CHECK(sameBits(out[k], outer[k]));
    outerRefractionMix(face, outer, 0.0f, out);
    for (int k = 0; k < 4; ++k) CHECK(sameBits(out[k], face[k]));
}

// ===========================================================================
// 10. block 10 -- the un-premultiply and the face colour matrix
// ===========================================================================

TEST_CASE(block10_face_colour_matches_the_gpu_and_mixes_from_the_uncompressed) {
    Device& d = gpu();
    if (!d.valid()) return;

    std::vector<Probe> unpre, face;
    for (int i = 0; i < 33; ++i) {
        Probe u{};
        u.a(0) = spread(i, -0.5f, 1.5f, 33);
        u.a(1) = spread(32 - i, -0.5f, 1.5f, 33);
        u.a(2) = 0.4f;
        // Alpha crosses the epsilon and reaches exact zero, which is the only
        // place the guard changes the answer.
        u.a(3) = (i % 4 == 0) ? 0.0f : spread(i, 0.0f, 1.0f, 33);
        unpre.push_back(u);

        Probe f{};
        f.a(0) = spread(i, 0.0f, 2.0f, 33);
        f.a(1) = spread(32 - i, 0.0f, 2.0f, 33);
        f.a(2) = 0.55f;
        f.a(3) = spread(i, 0.0f, 1.0f, 33);      // faceOpacity, zero included
        f.b(0) = (i % 3 == 0) ? 0.0f : spread(i, 0.0f, 1.5f, 33);  // maxLuma
        f.matrix(kMixer);
        face.push_back(f);
    }
    const auto gu = onGpu(d, unpre, kUnpremultiply);
    const auto gf = onGpu(d, face, kFaceColor);
    REQUIRE(gu.size() == unpre.size() * 4);

    int bad = 0;
    for (std::size_t i = 0; i < unpre.size(); ++i) {
        float out[4];
        unpremultiply(unpre[i].f, out);
        for (int k = 0; k < 4; ++k)
            if (!divAgrees(gu[i * 4 + k], out[k], 1)) ++bad;

        float fc[3];
        const float c[3] = {face[i].f[0], face[i].f[1], face[i].f[2]};
        faceColor(c, face[i].f[3], face[i].f[4], kMixer, fc);
        for (int k = 0; k < 3; ++k)
            if (!sameBits(gf[i * 4 + k], fc[k])) ++bad;
    }
    CHECK_EQ(bad, 0);

    // `[BIN]` %868 -- the alpha the un-premultiply returns is a LITERAL one, not
    // the alpha it divided by.
    const float rgba[4] = {0.5f, 0.25f, 0.125f, 0.5f};
    float out[4];
    unpremultiply(rgba, out);
    CHECK(sameBits(out[0], 1.0f));
    CHECK(sameBits(out[3], 1.0f));

    // `[BIN]` %871 -- face opacity zero returns the input untouched.
    const float c[3] = {0.7f, 0.3f, 0.9f};
    float fc[3];
    faceColor(c, 0.0f, 1.0f, kMixer, fc);
    for (int k = 0; k < 3; ++k) CHECK(sameBits(fc[k], c[k]));

    // THE ARRANGEMENT A PLAUSIBLE TRANSCRIPTION LOSES. `[BIN]` %946 interpolates
    // from `%866` -- the UNCOMPRESSED colour -- while only the matrix sees the
    // compressed one. So with the IDENTITY matrix and a biting `maxLuma`, the
    // result has to be the compressed colour and NOT the input: mixing from the
    // compressed value would still give the compressed colour, but at opacity
    // one-half the two arrangements part, and that is what this pins.
    float compressed[3];
    glass::compressMaxLuma(c, 1.0f, compressed);
    CHECK(!sameBits(compressed[0], c[0]));   // the compression has to bite
    faceColor(c, 0.5f, 1.0f, kIdentity, fc);
    for (int k = 0; k < 3; ++k) {
        const float wantMixFromRaw = c[k] + (compressed[k] - c[k]) * 0.5f;
        CHECK(sameBits(fc[k], wantMixFromRaw));
    }
}

// ===========================================================================
// 11. block 11 -- the specular
// ===========================================================================

TEST_CASE(block11_specular_matches_the_gpu_and_the_weight_is_a_fourth_power) {
    Device& d = gpu();
    if (!d.valid()) return;

    std::vector<Probe> weight, apply, height;
    for (int i = 0; i < 33; ++i) {
        Probe w{};
        w.a(0) = spread(i, -500.0f, 20.0f, 33);   // d
        w.b(0) = spread(i, 0.0f, 1.2f, 33);
        w.b(1) = spread(32 - i, 0.0f, 1.2f, 33);
        w.b(2) = 0.3f;
        w.c(0) = -400.0f;                         // distance0
        w.c(1) = -42.0f;                          // distance1
        w.c(2) = 0.2f;                            // opacity
        w.c(3) = 1.5f;                            // weightScale
        w.e(0) = -0.25f;                          // weightBias
        weight.push_back(w);

        Probe a{};
        for (int k = 0; k < 4; ++k) a.a(k) = spread(i + k, -0.5f, 1.5f, 40);
        for (int k = 0; k < 3; ++k) a.b(k) = spread(33 - i + k, 0.0f, 1.0f, 40);
        a.c(0) = spread(i, 0.0f, 1.0f, 33);
        a.matrix(kMixer);
        apply.push_back(a);

        Probe h{};
        h.a(0) = spread(i, -900.0f, 50.0f, 33);
        h.a(1) = 400.0f;
        h.a(2) = 1.0f / 500.0f;
        height.push_back(h);
    }
    const auto gw = onGpu(d, weight, kSpecularWeight);
    const auto ga = onGpu(d, apply, kSpecular);
    const auto gh = onGpu(d, height, kSpecularHeight);
    REQUIRE(gw.size() == weight.size() * 4);

    int bad = 0;
    for (std::size_t i = 0; i < weight.size(); ++i) {
        const Probe& w = weight[i];
        const float rgb[3] = {w.f[4], w.f[5], w.f[6]};
        if (!divAgrees(gw[i * 4],
                       specularWeight(w.f[0], rgb, w.f[8], w.f[9], w.f[10], w.f[11],
                                      w.f[12]), 2))
            ++bad;

        float out[4];
        const float bd[3] = {apply[i].f[4], apply[i].f[5], apply[i].f[6]};
        specular(apply[i].f, bd, apply[i].f[8], kMixer, out);
        for (int k = 0; k < 4; ++k)
            if (!sameBits(ga[i * 4 + k], out[k])) ++bad;

        if (!bandAgrees(gh[i * 4],
                        specularHeight(height[i].f[0], height[i].f[1], height[i].f[2]),
                        height[i].f[1]))
            ++bad;
    }
    CHECK_EQ(bad, 0);

    // `[BIN]` %1070/%1071 -- the weight is squared TWICE. With the ramp fully
    // saturated and opacity one, the result is exactly `(scale*luma + bias)^4`,
    // and a second power or a third would miss.
    const float grey[3] = {1.0f, 1.0f, 1.0f};
    const float w4 = specularWeight(0.0f, grey, -400.0f, -42.0f, 1.0f, 0.5f, 0.0f);
    const float y = (glass::kLumaWeight[0] * 1.0f + glass::kLumaWeight[1] * 1.0f) +
                    glass::kLumaWeight[2] * 1.0f;
    const float q = 0.5f * (y < 1.0f ? y : 1.0f);
    CHECK(std::fabs(w4 - q * q * q * q) < 1e-7f);

    // `[BIN]` %1062 uses `kLumaWeight`, NOT `kLumaCompress`. The two differ in
    // red and in blue by one half ULP; a pure-red input is where that shows.
    const float red[3] = {1.0f, 0.0f, 0.0f};
    const float wr = specularWeight(0.0f, red, -400.0f, -42.0f, 1.0f, 1.0f, 0.0f);
    const float withWeight = glass::kLumaWeight[0];
    const float withCompress = glass::kLumaCompress[0];
    CHECK(!sameBits(withWeight, withCompress));
    CHECK(sameBits(wr, withWeight * withWeight * withWeight * withWeight));

    // `[BIN]` %1077 -- alpha is carried through, not mixed.
    const float face[4] = {0.1f, 0.2f, 0.3f, 0.44f};
    const float bd[3] = {1.0f, 1.0f, 1.0f};
    float out[4];
    specular(face, bd, 1.0f, kMixer, out);
    CHECK(sameBits(out[3], 0.44f));
}

// ===========================================================================
// 12. block 12 -- composite, ring shadow, key-fill highlight, epilogue
// ===========================================================================

TEST_CASE(block12_composite_and_the_epilogue_match_the_gpu) {
    Device& d = gpu();
    if (!d.valid()) return;

    std::vector<Probe> zero, comp, ring, fill, hl, fd, cl;
    for (int i = 0; i < 33; ++i) {
        Probe z{};
        z.a(0) = (i % 4 == 0) ? 0.0f : spread(i, 0.0f, 1.0f, 33);
        z.a(1) = (i % 3 == 0) ? 0.0f : glass::kEpsilon * float(i);
        z.a(2) = (i % 5 == 0) ? 0.0f : glass::kEpsilon * float(i);
        z.a(3) = (i % 7 == 0) ? 0.0f : glass::kEpsilon * float(i);
        zero.push_back(z);

        Probe c{};
        for (int k = 0; k < 4; ++k) c.a(k) = spread(i + k, -1.0f, 1.0f, 40);
        for (int k = 0; k < 4; ++k) c.b(k) = spread(33 - i + k, -1.0f, 1.0f, 40);
        c.c(0) = spread(i, 0.0f, 1.0f, 33);
        comp.push_back(c);

        Probe r{};
        for (int k = 0; k < 4; ++k) r.a(k) = spread(i + k, 0.0f, 1.0f, 40);
        r.b(0) = spread(i, 0.0f, 1.0f, 33);
        ring.push_back(r);

        Probe f{};
        f.a(0) = spread(i, 0.0f, 1.2f, 33);
        f.a(1) = spread(32 - i, 0.0f, 1.2f, 33);
        f.a(2) = 0.35f;
        f.a(3) = spread(i, 0.0f, 1.0f, 33);        // highlight
        f.b(0) = (i % 3 == 0) ? 0.0f : 0.8f;       // faceOpacity, zero included
        f.b(1) = (i % 4 == 0) ? 0.0f : 1.0f;       // maxLuma, zero included
        f.b(2) = float(i % 2);                     // clampMode, both
        f.b(3) = (i % 2 == 0) ? -0.5f : 0.5f;      // colorBias, both signs
        f.matrix(kMixer);
        fill.push_back(f);

        Probe k{};
        for (int q = 0; q < 3; ++q) k.a(q) = spread(i + q, 0.0f, 1.0f, 40);
        // Alpha visits below one, exactly one and above one.
        k.a(3) = (i % 3 == 0) ? 1.0f : ((i % 3 == 1) ? 0.4f : 1.5f);
        for (int q = 0; q < 3; ++q) k.b(q) = spread(33 - i + q, 0.0f, 1.0f, 40);
        // The highlight visits below the epsilon, at it, and above.
        k.b(3) = (i % 4 == 0) ? 0.0f : ((i % 4 == 1) ? glass::kEpsilon : 0.6f);
        k.c(0) = 0.8f;
        k.c(1) = 1.0f;
        k.c(2) = float(i % 2);
        k.c(3) = (i % 2 == 0) ? -0.5f : 0.5f;
        k.e(0) = 0.9f;
        k.matrix(kMixer);
        hl.push_back(k);

        Probe v{};
        for (int q = 0; q < 3; ++q) v.a(q) = spread(i + q, -0.2f, 1.2f, 40);
        v.a(3) = (i % 5 == 0) ? 0.0f : spread(i, 0.0f, 1.4f, 33);
        // `[BIN]` %1251..%1256 -- `d` has to stay inside the SDR ramp or `s` is
        // zero and the whole fade is an identity, which would leave %1268's
        // SATURATED alpha (as against the raw one it divides by) unobservable
        // at every alpha above one. Sweeping `d` over the ramp and the alpha
        // past one INDEPENDENTLY is what makes that saturate a claim.
        v.b(0) = spread(i, -6.0f, -2.0f, 33);     // d, inside the ramp throughout
        v.b(1) = (i % 3 == 0) ? 0.0f : 1.2f;      // headroom, zero included
        v.b(2) = -2.5f;
        v.b(3) = 1.0f / 1.0f;
        v.c(0) = 0.97f;
        fd.push_back(v);

        Probe q{};
        for (int k2 = 0; k2 < 3; ++k2) q.a(k2) = spread(i + k2, -3.0f, 4.0f, 40);
        q.a(3) = (i % 5 == 0) ? 0.0f : spread(i, 0.0f, 1.4f, 33);
        q.b(0) = (i % 3 == 0) ? 0.0f : 1.2f;      // ceiling, zero included
        cl.push_back(q);
    }

    const auto gz = onGpu(d, zero, kIsZero);
    const auto gc = onGpu(d, comp, kComposite);
    const auto gr = onGpu(d, ring, kApplyRingShadow);
    const auto gf = onGpu(d, fill, kHighlightFillColor);
    const auto gh = onGpu(d, hl, kApplyKeyFillHighlight);
    const auto gd2 = onGpu(d, fd, kFade);
    const auto gl = onGpu(d, cl, kClampHeadroom);
    REQUIRE(gz.size() == zero.size() * 4);

    int bad = 0;
    for (std::size_t i = 0; i < zero.size(); ++i) {
        const float wantZero =
            isZero(zero[i].f[0], zero[i].f[1], zero[i].f[2], zero[i].f[3]) ? 1.0f : 0.0f;
        if (!sameBits(gz[i * 4], wantZero)) ++bad;

        float out[4];
        composite(comp[i].f, comp[i].f + 4, comp[i].f[8], out);
        for (int k = 0; k < 4; ++k)
            if (!sameBits(gc[i * 4 + k], out[k])) ++bad;

        applyRingShadow(ring[i].f, ring[i].f[4], out);
        for (int k = 0; k < 4; ++k)
            if (!sameBits(gr[i * 4 + k], out[k])) ++bad;

        float fc[3];
        const float bd[3] = {fill[i].f[0], fill[i].f[1], fill[i].f[2]};
        highlightFillColor(bd, fill[i].f[3], fill[i].f[4], fill[i].f[5], kMixer,
                           fill[i].f[6], fill[i].f[7], fc);
        for (int k = 0; k < 3; ++k)
            if (!sameBits(gf[i * 4 + k], fc[k])) ++bad;

        const float hbd[3] = {hl[i].f[4], hl[i].f[5], hl[i].f[6]};
        applyKeyFillHighlight(hl[i].f, hl[i].f[7], hbd, hl[i].f[8], hl[i].f[9], kMixer,
                              hl[i].f[10], hl[i].f[11], hl[i].f[12], out);
        for (int k = 0; k < 4; ++k)
            if (!sameBits(gh[i * 4 + k], out[k])) ++bad;

        fade(fd[i].f, fd[i].f[4], fd[i].f[5], fd[i].f[6], fd[i].f[7], fd[i].f[8], out);
        for (int k = 0; k < 4; ++k)
            if (!divAgrees(gd2[i * 4 + k], out[k], 1)) ++bad;

        clampHeadroom(cl[i].f, cl[i].f[4], out);
        for (int k = 0; k < 4; ++k)
            if (!divAgrees(gl[i * 4 + k], out[k], 1)) ++bad;
    }
    CHECK_EQ(bad, 0);

    // `[BIN]` %355..%361 -- the short-circuit is a CONJUNCTION of four, and the
    // mask term is an equality with zero while the other three are `< epsilon`.
    CHECK(isZero(0.0f, 0.0f, 0.0f, 0.0f));
    CHECK(!isZero(1e-9f, 0.0f, 0.0f, 0.0f));               // mask is `== 0`, not `< eps`
    CHECK(!isZero(0.0f, glass::kEpsilon, 0.0f, 0.0f));     // `olt`, so equality fails
    CHECK(!isZero(0.0f, 0.0f, glass::kEpsilon, 0.0f));
    CHECK(!isZero(0.0f, 0.0f, 0.0f, glass::kEpsilon));
    const float justUnder = std::nextafter(glass::kEpsilon, 0.0f);
    CHECK(isZero(0.0f, justUnder, justUnder, justUnder));

    // `[BIN]` %1084 -- the ring shadow lands in ALPHA ONLY. At one, the colour
    // goes to zero and the alpha to one; a four-channel add would put the value
    // in the colour too.
    const float col[4] = {0.5f, 0.6f, 0.7f, 0.25f};
    float out[4];
    applyRingShadow(col, 1.0f, out);
    CHECK(sameBits(out[0], 0.0f));
    CHECK(sameBits(out[1], 0.0f));
    CHECK(sameBits(out[2], 0.0f));
    CHECK(sameBits(out[3], 1.0f));

    // `[BIN]` %1089 is `ogt` -- a highlight EXACTLY at the epsilon does not
    // enter the block, and one a single float above it does.
    const float bd[3] = {0.2f, 0.4f, 0.6f};
    applyKeyFillHighlight(col, glass::kEpsilon, bd, 0.8f, 1.0f, kMixer, 0.0f, 0.5f, 0.9f,
                          out);
    for (int k = 0; k < 4; ++k) CHECK(sameBits(out[k], col[k]));
    const float justOver = std::nextafter(glass::kEpsilon,
                                          std::numeric_limits<float>::infinity());
    applyKeyFillHighlight(col, justOver, bd, 0.8f, 1.0f, kMixer, 0.0f, 0.5f, 0.9f, out);
    CHECK(!sameBits(out[3], col[3]));

    // `[BIN]` %1194/%1199 -- the clamp is gated by one uniform and its DIRECTION
    // chosen by the sign of another. With the identity matrix the clamp is a
    // no-op either way, so the mixer is used and the two directions must bracket
    // the unclamped answer.
    float lo[3], hi[3], none[3];
    highlightFillColor(bd, 1.0f, 1.0f, 0.0f, kMixer, 1.0f, -1.0f, lo);
    highlightFillColor(bd, 1.0f, 1.0f, 0.0f, kMixer, 1.0f, 1.0f, hi);
    highlightFillColor(bd, 1.0f, 1.0f, 0.0f, kMixer, 0.0f, 1.0f, none);
    int bracketed = 0;
    for (int k = 0; k < 3; ++k) {
        if (lo[k] <= none[k] + 1e-6f && hi[k] >= none[k] - 1e-6f) ++bracketed;
        if (!sameBits(lo[k], hi[k])) {
            // where the clamp bites, the two directions have to differ
            CHECK(lo[k] < hi[k] || lo[k] > hi[k]);
        }
    }
    CHECK_EQ(bracketed, 3);
    // and the clamp mode off has to differ from at least one of the two, or the
    // gate would be doing nothing on this input and the check would be vacuous.
    CHECK(!sameBits(none[0], lo[0]) || !sameBits(none[0], hi[0]));

    // `[BIN]` %1246 / %1283 -- both epilogue gates are `ogt`, so zero skips.
    const float src[4] = {2.0f, -3.0f, 0.5f, 0.5f};
    fade(src, 0.0f, 0.0f, -2.5f, 1.0f, 0.97f, out);
    for (int k = 0; k < 4; ++k) CHECK(sameBits(out[k], src[k]));
    clampHeadroom(src, 0.0f, out);
    for (int k = 0; k < 4; ++k) CHECK(sameBits(out[k], src[k]));

    // `[BIN]` %1293 -- the floor is `0xHBA00`, exactly -0.75, on the
    // UN-PREMULTIPLIED colour, and the ceiling is the uniform. With alpha at one
    // the un-premultiply is the identity and both ends are readable directly.
    const float wide[4] = {5.0f, -9.0f, 0.25f, 1.0f};
    clampHeadroom(wide, 1.2f, out);
    CHECK(sameBits(out[0], 1.2f));
    CHECK(sameBits(out[1], -0.75f));
    CHECK(sameBits(out[2], 0.25f));
    CHECK(sameBits(out[3], 1.0f));

    // `[BIN]` %1294/%1295 -- the re-premultiply uses the RAW alpha, and the
    // divide used the FLOORED one. Below the epsilon the two are different
    // numbers, and this is the only place that shows: with alpha zero the
    // result must be exactly zero, not the clamped colour.
    const float faint[4] = {5.0f, -9.0f, 0.25f, 0.0f};
    clampHeadroom(faint, 1.2f, out);
    CHECK(sameBits(out[0], 0.0f));
    CHECK(sameBits(out[1], -0.0f));
    CHECK(sameBits(out[3], 0.0f));

    std::printf("  [ART] division-bearing comparisons: %d bit-identical, %d within the "
                "2.5-ULP OpFDiv allowance\n",
                gDivisionExact, gDivisionSlack);
}

// ===========================================================================
// 13. the assembly -- the variant bits, the early-outs, the short-circuit
// ===========================================================================
//
// This is the only place mod98's CONTROL FLOW is exercised. It is CPU-only, for
// the reason `GlassBackground.h` gives: assembling the function means taking
// fifteen texture fetches, and a GPU twin would be measuring a sampler rather
// than a transcription. The arithmetic already ran on the GPU, block by block,
// above.

namespace {

// A field that reports a constant texel, so what varies between cases is the
// control flow and not the input. `[OBS]` This is NOT the target's SDF -- it is
// a stub whose only job is to be deterministic.
struct FlatField : glassbg::Field {
    float texel[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    mutable int calls = 0;
    void sample(const float[2], float out[4]) const override {
        ++calls;
        for (int i = 0; i < 4; ++i) out[i] = texel[i];
    }
};

// A backdrop that records what it was asked for. The radius log is what makes
// the LOD-bearing branches visible without a mip chain.
//
// `[OBS]` This is NOT the target's sampler -- no layer transform, no mip chain,
// no filtering. What it has to be is DEPENDENT on the point and the radius:
// a stub returning one constant would make several of mod98's branches
// indistinguishable from each other, and a guard that cannot be told from its
// alternative is not being tested. The modulation is bounded and deterministic.
struct RecordingBackdrop : glassbg::Backdrop {
    float texel[4] = {0.4f, 0.5f, 0.6f, 1.0f};
    mutable std::vector<float> radii;
    mutable std::vector<float> xs;
    void sample(const float p[2], float radius, float out[4]) const override {
        radii.push_back(radius);
        xs.push_back(p[0]);
        const float k = 1.0f + 0.25f * std::sin(p[0] + 2.0f * p[1] + 0.5f * radius);
        for (int i = 0; i < 4; ++i) out[i] = texel[i] * k;
    }
};

Uniforms lively() {
    Uniforms u;
    // The defaults doc 03 §27.4 read, with every `...Height` already reciprocal
    // because the packer divides (§27.2). `[INF]` The names are the doc's, laid
    // against this struct through the +16 shift `GlassBackground.h` derives.
    u.fieldScale = 240.0f;
    u.fieldBias = -120.0f;
    u.gradientScale = 2.0f;
    u.gradientBias = -1.0f;
    u.innerRefractionAmount = -150.0f;
    u.innerRefractionInvHeight = 1.0f / 60.0f;
    u.outerRefractionAmount = 100.0f;
    u.outerRefractionInvHeight = 1.0f / 50.0f;
    u.refractionDistance0 = -11.0f;
    u.refractionDistance1 = -3.0f;
    u.blurRadius = 30.0f;
    u.bleedBlurRadius = 100.0f;
    u.bleedAmount = 400.0f;
    u.bleedInvHeight = 1.0f / 500.0f;
    u.shadowAmount = 200.0f;
    u.shadowInvHeight = 1.0f / 250.0f;
    u.shadowOffsetX = 0.0f;
    u.shadowOffsetY = 2.0f;
    u.shadowBlurRadius = 25.0f;
    u.shadowRadius = 1.0f / 25.0f;
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 4; ++k) {
            u.faceMatrix[r][k] = kMixer[r][k];
            u.specularMatrix[r][k] = kMixer[r][k];
            u.shadowMatrix[r][k] = kMixer[r][k];
        }
    u.shadowVibrancy = 1.0f;
    u.shadowMatrixAlpha = 0.5f;
    u.blurOpacity0 = 1.0f;
    u.blurOpacity1 = 0.1f;
    u.blurOpacity2 = 0.2f;
    u.blurOpacity3 = 0.4f;
    u.blurDistance0 = -450.0f;
    u.blurDistance1 = -3.0f;
    u.blurDistance2 = 4.0f;
    u.blurDistance3 = 9.0f;
    u.bleedDistance0 = -400.0f;
    u.bleedDistance1 = -42.0f;
    u.bleedOpacity = 0.2f;
    u.faceOpacity = 1.0f;
    u.bleedDarkenScale = 1.0f;
    u.bleedDarkenBias = 0.0f;
    u.shadowDistanceOffset = -50.0f;
    u.shadowOpacity = 1.0f;
    u.refractionOpacity = 0.75f;
    u.maxHeadroom = 1.2f;
    u.sdrGradientDistance0 = -2.5f;
    u.sdrGradientInvSpan = 1.0f;
    u.clampCeiling = 1.2f;
    u.faceMatrixMaxLuma = 1.0f;
    u.sdrHoldingToneWhite = 0.97f;
    u.aberrationAmount = 1.0f;
    u.aberrationInvHeight = 1.0f / 20.0f;
    u.aberrationOffset = 1.0f;
    u.aberrationCos = 1.0f;
    u.aberrationSin = 0.0f;
    u.ringShadowOffsetY = 1.0f;
    u.ringShadowStrokeWidth = 2.0f;
    u.ringShadowBlurRadius = 1.5f;
    u.ringShadowOpacity = 0.8f;
    u.ringShadowMask = 0.5f;
    // `[BIN]` %188/%189 -- the highlight's own guard compares `halfWidth + height`
    // against `-(effectOffset + d)`, so a height of 2 with the doc's -2 offset
    // silently skips the block at every distance this file probes. Six is chosen
    // to clear that guard; doc 03 §27.4 reads this key as *runtime*, with no
    // default to contradict.
    u.highlightHeight = 6.0f;
    u.highlightProjX = 1.0f;
    u.highlightProjY = 0.0f;
    u.highlightEdge = 0.3f;
    u.highlightSpread = 1.75f;
    u.highlightEffectOffset = -2.0f;
    u.highlightColorBias = 0.5f;
    u.blurFillBlurRadius = 1.0f;
    u.blurFillLighten = 0.25f;
    u.blurFillDarken = 0.3f;
    u.blurFillNormal = 0.1f;
    u.highlightPlacement = 0.0f;
    u.highlightAlphaGain = 0.9f;
    u.faceMatrixClampMode = 0.0f;
    return u;
}

// A point where the mask is strictly between zero and one, so both the shadow
// lobe and the face run. The field texel is chosen for that, not the point.
FlatField partialField() {
    FlatField f;
    // `d = 0.5 * 240 - 120 = 0`, so `coverage = 0.5` with any fwidth, and
    // `mask = 0.75 * 0.5`.
    f.texel[0] = 0.5f;
    f.texel[1] = 0.75f;   // gradient x, decoded to 0.5
    f.texel[2] = 0.25f;   // gradient y, decoded to -0.5
    f.texel[3] = 0.75f;
    return f;
}

// A point just OUTSIDE the shape: the mask is exactly zero, so the face is
// skipped, but the shadow's own coverage is not -- which is what separates
// "outside" from "dark everywhere" and therefore from the short-circuit.
FlatField outsideField() {
    FlatField f;
    f.texel[0] = 121.0f / 240.0f;   // d = +1
    f.texel[1] = 0.75f;
    f.texel[2] = 0.25f;
    f.texel[3] = 1.0f;
    return f;
}

// A point inside the SDR fade's ramp. `[BIN]` %1251..%1255: the fade is
// `saturate((d - gradientDistance0) * invSpan)` with the doc's -2.5 and a span of
// one, so it is fully off for any `d` above -1.5. A fixture that never puts `d`
// in that window cannot tell the fade's guard from the fade doing nothing.
FlatField fadingField() {
    FlatField f;
    f.texel[0] = 118.0f / 240.0f;   // d = -2
    f.texel[1] = 0.75f;
    f.texel[2] = 0.25f;
    f.texel[3] = 0.75f;
    return f;
}

}  // namespace

TEST_CASE(the_assembly_short_circuits_to_zero_when_all_four_terms_are_dark) {
    // `[BIN]` %355..%361 -> %470 -> %1298. With the mask exactly zero and the
    // shadow, the highlight and the ring shadow all below the epsilon, the whole
    // function returns `zeroinitializer` -- so even a lively clamp ceiling and a
    // lively fade cannot put anything back.
    Uniforms u = lively();
    u.shadowOpacity = 0.0f;      // kills the shadow coverage
    u.ringShadowOpacity = 0.0f;  // kills the ring shadow AND its re-sample
    u.highlightHeight = 0.0f;    // kills the highlight

    // Just outside, with a live SDF alpha: the mask is exactly zero and the only
    // thing keeping the shadow term dark is the opacity knocked out above. That
    // is what lets the lift below be one uniform and not a different pixel.
    FlatField f = outsideField();
    RecordingBackdrop b;

    Inputs in;
    in.fwidthD = 1.0f;
    in.variant = kVariantRing | kVariantInnerRefraction | kVariantBackdrop;
    float out[4] = {9, 9, 9, 9};
    evaluate(in, u, f, b, out);
    for (int k = 0; k < 4; ++k) CHECK(sameBits(out[k], 0.0f));
    // and it short-circuits BEFORE any backdrop fetch -- the return is at %470,
    // upstream of every `air.sample_texture_2d` that reads `t0`.
    CHECK_EQ(int(b.radii.size()), 0);

    // Lift ONE of the four and the short-circuit must stop firing. That is what
    // makes this a gate on the conjunction rather than on "it returns zero".
    u.shadowOpacity = 1.0f;
    RecordingBackdrop b2;
    evaluate(in, u, f, b2, out);
    CHECK(b2.radii.size() > 0);
}

TEST_CASE(the_assembly_variant_bit_1_selects_the_flat_face_fetch) {
    // `[BIN]` %167 -- when bit 1 is CLEAR the face is ONE fetch at `p` with the
    // flat radius, and the refraction, the dispersion and the outer refraction
    // never run. The radius log is what shows it.
    Uniforms u = lively();
    FlatField f = partialField();
    Inputs in;
    in.fwidthD = 1.0f;
    float out[4];

    RecordingBackdrop flat;
    in.variant = kVariantBackdrop;   // bit 1 clear, bit 2 set
    evaluate(in, u, f, flat, out);
    // exactly two fetches: the shadow lobe's and the flat face's -- plus the
    // specular's, which bit 2 also enables.
    CHECK_EQ(int(flat.radii.size()), 3);
    bool sawFlat = false;
    for (float r : flat.radii) {
        if (sameBits(r, flatBlurRadius(u.blurOpacity0, u.blurRadius))) sawFlat = true;
    }
    CHECK(sawFlat);

    RecordingBackdrop deep;
    in.variant = kVariantInnerRefraction | kVariantBackdrop;
    evaluate(in, u, f, deep, out);
    // With bit 1 set the dispersion's seven taps appear, plus the outer
    // refraction, plus the shadow and the specular: strictly more work.
    CHECK(deep.radii.size() > flat.radii.size());
    CHECK(deep.radii.size() >= 10u);
}

TEST_CASE(the_assembly_variant_bit_2_gates_every_backdrop_lobe) {
    // `[BIN]` %168 -- bit 2 gates the shadow's vibrancy fetch (%363), the
    // dispersion (%551) and the specular (%951). With it clear the shadow falls
    // to its matrix biases, the face takes ONE plain fetch, and the specular is
    // skipped entirely.
    Uniforms u = lively();
    FlatField f = partialField();
    Inputs in;
    in.fwidthD = 1.0f;
    in.variant = kVariantInnerRefraction;   // bit 2 clear
    float out[4];

    RecordingBackdrop b;
    evaluate(in, u, f, b, out);
    // the inner refraction's single fetch plus the outer refraction's; no seven
    // dispersion taps, no shadow vibrancy fetch, no specular fetch.
    CHECK_EQ(int(b.radii.size()), 2);

    RecordingBackdrop b2;
    in.variant = kVariantInnerRefraction | kVariantBackdrop;
    evaluate(in, u, f, b2, out);
    CHECK_EQ(int(b2.radii.size()), 7 + 1 + 1 + 1);   // taps, outer, shadow, specular
}

TEST_CASE(the_assembly_variant_bit_0_gates_the_ring_the_highlight_and_the_blur_fill) {
    // `[BIN]` %128 -- bit 0 gates the ring-shadow re-sample (%130), the
    // highlight and the ring shadow (%165), the blur fill (%711) and the whole
    // epilogue at %1083.
    Uniforms u = lively();
    FlatField f = partialField();
    Inputs in;
    in.fwidthD = 1.0f;
    float out[4];

    FlatField without = f;
    RecordingBackdrop b0;
    in.variant = kVariantInnerRefraction | kVariantBackdrop;   // bit 0 clear
    evaluate(in, u, without, b0, out);
    const int fieldCallsWithout = without.calls;

    FlatField with = f;
    RecordingBackdrop b1;
    in.variant = kVariantRing | kVariantInnerRefraction | kVariantBackdrop;
    float lit[4];
    evaluate(in, u, with, b1, lit);

    // `[BIN]` %130 -- the ring-shadow re-sample is a THIRD field fetch, and it
    // only happens under bit 0 with a positive ring opacity.
    CHECK_EQ(with.calls - fieldCallsWithout, 1);
    // and the blur fill plus the highlight's own base-level fetch are two more
    // backdrop reads.
    CHECK(b1.radii.size() > b0.radii.size());
    // The pixel has to actually change; a bit that gates nothing is not a gate.
    bool differs = false;
    for (int k = 0; k < 4; ++k)
        if (!sameBits(out[k], lit[k])) differs = true;
    CHECK(differs);

    // `[BIN]` %134 -- with the ring opacity at zero the re-sample does not
    // happen even with bit 0 set, which is the second half of that guard.
    Uniforms quiet = u;
    quiet.ringShadowOpacity = 0.0f;
    FlatField q = f;
    RecordingBackdrop bq;
    evaluate(in, quiet, q, bq, out);
    CHECK_EQ(q.calls, fieldCallsWithout);
}

TEST_CASE(the_assembly_uniform_early_outs_each_bite_on_their_own) {
    Uniforms base = lively();
    // `[BIN]` %1251 -- the fade is only observable where `d` is inside its ramp,
    // which with the doc's -2.5 and a span of one means `d` in (-2.5, -1.5).
    // That is a property of the INPUT, not of the guard, so the fixture is
    // chosen to make the guard visible rather than the guard's claim weakened.
    FlatField f = fadingField();
    Inputs in;
    in.fwidthD = 1.0f;
    in.variant = kVariantRing | kVariantInnerRefraction | kVariantBackdrop;

    RecordingBackdrop b;
    float reference[4];
    evaluate(in, base, f, b, reference);
    const std::size_t referenceFetches = b.radii.size();

    // Each of these is one `[BIN]` guard, driven alone. The claim in every case
    // is the same: turning this uniform off changes the pixel. A guard that can
    // be turned off without changing anything is not being reached, and the test
    // would be reporting on nothing.
    struct Knock {
        const char* what;
        float Uniforms::*slot;
        float off;
    };
    const Knock knocks[] = {
        {"%366 shadow vibrancy", &Uniforms::shadowVibrancy, 0.0f},
        {"%555 aberration amount", &Uniforms::aberrationAmount, 0.0f},
        {"%772 refraction opacity", &Uniforms::refractionOpacity, 0.0f},
        {"%871 face opacity", &Uniforms::faceOpacity, 0.0f},
        {"%876 face matrix max luma", &Uniforms::faceMatrixMaxLuma, 0.0f},
        {"%955 bleed opacity (specular)", &Uniforms::bleedOpacity, 0.0f},
        {"%266 ring shadow opacity", &Uniforms::ringShadowOpacity, 0.0f},
        {"%177 highlight height", &Uniforms::highlightHeight, 0.0f},
        {"%1246 max headroom (fade)", &Uniforms::maxHeadroom, 0.0f},
    };
    for (const Knock& k : knocks) {
        Uniforms u = base;
        u.*(k.slot) = k.off;
        FlatField ff = f;
        RecordingBackdrop bb;
        float out[4];
        evaluate(in, u, ff, bb, out);
        bool differs = false;
        for (int q = 0; q < 4; ++q)
            if (!sameBits(out[q], reference[q])) differs = true;
        if (!differs) std::printf("  FAIL guard did not bite: %s\n", k.what);
        CHECK(differs);
    }

    // `[BIN]` %1283 -- the clamp is left out of the sweep above and driven here
    // instead, because it is the LAST thing the function does: a ceiling low
    // enough to bite saturates every upstream difference too, and the sweep
    // would then be reporting that nine live guards are dead. Its own case gets
    // a ceiling below the colour, which is the only way the guard is visible at
    // all, and says plainly that 0.05 is a probe and not a default.
    {
        Uniforms tight = base;
        tight.clampCeiling = 0.05f;
        FlatField ff = f;
        RecordingBackdrop bb;
        float clamped[4];
        evaluate(in, tight, ff, bb, clamped);

        Uniforms off = tight;
        off.clampCeiling = 0.0f;
        FlatField f2 = f;
        RecordingBackdrop b2;
        float unclamped[4];
        evaluate(in, off, f2, b2, unclamped);

        bool differs = false;
        for (int q = 0; q < 4; ++q)
            if (!sameBits(clamped[q], unclamped[q])) differs = true;
        CHECK(differs);
    }

    // `[BIN]` %713..%726 -- the blur fill's guard is an OR of three uniforms, so
    // it takes all three at zero to skip it, and any one of them alone is
    // enough to run it. That is a shape a single-uniform guard would not have.
    Uniforms none = base;
    none.blurFillLighten = 0.0f;
    none.blurFillDarken = 0.0f;
    none.blurFillNormal = 0.0f;
    FlatField f0 = f;
    RecordingBackdrop b0;
    float skipped[4];
    evaluate(in, none, f0, b0, skipped);
    CHECK(b0.radii.size() < referenceFetches);

    for (float Uniforms::*slot : {&Uniforms::blurFillLighten, &Uniforms::blurFillDarken,
                                  &Uniforms::blurFillNormal}) {
        Uniforms one = none;
        one.*slot = 0.5f;
        FlatField f1 = f;
        RecordingBackdrop b1;
        float out[4];
        evaluate(in, one, f1, b1, out);
        CHECK(b1.radii.size() > b0.radii.size());
    }
}

TEST_CASE(the_assembly_takes_the_flat_and_shadow_paths_at_the_two_mask_extremes) {
    Uniforms u = lively();
    Inputs in;
    in.fwidthD = 1.0f;
    in.variant = kVariantInnerRefraction | kVariantBackdrop;
    float out[4];

    // `[BIN]` %101 -- with the mask at or above one the shadow lobe and its
    // re-sample are both skipped: the field is read ONCE.
    FlatField deep;
    deep.texel[0] = 0.0f;   // d = -120, deep inside, so coverage saturates
    deep.texel[3] = 1.0f;
    RecordingBackdrop bd;
    evaluate(in, u, deep, bd, out);
    CHECK_EQ(deep.calls, 1);

    // `[BIN]` %474 -- with the mask at zero the face is skipped entirely and the
    // result is the shadow alone. The field is read twice (the base and the
    // shadow's offset re-sample) and the face takes no backdrop fetch.
    //
    // The distance is +1 and not +120: far enough out that the coverage is zero,
    // near enough that the shadow's own edge coverage is not, so the function
    // reaches %472 instead of short-circuiting at %361. Those are two different
    // ways to draw nothing and this case is about the first.
    FlatField outside = outsideField();
    RecordingBackdrop bo;
    evaluate(in, u, outside, bo, out);
    CHECK_EQ(outside.calls, 2);
    // only the shadow lobe's own fetch, and only because bit 2 is set
    REQUIRE(bo.radii.size() == 1);
    CHECK(sameBits(bo.radii[0], u.shadowBlurRadius));
}
