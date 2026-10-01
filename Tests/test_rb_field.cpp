// The distance field, in its two halves -- and the halves are gated differently
// ON PURPOSE.
//
// PART ONE gates a TRANSCRIPTION: `distanceGradient_v1` (mod95) against the GPU
// running `DistanceField.glsl`, the same file the renderer will include. Both
// of the IR's branches are exercised, because an untested guard is not a guard:
// the `sdf.x == 0` early-out and the `any(delta != 0)` that stands between a
// flat neighbourhood and a NaN.
//
// PART TWO gates OUR OWN ALGORITHM -- the field generator -- and it cannot be
// gated by a differential, because there is nothing to differ from. So it is
// gated against CLOSED FORMS: an axis-aligned rectangle and a circle both have
// exact analytic signed distances, and the MEASURED MAXIMUM ERROR against each
// is printed by the test rather than asserted in silence. Those two numbers are
// the whole claim this generator makes.
//
// MEASURED, on the sweeps below (printed every run, so a regression shows as a
// number moving rather than a pass/fail flip):
//
//   rectangle, contour exact:      max |error| = 9.42e-07 px
//   circle, 1024-segment contour:  max |error| = 9.55e-05 px,
//                                  against a polygon sagitta of 9.41e-05 px
//   gradient over a circle:        max | |g| - 1 | = 5.96e-08 over 2300 samples
//
// The rectangle is the one that measures the GENERATOR: its contour is exact,
// so the only error left is float storage of a double result, and 9.42e-07 at a
// magnitude of ~30 px is two float ULP. The circle measures the CALLER's
// flattening instead -- 9.55e-05 against a sagitta of 9.41e-05 is a ratio of
// 1.015, which is the statement that the generator adds essentially nothing of
// its own to the geometry it is handed.
//
// WHICH MUTATIONS BITE, RUN AND RECORDED RATHER THAN ASSUMED. Six changes were
// made to the transcription and the gate rerun against each:
//
//   drop the `abs` on the derivatives           -> caught (the taps sweep)
//   zero branch returns coverage 1 not 0        -> caught (the zero branch case)
//   remove the `any(delta != 0)` guard          -> caught, 385 failures of NaN
//   difference in float instead of half         -> caught (the taps sweep)
//   drop the layer's clamp rect                 -> caught (the uv sweep)
//   swap the two fma so p.x nests inside p.y    -> NOT CAUGHT
//
// The last one is named because it is a real hole and a small one. That swap is
// the SAME expression -- `p.x*m0 + p.y*m1 + m2` either way -- and differs only
// in which rounding happens first, by about one ULP. The uv allowance is two
// ULP because the driver's own `Fma` costs that much (see `kUvUlps`), so no
// allowance that lets the driver through can reject the reordering. The
// SEMANTIC mutations of the same transform are caught: transposing the columns
// and dropping the clamp both fail, in
// `the_uv_allowance_is_narrow_enough_to_still_reject` and the sweep.
#include "check.h"
#include "Source/RenderBox/Compute.h"
#include "Source/RenderBox/DistanceField.h"
#include "Source/RenderBox/PathVertexOracle.h"   // ulpsApart

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

static const std::uint32_t kFieldSpirv[] =
#include "field_probe.comp.inc"
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

// Mirrors the four vec4 `field_probe.comp` reads per probe. Sixteen floats with
// no padding anywhere, which is why it is a flat struct and not four vec4
// members: std430 would align a `vec2` member to 8 bytes and the grouping here
// is already exact.
struct Probe {
    float p[2]{0, 0};
    float dpdx = 0.0f;
    float dpdy = 0.0f;
    float m0[2]{1, 0};
    float m1[2]{0, 1};
    float m2[2]{0, 0};
    float m3[2]{0, 0};
    float m4[2]{1, 1};
    float scale = 1.0f;
    float fallback = 0.0f;
};
static_assert(sizeof(Probe) == 64, "four vec4, as the probe reads them");

struct Control {
    std::uint32_t count = 0;
    std::uint32_t stage = 0;
    std::uint32_t fieldBase = 0;
    std::uint32_t fieldW = 0;
    std::uint32_t fieldH = 0;
};

DistanceGradientParams paramsOf(const Probe& v) {
    DistanceGradientParams p;
    p.scale = v.scale;
    p.fallback = v.fallback;
    std::memcpy(p.layer.m0, v.m0, sizeof v.m0);
    std::memcpy(p.layer.m1, v.m1, sizeof v.m1);
    std::memcpy(p.layer.m2, v.m2, sizeof v.m2);
    std::memcpy(p.layer.m3, v.m3, sizeof v.m3);
    std::memcpy(p.layer.m4, v.m4, sizeof v.m4);
    return p;
}

// The probes and the field's texels share one buffer, because `ComputePass`
// binds exactly two and the second is the output.
std::vector<float> onGpu(Device& d, const std::vector<Probe>& in, const FieldTexture& tex,
                         std::uint32_t stage) {
    if (in.empty()) return {};
    const std::size_t probeBytes = in.size() * sizeof(Probe);
    const std::size_t texBytes = tex.rgba.size() * sizeof(float);

    auto input = Buffer::create(d, probeBytes + texBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!input) return {};
    auto* bytes = static_cast<unsigned char*>(input->mapped());
    std::memcpy(bytes, in.data(), probeBytes);
    if (texBytes) std::memcpy(bytes + probeBytes, tex.rgba.data(), texBytes);

    auto out = Buffer::create(d, in.size() * 4 * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!out) return {};
    std::memset(out->mapped(), 0, in.size() * 4 * sizeof(float));

    auto pass = ComputePass::create(d, kFieldSpirv, sizeof kFieldSpirv, sizeof(Control));
    if (!pass) {
        std::printf("  FAIL compute pass: %s\n", pass.error().c_str());
        ++ictest::failures();
        return {};
    }
    Control c;
    c.count = static_cast<std::uint32_t>(in.size());
    c.stage = stage;
    c.fieldBase = static_cast<std::uint32_t>(in.size() * 4);
    c.fieldW = tex.width;
    c.fieldH = tex.height;
    auto ran = pass->run(d, *input, *out, &c, sizeof c, c.count);
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

// WHY THE ALLOWANCE ON `.yz` IS COUNTED IN HALF ULP AND NOT FLOAT ULP.
//
// The gradient goes through `inversesqrt`, which Vulkan specifies to 2 ULP with
// no SPIR-V control that makes it exact -- the same limit `sqrt` already
// imposed on the gradient stage. But the result is then NARROWED TO HALF
// (`%122`), and half's grid is roughly two thousand times coarser than float's,
// so those two float ULP normally vanish entirely and, when they do not, they
// move the answer by exactly one step of the grid the target actually stores.
//
// So the measure is one half ULP: wide enough for the transcendental, and far
// too narrow for a transcription error. Dropping the normalize, or scaling by
// the wrong uniform, or differencing in float instead of half moves the answer
// by whole tenths -- the cases below pin that.
std::uint16_t halfBitsOf(float v) {
    const _Float16 h = static_cast<_Float16>(v);
    std::uint16_t b = 0;
    std::memcpy(&b, &h, sizeof b);
    return b;
}

std::uint32_t halfUlpsApart(float a, float b) {
    auto key = [](std::uint16_t bits) -> std::int32_t {
        const std::int32_t mag = static_cast<std::int32_t>(bits & 0x7FFFu);
        return (bits & 0x8000u) ? (0x8000 - mag) : (0x8000 + mag);
    };
    return static_cast<std::uint32_t>(std::abs(key(halfBitsOf(a)) - key(halfBitsOf(b))));
}

constexpr std::uint32_t kGradientHalfUlps = 1;

// AND WHY THE UV STAGE IS NOT BIT-FOR-BIT, AND WHERE ITS ALLOWANCE IS MEASURED.
//
// The transform is two nested `air.fma`, and this side transcribes them as
// SPIR-V's `Fma`. That instruction is only required to round once when it
// carries `NoContraction`, and glslc does not put that decoration on an
// `OpExtInst` for a `precise` declaration -- checked in the disassembly, where
// `%57` and `%66` come out bare. This driver's `Fma` then lands one ULP off the
// correctly rounded answer: at p = (-3, -0.95) through the matrix the sweep
// uses it returns 0.342000037, where correct rounding gives 0.342000008 --
// and that is what EVERY arrangement gives, fused, split, reassociated, or
// computed in double and rounded once. The gap is the driver's arithmetic, not
// the transcription's.
//
// WHERE IT IS COUNTED IS THE WHOLE POINT. The error is created in the fma's
// intermediate terms, and the result is a SUM of terms that cancel: at
// p = (-1.15, -0.95) the terms are 0.357, 0.086 and 0.5, and they add to 0.058.
// Three hundredths of a ULP at 0.5 reads as NINE ULP at 0.058 -- measured, and
// the reason the first version of this test failed while being right.
//
// So the allowance is two ULP OF THE LARGEST TERM THE CHAIN ADDS, an absolute
// bound derived where the error is made. Two rather than one because the chain
// has two fmas and either may round. This is the same lesson the gradient stage
// had to learn about `sqrt(dot(p,p)) * a + b`, and it is repeated here because
// the shape of the arithmetic is the same: a subtraction of near-equal terms.
constexpr std::uint32_t kUvUlps = 2;

float ulpSizeAt(float magnitude) {
    const float m = std::fabs(magnitude);
    return std::nextafter(m, std::numeric_limits<float>::infinity()) - m;
}

// The magnitude the fma chain's error is created at, for one component.
float uvTermScale(const FieldLayer& l, float px, float py, int k) {
    return std::max({std::fabs(px * l.m0[k]), std::fabs(py * l.m1[k]), std::fabs(l.m2[k])});
}

bool uvAgrees(float gpu, float cpu, float scale) {
    if (std::memcmp(&gpu, &cpu, sizeof(float)) == 0) return true;
    return std::fabs(gpu - cpu) <= static_cast<float>(kUvUlps) * ulpSizeAt(scale);
}

// A field to sample: the exact distance to a circle, at texel centres. Analytic
// rather than produced by our own generator, so a defect in the generator
// cannot hide inside the transcription's gate -- the two halves of this file
// are meant to fail independently.
//
// EVERY VALUE IS SNAPPED TO A MULTIPLE OF 1/64, AND THAT IS NOT COSMETIC.
// GLSL leaves `packHalf2x16`'s rounding mode UNSPECIFIED, and this driver
// truncates where the CPU's conversion rounds to nearest -- measured: the texel
// at 4.92471 comes back as 4.921875 on the GPU and 4.92578125 on the CPU, one
// half ULP apart. That is the same rounding-mode gap `PathCoverageOracle`
// already had to derive an absolute allowance for.
//
// Spending the differential's budget on the FIXTURE's rounding would leave
// nothing to measure the transcription with -- and worse, the central
// difference AMPLIFIES it: two samples each a half ULP off made the normalized
// gradient disagree by 38 half ULP, which is a number about the fixture and not
// about mod95. Multiples of 2^-6 with |v| <= 8 are exactly representable in
// half, AND so is every pairwise difference of them (|d| <= 16, where half's
// spacing is 2^-7 or finer). So `rbHalf` is the identity on both sides all the
// way through the taps and the difference, and what remains to disagree about
// is exactly the arithmetic the IR names.
FieldTexture circleField(std::uint32_t n, float cx, float cy, float r) {
    FieldTexture t;
    t.width = n;
    t.height = n;
    t.rgba.assign(static_cast<std::size_t>(n) * n * 4, 0.0f);
    for (std::uint32_t y = 0; y < n; ++y) {
        for (std::uint32_t x = 0; x < n; ++x) {
            const float dx = static_cast<float>(x) + 0.5f - cx;
            const float dy = static_cast<float>(y) + 0.5f - cy;
            float* p = t.rgba.data() + (static_cast<std::size_t>(y) * n + x) * 4;
            p[0] = std::round((std::sqrt(dx * dx + dy * dy) - r) * 64.0f) / 64.0f;
            p[3] = 1.0f;
        }
    }
    return t;
}

// The precondition the snapping above depends on. A field that outgrew it would
// start disagreeing on the half grid again, and this says so at the point of
// use rather than leaving it to be rediscovered.
bool fieldIsExactInHalf(const FieldTexture& t) {
    for (std::size_t i = 0; i < t.rgba.size(); i += 4) {
        const float v = t.rgba[i];
        if (std::fabs(v) > 8.0f) return false;
        if (v * 64.0f != std::round(v * 64.0f)) return false;
        if (fieldNarrowToHalf(v) != v) return false;
    }
    return true;
}

// A layer that maps a point in TEXEL space straight onto the texture: uv = p/n,
// clamped to the whole rect. With this layer a tap step of 1.0 moves exactly one
// texel, which is what makes the delta == 0 case reachable by CHOOSING the step
// rather than by luck.
void texelLayer(Probe& v, std::uint32_t n) {
    const float inv = 1.0f / static_cast<float>(n);
    v.m0[0] = inv; v.m0[1] = 0.0f;
    v.m1[0] = 0.0f; v.m1[1] = inv;
    v.m2[0] = 0.0f; v.m2[1] = 0.0f;
    v.m3[0] = 0.0f; v.m3[1] = 0.0f;
    v.m4[0] = 1.0f; v.m4[1] = 1.0f;
}

// ---- part two's closed forms ---------------------------------------------

FieldContour rectContour(float x0, float y0, float x1, float y1) {
    FieldContour c;
    c.xy = {x0, y0, x1, y0, x1, y1, x0, y1};
    return c;
}

FieldContour circleContour(float cx, float cy, float r, int segments) {
    FieldContour c;
    c.xy.reserve(static_cast<std::size_t>(segments) * 2);
    for (int i = 0; i < segments; ++i) {
        const double a = 6.283185307179586 * static_cast<double>(i) / segments;
        c.xy.push_back(static_cast<float>(cx + r * std::cos(a)));
        c.xy.push_back(static_cast<float>(cy + r * std::sin(a)));
    }
    return c;
}

// The exact signed distance to an axis-aligned rectangle. Negative inside, as
// the field's convention demands.
double rectSdf(double px, double py, double x0, double y0, double x1, double y1) {
    const double dx = std::max(x0 - px, px - x1);
    const double dy = std::max(y0 - py, py - y1);
    const double outside = std::hypot(std::max(dx, 0.0), std::max(dy, 0.0));
    const double inside = std::min(std::max(dx, dy), 0.0);
    return outside + inside;
}

}  // namespace

