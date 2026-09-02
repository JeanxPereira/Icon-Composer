// `glassForeground_v1`: the GPU against the oracle, and the oracle against the
// IR it was read from.
//
// The shader this runs is `GlassForeground.glsl` -- the SAME file the renderer
// will include -- so what draws and what is verified cannot drift apart.
//
// WHAT THIS GATE HAS TO CATCH, beyond "the two sides agree". A differential
// proves two transcriptions match each other; it cannot prove either is the
// target's. Six things are pinned here that agreement alone would not see, and
// every one of them is a mistake that still produces a picture:
//
//   * TWO EPSILONS, four instructions apart. `0xH1419` (0.001, half) gates the
//     early-out and the per-tap divisor; `float(1e-4)` floors `fwidth`. mod98
//     uses the first for both. Substituting one for the other moves the
//     antialiasing floor by two orders of magnitude.
//   * THE ABERRATION LOBE'S COMPONENTS ARE SWAPPED. Its 2x2 is a reflection, not
//     the rotation the refraction lobe uses. It looks like a transposition in
//     the reading, so the swapped and unswapped forms are BOTH computed here and
//     required to differ.
//   * THE RESOLVE IS NOT `rbGlassAberrationCombine`. mod99 folds the coverage
//     into the alpha between the `1/7` and the premultiply. The shared combine
//     is computed too and required to differ.
//   * WHICH TEXTURE IS WHICH. `t0` is the layer's own contents, seven taps; `t1`
//     is the field, one tap. The two are swapped here and the answer required to
//     move.
//   * THE LADDER, TAP BY TAP. Seven offsets evenly spaced from `+dispA` to
//     `-dispA`; permuting them or negating the set leaves any aggregate alone.
//   * THE ZERO HEIGHT. The packer writes `1/height` with a guard that sends zero
//     to ZERO, so a height of zero must give a CONSTANT band and not an
//     infinity.
//
// `[ART]` THIS FILE WAS RUN AGAINST FOURTEEN DELIBERATELY WRONG TRANSCRIPTIONS,
// because a gate that has never been seen to fail is a demonstration. Each was
// applied to `GlassForeground.cpp` alone -- so the GPU kept the right answer --
// and the whole file re-run:
//
//   1  aberration components un-swapped ................. 196 failures
//   2  fwidth floored at mod98's half epsilon ............   2 failures
//   3  resolve premultiplies by the un-covered alpha .... 423 failures
//   4  `t0` and `t1` swapped ............................ 250 failures
//   5  `mix` expanded as `x*(1-a) + y*a` ................  21 failures
//   6  fade bias by reciprocal-multiply (AquaKit's) .....   0 failures
//   7  alpha sum takes the FLOORED alpha ................ 213 failures
//   8  ladder offsets all made positive ................. 403 failures
//   9  gradient read from the field's `.xy` ............. 403 failures
//  10  refraction displacement dropped from the base .... 197 failures
//  11  fade not applied to the alpha channel ............ 103 failures
//  12  `distanceBias` dropped ........................... 363 failures
//  13  `gradientBias` dropped ........................... 278 failures
//  14  gradient lanes swapped ........................... 395 failures
//
// Mutant 7 was GREEN on the first sweep and is the reason the last test in this
// file grew its final block: proving the two candidate alpha sums are different
// numbers is not the same as proving the stage picks one of them. Mutant 6 is
// green on purpose and cannot be made red -- see the note at the fade's test.
//
// AND WHERE BIT-FOR-BIT STOPS, AND WHY IT IS NOT A TOLERANCE. The one
// non-polynomial in this stage is the square root inside `rbGlassBandAmount`,
// which Vulkan specifies to 3 ULP with no control that makes it exact. So the
// whole-stage differential is run TWICE: once on a scene whose band is driven to
// `x = 0`, where `sqrt(0)` is exact on any conforming implementation and the
// comparison is on the bits; and once on a general scene, where the allowance is
// DERIVED by evaluating this same stage at `circ +- 3 ULP` and taking the spread
// it produces. Neither number was picked to make a test pass.
#include "check.h"
#include "Source/RenderBox/Compute.h"
#include "Source/RenderBox/GlassForeground.h"
#include "Source/RenderBox/GlassOracle.h"
#include "Source/RenderBox/PathVertexOracle.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

static const std::uint32_t kForegroundSpirv[] =
#include "glass_foreground_probe.comp.inc"
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

// Mirrors the push-constant block in glass_foreground_probe.comp.
struct Control {
    std::uint32_t count = 0;
    std::uint32_t stage = 0;
    std::uint32_t tap = 0;
    std::uint32_t srcBase = 0;
    std::uint32_t srcW = 0;
    std::uint32_t srcH = 0;
    std::uint32_t fldBase = 0;
    std::uint32_t fldW = 0;
    std::uint32_t fldH = 0;
    std::uint32_t probeBase = 0;
};

enum Stage : std::uint32_t {
    kWhole = 0,
    kLayerUVs = 1,
    kDecode = 2,
    kDisplacements = 3,
    kEdgeFade = 4,
    kResolve = 5,
    kTapPoint = 6,
    kSampleSource = 7,
    kSampleField = 8,
};

// One probe: the point, the derivative the target would have taken with
// `air.fwidth` (data here, because a compute dispatch has no fragment quad), and
// nine spare floats the per-piece stages drive their own inputs with.
struct Probe {
    float p[2]{0, 0};
    float fwidthD = 0.0f;
    float aux[9]{};
};

struct Bitmap {
    int w = 0;
    int h = 0;
    std::vector<float> rgba;

    Bitmap() = default;
    Bitmap(int width, int height)
        : w(width), h(height), rgba(std::size_t(width * height * 4), 0.0f) {}
    float& at(int x, int y, int k) { return rgba[std::size_t((y * w + x) * 4 + k)]; }
    float at(int x, int y, int k) const { return rgba[std::size_t((y * w + x) * 4 + k)]; }
    SampledImage view() const { return SampledImage{w, h, rgba.data()}; }
};

struct Scene {
    float params[38]{};
    Bitmap source;
    Bitmap field;
    std::vector<Probe> probes;

    ForegroundParams parsed() const { return parseForegroundParams(params); }
};

std::vector<float> onGpu(Device& d, const Scene& s, std::uint32_t stage, std::uint32_t tap = 0) {
    if (s.probes.empty()) return {};

    Control c;
    std::vector<float> data(s.params, s.params + 38);
    c.srcBase = static_cast<std::uint32_t>(data.size());
    c.srcW = static_cast<std::uint32_t>(s.source.w);
    c.srcH = static_cast<std::uint32_t>(s.source.h);
    data.insert(data.end(), s.source.rgba.begin(), s.source.rgba.end());
    c.fldBase = static_cast<std::uint32_t>(data.size());
    c.fldW = static_cast<std::uint32_t>(s.field.w);
    c.fldH = static_cast<std::uint32_t>(s.field.h);
    data.insert(data.end(), s.field.rgba.begin(), s.field.rgba.end());
    c.probeBase = static_cast<std::uint32_t>(data.size());
    for (const Probe& p : s.probes) {
        data.push_back(p.p[0]);
        data.push_back(p.p[1]);
        data.push_back(p.fwidthD);
        for (int k = 0; k < 9; ++k) data.push_back(p.aux[k]);
    }
    c.count = static_cast<std::uint32_t>(s.probes.size());
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
        ComputePass::create(d, kForegroundSpirv, sizeof kForegroundSpirv, sizeof(Control));
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

float ulpSizeAt(float magnitude) {
    const float m = std::fabs(magnitude);
    return std::nextafter(m, std::numeric_limits<float>::infinity()) - m;
}

// ---------------------------------------------------------------- the scene

// A source image with structure in all four channels including a genuinely
// varying alpha -- the seven taps DIVIDE by that alpha, so a flat one would hide
// both the guarded divisor and the un-premultiply entirely.
Bitmap makeSource(int n = 8) {
    Bitmap b(n, n);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            b.at(x, y, 0) = 0.06f * float(x) + 0.02f * float(y);
            b.at(x, y, 1) = 0.85f - 0.06f * float(y);
            b.at(x, y, 2) = ((x + y) & 1) ? 0.72f : 0.19f;
            b.at(x, y, 3) = 0.30f + 0.08f * float((x * 3 + y) % 8);
        }
    }
    return b;
}

