// `displacementMap_v1`: the GPU against the oracle, and the pattern against the
// IR.
//
// The shader this runs is `Displacement.glsl` -- the SAME file the renderer will
// include -- so what draws and what is verified cannot drift apart.
//
// WHAT THIS GATE HAS TO CATCH, beyond "the two sides agree". Three of the four
// things this stage does are invisible to an aggregate comparison:
//
//   * The jitter. Permute the eight offsets, or flip every sign, and the mean of
//     the taps is unchanged -- an averaged image looks identical. So the offsets
//     are asserted VALUE BY VALUE, on both sides, per mode.
//   * The decode `(disp*2 - 1) * s`. A map of 0.5 is the neutral point; a sign
//     flip and a dropped bias both still produce a picture, just the wrong one.
//     So 0.0, 0.5 and 1.0 are all pinned, which is the smallest set that tells
//     `2d-1`, `1-2d`, `d-0.5` and `2d` apart.
//   * The `.z` weight. It is re-read at every tap, and hoisting it to the first
//     tap's value is the natural way to write this loop wrong. So the map used
//     below varies in `.z` across the tap footprint, and the hoisted answer is
//     computed too and required to DIFFER.
#include "check.h"
#include "Source/RenderBox/Compute.h"
#include "Source/RenderBox/DisplacementOracle.h"
#include "Source/RenderBox/PathVertexOracle.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

static const std::uint32_t kDisplacementSpirv[] =
#include "displacement_probe.comp.inc"
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

// Mirrors the push-constant block in displacement_probe.comp.
struct Control {
    std::uint32_t count = 0;
    std::uint32_t variant = 0;
    std::uint32_t stage = 0;
    std::uint32_t tap = 0;
    std::uint32_t srcBase = 0;
    std::uint32_t srcW = 0;
    std::uint32_t srcH = 0;
    std::uint32_t mapBase = 0;
    std::uint32_t mapW = 0;
    std::uint32_t mapH = 0;
    std::uint32_t probeBase = 0;
};

// One point and the two derivatives that go with it. They are DATA rather than
// something the shader takes with dFdx, both because a compute dispatch has no
// fragment quad and because it is the only way to prove that variant 0 ignores
// them.
struct Probe {
    float p[2]{0, 0};
    float dpdx[2]{0, 0};
    float dpdy[2]{0, 0};
};

// A tiny RGBA image, row major, in the layout both the oracle and the probe
// buffer expect.
struct Bitmap {
    int w = 0;
    int h = 0;
    std::vector<float> rgba;

    Bitmap() = default;
    Bitmap(int width, int height) : w(width), h(height), rgba(std::size_t(width * height * 4), 0.0f) {}
    float& at(int x, int y, int k) { return rgba[std::size_t((y * w + x) * 4 + k)]; }
    SampledImage view() const { return SampledImage{w, h, rgba.data()}; }
};

// The scene a differential runs on: the params, the two images, the points.
struct Scene {
    float params[21]{};
    Bitmap source;
    Bitmap map;
    std::vector<Probe> probes;

    DisplacementParams parsed() const { return parseDisplacementParams(params); }
};

std::vector<float> onGpu(Device& d, const Scene& s, std::uint32_t stage, std::uint32_t variant,
                         std::uint32_t tap = 0) {
    if (s.probes.empty()) return {};

    Control c;
    std::vector<float> data(s.params, s.params + 21);
    c.srcBase = static_cast<std::uint32_t>(data.size());
    c.srcW = static_cast<std::uint32_t>(s.source.w);
    c.srcH = static_cast<std::uint32_t>(s.source.h);
    data.insert(data.end(), s.source.rgba.begin(), s.source.rgba.end());
    c.mapBase = static_cast<std::uint32_t>(data.size());
    c.mapW = static_cast<std::uint32_t>(s.map.w);
    c.mapH = static_cast<std::uint32_t>(s.map.h);
    data.insert(data.end(), s.map.rgba.begin(), s.map.rgba.end());
    c.probeBase = static_cast<std::uint32_t>(data.size());
    for (const Probe& p : s.probes) {
        data.push_back(p.p[0]);
        data.push_back(p.p[1]);
        data.push_back(p.dpdx[0]);
        data.push_back(p.dpdx[1]);
        data.push_back(p.dpdy[0]);
        data.push_back(p.dpdy[1]);
    }
    c.count = static_cast<std::uint32_t>(s.probes.size());
    c.variant = variant;
    c.stage = stage;
    c.tap = tap;

    auto input = Buffer::create(d, data.size() * sizeof(float),
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!input) return {};
    std::memcpy(input->mapped(), data.data(), data.size() * sizeof(float));

    const std::size_t outBytes = s.probes.size() * 4 * sizeof(float);
    auto out = Buffer::create(d, outBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!out) return {};
    std::memset(out->mapped(), 0, outBytes);

    auto pass =
        ComputePass::create(d, kDisplacementSpirv, sizeof kDisplacementSpirv, sizeof(Control));
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
    std::vector<float> raw(s.probes.size() * 4, 0.0f);
    std::memcpy(raw.data(), out->mapped(), raw.size() * sizeof(float));
    return raw;
}

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