// ===========================================================================
// PART ONE -- `distanceGradient_v1`, transcribed. GPU against the oracle.
// ===========================================================================

// The layer's affine and its clamp, alone. One float ULP, for the driver's
// `Fma` and nothing else -- see `kUvUlps` for the measurement.
//
// The clamp rect is made to BITE on all four sides, because a transcription
// that dropped it would pass every sample that stayed inside the rect and then
// read off the end of the texture on the one that did not.
TEST_CASE(the_layers_affine_and_its_clamp_agree_with_the_gpu) {
    Device& d = gpu();
    if (!d.valid()) return;

    std::vector<Probe> in;
    for (float x = -3.0f; x <= 3.0f; x += 0.37f) {
        for (float y = -3.0f; y <= 3.0f; y += 0.41f) {
            Probe v;
            v.p[0] = x;
            v.p[1] = y;
            // A rotation and a scale, so the two columns cannot be swapped
            // without the answer moving, plus a translation.
            v.m0[0] = 0.31f;  v.m0[1] = -0.17f;
            v.m1[0] = 0.09f;  v.m1[1] = 0.44f;
            v.m2[0] = 0.5f;   v.m2[1] = 0.25f;
            v.m3[0] = 0.05f;  v.m3[1] = 0.1f;
            v.m4[0] = 0.9f;   v.m4[1] = 0.8f;
            in.push_back(v);
        }
    }

    const FieldTexture empty;
    const std::vector<float> got = onGpu(d, in, empty, 0u);
    REQUIRE(got.size() == in.size() * 4);

    int bad = 0;
    int clampedLow = 0, clampedHigh = 0;
    double worst = 0.0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        float want[2];
        fieldUv(paramsOf(in[i]).layer, in[i].p[0], in[i].p[1], want);
        if (want[0] == in[i].m3[0] || want[1] == in[i].m3[1]) ++clampedLow;
        if (want[0] == in[i].m4[0] || want[1] == in[i].m4[1]) ++clampedHigh;
        for (int k = 0; k < 2; ++k) {
            const float scale = uvTermScale(paramsOf(in[i]).layer, in[i].p[0], in[i].p[1], k);
            const double ulps = std::fabs(got[i * 4 + k] - want[k]) / ulpSizeAt(scale);
            worst = std::max(worst, ulps);
            if (!uvAgrees(got[i * 4 + k], want[k], scale)) {
                if (bad < 3) {
                    std::printf("  FAIL uv p=(%.4f,%.4f) k=%d  gpu %.9g  cpu %.9g  (%.2f ULP "
                                "of the term)\n",
                                in[i].p[0], in[i].p[1], k, got[i * 4 + k], want[k], ulps);
                }
                ++bad;
            }
        }
    }
    // Printed so a regression shows as a NUMBER MOVING rather than as a
    // pass/fail flip: an allowance nobody watches drifts.
    std::printf("  field uv: worst disagreement %.2f ULP of the term (allowed %u)\n", worst,
                kUvUlps);
    CHECK_EQ(bad, 0);
    // The sweep is only a test of the clamp if the clamp actually fired.
    CHECK(clampedLow > 0);
    CHECK(clampedHigh > 0);
}