// A distance field in the layout `distanceGradient_v1` produces and this stage
// reads back: `(d, gx, gy, coverage)`, d NEGATIVE INSIDE. A disc, so the
// gradient actually turns across the image and the two rotations have something
// to act on -- an axis-aligned gradient would let a transposed matrix pass.
Bitmap makeField(int n = 8, float radius = 2.6f) {
    Bitmap b(n, n);
    const float cx = float(n) * 0.5f;
    const float cy = float(n) * 0.5f;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const float px = float(x) + 0.5f - cx;
            const float py = float(y) + 0.5f - cy;
            const float r = std::sqrt(px * px + py * py);
            const float d = r - radius;
            const float inv = r > 0.0f ? 1.0f / r : 0.0f;
            b.at(x, y, 0) = d;
            b.at(x, y, 1) = px * inv;
            b.at(x, y, 2) = py * inv;
            // Coverage: 1 well inside, 0 well outside, a ramp across the edge.
            // The zeros are what drives the early-out branch.
            const float c = 0.5f - d;
            b.at(x, y, 3) = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
        }
    }
    return b;
}

// The 38 floats. The two layers are deliberately DIFFERENT transforms with
// different clamp rects, so a swap between them is visible rather than benign.
void setLayers(Scene& s) {
    // Layer A (source, t0): eight pixels across the unit uv square.
    const float a[10] = {0.125f, 0.0f, 0.0f, 0.125f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f};
    // Layer B (field, t1): a different scale, a translation, and a clamp rect
    // that actually bites.
    const float b[10] = {0.1f, 0.0f, 0.0f, 0.11f, 0.03f, -0.015f, 0.01f, 0.0f, 0.985f, 0.96f};
    for (int i = 0; i < 10; ++i) {
        s.params[18 + i] = a[i];
        s.params[28 + i] = b[i];
    }
}

// The packer's own defaults, as read at `0x0E7D48`: the field decode is the
// identity the descriptor at `0x16193C` carries, the refraction is
// (-150, 1/100, -10), the aberration is (-15, 1/20, 0) and the edge range is
// (-4.5, -3) fading from 0 to 1. The angles default to zero, so both directions
// are (1, 0).
//
// They are scaled down here -- an amount of -150 against a gradient of length
// one throws every tap outside an eight pixel image and the differential then
// only ever measures the clamp. What is kept is their SHAPE; the exact defaults
// are asserted separately, against the packer, in their own test.
void setUniforms(Scene& s, float refractionHeight = 4.0f, float aberrationHeight = 3.0f) {
    // The field decode is deliberately NOT the identity the packer's pool
    // default carries. `[OBS]` What fills these four is a call this reading did
    // not follow (`GlassForeground.h`), so leaving them at 1/0/1/0 would make
    // the whole scene blind to a dropped scale or a dropped bias -- and the two
    // pairs are separate on purpose, so swapping them shows up too.
    s.params[0] = 0.85f;   // distanceScale
    s.params[1] = 0.12f;   // distanceBias
    s.params[2] = 0.9f;    // gradientScale
    s.params[3] = 0.05f;   // gradientBias
    s.params[4] = -1.75f;  // refractionAmount
    s.params[5] = glass::reciprocalHeight(refractionHeight);
    s.params[6] = -0.5f;   // refractionOffset
    s.params[7] = -0.6f;   // aberrationAmount
    s.params[8] = glass::reciprocalHeight(aberrationHeight);
    s.params[9] = 0.25f;   // aberrationOffset
    // A direction that is NOT axis aligned: cos/sin of 0.7 rad. An identity
    // direction would let a dropped rotation pass.
    s.params[10] = std::cos(0.7f);
    s.params[11] = std::sin(0.7f);
    s.params[12] = -2.0f;  // edgeStart
    s.params[13] = -0.5f;  // edgeEnd
    s.params[14] = 0.15f;  // edgeOpacityStart
    s.params[15] = 0.8f;   // edgeOpacityEnd
    s.params[16] = std::cos(-0.35f);
    s.params[17] = std::sin(-0.35f);
}

void addProbes(Scene& s) {
    for (float py = 0.35f; py < 7.6f; py += 0.9f) {
        for (float px = 0.4f; px < 7.6f; px += 0.85f) {
            Probe p;
            p.p[0] = px;
            p.p[1] = py;
            // A derivative comfortably above the 1e-4 floor for most probes; the
            // floor gets its own test where it is driven under.
            p.fwidthD = 0.9f + 0.05f * px;
            s.probes.push_back(p);
        }
    }
}

Scene makeScene() {
    Scene s;
    setUniforms(s);
    setLayers(s);
    s.source = makeSource();
    s.field = makeField();
    addProbes(s);
    return s;
}

// WHERE BIT-FOR-BIT STOPS, AND IT IS NOT WHERE THE FIRST DRAFT OF THIS FILE
// ASSUMED. `[ART]` Vulkan specifies only two of this stage's operations
// loosely, and both of them are in it: `OpFDiv` to 2.5 ULP and `Sqrt` to 3.
// Every other operation the module runs -- the multiplies, the adds, the
// clamps, the bilinear -- is correctly rounded on both sides, so given the same
// inputs it produces the same bits. This differential ran at three to eight ULP
// until the DIVISIONS were accounted for, and what fixed it was not a tolerance:
// it was naming them.
//
// mod99 divides in four places, and each is a knob below:
//   * `%91`          -- `d / max(fwidth(d), 1e-4)`, once
//   * `%194`/`%235`  -- `sample.rgb / max(sample.a, 0.001)`, seven times
//   * `%282`/`%284`  -- `1/span` and `-edgeStart/span`, once each
//
// So the stage is re-run with each of those moved by the precision the spec
// allows, and the spread that produces IS the bound. If a driver ever divides
// better than the spec requires, the bit-exact counter this file prints goes up
// on its own and nothing here has to be relaxed to notice.
struct Knobs {
    float bandR = 0.0f;    // absolute, added to the refraction band
    float bandA = 0.0f;    // absolute, added to the aberration band
    float edgeDiv = 0.0f;  // relative, on `d / w`
    float tapDiv = 0.0f;   // relative, on `sample.rgb / a`
    float fadeDiv = 0.0f;  // relative, on the fade's two quotients
    // Not a precision knob: the WRONG transcription, computed on purpose so a
    // test can require the right one to differ from it. `%211`/`%252` add the
    // sample's raw alpha; summing the floored divisor instead is the mistake
    // AquaKit records having made in the other direction.
    bool flooredAlphaSum = false;
};

// `[ART]` The relative size of one ULP of a normalised float is at most 2^-23,
// which is `ulpSizeAt(1.0f)`. 2.5 of them is what the Vulkan spec allows
// `OpFDiv`.
const float kDivideError = 2.5f * ulpSizeAt(1.0f);
// `[ART]` And 3 of them is what it allows `Sqrt`. `circ` lives in [0, 1], so
// three ULP AT ONE is the whole of the error the band profile can make -- the
// same constant, and the same reasoning, as `Tests/test_rb_glass.cpp`.
constexpr std::uint32_t kSqrtUlps = 3;
const float kCircError = float(kSqrtUlps) * ulpSizeAt(1.0f);