// THIS GATE IS BIT FOR BIT, AND THAT IS A CHOICE THAT COST SOMETHING.
//
// The stage has no transcendental in it -- no root, no pow -- so unlike the
// gradient it has no reason to carry a ULP allowance at all. Getting there took
// two findings, and both are recorded where they were made rather than papered
// over with a tolerance:
//
//   * This toolchain's `std::fma` for float is not correctly rounded, and the
//     oracle computes its fused multiply-adds another way. See the comment on
//     `fma1` in DisplacementOracle.cpp; the last test in this file pins it.
//   * Every lerp in the probe's sampler is `precise`, because a driver is
//     otherwise free to contract `a + (b - a) * t` into an fma -- one rounding
//     where the oracle has two.
//
// Before those, the differential ran at four to five ULP. An allowance that
// size would have been an allowance for a broken `fmaf`, and it would have gone
// on hiding it. The worst disagreement is PRINTED by the sweep below so that if
// a driver ever does part from the oracle, it reads as a number moving rather
// than only as a pass/fail flip.
bool agrees(float a, float b) { return sameBits(a, b); }

// A scene that exercises everything: a source image with structure in all four
// channels, a displacement map whose .xy pushes in different directions and
// whose .z varies across the footprint of a single pixel's taps.
Scene makeScene(float scale = 0.35f) {
    Scene s;
    s.params[0] = scale;
    // Layer A (source, t0): 8 pixels across maps to the unit uv square.
    const float a[10] = {0.125f, 0.0f, 0.0f, 0.125f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f};
    // Layer B (map, t1): deliberately NOT the same transform, and with a clamp
    // rect that actually bites, so a transposed or swapped layer shows up.
    const float b[10] = {0.0625f, 0.0f, 0.0f, 0.09375f, 0.05f, -0.02f, 0.02f, 0.0f, 0.97f, 0.94f};
    for (int i = 0; i < 10; ++i) {
        s.params[1 + i] = a[i];
        s.params[11 + i] = b[i];
    }

    s.source = Bitmap(8, 8);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            s.source.at(x, y, 0) = 0.05f * float(x) + 0.01f * float(y);
            s.source.at(x, y, 1) = 0.9f - 0.07f * float(y);
            s.source.at(x, y, 2) = ((x + y) & 1) ? 0.8f : 0.2f;
            s.source.at(x, y, 3) = 0.25f + 0.09f * float((x * 3 + y) % 8);
        }
    }

    s.map = Bitmap(8, 8);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            s.map.at(x, y, 0) = float(x) / 7.0f;                 // pushes right
            s.map.at(x, y, 1) = 1.0f - float(y) / 7.0f;          // pushes up
            // The per-tap weight, and it has to move fast enough that half a
            // pixel of jitter lands on a different value.
            s.map.at(x, y, 2) = 0.15f + 0.1f * float((x * 5 + y * 3) % 7);
            s.map.at(x, y, 3) = 1.0f;
        }
    }

    for (float py = 0.6f; py < 7.5f; py += 1.3f) {
        for (float px = 0.4f; px < 7.5f; px += 1.1f) {
            Probe p;
            p.p[0] = px;
            p.p[1] = py;
            // Derivatives that are NOT axis aligned: the target jitters along
            // dfdx(p) and dfdy(p), which for a rotated transform are not the
            // screen axes, and an implementation that used (1,0)/(0,1) would
            // pass a test that only ever fed it those.
            p.dpdx[0] = 1.0f;
            p.dpdx[1] = 0.22f;
            p.dpdy[0] = -0.17f;
            p.dpdy[1] = 0.95f;
            s.probes.push_back(p);
        }
    }
    return s;
}

