// The gradient: GPU against the oracle, bit for bit.
//
// The shader this runs is `Gradient.glsl` -- the SAME file the renderer will
// include -- so what draws and what is verified cannot drift apart. That is the
// arrangement every transcribed stage in this repository uses.
//
// THE FINDING THIS PINS. `Gradient::value` and `Gradient::fold_value` both
// switch on `(word0 >> 19) & 15`, and grouping the sixteen cases the two
// different ways decomposes the field into FOUR GEOMETRIES BY FOUR SPREADS with
// nothing left over (doc 03 §23.1). The whole table is checked below, because
// the table IS the measurement: a transcription that got one cell wrong would
// still draw a plausible gradient.
#include "check.h"
#include "Source/RenderBox/Compute.h"
#include "Source/RenderBox/GradientOracle.h"
#include "Source/RenderBox/PathVertexOracle.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <cstring>
#include <string>
#include <vector>

static const std::uint32_t kGradientSpirv[] =
#include "gradient_probe.comp.inc"
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

// Mirrors `Input` in gradient_probe.comp. std430 puts each vec4 on a 16-byte
// boundary, which is why the scalars are grouped and padded by hand rather than
// left to chance.
struct Probe {
    float point[2]{0, 0};
    float a = 0, b = 0;
    float lo[4]{0, 0, 0, 0};
    float hi[4]{0, 0, 0, 0};
    float gamma = 1.0f;
    float t = 0.0f;
    std::uint32_t pad0 = 0;
    std::uint32_t pad1 = 0;
};

struct Control {
    std::uint32_t count = 0;
    std::uint32_t stage = 0;
    std::uint32_t state = 0;
};

std::vector<float> onGpu(Device& d, const std::vector<Probe>& in, std::uint32_t stage,
                         std::uint32_t state) {
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

    auto pass = ComputePass::create(d, kGradientSpirv, sizeof kGradientSpirv, sizeof(Control));
    if (!pass) {
        std::printf("  FAIL compute pass: %s\n", pass.error().c_str());
        ++ictest::failures();
        return {};
    }
    Control c;
    c.count = static_cast<std::uint32_t>(in.size());
    c.stage = stage;
    c.state = state;
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

// WHERE BIT-FOR-BIT IS NOT AVAILABLE, AND WHY THE ALLOWANCE IS THIS NARROW.
//
// The LINEAR geometry is exact -- it is a coordinate, and the GPU and the oracle
// agree on every bit. The RADIAL one is not, and cannot be: it goes through a
// square root, and Vulkan specifies `sqrt` only to **3 ULP** with no SPIR-V
// control that makes it exact. The same limit already shows in the path vertex
// stage, which is why `ulpsApart` exists rather than being invented here.
//
// So the allowance is 3 ULP and it applies ONLY to the geometries that take a
// root. A blanket tolerance would be the defect the sweep's "the ULP comparison
// accepts any two floats" mutation exists to catch: it would let a real
// transcription error through wearing the costume of a rounding difference.
constexpr std::uint32_t kSqrtUlps = 3;

// AND WHY IT IS MEASURED AT THE ROOT RATHER THAN AT THE RESULT.
//
// The radial geometry is `sqrt(dot(p,p)) * a + b`. That last addition is a
// SUBTRACTION whenever `a` and `b` have opposite signs, and near the circle
// where the two terms cancel the result is small while the error is not: at
// a = 0.7, b = -0.25 a point of radius 0.354 gives 0.2482 - 0.25, and three ULP
// created at 0.25 read as a hundred and forty ULP at 0.0018.
//
// Counting ULP at the RESULT would therefore reject a correct transcription for
// being correct, and widening the count until it passed would make the check
// meaningless everywhere else. So the allowance is three ULP OF THE TERM THE
// ROOT PRODUCES -- an absolute bound, derived where the error is created. This
// is the same lesson the distance tolerance taught in §12: an allowance has to
// be scaled to the magnitude the error lives at, not the one it is read at.
float ulpSizeAt(float magnitude) {
    const float m = std::fabs(magnitude);
    return std::nextafter(m, std::numeric_limits<float>::infinity()) - m;
}

// `scale` is the magnitude the root's error was created at, or 0 to demand
// bit-for-bit.
bool agrees(float gpu, float cpu, float scale) {
    if (sameBits(gpu, cpu)) return true;
    if (scale == 0.0f) return false;
    return std::fabs(gpu - cpu) <= static_cast<float>(kSqrtUlps) * ulpSizeAt(scale);
}

// Only the radial geometry takes a root. Linear does not, and the three that are
// not transcribed return a literal zero -- all of those must be exact.
bool takesARoot(std::uint32_t kind) {
    return geometryOf(kind << kGradientKindShift) == Geometry::Radial;
}

// The magnitude the root's error is created at, for one sample.
float rootTerm(std::uint32_t kind, float px, float py, float a) {
    if (!takesARoot(kind)) return 0.0f;
    return std::sqrt(px * px + py * py) * a;
}

// THE GAMMA PATH, AND WHY IT IS NOT BIT-FOR-BIT EITHER.
//
// Bit 26 puts the ramp parameter through `pow`, and Vulkan does not specify
// `pow` tightly: it inherits from `exp2(y * log2(x))`, and each of those two is
// allowed 3 ULP. So the two sides round differently for the same reason `sqrt`
// does, and no SPIR-V control removes it.
//
// Eight ULP is wide enough for the transcendental and far too narrow for a
// transcription error: applying the gamma to the wrong operand, or dropping one
// of the two saturates around it, moves the result by percent, not by ULP. The
// test below pins exactly that.
constexpr std::uint32_t kPowUlps = 8;

bool agreesUlps(float gpu, float cpu, std::uint32_t maxUlps) {
    if (sameBits(gpu, cpu)) return true;
    return rb::ulpsApart(gpu, cpu) <= maxUlps;
}

std::uint32_t stateFor(std::uint32_t kind) { return kind << kGradientKindShift; }

}  // namespace