// The stage again, with every loosely-specified operation exposed. Every piece
// is the oracle's own; only the two band results and the four quotients move.
void stageWithKnobs(const Scene& s, const Probe& probe, const Knobs& k, float (&out)[4]) {
    const ForegroundParams params = s.parsed();
    const ForegroundUniforms& u = params.uniforms;

    float uvField[2];
    foregroundLayerUV(params.field, probe.p[0], probe.p[1], uvField);
    float f[4];
    sampleBilinear(s.field.view(), uvField[0], uvField[1], f);
    const float d = foregroundDistance(u, f[0]);
    if (f[3] < glass::kEpsilon) {
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        return;
    }

    const float w =
        probe.fwidthD > kForegroundFwidthFloor ? probe.fwidthD : kForegroundFwidthFloor;
    const float edgeRaw = 0.5f - (d / w) * (1.0f + k.edgeDiv);

    float g[2];
    foregroundGradient(u, f[1], f[2], g);

    const float bandR =
        glass::bandAmount(d, u.refractionAmount, u.invRefractionHeight, u.refractionOffset) +
        k.bandR;
    const float bandA =
        glass::bandAmount(d, u.aberrationAmount, u.invAberrationHeight, u.aberrationOffset) +
        k.bandA;

    const float rx[2] = {u.refractionDir[0], -u.refractionDir[1]};
    const float ry[2] = {u.refractionDir[1], u.refractionDir[0]};
    const float ax[2] = {u.aberrationDir[0], -u.aberrationDir[1]};
    const float ay[2] = {u.aberrationDir[1], u.aberrationDir[0]};
    const float dispR[2] = {(g[0] * rx[0] + g[1] * rx[1]) * bandR,
                            (g[0] * ry[0] + g[1] * ry[1]) * bandR};
    // The swap: lane 0 takes the `(sin, cos)` row.
    const float dispA[2] = {(g[0] * ay[0] + g[1] * ay[1]) * bandA,
                            (g[0] * ax[0] + g[1] * ax[1]) * bandA};

    float base[2];
    foregroundBase(probe.p[0], probe.p[1], dispR, base);

    float acc[3] = {0, 0, 0};
    float alphaSum = 0.0f;
    for (int i = 0; i < glass::kAberrationTaps; ++i) {
        const glass::AberrationTap tap = glass::aberrationTap(i);
        float q[2];
        foregroundTapPoint(base, dispA, tap.offset, q);
        float uvSource[2];
        foregroundLayerUV(params.source, q[0], q[1], uvSource);
        float c[4];
        sampleBilinear(s.source.view(), uvSource[0], uvSource[1], c);
        const float a = c[3] > glass::kEpsilon ? c[3] : glass::kEpsilon;
        for (int j = 0; j < 3; ++j) acc[j] += ((c[j] / a) * (1.0f + k.tapDiv)) * tap.weight[j];
        alphaSum += k.flooredAlphaSum ? a : c[3];
    }
    float rgba[4];
    foregroundResolve(acc, alphaSum, f[3], edgeRaw, rgba);

    const float span = u.edgeEnd - u.edgeStart;
    const float invSpan = (1.0f / span) * (1.0f + k.fadeDiv);
    const float bias = (-u.edgeStart / span) * (1.0f + k.fadeDiv);
    float t = d * invSpan + bias;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const float opacity = u.edgeOpacityStart + (u.edgeOpacityEnd - u.edgeOpacityStart) * t;
    const float fade = 1.0f - opacity;
    for (int j = 0; j < 4; ++j) out[j] = fade * rgba[j];
}

// The bound this stage is entitled to at one probe: move each loosely-specified
// operation by the precision the spec allows, both ways, and SUM the largest
// deviation each one produces. Summing rather than taking the worst is what
// makes it an upper bound on the joint response instead of a guess about which
// error dominates; one ULP of the result is added for the last rounding.
//
// `bandsExact` is passed when the scene drives the band profile's input to zero.
// There `sqrt(0)` is exact on any conforming implementation, so the root
// contributes nothing and including it would slacken the bound for no reason.
float derivedBound(const Scene& s, const Probe& probe, int channel, bool bandsExact) {
    const ForegroundUniforms u = s.parsed().uniforms;
    // `band = amount * (1 - circ)`, so an error of `e` at `circ` arrives here
    // scaled by `|amount|`.
    const float eR = bandsExact ? 0.0f : kCircError * std::fabs(u.refractionAmount);
    const float eA = bandsExact ? 0.0f : kCircError * std::fabs(u.aberrationAmount);

    float mid[4];
    stageWithKnobs(s, probe, Knobs{}, mid);

    float total = 0.0f;
    for (int which = 0; which < 5; ++which) {
        float worst = 0.0f;
        for (int sign = -1; sign <= 1; sign += 2) {
            Knobs k;
            const float m = float(sign);
            if (which == 0) k.bandR = m * eR;
            else if (which == 1) k.bandA = m * eA;
            else if (which == 2) k.edgeDiv = m * kDivideError;
            else if (which == 3) k.tapDiv = m * kDivideError;
            else k.fadeDiv = m * kDivideError;
            float moved[4];
            stageWithKnobs(s, probe, k, moved);
            worst = std::fmax(worst, std::fabs(moved[channel] - mid[channel]));
        }
        total += worst;
    }
    return total + ulpSizeAt(mid[channel]);
}

// The same derivation for the two pieces that are gated on their own.
float edgeRawBound(float d, float fwidthD) {
    const float w = fwidthD > kForegroundFwidthFloor ? fwidthD : kForegroundFwidthFloor;
    const float q = d / w;
    return kDivideError * std::fabs(q) + ulpSizeAt(0.5f - q);
}

float edgeFadeBound(const ForegroundUniforms& u, float d) {
    const float span = u.edgeEnd - u.edgeStart;
    const float mid = foregroundEdgeFade(u, d);
    float worst = 0.0f;
    for (int sign = -1; sign <= 1; sign += 2) {
        const float r = 1.0f + float(sign) * kDivideError;
        const float invSpan = (1.0f / span) * r;
        const float bias = (-u.edgeStart / span) * r;
        float t = d * invSpan + bias;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        const float opacity = u.edgeOpacityStart + (u.edgeOpacityEnd - u.edgeOpacityStart) * t;
        worst = std::fmax(worst, std::fabs((1.0f - opacity) - mid));
    }
    return worst + ulpSizeAt(mid);
}

void oracleAt(const Scene& s, const Probe& p, float (&out)[4]) {
    glassForeground(s.parsed(), p.p[0], p.p[1], p.fwidthD, s.source.view(), s.field.view(), out);
}

}  // namespace

// ===========================================================================
// 1. the layout, which is what the whole reading rests on
// ===========================================================================

// `[BIN]` 152 bytes, 38 floats. The uniforms are 0..17; `%11` is
// `getelementptr float, ptr %2, i64 18` and `%30` is `i64 28`, and those two
// numbers are the only thing that fixes which layer is which.
TEST_CASE(the_params_buffer_is_thirty_eight_floats_in_the_targets_order) {
    float raw[38];
    for (int i = 0; i < 38; ++i) raw[i] = float(i) + 0.5f;
    const ForegroundParams p = parseForegroundParams(raw);

    CHECK_EQ(p.uniforms.distanceScale, 0.5f);
    CHECK_EQ(p.uniforms.distanceBias, 1.5f);
    CHECK_EQ(p.uniforms.gradientScale, 2.5f);
    CHECK_EQ(p.uniforms.gradientBias, 3.5f);
    CHECK_EQ(p.uniforms.refractionAmount, 4.5f);
    CHECK_EQ(p.uniforms.invRefractionHeight, 5.5f);
    CHECK_EQ(p.uniforms.refractionOffset, 6.5f);
    CHECK_EQ(p.uniforms.aberrationAmount, 7.5f);
    CHECK_EQ(p.uniforms.invAberrationHeight, 8.5f);
    CHECK_EQ(p.uniforms.aberrationOffset, 9.5f);
    CHECK_EQ(p.uniforms.aberrationDir[0], 10.5f);
    CHECK_EQ(p.uniforms.aberrationDir[1], 11.5f);
    CHECK_EQ(p.uniforms.edgeStart, 12.5f);
    CHECK_EQ(p.uniforms.edgeEnd, 13.5f);
    CHECK_EQ(p.uniforms.edgeOpacityStart, 14.5f);
    CHECK_EQ(p.uniforms.edgeOpacityEnd, 15.5f);
    CHECK_EQ(p.uniforms.refractionDir[0], 16.5f);
    CHECK_EQ(p.uniforms.refractionDir[1], 17.5f);

    CHECK_EQ(p.source.m[0][0], 18.5f);
    CHECK_EQ(p.source.m[4][1], 27.5f);
    CHECK_EQ(p.field.m[0][0], 28.5f);
    CHECK_EQ(p.field.m[4][1], 37.5f);

    // 38 floats is 152 bytes, and nothing past them is read.
    CHECK_EQ(int(sizeof raw), 152);
}