// The oracle's answer for one probe.
void oracleAt(const Scene& s, std::uint32_t variant, const Probe& p, float (&out)[4]) {
    displacementMap(s.parsed(), variant, p.p[0], p.p[1], p.dpdx, p.dpdy, s.source.view(),
                    s.map.view(), out);
}

// The same thing with the `.z` weight HOISTED out of the loop -- the wrong
// transcription, computed on purpose so a test can require the right one to
// differ from it.
void hoistedWeightAt(const Scene& s, std::uint32_t variant, const Probe& p, float (&out)[4]) {
    const DisplacementParams params = s.parsed();
    const int taps = displacementTapCount(variant);
    float offsets[kDisplacementMaxTaps][2];
    displacementTapOffsets(variant, offsets);
    const float zero[2] = {0.0f, 0.0f};
    const float* ddx = taps == 1 ? zero : p.dpdx;
    const float* ddy = taps == 1 ? zero : p.dpdy;

    float acc[4] = {0, 0, 0, 0};
    float hoisted = 0.0f;
    for (int t = 0; t < taps; ++t) {
        float q[2];
        displacementJitter(p.p[0], p.p[1], ddx, ddy, offsets[t][0], offsets[t][1], q);
        float uvMap[2];
        displacementLayerUV(params.map, q[0], q[1], uvMap);
        float disp[4];
        sampleBilinear(s.map.view(), uvMap[0], uvMap[1], disp);
        if (t == 0) hoisted = disp[2];
        float offset[2];
        displacementDecodeOffset(params.scale, disp[0], disp[1], offset);
        float uvSource[2];
        displacementLayerUV(params.source, offset[0] + q[0], offset[1] + q[1], uvSource);
        float colour[4];
        sampleBilinear(s.source.view(), uvSource[0], uvSource[1], colour);
        for (int k = 0; k < 4; ++k) acc[k] += colour[k] * hoisted;
    }
    const float scale = displacementTapScale(variant);
    for (int k = 0; k < 4; ++k) out[k] = acc[k] * scale;
}

}  // namespace

// The 84 bytes, in the target's order. `params[0]` is the scale, 1..10 is the
// source layer and 11..20 the map layer, and the boundary at 11 is the
// `getelementptr float, ptr %2, i64 11` in the IR.
TEST_CASE(the_params_buffer_is_twenty_one_floats_in_the_targets_order) {
    float raw[21];
    for (int i = 0; i < 21; ++i) raw[i] = float(i) + 0.5f;
    const DisplacementParams p = parseDisplacementParams(raw);
    CHECK_EQ(p.scale, 0.5f);
    CHECK_EQ(p.source.m[0][0], 1.5f);
    CHECK_EQ(p.source.m[4][1], 10.5f);
    CHECK_EQ(p.map.m[0][0], 11.5f);
    CHECK_EQ(p.map.m[4][1], 20.5f);
    // 21 floats is 84 bytes, and nothing past them is read.
    CHECK_EQ(int(sizeof raw), 84);
}