// The whole 4 x 4 table, checked cell by cell. This is the measurement, so it is
// the thing most worth pinning: a single wrong cell still draws a gradient.
TEST_CASE(the_four_bit_field_is_four_geometries_by_four_spreads) {
    struct Row {
        std::uint32_t kind;
        Geometry geometry;
        Spread spread;
    };
    const Row table[16] = {
        {0, Geometry::Linear, Spread::Pad},      {1, Geometry::Linear, Spread::Repeat},
        {2, Geometry::Linear, Spread::Reflect},  {3, Geometry::Radial, Spread::Pad},
        {4, Geometry::Radial, Spread::Repeat},   {5, Geometry::Radial, Spread::Reflect},
        {6, Geometry::Conic, Spread::None},      {7, Geometry::Linear, Spread::None},
        {8, Geometry::Radial, Spread::None},     {9, Geometry::Focal, Spread::Pad},
        {10, Geometry::Focal, Spread::Repeat},   {11, Geometry::Focal, Spread::Reflect},
        {12, Geometry::Focal, Spread::None},     {13, Geometry::FromVertex, Spread::Pad},
        {14, Geometry::FromVertex, Spread::Repeat},
        {15, Geometry::FromVertex, Spread::Reflect},
    };
    for (const Row& r : table) {
        const std::uint32_t s = stateFor(r.kind);
        CHECK(geometryOf(s) == r.geometry);
        CHECK(spreadOf(s) == r.spread);
    }
    // The field is FOUR BITS: anything above it must not reach the selector.
    CHECK(geometryOf(stateFor(0) | 0xFFFFFFFFu & ~(15u << kGradientKindShift)) ==
          Geometry::Linear);
}

// The three spreads that do something, on values that tell them apart. `pad` and
// `repeat` agree inside [0,1] and differ outside it; `reflect` differs from both
// on the second period, which is why the samples run past 2.
TEST_CASE(the_spread_agrees_with_the_gpu) {
    Device& d = gpu();
    if (!d.valid()) return;
    std::vector<Probe> in;
    for (float t = -2.4f; t <= 2.6f; t += 0.13f) {
        Probe p;
        p.t = t;
        in.push_back(p);
    }
    for (std::uint32_t kind : {0u, 1u, 2u, 7u}) {
        const std::uint32_t state = stateFor(kind);
        const std::vector<float> got = onGpu(d, in, 0u, state);
        REQUIRE(got.size() == in.size() * 4);
        int bad = 0;
        for (std::size_t i = 0; i < in.size(); ++i) {
            const float want = foldValue(state, in[i].t);
            if (!sameBits(got[i * 4], want)) {
                if (bad < 3) {
                    std::printf("  FAIL kind=%u t=%.4f  gpu %.9g  cpu %.9g\n", kind, in[i].t,
                                got[i * 4], want);
                }
                ++bad;
            }
        }
        CHECK_EQ(bad, 0);
    }
}