// `[BIN]` The TBAA node `!35` in mod99 declares `ForegroundUniforms` as 16
// members: two aggregates, at 0 and 8, then fourteen floats at 16, 20, ... 68.
// The C++ struct has to reproduce those offsets exactly, because the probe reads
// the same buffer by float index and the ARM64 packer writes it by byte offset.
TEST_CASE(the_uniform_block_is_seventy_two_bytes_at_the_packers_offsets) {
    CHECK_EQ(int(sizeof(ForegroundUniforms)), 72);
    CHECK_EQ(int(offsetof(ForegroundUniforms, distanceScale)), 0);
    CHECK_EQ(int(offsetof(ForegroundUniforms, gradientScale)), 8);
    // The five the packer binds by key, at the base the zeroing `stp xzr, xzr,
    // [sp, #0x58]` names -- sixteen bytes above where a base of 0x68 put them.
    CHECK_EQ(int(offsetof(ForegroundUniforms, refractionAmount)), 16);
    CHECK_EQ(int(offsetof(ForegroundUniforms, invRefractionHeight)), 20);
    CHECK_EQ(int(offsetof(ForegroundUniforms, refractionOffset)), 24);
    CHECK_EQ(int(offsetof(ForegroundUniforms, aberrationAmount)), 28);
    CHECK_EQ(int(offsetof(ForegroundUniforms, invAberrationHeight)), 32);
    // And the nine the packer writes in two vector stores.
    CHECK_EQ(int(offsetof(ForegroundUniforms, aberrationOffset)), 36);
    CHECK_EQ(int(offsetof(ForegroundUniforms, aberrationDir)), 40);
    CHECK_EQ(int(offsetof(ForegroundUniforms, edgeStart)), 48);
    CHECK_EQ(int(offsetof(ForegroundUniforms, edgeEnd)), 52);
    CHECK_EQ(int(offsetof(ForegroundUniforms, edgeOpacityStart)), 56);
    CHECK_EQ(int(offsetof(ForegroundUniforms, edgeOpacityEnd)), 60);
    CHECK_EQ(int(offsetof(ForegroundUniforms, refractionDir)), 64);
}

// ===========================================================================
// 2. the two epsilons
// ===========================================================================

// THE MISTAKE THIS EXISTS TO STOP. mod98 floors its `fwidth` with the half
// `0xH1419`; mod99 floors its own with `float(1e-4)`, because mod98 computes
// coverage in `f16` and mod99 in `f32`. Different type, different value, ten
// times finer. Both numbers appear in mod99 four instructions apart doing
// different jobs, and carrying one where the other belongs is invisible to every
// test that does not name them.
TEST_CASE(the_module_carries_two_different_epsilons) {
    // `[BIN]` `0xH1419`, the half nearest 0.001.
    CHECK_EQ(glass::kEpsilon, 0.00100040435791015625f);
    // `[BIN]` `0x3F1A36E2E0000000` printed as a double is a float widened; the
    // float is `0x38D1B717`, the nearest float to 1e-4.
    CHECK_EQ(kForegroundFwidthFloor, 1.0e-4f);
    std::uint32_t bits = 0;
    std::memcpy(&bits, &kForegroundFwidthFloor, 4);
    CHECK_EQ(bits, 0x38D1B717u);
    // And they are not each other, by an order of magnitude.
    CHECK(glass::kEpsilon > kForegroundFwidthFloor * 9.0f);
}

// The floor has to BITE, and it has to bite at 1e-4 and not at 1e-3. Three
// derivatives: one under the floor (where the answer must be the floor's), one
// between the two candidate floors (where the two constants give DIFFERENT
// answers, which is what makes this a discriminating test) and one above both.
TEST_CASE(the_fwidth_floor_is_one_ten_thousandth_and_not_one_thousandth) {
    const float d = -0.02f;

    // Under the floor: clamped, so any smaller derivative gives the same value.
    CHECK_EQ(foregroundEdgeRaw(d, 0.0f), foregroundEdgeRaw(d, kForegroundFwidthFloor));
    CHECK_EQ(foregroundEdgeRaw(d, 1e-9f), foregroundEdgeRaw(d, kForegroundFwidthFloor));

    // Between the two candidates: mod99's floor does not bite, mod98's would.
    const float between = 5.0e-4f;
    CHECK_EQ(foregroundEdgeRaw(d, between), 0.5f - d / between);
    CHECK(!sameBits(foregroundEdgeRaw(d, between), 0.5f - d / glass::kEpsilon));

    // Above both: untouched.
    CHECK_EQ(foregroundEdgeRaw(d, 0.75f), 0.5f - d / 0.75f);

    // And the coverage is NOT the edge polynomial. mod98 reaches for
    // `edgeCoverage` at three sites; mod99 at none, and the two are different
    // functions of the same distance.
    CHECK(!sameBits(foregroundEdgeRaw(d, 1.0f), glass::edgeCoverage(d)));
}

// ===========================================================================
// 3. the aberration lobe's component swap
// ===========================================================================

// `[BIN]` `%143` lands in lane 1 and `%147` in lane 0, so the aberration's 2x2
// is `[[sin, cos], [cos, -sin]]` -- determinant `-(sin^2 + cos^2)`, a REFLECTION
// -- while the refraction's is the rotation `[[cos, -sin], [sin, cos]]`.
//
// `[ART]` AquaKit read the same swap out of QuartzCore from a different binary.
// It is asserted here against its own algebra AND required to differ from the
// unswapped form, because "I transposed it while reading" is exactly the failure
// this shape invites.
TEST_CASE(the_aberration_lobe_reflects_where_the_refraction_lobe_rotates) {
    Scene s;
    setUniforms(s);
    setLayers(s);
    const ForegroundUniforms u = s.parsed().uniforms;
    const float g[2] = {0.6f, -0.8f};
    const float d = -0.4f;

    float dispR[2], dispA[2];
    foregroundRefractionDisplacement(u, d, g, dispR);
    foregroundAberrationDisplacement(u, d, g, dispA);

    const float bandR =
        glass::bandAmount(d, u.refractionAmount, u.invRefractionHeight, u.refractionOffset);
    const float bandA =
        glass::bandAmount(d, u.aberrationAmount, u.invAberrationHeight, u.aberrationOffset);

    // The refraction: a plain rotation by `refractionDir`.
    const float rc = u.refractionDir[0], rs = u.refractionDir[1];
    CHECK_EQ(dispR[0], (g[0] * rc + g[1] * -rs) * bandR);
    CHECK_EQ(dispR[1], (g[0] * rs + g[1] * rc) * bandR);

    // The aberration: the same two rows, exchanged.
    const float ac = u.aberrationDir[0], as = u.aberrationDir[1];
    CHECK_EQ(dispA[0], (g[0] * as + g[1] * ac) * bandA);
    CHECK_EQ(dispA[1], (g[0] * ac + g[1] * -as) * bandA);

    // And the unswapped form -- the one a careless reading produces -- is a
    // different vector. Without this the two assertions above would pass on a
    // transcription that had simply named its rows the other way round.
    const float unswapped[2] = {(g[0] * ac + g[1] * -as) * bandA,
                                (g[0] * as + g[1] * ac) * bandA};
    CHECK(!sameBits(dispA[0], unswapped[0]));
    CHECK(!sameBits(dispA[1], unswapped[1]));

    // The reflection is norm preserving, like the rotation, but orientation
    // reversing: the cross product of the two rows has the opposite sign.
    const float rowsRefraction = rc * rc + rs * rs;          // det [[c,-s],[s,c]]
    const float rowsAberration = -(as * as + ac * ac);       // det [[s,c],[c,-s]]
    CHECK(rowsRefraction > 0.0f);
    CHECK(rowsAberration < 0.0f);
}

// ===========================================================================
// 4. the zero height, which the packer guards and the shader does not
// ===========================================================================