// The allowance has to be narrow enough to still REJECT. These are the ways the
// transform could be transcribed wrong, and every one of them misses by
// something no amount of the driver's rounding could account for. Without this,
// "two ULP of the largest term" is one bad day away from becoming "any two
// floats".
TEST_CASE(the_uv_allowance_is_narrow_enough_to_still_reject) {
    FieldLayer right;
    right.m0[0] = 0.31f;  right.m0[1] = -0.17f;
    right.m1[0] = 0.09f;  right.m1[1] = 0.44f;
    right.m2[0] = 0.5f;   right.m2[1] = 0.25f;
    right.m3[0] = 0.05f;  right.m3[1] = 0.1f;
    right.m4[0] = 0.9f;   right.m4[1] = 0.8f;

    // A point WELL INSIDE the clamp rect, so the transposition is not masked by
    // both versions being pinned to the same bound -- which is what the first
    // draft of this case did, and what made it pass for the wrong reason.
    const float px = 0.5f, py = 0.3f;
    float want[2];
    fieldUv(right, px, py, want);
    CHECK(want[0] > right.m3[0] && want[0] < right.m4[0]);

    // The columns transposed -- the classic way to get a 2x3 wrong, and it
    // still produces a plausible-looking coordinate.
    FieldLayer swapped = right;
    std::swap(swapped.m0[0], swapped.m1[0]);
    std::swap(swapped.m0[1], swapped.m1[1]);
    float got[2];
    fieldUv(swapped, px, py, got);
    CHECK(!uvAgrees(got[0], want[0], uvTermScale(right, px, py, 0)));

    // The inner and outer fma swapped, so `p.y` multiplies the wrong column.
    FieldLayer crossed = right;
    std::swap(crossed.m0[0], crossed.m0[1]);
    std::swap(crossed.m1[0], crossed.m1[1]);
    fieldUv(crossed, px, py, got);
    CHECK(!uvAgrees(got[0], want[0], uvTermScale(right, px, py, 0)));

    // The clamp dropped. At THIS point the x coordinate is pinned to the low
    // bound, so removing the clamp is not a rounding difference -- it is a
    // texture read that runs off the edge.
    float clamped[2];
    fieldUv(right, -3.0f, -0.95f, clamped);
    CHECK_EQ(clamped[0], right.m3[0]);
    FieldLayer unclamped = right;
    unclamped.m3[0] = -1e9f;  unclamped.m3[1] = -1e9f;
    unclamped.m4[0] = 1e9f;   unclamped.m4[1] = 1e9f;
    fieldUv(unclamped, -3.0f, -0.95f, got);
    CHECK(!uvAgrees(got[0], clamped[0], uvTermScale(right, -3.0f, -0.95f, 0)));

    // And what the allowance IS for: the driver's `Fma`, two ULP of the largest
    // term the chain adds -- which here is `m2`, at 0.5.
    const float scale = uvTermScale(right, px, py, 0);
    CHECK_EQ(scale, 0.5f);
    const float step = ulpSizeAt(scale);
    CHECK(uvAgrees(want[0], want[0] + 2.0f * step, scale));    // exactly at the limit
    CHECK(!uvAgrees(want[0], want[0] + 4.0f * step, scale));   // past it
    // The measured case that motivated counting at the term: nine ULP AT THE
    // RESULT is inside the allowance, because the error was made at 0.5.
    CHECK(rb::ulpsApart(0.0579999276f, 0.0579999611f) > 4u);
    CHECK(uvAgrees(0.0579999276f, 0.0579999611f, 0.5f));
}

// The five taps, the half-precision central difference, the guarded normalize
// and the scale -- the whole function, on a field with a real gradient in it.
//
// The step is swept including NEGATIVE values, because the IR takes the taps at
// `p +/- |dfdx|`: a transcription that dropped the `abs` would put two of the
// four taps on the wrong side and the gradient would come out reversed on half
// the quads.
TEST_CASE(the_five_taps_and_the_central_difference_agree_with_the_gpu) {
    Device& d = gpu();
    if (!d.valid()) return;

    const std::uint32_t n = 16;
    const FieldTexture tex = circleField(n, 8.0f, 8.0f, 5.0f);

    // The zero branch is a DIFFERENT test; this one has to stay off it, so the
    // field is checked for a texel that narrows to zero before anything else.
    int zeroTexels = 0;
    for (std::size_t i = 0; i < tex.rgba.size(); i += 4) {
        if (fieldNarrowToHalf(tex.rgba[i]) == 0.0f) ++zeroTexels;
    }
    REQUIRE(zeroTexels == 0);
    // And the fixture's own precondition, checked rather than assumed: if the
    // texels stopped being exact in half, this test would start measuring the
    // driver's `packHalf2x16` rounding mode instead of mod95.
    REQUIRE(fieldIsExactInHalf(tex));

    std::vector<Probe> in;
    for (const float step : {1.0f, 2.0f, -1.0f, -3.0f}) {
        for (float x = 0.5f; x < static_cast<float>(n); x += 1.0f) {
            for (float y = 0.5f; y < static_cast<float>(n); y += 1.0f) {
                Probe v;
                texelLayer(v, n);
                v.p[0] = x;
                v.p[1] = y;
                v.dpdx = step;
                v.dpdy = step;
                v.scale = 0.375f;
                v.fallback = 0.125f;
                in.push_back(v);
            }
        }
    }

    const std::vector<float> got = onGpu(d, in, tex, 1u);
    REQUIRE(got.size() == in.size() * 4);

    int bad = 0;
    std::uint32_t worst = 0;
    int normalised = 0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        float want[4];
        distanceGradient(in[i].p[0], in[i].p[1], in[i].dpdx, in[i].dpdy, paramsOf(in[i]), tex,
                         want);
        // `.yz` moved off the fallback means the normalize ran.
        if (want[1] != in[i].fallback || want[2] != in[i].fallback) ++normalised;
        for (int k = 0; k < 4; ++k) {
            // `.x` is the centre tap passed straight through and `.w` is a
            // literal: neither can round, so neither gets an allowance.
            const std::uint32_t allow = (k == 1 || k == 2) ? kGradientHalfUlps : 0u;
            const std::uint32_t apart = halfUlpsApart(got[i * 4 + k], want[k]);
            if (allow) worst = std::max(worst, apart);
            if (!sameBits(got[i * 4 + k], want[k]) && apart > allow) {
                if (bad < 3) {
                    std::printf("  FAIL tap p=(%.2f,%.2f) step=%.2f k=%d  gpu %.9g  cpu %.9g\n",
                                in[i].p[0], in[i].p[1], in[i].dpdx, k, got[i * 4 + k], want[k]);
                }
                ++bad;
            }
        }
    }
    std::printf("  distanceGradient: worst .yz disagreement %u half ULP (allowed %u), "
                "%d of %zu samples took the normalize\n",
                worst, kGradientHalfUlps, normalised, in.size());
    CHECK_EQ(bad, 0);
    // A sweep that never reached the normalize would be gating the guard only.
    CHECK(normalised > 0);
}

// BRANCH ONE: `sdf.x == 0`. The IR leaves through a different block entirely and
// returns `(0, u1, u1, 0)` -- and the zero in `.w` is the load-bearing part,
// because `glassBackground_v1` multiplies its mask by that channel. A
// transcription that returned 1 there would paint glass over a place the field
// says has no shape in it.
TEST_CASE(a_zero_distance_returns_the_fallback_and_reports_no_coverage) {
    Device& d = gpu();
    if (!d.valid()) return;

    const std::uint32_t n = 8;
    FieldTexture tex = circleField(n, 4.0f, 4.0f, 2.0f);
    // Plant exact zeros down one column, so the branch is reached by AIM rather
    // than by a value that happened to land there.
    for (std::uint32_t y = 0; y < n; ++y) {
        tex.rgba[(static_cast<std::size_t>(y) * n + 3) * 4] = 0.0f;
    }
    REQUIRE(fieldIsExactInHalf(tex));

    std::vector<Probe> in;
    for (float y = 0.5f; y < static_cast<float>(n); y += 1.0f) {
        for (const float x : {3.5f, 5.5f}) {   // on the zero column, and off it
            Probe v;
            texelLayer(v, n);
            v.p[0] = x;
            v.p[1] = y;
            v.dpdx = 1.0f;
            v.dpdy = 1.0f;
            v.scale = 2.5f;
            v.fallback = 0.75f;
            in.push_back(v);
        }
    }

    const std::vector<float> got = onGpu(d, in, tex, 1u);
    REQUIRE(got.size() == in.size() * 4);

    int onTheBranch = 0;
    int bad = 0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        float want[4];
        distanceGradient(in[i].p[0], in[i].p[1], in[i].dpdx, in[i].dpdy, paramsOf(in[i]), tex,
                         want);
        if (in[i].p[0] == 3.5f) {
            ++onTheBranch;
            // The whole of `%51`, spelled out rather than left to the oracle:
            // if the oracle and the shader were both wrong the same way, this
            // is the assertion that still catches it.
            CHECK_EQ(want[0], 0.0f);
            CHECK_EQ(want[1], in[i].fallback);
            CHECK_EQ(want[2], in[i].fallback);
            CHECK_EQ(want[3], 0.0f);
            // And the fallback is NOT run through the scale on this path -- it
            // is written raw. `u1 * scale` would be 1.875 here.
            CHECK(want[1] != in[i].fallback * in[i].scale);
        } else {
            CHECK_EQ(want[3], 1.0f);
        }
        for (int k = 0; k < 4; ++k) {
            const std::uint32_t allow = (k == 1 || k == 2) ? kGradientHalfUlps : 0u;
            if (!sameBits(got[i * 4 + k], want[k]) &&
                halfUlpsApart(got[i * 4 + k], want[k]) > allow) {
                if (bad < 3) {
                    std::printf("  FAIL zero p=(%.2f,%.2f) k=%d  gpu %.9g  cpu %.9g\n", in[i].p[0],
                                in[i].p[1], k, got[i * 4 + k], want[k]);
                }
                ++bad;
            }
        }
    }
    CHECK_EQ(bad, 0);
    CHECK_EQ(onTheBranch, static_cast<int>(n));
}