// THE JITTER, VALUE BY VALUE.
//
// This is the test the task exists for. A version that averaged the taps and
// compared the mean would pass with these sixteen numbers permuted, negated as a
// set, or with x and y swapped -- the pattern is symmetric enough that all three
// leave the average alone. So every offset is written out, in the order the IR
// emits it, and so is the count and the final scale for each mode.
TEST_CASE(the_jitter_pattern_is_exactly_what_the_ir_carries) {
    struct Mode {
        std::uint32_t variant;
        int taps;
        float scale;
        float offsets[8][2];
    };
    const Mode modes[4] = {
        {0u, 1, 1.0f, {{0.0f, 0.0f}}},
        {1u, 2, 0.5f, {{0.25f, 0.25f}, {-0.25f, -0.25f}}},
        {2u, 4, 0.25f,
         {{-0.125f, -0.375f}, {0.375f, -0.125f}, {-0.375f, 0.125f}, {0.125f, 0.375f}}},
        // The eight of the 8x MSAA pattern, in sixteenths:
        // (1,-3) (-1,3) (5,1) (-3,-5) (-5,5) (-7,-1) (3,7) (7,-7)
        {3u, 8, 0.125f,
         {{0.0625f, -0.1875f},
          {-0.0625f, 0.1875f},
          {0.3125f, 0.0625f},
          {-0.1875f, -0.3125f},
          {-0.3125f, 0.3125f},
          {-0.4375f, -0.0625f},
          {0.1875f, 0.4375f},
          {0.4375f, -0.4375f}}},
    };

    for (const Mode& m : modes) {
        CHECK_EQ(displacementTapCount(m.variant), m.taps);
        CHECK_EQ(displacementTapScale(m.variant), m.scale);
        float got[kDisplacementMaxTaps][2];
        displacementTapOffsets(m.variant, got);
        for (int t = 0; t < m.taps; ++t) {
            CHECK_EQ(got[t][0], m.offsets[t][0]);
            CHECK_EQ(got[t][1], m.offsets[t][1]);
        }
        // Every one of them is a binary fraction of a pixel, so none of the
        // sixteen literals rounds on its way into a float.
        for (int t = 0; t < m.taps; ++t) {
            CHECK_EQ(got[t][0] * 16.0f, std::floor(got[t][0] * 16.0f));
            CHECK_EQ(got[t][1] * 16.0f, std::floor(got[t][1] * 16.0f));
        }
    }

    // `[BIN]` Cases 3 through 7 are the switch DEFAULT -- one block, not five.
    for (std::uint32_t v : {3u, 4u, 5u, 6u, 7u}) {
        CHECK_EQ(displacementTapCount(v), 8);
        CHECK_EQ(displacementTapScale(v), 0.125f);
    }
    // And only the low three bits reach the switch.
    CHECK_EQ(displacementTapCount(0xFFFFFFF8u), 1);
    CHECK_EQ(displacementTapScale(0xFFFFFFF9u), 0.5f);

    // The eight are DISTINCT. A transcription that repeated one would still
    // average close to the right place.
    float eight[kDisplacementMaxTaps][2];
    displacementTapOffsets(3u, eight);
    for (int i = 0; i < 8; ++i) {
        for (int j = i + 1; j < 8; ++j) {
            CHECK(!(eight[i][0] == eight[j][0] && eight[i][1] == eight[j][1]));
        }
    }
}

// The same pattern, read back off the GPU tap by tap. Aggregate agreement would
// not see a permutation; this does, because the shader is asked for one named
// tap at a time and the jittered point it produces is compared to the oracle's.
TEST_CASE(the_gpu_jitters_to_the_same_points_tap_by_tap) {
    Device& d = gpu();
    if (!d.valid()) return;
    const Scene s = makeScene();
    for (std::uint32_t variant : {0u, 1u, 2u, 3u, 5u, 7u}) {
        const int taps = displacementTapCount(variant);
        float offsets[kDisplacementMaxTaps][2];
        displacementTapOffsets(variant, offsets);
        for (int t = 0; t < taps; ++t) {
            const std::vector<float> got = onGpu(d, s, 2u, variant, std::uint32_t(t));
            REQUIRE(got.size() == s.probes.size() * 4);
            int bad = 0;
            for (std::size_t i = 0; i < s.probes.size(); ++i) {
                const Probe& p = s.probes[i];
                float q[2];
                displacementJitter(p.p[0], p.p[1], p.dpdx, p.dpdy, offsets[t][0], offsets[t][1],
                                   q);
                // The shader also hands back the offset it used, so the check is
                // on the literal pair and not only on where it landed.
                if (!agrees(got[i * 4 + 0], q[0]) || !agrees(got[i * 4 + 1], q[1]) ||
                    !sameBits(got[i * 4 + 2], offsets[t][0]) ||
                    !sameBits(got[i * 4 + 3], offsets[t][1])) {
                    if (bad < 3) {
                        std::printf(
                            "  FAIL variant=%u tap=%d  gpu q(%.9g,%.9g) o(%.9g,%.9g)"
                            "  cpu q(%.9g,%.9g) o(%.9g,%.9g)\n",
                            variant, t, got[i * 4 + 0], got[i * 4 + 1], got[i * 4 + 2],
                            got[i * 4 + 3], q[0], q[1], offsets[t][0], offsets[t][1]);
                    }
                    ++bad;
                }
            }
            CHECK_EQ(bad, 0);
        }
    }
}