// The two geometries that ARE transcribed, and the `covered` flag for the three
// that are not. A transcription that returned 0 with `covered` true would paint
// the ramp's first colour over the whole plane, which looks like a gradient.
TEST_CASE(the_geometry_agrees_with_the_gpu_and_names_what_it_does_not_cover) {
    Device& d = gpu();
    if (!d.valid()) return;
    std::vector<Probe> in;
    for (float x = -3.0f; x <= 3.0f; x += 0.41f) {
        for (float y = -3.0f; y <= 3.0f; y += 0.37f) {
            Probe p;
            p.point[0] = x;
            p.point[1] = y;
            p.a = 0.7f;
            p.b = -0.25f;
            in.push_back(p);
        }
    }
    for (std::uint32_t kind = 0; kind < 16; ++kind) {
        const std::uint32_t state = stateFor(kind);
        const std::vector<float> got = onGpu(d, in, 1u, state);
        REQUIRE(got.size() == in.size() * 4);
        int bad = 0;
        for (std::size_t i = 0; i < in.size(); ++i) {
            bool covered = false;
            const float want = gradientValue(state, in[i].point[0], in[i].point[1], in[i].a,
                                             in[i].b, &covered);
            const bool gpuCovered = got[i * 4 + 1] != 0.0f;
            const float scale = rootTerm(kind, in[i].point[0], in[i].point[1], in[i].a);
            if (!agrees(got[i * 4], want, scale) || gpuCovered != covered) {
                if (bad < 3) {
                    std::printf("  FAIL kind=%u p=(%.3f,%.3f)  gpu %.9g/%d  cpu %.9g/%d\n",
                                kind, in[i].point[0], in[i].point[1], got[i * 4],
                                static_cast<int>(gpuCovered), want, static_cast<int>(covered));
                }
                ++bad;
            }
        }
        CHECK_EQ(bad, 0);
    }
}

// Ramp kind 0 -- the two-colour mix, which is what the document's
// `linear-gradient` is: 48 of 48 in the corpus carry exactly two stops.
TEST_CASE(the_two_colour_ramp_agrees_with_the_gpu) {
    Device& d = gpu();
    if (!d.valid()) return;
    std::vector<Probe> in;
    for (float t = -0.3f; t <= 1.3f; t += 0.07f) {
        Probe p;
        p.t = t;
        p.lo[0] = 0.2f; p.lo[1] = 0.4f; p.lo[2] = 0.9f; p.lo[3] = 1.0f;
        p.hi[0] = 1.0f; p.hi[1] = 0.1f; p.hi[2] = 0.0f; p.hi[3] = 0.5f;
        p.gamma = 2.2f;
        in.push_back(p);
    }
    // With the gamma bit clear and set: the bit is the difference, and a
    // transcription that ignored it would pass the first pass and fail the
    // second.
    for (std::uint32_t extra : {0u, static_cast<std::uint32_t>(kStopGamma)}) {
        const std::uint32_t state = stateFor(0) | extra;
        const std::vector<float> got = onGpu(d, in, 2u, state);
        REQUIRE(got.size() == in.size() * 4);
        int bad = 0;
        // Without the gamma bit the ramp is add and multiply only, and the two
        // sides agree on every BIT. With it, the parameter goes through `pow`.
        const std::uint32_t allow = extra ? kPowUlps : 0u;
        std::uint32_t worst = 0;
        for (std::size_t i = 0; i < in.size(); ++i) {
            float want[4];
            rampTwoColour(state, in[i].t, in[i].lo, in[i].hi, in[i].gamma, want);
            for (int k = 0; k < 4; ++k) {
                if (!sameBits(got[i * 4 + k], want[k])) {
                    worst = std::max(worst, rb::ulpsApart(got[i * 4 + k], want[k]));
                }
                if (!agreesUlps(got[i * 4 + k], want[k], allow)) {
                    if (bad < 3) {
                        std::printf("  FAIL gamma=%u t=%.3f k=%d  gpu %.9g  cpu %.9g\n",
                                    extra ? 1u : 0u, in[i].t, k, got[i * 4 + k], want[k]);
                    }
                    ++bad;
                }
            }
        }
        // Printed so a regression shows as a NUMBER MOVING rather than as a
        // pass/fail flip: an allowance nobody watches drifts.
        std::printf("  ramp gamma=%u: worst disagreement %u ULP (allowed %u)\n",
                    extra ? 1u : 0u, worst, allow);
        CHECK_EQ(bad, 0);
    }
}