// `[BIN]` doc 03 §27.2: every `...Height` key is written as `1/height` with an
// `fcmp`/`fcsel` that sends zero to ZERO. The shader never divides, so a height
// of zero has to arrive as an invHeight of zero and produce a CONSTANT band --
// not an infinity, and not a NaN seven taps later.
TEST_CASE(a_height_of_zero_gives_a_constant_band_and_not_an_infinity) {
    CHECK_EQ(glass::reciprocalHeight(0.0f), 0.0f);
    CHECK_EQ(glass::reciprocalHeight(-3.0f), 0.0f);   // the compare is `> 0`

    Scene s = makeScene();
    s.params[5] = glass::reciprocalHeight(0.0f);
    s.params[8] = glass::reciprocalHeight(0.0f);

    const ForegroundUniforms u = s.parsed().uniforms;
    // With invHeight zero the profile's input is zero, so `circ` is zero and the
    // band is the amount itself -- for every distance, which is the point.
    for (float d : {-9.0f, -1.0f, 0.0f, 1.0f, 40.0f}) {
        CHECK_EQ(glass::bandAmount(d, u.refractionAmount, 0.0f, u.refractionOffset),
                 u.refractionAmount);
    }

    int finite = 0;
    for (const Probe& p : s.probes) {
        float got[4];
        oracleAt(s, p, got);
        for (int k = 0; k < 4; ++k) {
            CHECK(std::isfinite(got[k]));
            if (std::isfinite(got[k])) ++finite;
        }
    }
    CHECK(finite == int(s.probes.size()) * 4);
}

// ===========================================================================
// 5. the resolve, which is NOT the shared combine
// ===========================================================================

// `[BIN]` `%261` computes `alphaSum / 7`, `%266` multiplies the coverage into it
// and only `%270` premultiplies the rgb. `rb::glass::aberrationCombine`
// premultiplies by the un-covered alpha, because that is what mod98's call site
// does -- so the two differ by a whole factor of `edge * coverage` inside a
// multiply, not by a rounding.
TEST_CASE(the_resolve_folds_the_coverage_in_before_it_premultiplies) {
    const float acc[3] = {1.4f, 2.1f, 0.9f};
    const float alphaSum = 3.5f;
    const float coverage = 0.6f;
    const float edgeRaw = 0.4f;

    float got[4];
    foregroundResolve(acc, alphaSum, coverage, edgeRaw, got);

    const float sevenths = alphaSum * glass::kAberrationAlphaScale;
    const float alpha = (edgeRaw * coverage) * sevenths;
    CHECK_EQ(got[3], alpha);
    for (int k = 0; k < 3; ++k) {
        CHECK_EQ(got[k], (acc[k] * glass::kAberrationRgbScale[k]) * alpha);
    }

    // The shared combine, on the same inputs, and it has to disagree.
    float shared[4];
    glass::aberrationCombine(acc, alphaSum, shared);
    CHECK(!sameBits(got[0], shared[0]));
    CHECK(!sameBits(got[3], shared[3]));

    // The edge is SATURATED here and nowhere earlier: `%258` is the first
    // `air.fast_saturate` in the module's data path.
    float above[4], clamped[4];
    foregroundResolve(acc, alphaSum, coverage, 3.0f, above);
    foregroundResolve(acc, alphaSum, coverage, 1.0f, clamped);
    for (int k = 0; k < 4; ++k) CHECK_EQ(above[k], clamped[k]);
    float below[4], zeroed[4];
    foregroundResolve(acc, alphaSum, coverage, -2.0f, below);
    foregroundResolve(acc, alphaSum, coverage, 0.0f, zeroed);
    for (int k = 0; k < 4; ++k) CHECK_EQ(below[k], zeroed[k]);
}

// ===========================================================================
// 6. the edge fade, at both endpoints and inside
// ===========================================================================

// `[BIN]` `%281`..`%289`. Below `edgeStart` the range saturates to 0 and the
// opacity is `edgeOpacityStart`; above `edgeEnd` it saturates to 1 and the
// opacity is `edgeOpacityEnd`; between, it is the mix. All three are exercised,
// because a guard nobody drives past is not a guard.
TEST_CASE(the_edge_fade_saturates_at_both_ends_of_its_range) {
    Scene s;
    setUniforms(s);
    setLayers(s);
    const ForegroundUniforms u = s.parsed().uniforms;

    // Below the start, and far below it: one value, and the mix at t = 0 is
    // `x + (y - x) * 0`, which IS exactly x.
    CHECK_EQ(foregroundEdgeFade(u, u.edgeStart - 1.0f), 1.0f - u.edgeOpacityStart);
    CHECK_EQ(foregroundEdgeFade(u, -1000.0f), 1.0f - u.edgeOpacityStart);

    // Above the end: one value, and it is NOT `1 - edgeOpacityEnd`.
    //
    // THIS ASYMMETRY IS THE TARGET'S, AND IT IS THE SHARPEST TEST IN THIS FILE.
    // `[BIN]` `%287` is `air.mix`, which is `x + (y - x) * a`; at `a = 1` that is
    // `x + y - x`, and in float that is not `y`. A `mix` written the other legal
    // way -- `x * (1 - a) + y * a` -- WOULD return `y` exactly here. So this one
    // comparison separates the two expansions, and a transcription that reached
    // for GLSL's `mix()` (whose expansion is unspecified) would fail it on the
    // GPU and pass it on the CPU.
    const float atEnd = u.edgeOpacityStart + (u.edgeOpacityEnd - u.edgeOpacityStart) * 1.0f;
    CHECK_EQ(foregroundEdgeFade(u, u.edgeEnd + 1.0f), 1.0f - atEnd);
    CHECK_EQ(foregroundEdgeFade(u, 1000.0f), 1.0f - atEnd);
    CHECK(!sameBits(atEnd, u.edgeOpacityEnd));
    CHECK(std::fabs(atEnd - u.edgeOpacityEnd) < 1e-6f);

    // Inside, and it is monotone between the two -- and NOT equal to either, or
    // the range would be doing nothing.
    const float mid = foregroundEdgeFade(u, 0.5f * (u.edgeStart + u.edgeEnd));
    CHECK(mid < 1.0f - u.edgeOpacityStart);
    CHECK(mid > 1.0f - u.edgeOpacityEnd);

    // THE BIAS IS A SECOND DIVISION, not a multiply by the reciprocal. `[BIN]`
    // `%284` is `fdiv(fneg(edgeStart), span)` beside `%282`'s `fdiv(1, span)`;
    // AquaKit writes `-edge_start * invSpan`. They are the same number to within
    // a rounding and not the same rounding, so the divergence is recorded rather
    // than tidied -- and it is real, not theoretical: the sweep below finds
    // ranges where the two answers are different floats.
    //
    // `[ART]` AND THE DIFFERENTIAL CANNOT BITE ON IT, which is worth stating
    // plainly rather than leaving as an unexamined pass. Swapping this line for
    // AquaKit's form and re-running the whole file leaves it green: the two
    // forms part by less than the 2.5 ULP the Vulkan spec already grants
    // `OpFDiv`, so no GPU-against-oracle comparison on conforming hardware can
    // separate them. mod99's form is carried because it is what mod99 does, not
    // because a test could catch the other one.
    int parted = 0;
    for (int i = 1; i <= 400; ++i) {
        const float e0 = -4.5f + 0.01f * float(i);
        const float e1 = e0 + 0.75f + 0.003f * float(i);
        const float sp = e1 - e0;
        if (!sameBits(-e0 / sp, -e0 * (1.0f / sp))) ++parted;
    }
    std::printf("  [ART] the fade's bias: %d of 400 ranges where a divide and a "
                "reciprocal-multiply give different floats\n", parted);
    CHECK(parted > 0);

    // And an inverted range still runs: the saturate makes it a step, not a NaN.
    ForegroundUniforms flipped = u;
    flipped.edgeStart = u.edgeEnd;
    flipped.edgeEnd = u.edgeStart;
    CHECK(std::isfinite(foregroundEdgeFade(flipped, 0.0f)));
}

// ===========================================================================
// 7. the seven-tap ladder
// ===========================================================================