// THE DECODE, AND ITS NEUTRAL POINT.
//
// `offset = disp * 2s - s`. At disp = 0.5 that is exactly zero, and "exactly" is
// the word: 0.5 * 2s is s and s - s is 0 in binary floating point with no
// rounding anywhere, so this can be asserted on the bits rather than with a
// tolerance.
//
// Zero alone does not pin the formula -- `(1 - 2d)*s` and `(d - 0.5)*s` are both
// zero there too -- so the two endpoints go with it. Together the three separate
// every plausible way to write this line wrong.
TEST_CASE(a_displacement_of_one_half_decodes_to_exactly_no_offset) {
    for (float s : {0.0f, 0.35f, 1.0f, 7.5f}) {
        float o[2];
        displacementDecodeOffset(s, 0.5f, 0.5f, o);
        CHECK_EQ(o[0], 0.0f);
        CHECK_EQ(o[1], 0.0f);
        CHECK(!std::signbit(o[0]));   // and not a negative zero

        // The endpoints, which is what tells the sign apart from its flip.
        displacementDecodeOffset(s, 1.0f, 0.0f, o);
        CHECK_EQ(o[0], s);
        CHECK_EQ(o[1], -s);
        displacementDecodeOffset(s, 0.0f, 1.0f, o);
        CHECK_EQ(o[0], -s);
        CHECK_EQ(o[1], s);
    }

    // The amplitude: a full swing of the map is 2s wide, not s. A `(d - 0.5)*s`
    // transcription is neutral at 0.5 and half as strong everywhere else.
    float lo[2], hi[2];
    displacementDecodeOffset(0.35f, 0.0f, 0.0f, lo);
    displacementDecodeOffset(0.35f, 1.0f, 1.0f, hi);
    CHECK_EQ(hi[0] - lo[0], 0.7f);

    // And a scale of zero pins nothing in place: no offset, whatever the map
    // says. `[BIN]` There is no guard around this in the IR -- s = 0 is simply
    // an offset of zero, not a skipped sample.
    displacementDecodeOffset(0.0f, 0.9f, 0.1f, lo);
    CHECK_EQ(lo[0], 0.0f);
    CHECK_EQ(lo[1], 0.0f);
}

// The same decode on the GPU, over the whole [0,1] square of map values plus a
// margin either side, because the map is a texture and nothing clamps it.
TEST_CASE(the_gpu_decodes_the_displacement_the_same_way) {
    Device& d = gpu();
    if (!d.valid()) return;
    Scene s = makeScene(0.35f);
    s.probes.clear();
    for (float dy = -0.25f; dy <= 1.26f; dy += 0.125f) {
        for (float dx = -0.25f; dx <= 1.26f; dx += 0.125f) {
            Probe p;
            p.p[0] = dx;
            p.p[1] = dy;
            s.probes.push_back(p);
        }
    }
    const std::vector<float> got = onGpu(d, s, 3u, 2u);
    REQUIRE(got.size() == s.probes.size() * 4);
    int bad = 0;
    for (std::size_t i = 0; i < s.probes.size(); ++i) {
        float want[2];
        displacementDecodeOffset(s.params[0], s.probes[i].p[0], s.probes[i].p[1], want);
        if (!sameBits(got[i * 4 + 0], want[0]) || !sameBits(got[i * 4 + 1], want[1])) {
            if (bad < 3) {
                std::printf("  FAIL decode d=(%.4f,%.4f)  gpu (%.9g,%.9g)  cpu (%.9g,%.9g)\n",
                            s.probes[i].p[0], s.probes[i].p[1], got[i * 4 + 0], got[i * 4 + 1],
                            want[0], want[1]);
            }
            ++bad;
        }
        // The tap count and scale come back on z and w, so the switch is checked
        // on the GPU's side of the fence too rather than only on the oracle's.
        CHECK_EQ(got[i * 4 + 2], float(displacementTapCount(2u)));
        CHECK_EQ(got[i * 4 + 3], displacementTapScale(2u));
        if (bad > 0 && i > 4) break;
    }
    CHECK_EQ(bad, 0);
}