// The chain a caller actually runs: geometry, spread, ramp. A stage gate cannot
// see an ordering error, and the order is measured -- `Gradient::color` calls
// `fold_value` BEFORE it looks at the ramp kind.
TEST_CASE(the_whole_chain_agrees_with_the_gpu) {
    Device& d = gpu();
    if (!d.valid()) return;
    std::vector<Probe> in;
    for (float x = -2.0f; x <= 2.0f; x += 0.31f) {
        Probe p;
        p.point[0] = x;
        p.point[1] = 0.6f;
        p.a = 1.3f;
        p.b = 0.2f;
        p.lo[0] = 0.0f; p.lo[1] = 0.5f; p.lo[2] = 1.0f; p.lo[3] = 1.0f;
        p.hi[0] = 1.0f; p.hi[1] = 1.0f; p.hi[2] = 0.0f; p.hi[3] = 0.25f;
        p.gamma = 1.0f;
        in.push_back(p);
    }
    for (std::uint32_t kind : {0u, 1u, 2u, 3u, 4u, 5u, 9u}) {
        const std::uint32_t state = stateFor(kind);
        const std::vector<float> got = onGpu(d, in, 3u, state);
        REQUIRE(got.size() == in.size() * 4);
        int bad = 0;
        for (std::size_t i = 0; i < in.size(); ++i) {
            bool covered = false;
            const float t = gradientValue(state, in[i].point[0], in[i].point[1], in[i].a,
                                          in[i].b, &covered);
            float want[4] = {0, 0, 0, 0};
            if (covered) {
                rampTwoColour(state, foldValue(state, t), in[i].lo, in[i].hi, in[i].gamma,
                              want);
            }
            for (int k = 0; k < 4; ++k) {
                // A colour mixed from a parameter that came through a root
                // carries the root's slack, so the allowance follows the
                // geometry rather than the stage.
                const float scale =
                    rootTerm(kind, in[i].point[0], in[i].point[1], in[i].a);
                if (!agrees(got[i * 4 + k], want[k], scale)) {
                    if (bad < 3) {
                        std::printf("  FAIL kind=%u x=%.3f k=%d  gpu %.9g  cpu %.9g\n", kind,
                                    in[i].point[0], k, got[i * 4 + k], want[k]);
                    }
                    ++bad;
                }
            }
        }
        CHECK_EQ(bad, 0);
    }
}

// The uniform stop table, on the CPU: the target's own layout, and the stride
// that bit 26 changes.
TEST_CASE(the_uniform_stop_table_walks_the_targets_layout) {
    std::vector<Stop> stops(3);
    stops[0].rgba[0] = 0.0f; stops[0].rgba[3] = 1.0f; stops[0].gamma = 2.0f;
    stops[1].rgba[0] = 0.5f; stops[1].rgba[3] = 1.0f; stops[1].gamma = 2.0f;
    stops[2].rgba[0] = 1.0f; stops[2].rgba[3] = 1.0f; stops[2].gamma = 2.0f;

    float out[4];
    rampUniform(0u, 0.0f, stops, out);
    CHECK_EQ(out[0], 0.0f);
    rampUniform(0u, 1.5f, stops, out);
    CHECK_EQ(out[0], 0.75f);          // halfway from stop 1 to stop 2
    rampUniform(0u, 2.0f, stops, out);
    CHECK_EQ(out[0], 1.0f);
    // Past the end it holds the last stop rather than reading off the table.
    rampUniform(0u, 9.0f, stops, out);
    CHECK_EQ(out[0], 1.0f);
    rampUniform(0u, -4.0f, stops, out);
    CHECK_EQ(out[0], 0.0f);

    // `[BIN]` Bit 26 raises the FRACTION to the stop's own exponent, so the
    // midpoint stops being the midpoint.
    rampUniform(kStopGamma, 1.5f, stops, out);
    CHECK(std::fabs(out[0] - (0.5f + 0.5f * 0.25f)) < 1e-6f);
}

// The allowance has to be narrow enough to still fail. Three ULP is a rounding
// difference; a transcription error is not, and this pins the difference so the
// tolerance cannot quietly become "any two floats".
TEST_CASE(the_ulp_allowance_is_narrow_enough_to_still_reject) {
    const float scale = 1.85f;
    const float step = ulpSizeAt(scale);

    CHECK(agrees(1.0f, 1.0f + 3.0f * step, scale));    // exactly at the limit
    CHECK(!agrees(1.0f, 1.0f + 5.0f * step, scale));   // past it
    CHECK(!agrees(1.0f, 1.0f + 3.0f * step, 0.0f));    // never, where exact is required

    // The point of measuring at the ROOT rather than the result: a value that
    // cancelled down to near zero still gets the allowance the root earned, and
    // a ULP count taken at the result would have rejected it.
    CHECK(agrees(-0.00172208378f, -0.00172206759f, 0.248f));
    CHECK(rb::ulpsApart(-0.00172208378f, -0.00172206759f) > 100u);

    // And the thing a blanket tolerance would wave through.
    CHECK(!agrees(0.5f, 0.6f, scale));
}