// BRANCH TWO: `any(delta != 0)`. An untested guard is not a guard, and this one
// is reachable two different ways on ordinary input -- a tap step too small to
// leave one texel, and a step of zero.
//
// Without the guard, `rsqrt(0)` is an infinity and the multiply after it is a
// NaN, which would be written into the field and would then propagate through
// every consumer of it. The assertion is therefore not just "the two sides
// agree" but "the answer is a finite number that is exactly the bias".
TEST_CASE(a_flat_neighbourhood_skips_the_normalize_rather_than_returning_a_nan) {
    Device& d = gpu();
    if (!d.valid()) return;

    const std::uint32_t n = 8;
    const FieldTexture tex = circleField(n, 4.0f, 4.0f, 2.0f);
    REQUIRE(fieldIsExactInHalf(tex));

    std::vector<Probe> in;
    // A step of exactly zero, and a step of a fifth of a texel from a texel
    // centre -- both leave all five taps inside one texel, so both differences
    // are exactly zero.
    for (const float step : {0.0f, 0.2f, -0.2f}) {
        for (float x = 0.5f; x < static_cast<float>(n); x += 1.0f) {
            for (float y = 0.5f; y < static_cast<float>(n); y += 1.0f) {
                Probe v;
                texelLayer(v, n);
                v.p[0] = x;
                v.p[1] = y;
                v.dpdx = step;
                v.dpdy = step;
                v.scale = 3.0f;
                v.fallback = 0.5f;
                in.push_back(v);
            }
        }
    }

    const std::vector<float> got = onGpu(d, in, tex, 1u);
    REQUIRE(got.size() == in.size() * 4);

    int bad = 0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        float want[4];
        distanceGradient(in[i].p[0], in[i].p[1], in[i].dpdx, in[i].dpdy, paramsOf(in[i]), tex,
                         want);
        // `delta` is (0,0), so `fma(0, scale, fallback)` is the bias exactly --
        // on BOTH sides, and never a NaN on either.
        CHECK_EQ(want[1], in[i].fallback);
        CHECK_EQ(want[2], in[i].fallback);
        CHECK_EQ(want[3], 1.0f);   // and this is NOT the zero-distance branch
        for (int k = 0; k < 4; ++k) {
            if (!sameBits(got[i * 4 + k], want[k])) {
                if (bad < 3) {
                    std::printf("  FAIL flat p=(%.2f,%.2f) step=%.2f k=%d  gpu %.9g  cpu %.9g\n",
                                in[i].p[0], in[i].p[1], in[i].dpdx, k, got[i * 4 + k], want[k]);
                }
                ++bad;
            }
            CHECK(got[i * 4 + k] == got[i * 4 + k]);   // never a NaN, on the GPU either
        }
    }
    CHECK_EQ(bad, 0);
}

// And what the guard is FOR, stated as arithmetic rather than as a claim: this
// is the value the unguarded expression produces. If somebody deletes the `if`,
// the test above starts reporting these.
TEST_CASE(the_unguarded_normalize_is_the_nan_the_guard_exists_to_stop) {
    const float zero = 0.0f;
    const float unguarded = zero * (1.0f / std::sqrt(zero * zero + zero * zero));
    CHECK(std::isinf(1.0f / std::sqrt(0.0f)));
    CHECK(unguarded != unguarded);   // NaN

    // Against which: the transcription's answer on the same input is finite and
    // is the bias, with coverage still 1.
    FieldTexture tex;
    tex.width = 1;
    tex.height = 1;
    tex.rgba = {0.25f, 0.0f, 0.0f, 1.0f};
    DistanceGradientParams p;
    p.scale = 4.0f;
    p.fallback = -0.125f;
    float out[4];
    distanceGradient(0.5f, 0.5f, 0.0f, 0.0f, p, tex, out);
    CHECK_EQ(out[0], 0.25f);
    CHECK_EQ(out[1], -0.125f);
    CHECK_EQ(out[2], -0.125f);
    CHECK_EQ(out[3], 1.0f);
}

// `%102`/`%105` ARE `fsub half`, AND THAT IS A DIFFERENCE THE GATE CAN SEE.
//
// The central difference ROUNDS TO HALF before it is widened and normalized.
// Transcribing it as a float difference would be the easy mistake -- the
// samples are floats in every host language one might write this in -- so the
// question is whether any test would notice. It would, and this measures by how
// much: 65 half ULP over the sweep below.
//
// The first estimate said it would NOT be visible, and the estimate was wrong
// in an instructive way. Rounding the difference moves it by at most half a
// half ULP, the normalize turns that into about as much angle, and the output's
// own half grid is coarser -- so on its own it would vanish. What it does not
// account for is `%121`, the `fma(g, scale, fallback)`: where `g * scale` and
// `fallback` nearly cancel, the result is small while the error is not, and a
// perturbation far below the resolution of `g` lands whole steps away in the
// stored answer. That is the same cancellation the gradient stage's ULP
// allowance had to be moved off the result to survive, met here from the other
// direction -- this time it is what makes a real distinction observable.
TEST_CASE(differencing_in_float_instead_of_half_is_a_change_this_gate_can_see) {
    // Five taps planted into a 3x3, probed from its centre with a step of one
    // texel, so `left/right/down/up` are exactly the four neighbours.
    const std::uint32_t n = 3;
    FieldTexture tex;
    tex.width = n;
    tex.height = n;
    tex.rgba.assign(static_cast<std::size_t>(n) * n * 4, 0.0f);

    Probe v;
    texelLayer(v, n);
    v.p[0] = 1.5f;
    v.p[1] = 1.5f;
    v.dpdx = 1.0f;
    v.dpdy = 1.0f;
    v.scale = 1.75f;
    v.fallback = -0.25f;

    // A spread of half values across four decades of exponent, which is where a
    // difference of two halves stops being exactly representable.
    const float values[] = {8.0f,     4.921875f, 1.0f,       0.5f,      0.0625f,
                            0.001953125f, 0.00048828125f, -2.5f, -0.125f, -7.75f};

    std::uint32_t worst = 0;
    int inexact = 0;
    int compared = 0;
    for (const float left : values) {
        for (const float right : values) {
            for (const float down : values) {
                for (const float up : values) {
                    tex.rgba[(0u * n + 1u) * 4] = down;    // (1,0)
                    tex.rgba[(1u * n + 0u) * 4] = left;    // (0,1)
                    tex.rgba[(1u * n + 1u) * 4] = 0.75f;   // the centre, non-zero
                    tex.rgba[(1u * n + 2u) * 4] = right;   // (2,1)
                    tex.rgba[(2u * n + 1u) * 4] = up;      // (1,2)

                    const float dx = right - left;
                    const float dy = up - down;
                    if (dx == 0.0f && dy == 0.0f) continue;
                    if (fieldNarrowToHalf(dx) != dx || fieldNarrowToHalf(dy) != dy) ++inexact;

                    float got[4];
                    distanceGradient(v.p[0], v.p[1], v.dpdx, v.dpdy, paramsOf(v), tex, got);

                    // The same function with the narrowing removed -- the only
                    // difference being the thing this case is measuring.
                    float g[2] = {dx, dy};
                    const float inv = 1.0f / std::sqrt(g[0] * g[0] + g[1] * g[1]);
                    g[0] *= inv;
                    g[1] *= inv;
                    const float noNarrow[2] = {
                        fieldNarrowToHalf(std::fma(g[0], v.scale, v.fallback)),
                        fieldNarrowToHalf(std::fma(g[1], v.scale, v.fallback))};

                    worst = std::max({worst, halfUlpsApart(got[1], noNarrow[0]),
                                      halfUlpsApart(got[2], noNarrow[1])});
                    ++compared;
                }
            }
        }
    }
    std::printf("  half- vs float-differencing: worst %u half ULP over %d cases "
                "(%d of them inexact in half)\n",
                worst, compared, inexact);
    // The sweep is only a measurement if it actually reached the inexact case:
    // where the difference IS representable in half the two are identical, and a
    // sweep that never left that region would prove nothing.
    CHECK(inexact > 100);
    CHECK(compared > 5000);
    // And the finding: the two are far apart, so the `fsub half` is not a detail
    // the differential is blind to. The bound is `kGradientHalfUlps`, which the
    // GPU sweep holds -- so a transcription that differenced in float would
    // break `the_five_taps_and_the_central_difference_agree_with_the_gpu` rather
    // than slipping past it.
    CHECK(worst > kGradientHalfUlps * 8u);
}

// ===========================================================================
// PART TWO -- the field generator. OUR ALGORITHM, gated against closed forms.
// ===========================================================================

// THE STRONGEST CHECK AVAILABLE, and the one that measures the generator rather
// than the caller: an axis-aligned rectangle's contour is EXACT -- four
// segments, no flattening -- so any error here is the generator's own.
TEST_CASE(the_rectangle_matches_its_closed_form) {
    const double x0 = 12.5, y0 = 8.25, x1 = 50.75, y1 = 44.5;
    const FieldShape shape({rectContour(static_cast<float>(x0), static_cast<float>(y0),
                                        static_cast<float>(x1), static_cast<float>(y1))});
    REQUIRE(shape.segmentCount() == 4);

    double worst = 0.0;
    double worstAt[2] = {0, 0};
    FieldOptions o;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const double px = x + 0.5, py = y + 0.5;
            const FieldSample s = shape.sampleAt(static_cast<float>(px), static_cast<float>(py), o);
            const double err = std::fabs(s.distance - rectSdf(px, py, x0, y0, x1, y1));
            if (err > worst) {
                worst = err;
                worstAt[0] = px;
                worstAt[1] = py;
            }
        }
    }
    std::printf("  field vs rectangle: max |error| = %.3g px (at %.1f,%.1f)\n", worst, worstAt[0],
                worstAt[1]);
    // Two float ULP at a magnitude of ~30 px. The bound is tight on purpose: a
    // generator that got the sign region wrong, or measured to the wrong edge,
    // misses by whole pixels and could never creep under this.
    CHECK(worst < 1e-4);
}