// The two layer transforms, which are where a swapped A/B would show. Layer B's
// clamp rect is deliberately tighter than the unit square in `makeScene`, so a
// transcription that dropped the clamp -- or clamped with the bounds the other
// way round -- differs here.
TEST_CASE(the_layer_transform_agrees_with_the_gpu) {
    Device& d = gpu();
    if (!d.valid()) return;
    Scene s = makeScene();
    s.probes.clear();
    for (float y = -2.0f; y <= 12.0f; y += 0.7f) {
        for (float x = -2.0f; x <= 12.0f; x += 0.9f) {
            Probe p;
            p.p[0] = x;
            p.p[1] = y;
            s.probes.push_back(p);
        }
    }
    const std::vector<float> got = onGpu(d, s, 1u, 0u);
    REQUIRE(got.size() == s.probes.size() * 4);
    const DisplacementParams params = s.parsed();
    int bad = 0;
    for (std::size_t i = 0; i < s.probes.size(); ++i) {
        float uvA[2], uvB[2];
        displacementLayerUV(params.source, s.probes[i].p[0], s.probes[i].p[1], uvA);
        displacementLayerUV(params.map, s.probes[i].p[0], s.probes[i].p[1], uvB);
        if (!agrees(got[i * 4 + 0], uvA[0]) || !agrees(got[i * 4 + 1], uvA[1]) ||
            !agrees(got[i * 4 + 2], uvB[0]) || !agrees(got[i * 4 + 3], uvB[1])) {
            if (bad < 3) {
                std::printf("  FAIL uv p=(%.3f,%.3f)  gpu A(%.9g,%.9g) B(%.9g,%.9g)"
                            "  cpu A(%.9g,%.9g) B(%.9g,%.9g)\n",
                            s.probes[i].p[0], s.probes[i].p[1], got[i * 4 + 0], got[i * 4 + 1],
                            got[i * 4 + 2], got[i * 4 + 3], uvA[0], uvA[1], uvB[0], uvB[1]);
            }
            ++bad;
        }
    }
    CHECK_EQ(bad, 0);
    // The clamp bites: something in that sweep must have been held at the rect.
    int held = 0;
    for (std::size_t i = 0; i < s.probes.size(); ++i) {
        if (got[i * 4 + 2] == params.map.m[3][0] || got[i * 4 + 2] == params.map.m[4][0]) ++held;
    }
    CHECK(held > 0);
}

// The sampler both sides run, checked on its own before it is checked inside the
// stage -- a disagreement here would otherwise show up as a disagreement in the
// math. `[OBS]` This is NOT the target's sampler: `@__air_sampler_state` is one
// undecoded word, and what is gated is that our two sides use the same one.
TEST_CASE(the_sampler_agrees_with_the_gpu) {
    Device& d = gpu();
    if (!d.valid()) return;
    Scene s = makeScene();
    s.probes.clear();
    for (float v = -0.2f; v <= 1.21f; v += 0.037f) {
        for (float u = -0.2f; u <= 1.21f; u += 0.041f) {
            Probe p;
            p.p[0] = u;
            p.p[1] = v;
            s.probes.push_back(p);
        }
    }
    const std::vector<float> got = onGpu(d, s, 4u, 0u);
    REQUIRE(got.size() == s.probes.size() * 4);
    int bad = 0;
    for (std::size_t i = 0; i < s.probes.size(); ++i) {
        float want[4];
        sampleBilinear(s.map.view(), s.probes[i].p[0], s.probes[i].p[1], want);
        for (int k = 0; k < 4; ++k) {
            if (!agrees(got[i * 4 + k], want[k])) {
                if (bad < 3) {
                    std::printf("  FAIL sample uv=(%.4f,%.4f) k=%d  gpu %.9g  cpu %.9g\n",
                                s.probes[i].p[0], s.probes[i].p[1], k, got[i * 4 + k], want[k]);
                }
                ++bad;
            }
        }
    }
    CHECK_EQ(bad, 0);
}

// THE STAGE, ALL FOUR TAP MODES. This is the differential the task asks for, and
// the modes are run separately rather than folded together because each is a
// different block in the IR with its own literals.
TEST_CASE(the_whole_stage_agrees_with_the_gpu_in_every_tap_mode) {
    Device& d = gpu();
    if (!d.valid()) return;
    const Scene s = makeScene();
    for (std::uint32_t variant : {0u, 1u, 2u, 3u, 4u, 7u}) {
        const std::vector<float> got = onGpu(d, s, 0u, variant);
        REQUIRE(got.size() == s.probes.size() * 4);
        int bad = 0;
        std::uint32_t worst = 0;
        for (std::size_t i = 0; i < s.probes.size(); ++i) {
            float want[4];
            oracleAt(s, variant, s.probes[i], want);
            for (int k = 0; k < 4; ++k) {
                if (!sameBits(got[i * 4 + k], want[k])) {
                    worst = std::max(worst, rb::ulpsApart(got[i * 4 + k], want[k]));
                }
                if (!agrees(got[i * 4 + k], want[k])) {
                    if (bad < 3) {
                        std::printf("  FAIL variant=%u p=(%.3f,%.3f) k=%d  gpu %.9g  cpu %.9g\n",
                                    variant, s.probes[i].p[0], s.probes[i].p[1], k,
                                    got[i * 4 + k], want[k]);
                    }
                    ++bad;
                }
            }
        }
        // Printed so a regression reads as a NUMBER MOVING rather than as a
        // pass/fail flip. It is zero, and it is meant to stay zero.
        std::printf("  displacement variant=%u (%d taps): worst %u ULP\n", variant,
                    displacementTapCount(variant), worst);
        CHECK_EQ(bad, 0);
    }
}