// The gamma allowance has to reject a TRANSCRIPTION error while tolerating the
// transcendental. These are the three ways the gamma could be written wrong, and
// each moves the answer far past eight ULP.
TEST_CASE(the_gamma_allowance_rejects_a_wrong_transcription) {
    const float lo[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const float hi[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    const std::uint32_t state = stateFor(0) | kStopGamma;
    float right[4];
    rampTwoColour(state, 0.4f, lo, hi, 2.2f, right);

    // The gamma applied to the wrong operand -- gamma raised to t, not t to
    // gamma.
    const float swapped = std::pow(2.2f, 0.4f);
    CHECK(!agreesUlps(swapped, right[0], kPowUlps));

    // The gamma dropped entirely.
    CHECK(!agreesUlps(0.4f, right[0], kPowUlps));

    // And what the allowance IS for: a few ULP of transcendental slack.
    float nudged = right[0];
    for (std::uint32_t i = 0; i < kPowUlps; ++i) {
        std::uint32_t bits;
        std::memcpy(&bits, &nudged, sizeof bits);
        ++bits;
        std::memcpy(&nudged, &bits, sizeof nudged);
    }
    CHECK(agreesUlps(nudged, right[0], kPowUlps));
    for (int i = 0; i < 4; ++i) {
        std::uint32_t bits;
        std::memcpy(&bits, &nudged, sizeof bits);
        ++bits;
        std::memcpy(&nudged, &bits, sizeof nudged);
    }
    CHECK(!agreesUlps(nudged, right[0], kPowUlps));
}

// The ends of a stop table HOLD, and holding is not the same as extrapolating.
//
// The first version of `rampAtPositions` guarded both ends with an early return,
// and the mutation sweep proved the guards were redundant: the saturate on the
// segment parameter already does it. So they were removed, and what is pinned
// here is the behaviour rather than the branch -- past the last stop the colour
// stays put, and a version that dropped the saturate would run PAST the last
// stop's colour instead.
TEST_CASE(a_stop_table_holds_at_both_ends_rather_than_extrapolating) {
    std::vector<RampPoint> stops(2);
    stops[0].location = 0.25f;
    stops[0].rgba[0] = 0.0f;
    stops[1].location = 0.75f;
    stops[1].rgba[0] = 1.0f;

    float out[4];
    // Inside, the ramp is linear: halfway between the two stops is 0.5.
    rampAtPositions(stops, 0.5f, out);
    CHECK(std::fabs(out[0] - 0.5f) < 1e-6f);

    // Past the last stop it HOLDS. Without the saturate this would be 1.5 at
    // t = 1.0 and 3.0 at t = 2.0 -- a colour no stop names.
    rampAtPositions(stops, 0.75f, out);
    CHECK_EQ(out[0], 1.0f);
    rampAtPositions(stops, 1.0f, out);
    CHECK_EQ(out[0], 1.0f);
    rampAtPositions(stops, 5.0f, out);
    CHECK_EQ(out[0], 1.0f);

    // And before the first, the same the other way: not a negative colour.
    rampAtPositions(stops, 0.25f, out);
    CHECK_EQ(out[0], 0.0f);
    rampAtPositions(stops, 0.0f, out);
    CHECK_EQ(out[0], 0.0f);
    rampAtPositions(stops, -3.0f, out);
    CHECK_EQ(out[0], 0.0f);

    // One stop is a constant, and it is the case that cannot fall through the
    // segment scan at all.
    std::vector<RampPoint> single(1);
    single[0].location = 0.5f;
    single[0].rgba[1] = 0.7f;
    rampAtPositions(single, -1.0f, out);
    CHECK_EQ(out[1], 0.7f);
    rampAtPositions(single, 9.0f, out);
    CHECK_EQ(out[1], 0.7f);

    // Two stops at the SAME position: the target's form stores `1/(off1-off0)`,
    // which is a division by zero there, so what it does is `[OBS]` -- not
    // measured. This transcription answers with the LATER stop, and that is a
    // CHOICE pinned so it cannot drift, not a reading. It is at least a defined
    // answer rather than a NaN, which is the property that matters for a
    // renderer.
    std::vector<RampPoint> step(2);
    step[0].location = 0.5f;
    step[0].rgba[2] = 0.0f;
    step[1].location = 0.5f;
    step[1].rgba[2] = 1.0f;
    rampAtPositions(step, 0.5f, out);
    CHECK_EQ(out[2], 1.0f);
    rampAtPositions(step, 0.4f, out);
    CHECK_EQ(out[2], 1.0f);
    CHECK(out[2] == out[2]);   // and never a NaN
}