// The circle, where the error is the CALLER's flattening and not ours. An
// inscribed N-gon differs from its circle by at most the sagitta
// `r * (1 - cos(pi/N))`, so a generator that adds nothing of its own lands at
// that number and not above it. Reported rather than merely bounded.
TEST_CASE(the_circle_matches_its_closed_form_to_the_polygons_sagitta) {
    const double cx = 28.0, cy = 28.0, r = 20.0;
    const int segments = 1024;
    const double sagitta = r * (1.0 - std::cos(3.141592653589793 / segments));

    const FieldShape shape({circleContour(static_cast<float>(cx), static_cast<float>(cy),
                                          static_cast<float>(r), segments)});
    REQUIRE(shape.segmentCount() == static_cast<std::size_t>(segments));

    double worst = 0.0;
    FieldOptions o;
    for (int y = 0; y < 56; ++y) {
        for (int x = 0; x < 56; ++x) {
            const double px = x + 0.5, py = y + 0.5;
            const FieldSample s = shape.sampleAt(static_cast<float>(px), static_cast<float>(py), o);
            const double want = std::hypot(px - cx, py - cy) - r;
            worst = std::max(worst, std::fabs(s.distance - want));
        }
    }
    std::printf("  field vs circle: max |error| = %.3g px, polygon sagitta = %.3g px\n", worst,
                sagitta);
    // 1.5x the sagitta, because the flattening error is what it is and the
    // claim being gated is that the GENERATOR adds essentially nothing on top
    // of it. A generator with a half-pixel bias of its own would sit five
    // thousand times above this line.
    CHECK(worst < sagitta * 1.5);
}

// The sign convention, which comes from AquaKit's `Field.frag` -- `[INF]`, and
// the tell is `CASDFOutputEffect.minimum = -10000`, a floor on a quantity that
// goes negative. Getting it backwards would invert every glass effect
// downstream while still producing a field that looks plausible in isolation.
TEST_CASE(the_field_is_negative_inside_positive_outside_and_zero_on_the_boundary) {
    const FieldShape shape({rectContour(10.0f, 10.0f, 40.0f, 30.0f)});
    FieldOptions o;

    CHECK(shape.distanceAt(25.0f, 20.0f, o.rule) < 0.0f);   // the middle
    CHECK(shape.distanceAt(11.0f, 20.0f, o.rule) < 0.0f);   // just inside an edge
    CHECK(shape.distanceAt(9.0f, 20.0f, o.rule) > 0.0f);    // just outside it
    CHECK(shape.distanceAt(60.0f, 60.0f, o.rule) > 0.0f);   // far away

    // And the magnitude is a real distance, not a sign with a number attached.
    CHECK(std::fabs(shape.distanceAt(11.0f, 20.0f, o.rule) + 1.0f) < 1e-5f);
    CHECK(std::fabs(shape.distanceAt(9.0f, 20.0f, o.rule) - 1.0f) < 1e-5f);

    // Zero ON the boundary, from both sides, to a tolerance that is the float
    // grid and not a fudge.
    for (float t = 11.0f; t < 30.0f; t += 1.0f) {
        CHECK(std::fabs(shape.distanceAt(10.0f, t, o.rule)) < 1e-5f);
        CHECK(std::fabs(shape.distanceAt(40.0f, t, o.rule)) < 1e-5f);
    }
    // Including a corner, where the closest point is a vertex rather than an
    // interior point of any segment.
    CHECK(std::fabs(shape.distanceAt(10.0f, 10.0f, o.rule)) < 1e-5f);
}

// The gradient is a UNIT vector pointing the way the distance increases -- out
// of the shape. That is a property of a true distance field, and it is what the
// glass shader's ring and specular projections assume.
TEST_CASE(the_gradient_is_a_unit_vector_pointing_out_of_the_shape) {
    const double cx = 24.0, cy = 24.0, r = 14.0;
    const FieldShape shape(
        {circleContour(static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(r), 512)});
    FieldOptions o;

    double worstLength = 0.0;
    double worstDirection = 0.0;
    int checked = 0;
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 48; ++x) {
            const double px = x + 0.5, py = y + 0.5;
            const double rr = std::hypot(px - cx, py - cy);
            if (rr < 0.75) continue;   // the centre IS the medial axis; see below
            const FieldSample s = shape.sampleAt(static_cast<float>(px), static_cast<float>(py), o);
            worstLength = std::max(worstLength,
                                   std::fabs(std::hypot(s.gx, s.gy) - 1.0));
            // For a circle the gradient is radial everywhere -- outward outside,
            // and outward INSIDE too, because that is the way the (negative)
            // distance rises.
            worstDirection = std::max(worstDirection,
                                      std::fabs(s.gx - (px - cx) / rr) +
                                          std::fabs(s.gy - (py - cy) / rr));
            ++checked;
        }
    }
    std::printf("  field gradient: worst |len - 1| = %.3g, worst direction error = %.3g "
                "over %d samples\n",
                worstLength, worstDirection, checked);
    CHECK(checked > 2000);
    CHECK(worstLength < 1e-5);
    // The direction bound is the polygon's, not ours: an inscribed 512-gon's
    // outward normal swings by up to pi/512 away from the circle's.
    CHECK(worstDirection < 0.02);
}

// WHERE THE FIELD IS NOT SMOOTH, said out loud instead of assumed away.
//
// On the medial axis two or more edges are equidistant, the true gradient is
// discontinuous, and no unit vector is "the" answer. The generator's exact form
// picks one of the competing directions and stays unit; a CENTRAL DIFFERENCE of
// the same field -- the way `Field.frag` derives its gradient -- collapses
// there, because the two sides of the difference disagree. Both facts are
// asserted, because a test that only checked `length == 1` would be asserting
// something the field does not actually have.
TEST_CASE(the_medial_axis_is_where_the_gradient_stops_being_differenceable) {
    const FieldShape shape({rectContour(10.0f, 10.0f, 40.0f, 40.0f)});   // a square
    FieldOptions o;
    const double e = 0.5;   // the same step `Field.frag` takes

    auto centralDifference = [&](double px, double py) {
        const double gx = shape.distanceAt(static_cast<float>(px + e), static_cast<float>(py),
                                           o.rule) -
                          shape.distanceAt(static_cast<float>(px - e), static_cast<float>(py),
                                           o.rule);
        const double gy = shape.distanceAt(static_cast<float>(px), static_cast<float>(py + e),
                                           o.rule) -
                          shape.distanceAt(static_cast<float>(px), static_cast<float>(py - e),
                                           o.rule);
        return std::hypot(gx, gy);
    };

    // AWAY from the axis the two agree: the difference over a step of 2e is a
    // unit vector, so its length is 1.
    const FieldSample off = shape.sampleAt(15.0f, 25.0f, o);
    CHECK(std::fabs(std::hypot(off.gx, off.gy) - 1.0) < 1e-5);
    CHECK(std::fabs(centralDifference(15.0, 25.0) - 2.0 * e) < 1e-5);

    // AT the centre of the square all four edges tie. The difference vanishes
    // in both axes -- there is no gradient to difference.
    CHECK(centralDifference(25.0, 25.0) < 1e-5);
    // On the diagonal medial axis running out of a corner, two edges tie and the
    // difference is short by the factor a 90-degree disagreement makes.
    CHECK(centralDifference(15.0, 15.0) < 2.0 * e * 0.75);

    // And the exact gradient stays a unit vector through all of it, which is
    // exactly why the generator does not difference: a differenced field would
    // hand the glass shader a short vector on the axis and it would read as a
    // shallower surface rather than as an ambiguity.
    const FieldSample centre = shape.sampleAt(25.0f, 25.0f, o);
    CHECK(std::fabs(std::hypot(centre.gx, centre.gy) - 1.0) < 1e-5);
    const FieldSample diagonal = shape.sampleAt(15.0f, 15.0f, o);
    CHECK(std::fabs(std::hypot(diagonal.gx, diagonal.gy) - 1.0) < 1e-5);
}

// The coverage channel: `Field.frag`'s pattern with `fwidth(d)` at the value a
// unit-slope field on a pixel grid actually has. The width is a parameter only
// so this can prove the band is a HALF PIXEL either side and not a constant
// somebody liked the look of.
TEST_CASE(the_coverage_is_the_half_pixel_band_around_the_boundary) {
    const FieldShape shape({rectContour(10.0f, 10.0f, 40.0f, 40.0f)});
    FieldOptions o;

    CHECK_EQ(shape.sampleAt(25.0f, 25.0f, o).coverage, 1.0f);   // deep inside
    CHECK_EQ(shape.sampleAt(80.0f, 80.0f, o).coverage, 0.0f);   // far outside
    CHECK(std::fabs(shape.sampleAt(10.0f, 25.0f, o).coverage - 0.5f) < 1e-5f);   // on it
    CHECK(std::fabs(shape.sampleAt(10.25f, 25.0f, o).coverage - 0.75f) < 1e-5f);
    CHECK(std::fabs(shape.sampleAt(9.75f, 25.0f, o).coverage - 0.25f) < 1e-5f);
    // The band ends exactly half a pixel out.
    CHECK_EQ(shape.sampleAt(9.5f, 25.0f, o).coverage, 0.0f);
    CHECK_EQ(shape.sampleAt(10.5f, 25.0f, o).coverage, 1.0f);

    // Widen it and it widens, which is what makes the 0.5 an offset rather than
    // a magic number.
    o.aaWidth = 2.0f;
    CHECK(std::fabs(shape.sampleAt(10.0f, 25.0f, o).coverage - 0.5f) < 1e-5f);
    CHECK(std::fabs(shape.sampleAt(9.5f, 25.0f, o).coverage - 0.25f) < 1e-5f);
    CHECK(std::fabs(shape.sampleAt(10.5f, 25.0f, o).coverage - 0.75f) < 1e-5f);
}