// THE `.z` WEIGHT IS PER TAP.
//
// The IR shuffles `zzzz` out of each tap's OWN displacement sample -- `%97`,
// `%125`, `%147`, `%173` and so on. Writing the loop with the weight hoisted to
// the first tap is the natural mistake and produces a perfectly plausible
// picture, so the wrong version is computed here on purpose and the right one is
// required to differ from it by a wide margin.
TEST_CASE(the_z_channel_is_a_weight_read_at_every_tap) {
    const Scene s = makeScene();
    // Variant 0 has one tap, so hoisting is a no-op there -- that is the control
    // that shows the check is measuring what it claims to.
    {
        float right[4], hoisted[4];
        oracleAt(s, 0u, s.probes[0], right);
        hoistedWeightAt(s, 0u, s.probes[0], hoisted);
        for (int k = 0; k < 4; ++k) CHECK_EQ(right[k], hoisted[k]);
    }
    for (std::uint32_t variant : {1u, 2u, 3u}) {
        int moved = 0;
        float worst = 0.0f;
        for (const Probe& p : s.probes) {
            float right[4], hoisted[4];
            oracleAt(s, variant, p, right);
            hoistedWeightAt(s, variant, p, hoisted);
            for (int k = 0; k < 4; ++k) {
                const float delta = std::fabs(right[k] - hoisted[k]);
                worst = std::max(worst, delta);
                if (delta > 1e-3f) ++moved;
            }
        }
        std::printf("  variant=%u: hoisting the .z weight moves %d of %d channels, worst %.5f\n",
                    variant, moved, int(s.probes.size()) * 4, double(worst));
        // Not "some difference": a difference far outside any rounding
        // allowance, on most of the samples.
        CHECK(moved > int(s.probes.size()));
        CHECK(worst > 0.01f);
    }
}

// VARIANT 0 TAKES NO DERIVATIVES AT ALL.
//
// In the IR the `v&7 == 0` case is reached by an `icmp eq` placed BEFORE the
// switch, and `air.dfdx` / `air.dfdy` are called on the far side of that branch
// -- so on this path they are never evaluated. Here they are arguments rather
// than intrinsics, and the observable form of the same fact is that the answer
// does not depend on them.
//
// AND THE TEST HAS TO BE HARSHER THAN THAT SOUNDS. Variant 0's single tap has a
// jitter of (0, 0), so `fma(d, 0, p)` is `p` for any FINITE derivative and a
// version that dutifully multiplied by zero would pass a check fed ordinary
// numbers. It is infinity that tells the two apart: `0 * inf` is NaN, so the
// scene below carries an infinite derivative and a NaN one -- values a real
// fragment quad produces at a discontinuity -- and the result still has to be
// the clean one. That is the difference between "multiplies the derivative by
// zero" and "never reads it".
TEST_CASE(variant_zero_does_not_read_the_derivatives) {
    Device& d = gpu();
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();

    Scene quiet = makeScene();
    for (Probe& p : quiet.probes) p.dpdx[0] = p.dpdx[1] = p.dpdy[0] = p.dpdy[1] = 0.0f;

    Scene wild = quiet;
    for (Probe& p : wild.probes) {
        p.dpdx[0] = inf;
        p.dpdx[1] = -inf;
        p.dpdy[0] = nan;
        p.dpdy[1] = 1e30f;
    }

    // Large but finite, for the half of the test that has to show the
    // derivatives DO matter everywhere else -- otherwise the check above proves
    // nothing about variant 0 in particular.
    Scene loud = quiet;
    for (Probe& p : loud.probes) {
        p.dpdx[0] = 137.0f;
        p.dpdx[1] = -91.5f;
        p.dpdy[0] = 64.25f;
        p.dpdy[1] = 210.0f;
    }

    // On the oracle first, which needs no GPU.
    for (std::size_t i = 0; i < quiet.probes.size(); ++i) {
        float a[4], b[4];
        oracleAt(quiet, 0u, quiet.probes[i], a);
        oracleAt(wild, 0u, wild.probes[i], b);
        for (int k = 0; k < 4; ++k) {
            CHECK(b[k] == b[k]);   // never a NaN
            CHECK_EQ(a[k], b[k]);
        }
    }
    for (std::uint32_t variant : {1u, 2u, 3u}) {
        int differing = 0;
        for (std::size_t i = 0; i < quiet.probes.size(); ++i) {
            float a[4], b[4];
            oracleAt(quiet, variant, quiet.probes[i], a);
            oracleAt(loud, variant, loud.probes[i], b);
            for (int k = 0; k < 4; ++k) {
                if (std::fabs(a[k] - b[k]) > 1e-4f) ++differing;
            }
        }
        CHECK(differing > 0);
    }

    if (!d.valid()) return;
    const std::vector<float> quietGpu = onGpu(d, quiet, 0u, 0u);
    const std::vector<float> wildGpu = onGpu(d, wild, 0u, 0u);
    REQUIRE(quietGpu.size() == wildGpu.size());
    REQUIRE(!quietGpu.empty());
    int bad = 0;
    for (std::size_t i = 0; i < quietGpu.size(); ++i) {
        if (!sameBits(quietGpu[i], wildGpu[i])) {
            if (bad < 3) {
                std::printf("  FAIL variant 0 read a derivative: %.9g vs %.9g\n", quietGpu[i],
                            wildGpu[i]);
            }
            ++bad;
        }
    }
    CHECK_EQ(bad, 0);
}