// `[BIN]` The two loops put the seven taps at `base + dispA * s` for `s` in
// (1, 2/3, 1/3) and then (0, -1/3, -2/3, -1): an evenly spaced sweep from
// `+dispA` to `-dispA`. Red rides the first three, blue the last three, green
// all seven. An averaged comparison would not see this permuted, so the offsets
// are asserted one at a time.
TEST_CASE(the_ladder_sweeps_evenly_from_plus_to_minus_the_aberration) {
    const float want[7] = {1.0f,          2.0f / 3.0f,  1.0f / 3.0f, 0.0f,
                           -1.0f / 3.0f,  -2.0f / 3.0f, -1.0f};
    float prev = 2.0f;
    for (int i = 0; i < glass::kAberrationTaps; ++i) {
        const glass::AberrationTap t = glass::aberrationTap(i);
        // The running sum is not `1 - i/3`, so this is a nearness check with the
        // accumulation's own error and nothing more.
        CHECK(std::fabs(t.offset - want[i]) < 1e-6f);
        // Strictly descending: no two taps land on the same point.
        CHECK(t.offset < prev);
        prev = t.offset;
        // Red only before the middle, blue only after it.
        if (i < 3) CHECK_EQ(t.weight[2], 0.0f);
        if (i >= 3) CHECK_EQ(t.weight[0], 0.0f);
    }

    // And the tap point is the base displaced along `dispA`, with the sign in the
    // offset rather than in a second negated vector.
    const float base[2] = {3.25f, -1.5f};
    const float dispA[2] = {0.4f, -0.7f};
    for (int i = 0; i < glass::kAberrationTaps; ++i) {
        const float o = glass::aberrationTap(i).offset;
        float q[2];
        foregroundTapPoint(base, dispA, o, q);
        CHECK_EQ(q[0], dispA[0] * o + base[0]);
        CHECK_EQ(q[1], dispA[1] * o + base[1]);
    }
}

// The same ladder read back off the GPU, one named tap at a time, with the uv it
// becomes. Aggregate agreement would not see a permutation; this does.
TEST_CASE(the_gpu_lands_each_of_the_seven_taps_where_the_oracle_does) {
    Device& d = gpu();
    if (!d.valid()) return;
    const Scene s = makeScene();
    const ForegroundParams params = s.parsed();

    for (std::uint32_t t = 0; t < std::uint32_t(glass::kAberrationTaps); ++t) {
        const std::vector<float> got = onGpu(d, s, kTapPoint, t);
        REQUIRE(got.size() == s.probes.size() * 4);
        const float offset = glass::aberrationTap(int(t)).offset;
        int bad = 0;
        for (std::size_t i = 0; i < s.probes.size(); ++i) {
            const Probe& p = s.probes[i];
            float uvField[2];
            foregroundLayerUV(params.field, p.p[0], p.p[1], uvField);
            float f[4];
            sampleBilinear(s.field.view(), uvField[0], uvField[1], f);
            const float dist = foregroundDistance(params.uniforms, f[0]);
            float g[2];
            foregroundGradient(params.uniforms, f[1], f[2], g);
            float dispR[2], dispA[2], base[2], q[2], uv[2];
            foregroundRefractionDisplacement(params.uniforms, dist, g, dispR);
            foregroundAberrationDisplacement(params.uniforms, dist, g, dispA);
            foregroundBase(p.p[0], p.p[1], dispR, base);
            foregroundTapPoint(base, dispA, offset, q);
            foregroundLayerUV(params.source, q[0], q[1], uv);

            // The band's root is the only inexact step, and it reaches here
            // scaled by the amounts -- so the allowance is derived from those
            // two numbers rather than counted in ULP of a coordinate.
            const float bound =
                kCircError * (std::fabs(params.uniforms.refractionAmount) +
                              std::fabs(params.uniforms.aberrationAmount)) *
                2.0f;
            if (std::fabs(got[i * 4 + 0] - q[0]) > bound ||
                std::fabs(got[i * 4 + 1] - q[1]) > bound) {
                if (bad < 3) {
                    std::printf("  FAIL tap %u probe %zu: gpu (%.9g, %.9g) oracle (%.9g, %.9g)\n",
                                t, i, double(got[i * 4 + 0]), double(got[i * 4 + 1]), double(q[0]),
                                double(q[1]));
                }
                ++bad;
            }
            // The uv is clamped into the layer's rect, so it is inside it on
            // both sides no matter where the tap landed.
            CHECK(uv[0] >= params.source.m[3][0] && uv[0] <= params.source.m[4][0]);
            (void)uv;
        }
        CHECK_EQ(bad, 0);
    }
}

// ===========================================================================
// 8. the differential
// ===========================================================================

// THE PIECES. The layer transforms, the distance decode, the gradient decode
// and the resolve are multiplies and adds and clamps -- correctly rounded on
// both sides -- so they are compared BIT FOR BIT and an allowance there would
// only be a place for a transcription error to hide. The edge and the fade
// divide, so they carry the bound `OpFDiv`'s 2.5 ULP earns them and nothing
// more.
TEST_CASE(the_gpu_and_the_oracle_agree_on_the_decode) {
    Device& d = gpu();
    if (!d.valid()) return;
    const Scene s = makeScene();
    const ForegroundParams params = s.parsed();

    const std::vector<float> uvs = onGpu(d, s, kLayerUVs);
    REQUIRE(uvs.size() == s.probes.size() * 4);
    for (std::size_t i = 0; i < s.probes.size(); ++i) {
        float a[2], b[2];
        foregroundLayerUV(params.source, s.probes[i].p[0], s.probes[i].p[1], a);
        foregroundLayerUV(params.field, s.probes[i].p[0], s.probes[i].p[1], b);
        CHECK(sameBits(uvs[i * 4 + 0], a[0]));
        CHECK(sameBits(uvs[i * 4 + 1], a[1]));
        CHECK(sameBits(uvs[i * 4 + 2], b[0]));
        CHECK(sameBits(uvs[i * 4 + 3], b[1]));
    }

    const std::vector<float> dec = onGpu(d, s, kDecode);
    REQUIRE(dec.size() == s.probes.size() * 4);
    for (std::size_t i = 0; i < s.probes.size(); ++i) {
        const Probe& p = s.probes[i];
        float uvField[2], f[4], g[2];
        foregroundLayerUV(params.field, p.p[0], p.p[1], uvField);
        sampleBilinear(s.field.view(), uvField[0], uvField[1], f);
        const float dist = foregroundDistance(params.uniforms, f[0]);
        foregroundGradient(params.uniforms, f[1], f[2], g);
        CHECK(sameBits(dec[i * 4 + 0], dist));
        CHECK(sameBits(dec[i * 4 + 1], g[0]));
        CHECK(sameBits(dec[i * 4 + 2], g[1]));
        // The one division in this stage, and the only value here that is not
        // required to match on the bits.
        const float wantEdge = foregroundEdgeRaw(dist, p.fwidthD);
        CHECK(std::fabs(dec[i * 4 + 3] - wantEdge) <= edgeRawBound(dist, p.fwidthD));
    }
}

// The sampler is an INPUT to this stage rather than hidden state, because
// `[OBS]` the target's `@__air_sampler_state` word is not decoded. That makes it
// the one thing in the differential which is ours on both sides, so it is
// checked directly and on both textures -- otherwise a sampler that disagreed
// with itself would show up only as a diffuse mismatch in the whole stage, where
// the band and the divisions would be blamed for it first.
TEST_CASE(the_two_samplers_agree_with_the_oracles_bilinear) {
    Device& d = gpu();
    if (!d.valid()) return;
    Scene s = makeScene();
    // Sweep the uv square, including outside it, so the edge clamp is driven.
    for (std::size_t i = 0; i < s.probes.size(); ++i) {
        const float t = float(i) / float(s.probes.size() - 1);
        s.probes[i].p[0] = -0.2f + 1.4f * t;
        s.probes[i].p[1] = 1.1f - 1.3f * t;
    }
    const std::vector<float> src = onGpu(d, s, kSampleSource);
    const std::vector<float> fld = onGpu(d, s, kSampleField);
    REQUIRE(src.size() == s.probes.size() * 4);
    REQUIRE(fld.size() == s.probes.size() * 4);
    for (std::size_t i = 0; i < s.probes.size(); ++i) {
        float a[4], b[4];
        sampleBilinear(s.source.view(), s.probes[i].p[0], s.probes[i].p[1], a);
        sampleBilinear(s.field.view(), s.probes[i].p[0], s.probes[i].p[1], b);
        for (int k = 0; k < 4; ++k) {
            CHECK(sameBits(src[i * 4 + k], a[k]));
            CHECK(sameBits(fld[i * 4 + k], b[k]));
        }
    }
}