// The fill rule decides the SIGN and nothing else, and the two rules have to
// actually disagree somewhere or one of them is not implemented. A contour
// inside another and wound the SAME way is the case that separates them:
// non-zero sees winding 2 and fills it, even-odd sees two crossings and cuts a
// hole.
TEST_CASE(the_two_fill_rules_disagree_on_a_contour_wound_the_same_way) {
    const std::vector<FieldContour> both = {rectContour(10.0f, 10.0f, 50.0f, 50.0f),
                                            rectContour(20.0f, 20.0f, 40.0f, 40.0f)};
    const FieldShape shape(both);

    const float inner = shape.distanceAt(30.0f, 30.0f, FieldRule::NonZero);
    const float innerEo = shape.distanceAt(30.0f, 30.0f, FieldRule::EvenOdd);
    CHECK(inner < 0.0f);     // non-zero: still solid
    CHECK(innerEo > 0.0f);   // even-odd: a hole

    // The DISTANCE does not depend on the rule -- only its sign does. The
    // nearest edge from the middle of the inner square is 10 px away either way.
    CHECK(std::fabs(std::fabs(inner) - std::fabs(innerEo)) < 1e-5f);
    CHECK(std::fabs(std::fabs(inner) - 10.0f) < 1e-5f);

    // Between the two squares both rules agree it is inside.
    CHECK(shape.distanceAt(15.0f, 30.0f, FieldRule::NonZero) < 0.0f);
    CHECK(shape.distanceAt(15.0f, 30.0f, FieldRule::EvenOdd) < 0.0f);
    // And outside everything, both agree it is out.
    CHECK(shape.distanceAt(5.0f, 5.0f, FieldRule::NonZero) > 0.0f);
    CHECK(shape.distanceAt(5.0f, 5.0f, FieldRule::EvenOdd) > 0.0f);
}

// The image is sampled at PIXEL CENTRES, in AquaKit's channel order. Corners
// would put the field half a pixel off from everything else this tower draws,
// and half a pixel is exactly the width of the coverage band above -- the error
// would be invisible in the field and obvious in the glass.
TEST_CASE(the_generated_image_samples_pixel_centres_in_the_d_gx_gy_coverage_order) {
    const FieldShape shape({rectContour(4.0f, 4.0f, 12.0f, 12.0f)});
    FieldOptions o;
    const FieldImage img = generateField(shape, 16, 16, o);
    REQUIRE(img.width == 16);
    REQUIRE(img.height == 16);
    REQUIRE(img.rgba.size() == 16u * 16u * 4u);

    int bad = 0;
    for (std::uint32_t y = 0; y < 16; ++y) {
        for (std::uint32_t x = 0; x < 16; ++x) {
            const FieldSample s = shape.sampleAt(static_cast<float>(x) + 0.5f,
                                                 static_cast<float>(y) + 0.5f, o);
            const float* p = img.at(x, y);
            if (p[0] != s.distance || p[1] != s.gx || p[2] != s.gy || p[3] != s.coverage) ++bad;
        }
    }
    CHECK_EQ(bad, 0);

    // A corner-sampled field would put this pixel's distance at -4 rather than
    // -3.5, so the two conventions are distinguishable right here.
    CHECK(std::fabs(*img.at(8, 8) - (-3.5f)) < 1e-5f);
    // And the overload that takes contours is the same field.
    const FieldImage direct = generateField({rectContour(4.0f, 4.0f, 12.0f, 12.0f)}, 16, 16, o);
    CHECK(direct.rgba == img.rgba);
}

// ===========================================================================
// PART THREE -- the field from a rasterised alpha
// ===========================================================================
//
// `[BIN]` This path exists because the target's own field is built from a
// rasterised alpha and not from geometry: `sdfTextureWithBufferAllocator:`
// (`0x000867F8`) is sent to a `CUINamedLayerImage` (classref `0x000CC928`)
// whose `image` is fetched at `0x00028AE0` and whose absence aborts the whole
// path at `0x00028AEC`. `DistanceField.h` §PART THREE carries the reading.
//
// What these cases gate is OURS: that the bitmap field lands in the same
// convention as the contour field, close enough to it that the glass cannot
// tell which generator made it.

namespace {

// A filled axis-aligned rectangle as a bitmap, alpha 1 inside and 0 out. The
// rectangle is the one shape whose exact distance is a closed form, which is
// what lets the error below be a number rather than an opinion.
std::vector<float> rectBitmap(std::uint32_t w, std::uint32_t h, int x0, int y0, int x1, int y1) {
    std::vector<float> rgba(static_cast<std::size_t>(w) * h * 4, 0.0f);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            rgba[(static_cast<std::size_t>(y) * w + x) * 4 + 3] = 1.0f;
        }
    }
    return rgba;
}

}  // namespace

// THE HALF PIXEL. The contour of a bitmap sits BETWEEN the last inside centre
// and the first outside one, so a texel one step inside the edge is at depth
// 0.5 and its signed distance is -0.5. A generator that measured to the centre
// itself would say 0 here, and the whole field would ride half a pixel out.
TEST_CASE(the_alpha_field_puts_the_contour_half_a_pixel_outside_the_last_inside_centre) {
    const std::vector<float> art = rectBitmap(16, 16, 4, 4, 12, 12);
    const rb::FieldImage f = rb::generateFieldFromAlpha(art, 16, 16);
    REQUIRE(f.width == 16);
    REQUIRE(f.height == 16);

    // Just inside the left edge, and just outside it.
    CHECK(std::fabs(*f.at(4, 8) - (-0.5f)) < 1e-5f);
    CHECK(std::fabs(*f.at(3, 8) - (0.5f)) < 1e-5f);
    // The centre of an 8-wide square is 4 centres from the outside, minus the
    // half: -3.5, the SAME number the contour generator reports for the same
    // rectangle in the case above. The two conventions meet.
    CHECK(std::fabs(*f.at(8, 8) - (-3.5f)) < 1e-5f);
    // Coverage is `-d + 0.5` clamped, so it saturates one step in and out.
    CHECK(std::fabs(f.at(4, 8)[3] - 1.0f) < 1e-5f);
    CHECK(std::fabs(f.at(3, 8)[3] - 0.0f) < 1e-5f);
}

// THE GRADIENT IS UNIT AND IT POINTS OUT. `glassHighlight` reads it as a
// surface normal and `glassDisplacementMap` reads it as a direction to push a
// sample along; a gradient of the wrong length dims the highlight and a
// gradient of the wrong SIGN bends the refraction inwards.
TEST_CASE(the_alpha_field_gradient_is_unit_and_points_away_from_the_shape) {
    const std::vector<float> art = rectBitmap(32, 32, 8, 8, 24, 24);
    const rb::FieldImage f = rb::generateFieldFromAlpha(art, 32, 32);
    REQUIRE(f.width == 32);

    int notUnit = 0;
    for (std::uint32_t y = 0; y < 32; ++y) {
        for (std::uint32_t x = 0; x < 32; ++x) {
            const float* p = f.at(x, y);
            const double len = std::sqrt(static_cast<double>(p[1]) * p[1] +
                                         static_cast<double>(p[2]) * p[2]);
            if (std::fabs(len - 1.0) > 1e-5) ++notUnit;
        }
    }
    CHECK_EQ(notUnit, 0);

    // Left of the shape, `d` grows as x falls, so the gradient points -x.
    CHECK(f.at(4, 16)[1] < -0.99f);
    // Inside against the left edge, `d` still grows towards -x: the gradient
    // does NOT flip at the boundary, it is continuous across it.
    CHECK(f.at(8, 16)[1] < -0.99f);
    // Above the shape it points -y, below it +y.
    CHECK(f.at(16, 4)[2] < -0.99f);
    CHECK(f.at(16, 27)[2] > 0.99f);
}

// AGAINST THE CLOSED FORM. A bitmap field cannot beat its own grid -- the
// contour it knows about is a staircase, not a line -- so this asserts the
// bound rather than exactness, and PRINTS the measured error so a regression
// shows as a number moving.
TEST_CASE(the_alpha_field_matches_the_exact_rectangle_distance_within_the_grid) {
    const std::vector<float> art = rectBitmap(64, 64, 16, 16, 48, 48);
    const rb::FieldImage f = rb::generateFieldFromAlpha(art, 64, 64);
    REQUIRE(f.width == 64);

    // The true edges: the contour is half a pixel outside the outermost inside
    // centre, so the rectangle runs from 16.0 to 48.0 in pixel coordinates.
    double worst = 0.0;
    for (std::uint32_t y = 0; y < 64; ++y) {
        for (std::uint32_t x = 0; x < 64; ++x) {
            const double px = x + 0.5, py = y + 0.5;
            const double dx = std::max(16.0 - px, px - 48.0);
            const double dy = std::max(16.0 - py, py - 48.0);
            const double exact =
                (dx <= 0.0 && dy <= 0.0)
                    ? std::max(dx, dy)
                    : std::sqrt(std::max(dx, 0.0) * std::max(dx, 0.0) +
                                std::max(dy, 0.0) * std::max(dy, 0.0));
            worst = std::max(worst, std::fabs(static_cast<double>(*f.at(x, y)) - exact));
        }
    }
    std::printf("    alpha field vs exact rectangle: max |error| = %.3g px\n", worst);
    // The staircase costs at most the diagonal of one texel near a corner.
    CHECK(worst < 0.75);
}

// NO INSIDE MEANS NO FIELD. An all-transparent bitmap has no contour to sign,
// and an all-outside field would let the glass draw nothing while reporting
// success. Empty is the answer that makes the caller say so.
TEST_CASE(the_alpha_field_is_empty_when_no_texel_reaches_the_threshold) {
    std::vector<float> faint(16u * 16u * 4u, 0.0f);
    for (std::size_t t = 0; t < 16u * 16u; ++t) faint[t * 4 + 3] = 0.49f;
    const rb::FieldImage f = rb::generateFieldFromAlpha(faint, 16, 16);
    CHECK_EQ(f.width, 0u);
    CHECK(f.rgba.empty());

    // One texel over the line is enough to have a field again.
    faint[(8u * 16u + 8u) * 4 + 3] = 0.5f;
    const rb::FieldImage one = rb::generateFieldFromAlpha(faint, 16, 16);
    REQUIRE(one.width == 16);
    CHECK(*one.at(8, 8) < 0.0f);
}