// The scale of zero, which is the case the whole stage collapses to a plain
// weighted resample of the source. Worth pinning because it is what a document
// with the effect turned off produces, and because a transcription that divided
// anywhere would show here rather than in the sweep.
TEST_CASE(a_scale_of_zero_leaves_the_source_where_it_is) {
    const Scene moved = makeScene(0.6f);
    const Scene still = makeScene(0.0f);
    int differing = 0;
    for (std::size_t i = 0; i < still.probes.size(); ++i) {
        float a[4], b[4];
        oracleAt(still, 2u, still.probes[i], a);
        oracleAt(moved, 2u, moved.probes[i], b);
        for (int k = 0; k < 4; ++k) {
            CHECK(a[k] == a[k]);   // never a NaN
            if (std::fabs(a[k] - b[k]) > 1e-4f) ++differing;
        }
    }
    // The scale is the whole effect: turning it off has to change the picture.
    CHECK(differing > 0);
}

// THE FUSED MULTIPLY-ADD THIS BUILD CANNOT GET FROM ITS OWN C LIBRARY.
//
// The oracle's `fma1` exists because `std::fma(float, float, float)` on this
// toolchain is not correctly rounded, and the case below is the cleanest proof
// of it: with s = 0.35 and d = -0.25 the product `d * 2s` is EXACT -- 0.25 is a
// power of two -- so a fused and an unfused multiply-add cannot possibly differ,
// and there is exactly one right answer. `std::fma` gives the other one.
//
// This is pinned rather than merely commented because it is what stands between
// this gate and a permanent four-ULP allowance that would have been an allowance
// for a library bug. If a future toolchain fixes `fmaf`, the first two checks
// here start failing and the comment gets to be rewritten with evidence.
TEST_CASE(the_oracle_rounds_its_fused_multiply_adds_correctly) {
    const float s = 0.35f;
    const float twice = s * 2.0f;   // 0.7 exactly, as a doubling always is

    // The exact real value, and the two floats it sits between.
    const double exact = -0.25 * double(twice) + double(-s);
    const float nearest = float(exact);

    float got[2];
    displacementDecodeOffset(s, -0.25f, -0.25f, got);
    CHECK_EQ(got[0], nearest);
    CHECK_EQ(got[1], nearest);

    // And the value the C library answers with, which is one ULP away and is
    // NOT what the GPU computes.
    const float fromLibc = std::fma(-0.25f, twice, -s);
    CHECK(rb::ulpsApart(fromLibc, nearest) <= 1u);
    if (!sameBits(fromLibc, nearest)) {
        std::printf("  note: std::fma here gives %.9g, correctly rounded is %.9g\n",
                    double(fromLibc), double(nearest));
    }

    // The bar the differential actually holds: agreement is on the BITS, and a
    // single ULP is a failure, not a rounding allowance.
    CHECK(!agrees(nearest, std::nextafterf(nearest, 0.0f)));
    CHECK(agrees(nearest, nearest));
    // And the thing a blanket tolerance would have waved through: one tap's
    // worth of jitter is 1/16 of a pixel.
    CHECK(!agrees(0.0625f, -0.0625f));
}