TEST_CASE(the_gpu_and_the_oracle_agree_on_the_fade_and_the_resolve) {
    Device& d = gpu();
    if (!d.valid()) return;
    Scene s = makeScene();
    const ForegroundUniforms u = s.parsed().uniforms;

    // Drive the fade's range past both endpoints and through the middle.
    for (std::size_t i = 0; i < s.probes.size(); ++i) {
        const float t = float(i) / float(s.probes.size() - 1);
        s.probes[i].aux[0] = u.edgeStart - 2.0f + t * (u.edgeEnd - u.edgeStart + 4.0f);
    }
    const std::vector<float> fade = onGpu(d, s, kEdgeFade);
    REQUIRE(fade.size() == s.probes.size() * 4);
    int belowStart = 0, aboveEnd = 0, inside = 0;
    for (std::size_t i = 0; i < s.probes.size(); ++i) {
        const float dd = s.probes[i].aux[0];
        CHECK(std::fabs(fade[i * 4 + 0] - foregroundEdgeFade(u, dd)) <= edgeFadeBound(u, dd));
        if (dd < u.edgeStart) ++belowStart;
        else if (dd > u.edgeEnd) ++aboveEnd;
        else ++inside;
    }
    // Every branch of the range actually ran.
    CHECK(belowStart > 0);
    CHECK(aboveEnd > 0);
    CHECK(inside > 0);

    for (std::size_t i = 0; i < s.probes.size(); ++i) {
        const float t = float(i) / float(s.probes.size());
        s.probes[i].aux[0] = 0.2f + t;          // acc.r
        s.probes[i].aux[1] = 1.5f - t;          // acc.g
        s.probes[i].aux[2] = 0.7f + 2.0f * t;   // acc.b
        s.probes[i].aux[3] = 0.5f + 6.0f * t;   // alphaSum
        s.probes[i].aux[4] = t;                 // coverage
        s.probes[i].aux[5] = -0.4f + 2.0f * t;  // edgeRaw, past both saturates
    }
    const std::vector<float> res = onGpu(d, s, kResolve);
    REQUIRE(res.size() == s.probes.size() * 4);
    for (std::size_t i = 0; i < s.probes.size(); ++i) {
        const float* a = s.probes[i].aux;
        const float acc[3] = {a[0], a[1], a[2]};
        float want[4];
        foregroundResolve(acc, a[3], a[4], a[5], want);
        for (int k = 0; k < 4; ++k) CHECK(sameBits(res[i * 4 + k], want[k]));
    }
}

// THE WHOLE STAGE WITH THE ROOT TAKEN OUT OF IT, on a scene whose band is
// driven to `x = 0`.
//
// With `invHeight` zero the profile's input is exactly zero, `x * (2 - x)` is
// exactly zero, and `sqrt(0)` is zero on any conforming implementation -- so one
// of the module's two loosely-specified operations is gone and the bound this
// test holds comes from the DIVISIONS alone. That is what makes it sharper than
// the general one below, not just a repeat of it: everything else still runs --
// the field tap, the early-out, both rotations, all seven dispersed taps, the
// resolve and the fade -- and every tap position and every UV is required to be
// bit-identical, because with the band exact nothing upstream of the divisions
// can move.
TEST_CASE(the_gpu_and_the_oracle_agree_with_the_root_taken_out_of_the_stage) {
    Device& d = gpu();
    if (!d.valid()) return;
    Scene s = makeScene();
    s.params[5] = 0.0f;   // 1/refractionHeight, from a height of zero
    s.params[8] = 0.0f;   // 1/aberrationHeight

    const std::vector<float> got = onGpu(d, s, kWhole);
    REQUIRE(got.size() == s.probes.size() * 4);
    int drew = 0, skipped = 0, bad = 0, exact = 0;
    double worst = 0.0, worstBound = 0.0;
    for (std::size_t i = 0; i < s.probes.size(); ++i) {
        float want[4];
        oracleAt(s, s.probes[i], want);
        if (want[3] == 0.0f && want[0] == 0.0f) ++skipped; else ++drew;
        for (int k = 0; k < 4; ++k) {
            if (sameBits(got[i * 4 + k], want[k])) {
                ++exact;
                continue;
            }
            const float bound = derivedBound(s, s.probes[i], k, /*bandsExact=*/true);
            const double err = std::fabs(double(got[i * 4 + k]) - double(want[k]));
            if (err > worst) {
                worst = err;
                worstBound = bound;
            }
            if (err > double(bound)) {
                if (bad < 4) {
                    std::printf("  FAIL probe %zu ch %d: gpu %.9g oracle %.9g (%u ulp), "
                                "err %.3e > %.3e\n",
                                i, k, double(got[i * 4 + k]), double(want[k]),
                                ulpsApart(got[i * 4 + k], want[k]), err, double(bound));
                }
                ++bad;
            }
        }
    }
    std::printf("  [ART] root removed: %d of %zu components bit-exact; worst |gpu-oracle| = "
                "%.3e against a divisions-only bound of %.3e\n",
                exact, s.probes.size() * 4, worst, worstBound);
    CHECK_EQ(bad, 0);
    // Both sides of the early-out were exercised.
    CHECK(drew > 0);
    CHECK(skipped > 0);
}

// THE WHOLE STAGE ON A GENERAL SCENE, with the allowance DERIVED rather than
// chosen: the same stage is evaluated with the band moved by `3 ULP * |amount|`
// and each quotient by 2.5 ULP, and the deviations those produce are summed. The
// worst disagreement is printed so a driver parting from the oracle reads as a
// number moving rather than only as a pass/fail flip.
TEST_CASE(the_gpu_and_the_oracle_agree_to_the_derived_bound_of_the_root) {
    Device& d = gpu();
    if (!d.valid()) return;
    const Scene s = makeScene();

    const std::vector<float> got = onGpu(d, s, kWhole);
    REQUIRE(got.size() == s.probes.size() * 4);
    double worst = 0.0;
    double worstBound = 0.0;
    int exact = 0, bad = 0;
    for (std::size_t i = 0; i < s.probes.size(); ++i) {
        float want[4];
        oracleAt(s, s.probes[i], want);
        for (int k = 0; k < 4; ++k) {
            const float a = got[i * 4 + k];
            const float b = want[k];
            if (sameBits(a, b)) {
                ++exact;
                continue;
            }
            const float bound = derivedBound(s, s.probes[i], k, /*bandsExact=*/false);
            const double err = std::fabs(double(a) - double(b));
            if (err > worst) {
                worst = err;
                worstBound = bound;
            }
            if (err > double(bound)) {
                if (bad < 4) {
                    std::printf("  FAIL probe %zu ch %d: gpu %.9g oracle %.9g err %.3e > %.3e\n",
                                i, k, double(a), double(b), err, double(bound));
                }
                ++bad;
            }
        }
    }
    std::printf("  [ART] whole stage: %d of %zu components bit-exact; worst |gpu-oracle| = "
                "%.3e against a derived bound of %.3e\n",
                exact, s.probes.size() * 4, worst, worstBound);
    CHECK_EQ(bad, 0);
}

// ===========================================================================
// 9. the two textures, and which is which
// ===========================================================================

// `[BIN]` `%74` is handed `%5` (arg 5, `t1`) with the layer at float 28 -- the
// FIELD, once. `%187` and `%228` are handed `%4` (arg 4, `t0`) with the layer at
// float 18 -- the layer's own CONTENTS, seven times. That direction was read off
// the pointer each sample is given, and `displacementMap_v1` is the precedent
// for why it has to be read and not assumed.
//
// Swapping the two images has to move the answer. It is a weak-looking test and
// it is the one that would have caught the mistake.
TEST_CASE(the_field_is_the_second_texture_and_the_contents_are_the_first) {
    const Scene s = makeScene();
    Scene swapped = s;
    swapped.source = s.field;
    swapped.field = s.source;

    int differing = 0;
    int drawn = 0;
    for (const Probe& p : s.probes) {
        float a[4], b[4];
        oracleAt(s, p, a);
        oracleAt(swapped, p, b);
        for (int k = 0; k < 4; ++k) {
            CHECK(a[k] == a[k]);   // never a NaN
            if (!sameBits(a[k], b[k])) ++differing;
        }
        if (a[3] != 0.0f) ++drawn;
    }
    CHECK(drawn > 0);
    CHECK(differing > 0);

    // And the seven taps really are seven: changing the source image ANYWHERE
    // inside the dispersion's footprint changes the answer, while the field's
    // single tap only responds at the one place it reads.
    Scene brighter = s;
    for (float& v : brighter.source.rgba) v = v * 0.5f;
    int moved = 0;
    for (const Probe& p : s.probes) {
        float a[4], c[4];
        oracleAt(s, p, a);
        oracleAt(brighter, p, c);
        if (!sameBits(a[0], c[0])) ++moved;
    }
    CHECK(moved > 0);
}