// THE VIRTUAL BORDER, which is not a default: `[BIN]` the target's SDF carries
// an empty one-texel ring (`0x00011CF8` subtracts `sdfTexelsW - 2`, `0x00010F48`
// puts it back), and `shadowRingMask` already clamps to it. Art that fills the
// whole canvas therefore has a FINITE depth at its centre, not an infinite one.
TEST_CASE(the_alpha_field_treats_one_step_past_each_edge_as_outside) {
    std::vector<float> full(16u * 16u * 4u, 0.0f);
    for (std::size_t t = 0; t < 16u * 16u; ++t) full[t * 4 + 3] = 1.0f;
    const rb::FieldImage f = rb::generateFieldFromAlpha(full, 16, 16);
    REQUIRE(f.width == 16);

    // The corner texel is one step from two virtual rows: depth 1, so d = -0.5.
    CHECK(std::fabs(*f.at(0, 0) - (-0.5f)) < 1e-5f);
    // The centre is 8 steps from the nearest edge row, minus the half pixel.
    CHECK(std::fabs(*f.at(7, 7) - (-7.5f)) < 1e-5f);
    // And it is still a unit gradient, pointing at the border that won.
    const float* p = f.at(0, 8);
    CHECK(std::fabs(std::sqrt(p[1] * p[1] + p[2] * p[2]) - 1.0f) < 1e-5f);
}

// THE NORMAL THE SPECULAR ACTUALLY READS, against the curve it came from.
//
// `drawSpecular` feeds `p[1]`,`p[2]` straight into
// `lit = saturate((dot(direction, normal) - spread) / max(1 - spread, 2^-10))`,
// so an error in the stored direction is an error in how lit the pixel is. The
// cone is a cosine: at `spread' = 0.5` a ten-degree wobble in the normal moves
// `lit` by about a sixth of its range, and it moves it DIFFERENTLY from one
// texel to the next.
//
// That is what this case exists to stop. A circle's normal is radial and known
// in closed form, so the error is measurable rather than a matter of taste, and
// the bound is stated in degrees because degrees are what the shader consumes.
TEST_CASE(the_grid_fields_normal_follows_the_curve_and_not_the_lattice) {
    const double cx = 64.0, cy = 64.0, r = 44.0;
    const std::vector<FieldContour> cs = {circleContour(static_cast<float>(cx),
                                                        static_cast<float>(cy),
                                                        static_cast<float>(r), 2048)};
    const FieldImage f = generateFieldFromContours(cs, 128, 128);
    REQUIRE(f.width == 128);

    double sum = 0.0, worst = 0.0;
    long n = 0;
    for (std::uint32_t y = 0; y < f.height; ++y) {
        for (std::uint32_t x = 0; x < f.width; ++x) {
            const double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
            const double rad = std::sqrt(dx * dx + dy * dy);
            const double depth = r - rad;   // positive inside
            if (depth <= 1.0 || depth >= 16.0) continue;
            const float* p = f.at(x, y);
            double dot = (p[1] * dx + p[2] * dy) / rad;
            if (dot > 1.0) dot = 1.0;
            if (dot < -1.0) dot = -1.0;
            const double deg = std::acos(dot) * 180.0 / 3.14159265358979323846;
            sum += deg;
            if (deg > worst) worst = deg;
            ++n;
        }
    }
    REQUIRE(n > 1000);
    std::printf("    normal vs the circle: mean %.2f deg, worst %.2f deg over %ld texels\n",
                sum / n, worst, n);
    // Measured 1.31 / 10.48 with the foot-point refinement in place, 2.58 /
    // 10.48 with the query's own 3x3 instead, and 7.81 / 33.85 with the normal
    // taken off the lattice. The bound sits between the first two, so BOTH of
    // those regressions are visible here and not only the loud one.
    CHECK(sum / n < 2.0);
    CHECK(worst < 12.0);
}

// THE ONE PLACE THE DIFFERENCE MUST NOT WIN: the medial axis of a thin shape.
//
// Where two opposite faces of the same shape tie, the field creases: the true
// gradient is `-y` on one side of the crease and `+y` on the other, and a
// difference taken ACROSS it returns their sum, which is nearly zero. The
// length carries no information there and normalising it would turn float
// residue into a direction -- a confident normal where the geometry has an
// ambiguity.
//
// A seven-pixel bar is the smallest honest version of the case, and it is not a
// contrivance: icon art is full of strokes this thin. Below the floor the
// generator keeps the vector to the seed, which still points across the bar.
TEST_CASE(the_medial_axis_of_a_thin_bar_keeps_the_seed_vector_not_a_cancelled_difference) {
    FieldContour bar;
    bar.xy = {16.0f, 60.0f, 112.0f, 60.0f, 112.0f, 67.0f, 16.0f, 67.0f};
    const FieldImage f = generateFieldFromContours({bar}, 128, 128);
    REQUIRE(f.width == 128);

    // Both rows either side of the crease at y = 63.5, away from the two ends
    // where the bar's own caps have a say.
    int sideways = 0, notUnit = 0, n = 0;
    double worstNy = 1.0;
    for (std::uint32_t y = 63; y <= 64; ++y) {
        for (std::uint32_t x = 24; x < 104; ++x) {
            const float* p = f.at(x, y);
            const double len = std::sqrt(static_cast<double>(p[1]) * p[1] +
                                         static_cast<double>(p[2]) * p[2]);
            // EVERY comparison here is written so that a NaN FAILS it. Dropping
            // the floor does not produce a wrong direction, it produces `0/0`,
            // and a NaN quietly answers `false` to `<`, `>` and `==` alike --
            // the first version of this case asserted `ny < 0.9` and passed
            // against a field that was entirely NaN.
            if (!(std::fabs(len - 1.0) <= 1e-5)) ++notUnit;
            const double ny = std::fabs(static_cast<double>(p[2]));
            if (!(ny >= worstNy)) worstNy = ny;
            if (!(ny >= 0.9)) ++sideways;
            ++n;
        }
    }
    REQUIRE(n == 160);
    std::printf("    thin bar's crease: worst |ny| %.4f over %d texels\n", worstNy, n);
    CHECK_EQ(notUnit, 0);
    // The normal crosses the bar. It never turns to run ALONG it, which is what
    // a cancelled difference would produce.
    CHECK_EQ(sideways, 0);
}

// THE SUB-TEXEL SEED KNOB, PINNED WHILE IT IS SWITCHED OFF.
//
// `kFieldSuperSample` is 1 at both call sites and the comment above it carries
// the measurement that decided it: `ss = 3` quarters the angular error of the
// normal and does NOT move the render any closer to `apple-512.png`, so it is
// not worth 2.4x-2.6x the render. See `Docs/Laudos/2026-09-15-supersample-campo.md`.
//
// A knob nobody turns is a knob that rots. Nothing in the suite would notice if
// a rewrite of the transform quietly stopped honouring `superSample`, because no
// render passes anything but 1 -- and the day somebody revisits the decision the
// first thing they need is for the knob to still work. So the two claims the
// decision RESTS on are gated here, at 128 px where they cost nothing:
//
//   1. (UNTIL 29/09/2026) `ss = 3` really did sharpen the normal -- 1.31/10.48
//      deg at `ss = 1` against 0.31/1.42 at `ss = 3`, on the grid field. The
//      contour field is now EXACT (`generateField`'s numbers), so there is
//      nothing left for a finer grid to sharpen: every factor measures
//      0.05/0.09 deg, which is the 2048-gon's own departure from the circle.
//      What is gated now is that the knob moves NOTHING and that the normal is
//      the exact one; and
//   2. `ss = 2` is NOT a middle ground -- it is `ss = 1` bit for bit, because
//      the generator rounds an even factor down to the odd below. That trap is
//      worth a case of its own: the next reader's first instinct is to try 2,
//      and it would look like "supersampling does nothing" rather than like
//      "2 means 1".
TEST_CASE(the_supersample_knob_no_longer_moves_the_exact_field_and_2_still_means_1) {
    const double cx = 64.0, cy = 64.0, r = 44.0;
    const std::vector<FieldContour> cs = {circleContour(static_cast<float>(cx),
                                                        static_cast<float>(cy),
                                                        static_cast<float>(r), 2048)};
    // mean and worst angle against the radial closed form, over the band the
    // specular actually reads.
    auto measure = [&](std::uint32_t ss, double& mean, double& worst) {
        const FieldImage f = generateFieldFromContours(cs, 128, 128, FieldOptions{}, ss);
        REQUIRE(f.width == 128);
        double sum = 0.0;
        long n = 0;
        worst = 0.0;
        for (std::uint32_t y = 0; y < f.height; ++y) {
            for (std::uint32_t x = 0; x < f.width; ++x) {
                const double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
                const double rad = std::sqrt(dx * dx + dy * dy);
                const double depth = r - rad;
                if (depth <= 1.0 || depth >= 16.0) continue;
                const float* p = f.at(x, y);
                double dot = (p[1] * dx + p[2] * dy) / rad;
                dot = dot > 1.0 ? 1.0 : (dot < -1.0 ? -1.0 : dot);
                sum += std::acos(dot) * 180.0 / 3.14159265358979323846;
                if (std::acos(dot) * 180.0 / 3.14159265358979323846 > worst)
                    worst = std::acos(dot) * 180.0 / 3.14159265358979323846;
                ++n;
            }
        }
        REQUIRE(n > 1000);
        mean = sum / n;
    };

    double m1 = 0.0, w1 = 0.0, m2 = 0.0, w2 = 0.0, m3 = 0.0, w3 = 0.0;
    measure(1, m1, w1);
    measure(2, m2, w2);
    measure(3, m3, w3);
    std::printf("    supersample: ss=1 %.2f/%.2f  ss=2 %.2f/%.2f  ss=3 %.2f/%.2f deg\n",
                m1, w1, m2, w2, m3, w3);

    // An even factor is rounded down to the odd below, so 2 IS 1 -- exactly,
    // not approximately. Written as `==` on purpose: anything else would mean
    // the rounding changed and the header's promise about the pixel centre no
    // longer holds.
    CHECK(m2 == m1);
    CHECK(w2 == w1);

    // An exact field has nothing a finer sign mask could sharpen: 3 is 1 too,
    // and both are the closed form to within the polygon's own flattening.
    CHECK(m3 == m1);
    CHECK(w3 == w1);
    CHECK(m1 < 0.2);
    CHECK(w1 < 0.5);
}

// ---- part four: one field for a group ------------------------------------

// `[BIN]` THE STACK, TEXEL BY TEXEL. `IconRendering` `0x11480` draws an upper
// element's field inside a clip made from that same field -- a GradientMap whose
// two stops sit one half-float step apart at `d = maxDistance + 1`
// (`0x11580`-`0x115D0`) -- and, with `useAdvancedStacking`, first merges it with
// blend `0x3F9`, which is `sdf_maximum`: `src.r > dst.r ? src : dst` (RenderBox
// `default_mod66.ll` %582-%586). `DistanceField.h`, part four, carries the
// reading and what in it is inference.
//
// The fixture is one row whose two fields disagree in every channel, so a texel
// that came from the wrong side -- or a texel stitched together from both -- is
// visible in whichever channel is looked at.
TEST_CASE(stacked_fields_take_the_upper_texel_inside_its_dilated_footprint) {
    //                 d      gx    gy   cov
    const float lowerTexels[][4] = {
        {-9.0f, 1.0f, 0.0f, 1.0f},   // 0: deep inside the lower element
        {-9.0f, 1.0f, 0.0f, 1.0f},   // 1
        {-9.0f, 1.0f, 0.0f, 1.0f},   // 2
        {40.0f, 1.0f, 0.0f, 0.0f},   // 3: far outside the lower one
        {40.0f, 1.0f, 0.0f, 0.0f},   // 4
        {2.5f, 1.0f, 0.0f, 0.0f},    // 5
    };
    const float upperTexels[][4] = {
        {-3.0f, 0.0f, 1.0f, 1.0f},   // 0: inside the upper element
        {4.0f, 0.0f, 1.0f, 0.0f},    // 1: outside it, but inside its reach + 1
        {30.0f, 0.0f, 1.0f, 0.0f},   // 2: beyond the reach, and the lower is inside
        {30.0f, 0.0f, 1.0f, 0.0f},   // 3: beyond the reach, and the upper is NEARER
        {50.0f, 0.0f, 1.0f, 0.0f},   // 4: beyond the reach, and the lower is nearer
        {5.0f, 0.0f, 1.0f, 0.0f},    // 5: exactly ON the edge, reach + 1
    };
    FieldImage lower, upper;
    lower.width = upper.width = 6;
    lower.height = upper.height = 1;
    for (int t = 0; t < 6; ++t) {
        lower.rgba.insert(lower.rgba.end(), lowerTexels[t], lowerTexels[t] + 4);
        upper.rgba.insert(upper.rgba.end(), upperTexels[t], upperTexels[t] + 4);
    }
    const float reach = 4.0f;   // so the edge is at d = 5

    auto came = [&](const FieldImage& f, int t) {
        // The gradient's two channels name the side: (1, 0) lower, (0, 1) upper.
        return f.at(static_cast<std::uint32_t>(t), 0)[2] == 1.0f ? 'U' : 'L';
    };
    const FieldImage plain = stackFields(lower, upper, reach, false);
    const FieldImage advanced = stackFields(lower, upper, reach, true);
    REQUIRE(plain.width == 6 && advanced.width == 6);

    // Inside the dilated footprint the upper texel wins, either way -- over the
    // lower element's interior too, which is the cost `kFieldStackNote` names.
    CHECK_EQ(came(plain, 0), 'U');
    CHECK_EQ(came(plain, 1), 'U');
    CHECK_EQ(came(advanced, 0), 'U');
    CHECK_EQ(came(advanced, 1), 'U');
    // Beyond it, without the advanced merge, the lower field is left alone.
    CHECK_EQ(came(plain, 2), 'L');
    CHECK_EQ(came(plain, 3), 'L');
    CHECK_EQ(came(plain, 4), 'L');
    // With it, the texel that is further INSIDE wins -- the smaller distance.
    CHECK_EQ(came(advanced, 2), 'L');
    CHECK_EQ(came(advanced, 3), 'U');
    CHECK_EQ(came(advanced, 4), 'L');
    // The edge itself belongs to the lower field: the gradient still answers
    // with its first stop there. (And 5.0 is not below the lower's 2.5.)
    CHECK_EQ(came(plain, 5), 'L');
    CHECK_EQ(came(advanced, 5), 'L');

    // A WHOLE texel, never a mix: every channel of the result is the winner's.
    for (int t = 0; t < 6; ++t) {
        for (const FieldImage* f : {&plain, &advanced}) {
            const float* want = came(*f, t) == 'U' ? upperTexels[t] : lowerTexels[t];
            for (int k = 0; k < 4; ++k) {
                CHECK_EQ(f->at(static_cast<std::uint32_t>(t), 0)[k], want[k]);
            }
        }
    }

    // An empty field stacks to the other one, and two grids that do not match
    // are nobody's field.
    CHECK_EQ(stackFields(FieldImage{}, upper, reach, true).width, upper.width);
    CHECK_EQ(stackFields(lower, FieldImage{}, reach, true).width, lower.width);
    FieldImage shifted = upper;
    shifted.originX = 3;
    CHECK_EQ(stackFields(lower, shifted, reach, true).width, std::uint32_t{0});
}

// THE SILHOUETTE OF A CONTOUR SET, which a group lit as one shape joins into a
// union before taking its field (`fieldCoverageFromContours`). OURS, so the
// oracle is a count done by hand: `samples * samples` evenly spaced points per
// pixel, each inside or not, box-averaged.
//
// A rectangle from (1.5, 2.25) to (5.5, 6.0) on an 8 x 8 grid at four samples a
// side. Along x the sample points of pixel 1 are 1.125, 1.375, 1.625 and 1.875:
// two of four are right of 1.5. Along y those of pixel 2 are 2.125, 2.375, 2.625
// and 2.875: three of four are below 2.25. So the corner pixel (1, 2) is
// `2/4 * 3/4 = 0.375`, an edge pixel is a half or three quarters, and the inside
// is exactly 1.
TEST_CASE(a_contours_coverage_is_the_fraction_of_its_sample_points_inside) {
    FieldContour rect;
    rect.xy = {1.5f, 2.25f, 5.5f, 2.25f, 5.5f, 6.0f, 1.5f, 6.0f};
    FieldOptions fo;
    std::vector<float> coverage;
    const std::size_t inside = fieldCoverageFromContours({rect}, 8, 8, fo, 4, coverage);
    REQUIRE(coverage.size() == 64);
    // 4 x 3.75 pixels of area, sixteen samples a pixel.
    CHECK_EQ(inside, std::size_t{240});
    auto at = [&](int x, int y) { return coverage[static_cast<std::size_t>(y) * 8 + x]; };
    CHECK_EQ(at(1, 2), 0.375f);   // the corner
    CHECK_EQ(at(5, 2), 0.375f);
    CHECK_EQ(at(3, 2), 0.75f);    // the top edge
    CHECK_EQ(at(1, 4), 0.5f);     // the left edge
    CHECK_EQ(at(5, 4), 0.5f);     // the right edge, [5, 5.5)
    CHECK_EQ(at(3, 4), 1.0f);     // inside: exactly one, not fifteen sixteenths rounded
    CHECK_EQ(at(3, 5), 1.0f);     // the bottom edge sits ON a pixel boundary
    CHECK_EQ(at(3, 6), 0.0f);
    CHECK_EQ(at(0, 4), 0.0f);
    CHECK_EQ(at(6, 4), 0.0f);

    // One sample a side is the inside mask itself, as zeros and ones.
    std::vector<float> hard;
    std::vector<char> mask;
    CHECK(fieldCoverageFromContours({rect}, 8, 8, fo, 1, hard) > 0);
    CHECK(fieldInsideMask({rect}, 8, 8, fo, 1, mask) > 0);
    REQUIRE(hard.size() == mask.size());
    for (std::size_t t = 0; t < hard.size(); ++t) CHECK_EQ(hard[t], mask[t] ? 1.0f : 0.0f);

    // A VIEWPORT'S coverage is the full grid's, pixel for pixel: the sample
    // points are absolute and only the index moves.
    FieldOptions part = fo;
    part.originX = 1;
    part.originY = 2;
    std::vector<float> cropped;
    CHECK(fieldCoverageFromContours({rect}, 5, 4, part, 4, cropped) > 0);
    REQUIRE(cropped.size() == 20);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 5; ++x) {
            CHECK_EQ(cropped[static_cast<std::size_t>(y) * 5 + x], at(x + 1, y + 2));
        }
    }

    // A contour that covers no sample point has no silhouette, and says so by
    // being empty rather than by being all zeros.
    FieldContour sliver;
    sliver.xy = {3.01f, 3.01f, 3.02f, 3.01f, 3.02f, 3.02f, 3.01f, 3.02f};
    std::vector<float> none;
    CHECK_EQ(fieldCoverageFromContours({sliver}, 8, 8, fo, 4, none), std::size_t{0});
    CHECK(none.empty());
}