// ===========================================================================
// 10. the early-out, and the guarded divisor
// ===========================================================================

// `[BIN]` `%88`: `fcmp olt half %87, 0xH1419` on the FIELD SAMPLE'S ALPHA, with
// the true edge going straight to a `zeroinitializer` return. The comparison is
// strictly-less, so a coverage exactly at the epsilon DRAWS.
TEST_CASE(the_coverage_cutoff_returns_zero_and_bites_at_the_half_epsilon) {
    Scene s = makeScene();
    // A flat field, so the sampled coverage is exactly the constant written --
    // the bilinear cannot move it and the cutoff can be driven to its boundary.
    const int n = s.field.w;
    auto flatField = [&](float coverage) {
        Bitmap f(n, n);
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) {
                f.at(x, y, 0) = -0.75f;
                f.at(x, y, 1) = 0.6f;
                f.at(x, y, 2) = -0.8f;
                f.at(x, y, 3) = coverage;
            }
        }
        return f;
    };

    Probe p;
    p.p[0] = 3.4f;
    p.p[1] = 2.6f;
    p.fwidthD = 0.8f;

    // Just below: zero, in all four channels, and not merely a small number.
    s.field = flatField(std::nextafterf(glass::kEpsilon, 0.0f));
    float below[4];
    oracleAt(s, p, below);
    for (int k = 0; k < 4; ++k) CHECK_EQ(below[k], 0.0f);

    // Exactly at the epsilon: the comparison is `olt`, so this DRAWS.
    s.field = flatField(glass::kEpsilon);
    float at[4];
    oracleAt(s, p, at);
    CHECK(at[3] != 0.0f);

    // And a long way above it, for scale.
    s.field = flatField(1.0f);
    float above[4];
    oracleAt(s, p, above);
    CHECK(above[3] > at[3]);

    // Zero coverage takes the early-out too, which is the case the whole branch
    // exists for: an entirely uncovered pixel costs one texture tap.
    s.field = flatField(0.0f);
    float none[4];
    oracleAt(s, p, none);
    for (int k = 0; k < 4; ++k) CHECK_EQ(none[k], 0.0f);
}

// The GPU takes the same branch at the same place. A shader that computed the
// full seven taps and multiplied by zero would agree on the value and not on the
// branch, which is why the boundary is driven from both sides.
TEST_CASE(the_gpu_takes_the_coverage_cutoff_where_the_oracle_does) {
    Device& d = gpu();
    if (!d.valid()) return;
    Scene s = makeScene();
    s.params[5] = 0.0f;
    s.params[8] = 0.0f;

    // A field whose coverage sweeps across the cutoff, so probes land on both
    // sides of it and on it.
    const int n = s.field.w;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            s.field.at(x, y, 3) = (x + y) < 5 ? 0.0f : glass::kEpsilon * float(x + y);
        }
    }

    const std::vector<float> got = onGpu(d, s, kWhole);
    REQUIRE(got.size() == s.probes.size() * 4);
    int zeros = 0, nonzeros = 0;
    for (std::size_t i = 0; i < s.probes.size(); ++i) {
        float want[4];
        oracleAt(s, s.probes[i], want);
        const bool out = want[0] == 0.0f && want[1] == 0.0f && want[2] == 0.0f && want[3] == 0.0f;
        if (out) {
            // The early-out is the one place where an allowance would let a
            // wrong branch through: a shader that computed the seven taps and
            // multiplied by a near-zero coverage would land near zero, not on
            // it. So the taken branch is required to produce EXACT zeros.
            ++zeros;
            for (int k = 0; k < 4; ++k) CHECK_EQ(got[i * 4 + k], 0.0f);
        } else {
            ++nonzeros;
            for (int k = 0; k < 4; ++k) {
                CHECK(std::fabs(got[i * 4 + k] - want[k]) <=
                      derivedBound(s, s.probes[i], k, /*bandsExact=*/true));
            }
        }
    }
    CHECK(zeros > 0);
    CHECK(nonzeros > 0);
}

// `[BIN]` `%190`/`%231`: the per-tap divisor is `max(sample.a, 0.001)` -- the
// HALF epsilon again -- while `%211`/`%252` sum the RAW alpha. Two different
// values off the same sample, and using the floored one in the sum is the error
// AquaKit records having made in the other direction.
TEST_CASE(the_tap_divisor_is_floored_and_the_alpha_sum_is_not) {
    Scene s = makeScene();
    s.params[5] = 0.0f;
    s.params[8] = 0.0f;
    // A source whose alpha is everywhere far below the epsilon but not constant,
    // so the floor is genuinely reached at every tap rather than merely present.
    for (int y = 0; y < s.source.h; ++y) {
        for (int x = 0; x < s.source.w; ++x) {
            s.source.at(x, y, 3) = 1e-7f * float(1 + x + y);
        }
    }

    const ForegroundParams params = s.parsed();
    int floored = 0;
    float wrongSum = 0.0f;
    float rightSum = 0.0f;
    const Probe& p = s.probes[s.probes.size() / 2];

    float uvField[2], f[4], g[2];
    foregroundLayerUV(params.field, p.p[0], p.p[1], uvField);
    sampleBilinear(s.field.view(), uvField[0], uvField[1], f);
    const float dist = foregroundDistance(params.uniforms, f[0]);
    foregroundGradient(params.uniforms, f[1], f[2], g);
    float dispR[2], dispA[2], base[2];
    foregroundRefractionDisplacement(params.uniforms, dist, g, dispR);
    foregroundAberrationDisplacement(params.uniforms, dist, g, dispA);
    foregroundBase(p.p[0], p.p[1], dispR, base);
    for (int i = 0; i < glass::kAberrationTaps; ++i) {
        float q[2], uv[2], c[4];
        foregroundTapPoint(base, dispA, glass::aberrationTap(i).offset, q);
        foregroundLayerUV(params.source, q[0], q[1], uv);
        sampleBilinear(s.source.view(), uv[0], uv[1], c);
        if (c[3] < glass::kEpsilon) ++floored;
        rightSum += c[3];
        wrongSum += c[3] > glass::kEpsilon ? c[3] : glass::kEpsilon;
    }
    // The floor was actually reached, and the two sums are different numbers.
    CHECK(floored > 0);
    CHECK(!sameBits(rightSum, wrongSum));

    float got[4];
    oracleAt(s, p, got);
    // The result is finite: without the floor the un-premultiply would divide by
    // 1e-7 and the seven taps would overflow the picture rather than the float.
    for (int k = 0; k < 4; ++k) CHECK(std::isfinite(got[k]));

    // AND THE ORACLE TAKES THE RAW SUM, which is the assertion that makes this a
    // gate. Checking only that the two SUMS differ leaves the stage free to use
    // either; so the stage is run both ways, on a scene where the floor bites at
    // every tap, and required to be the raw one on the bits.
    int matchedRaw = 0, differedFromFloored = 0;
    for (const Probe& probe : s.probes) {
        float mine[4], raw[4], floorSum[4];
        oracleAt(s, probe, mine);
        Knobs k;
        stageWithKnobs(s, probe, k, raw);
        k.flooredAlphaSum = true;
        stageWithKnobs(s, probe, k, floorSum);
        for (int c = 0; c < 4; ++c) {
            CHECK(sameBits(mine[c], raw[c]));
            if (sameBits(mine[c], raw[c])) ++matchedRaw;
            if (!sameBits(raw[c], floorSum[c])) ++differedFromFloored;
        }
    }
    CHECK_EQ(matchedRaw, int(s.probes.size()) * 4);
    // And the wrong transcription really is a different picture here, so the
    // agreement above is a choice this scene can see and not a coincidence.
    CHECK(differedFromFloored > 0);
}
