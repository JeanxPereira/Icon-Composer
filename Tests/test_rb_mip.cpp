// The backdrop mip pyramid: the reduction, the LOD formula, and the fetch.
//
// WHAT THIS GATE IS AND IS NOT. Two things here are transcriptions and are
// checked against the IR: the `RB::MultiLevelLayer` UV form and the LOD
// formula. Everything else is OUR pyramid -- Apple's mip generation was never
// read -- so the rest of the file is gated against CLOSED-FORM oracles: a
// constant that must survive every level exactly, a checkerboard whose mean is
// known, a three-tap kernel whose weights are written out by hand.
//
// THE TEST THIS FILE EXISTS FOR is `mip_premultiplied_average`. This repository
// has a recorded bug where `IconRenderer` accumulated premultiplied and never
// un-multiplied, and every test stayed green while semi-transparent output came
// out too dark -- because the tests looked at ALPHA and never at colour. The
// same mistake in a mip reduction is invisible in exactly the same way: alpha
// reduces correctly under either convention, and only the COLOUR at an edge
// where alpha varies tells the two apart. So that test asserts the colour, and
// writes down the wrong number next to the right one.
#include "check.h"
#include "Source/RenderBox/MipPyramid.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

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

// Bit equality, because "exactly" in the claims below means exactly. A `==` on
// two floats that are both zero would accept -0.0 for +0.0; here the two are
// the same number and the same bits, and saying so costs nothing.
bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

bool near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

MipLevel makeLevel(std::uint32_t w, std::uint32_t h, const std::vector<float>& rgba) {
    MipLevel l;
    l.width = w;
    l.height = h;
    l.rgba = rgba;
    return l;
}

// A deterministic image with structure in every channel, for the differential.
std::vector<float> patterned(std::uint32_t w, std::uint32_t h) {
    std::vector<float> out(static_cast<std::size_t>(w) * h * 4, 0.0f);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * w + x) * 4;
            const float a = 0.25f + 0.5f * static_cast<float>((x + 2 * y) % 3) / 3.0f;
            out[at + 0] = a * (static_cast<float>(x % 5) / 4.0f);
            out[at + 1] = a * (static_cast<float>(y % 7) / 6.0f);
            out[at + 2] = a * (static_cast<float>((x * y) % 11) / 10.0f);
            out[at + 3] = a;
        }
    }
    return out;
}

}  // namespace

// ---- the two transcriptions ----------------------------------------------

// `[BIN]` uv(v) = clamp(fma(v.xx, m0, fma(v.yy, m1, m2)), m3, m4), mod98.
//
// The clamp rect is checked on BOTH sides and per AXIS, because a transcription
// that clamped to a scalar range, or that clamped before the transform, would
// still produce plausible UV for a point in the middle of the rect.
TEST_CASE(mip_layer_uv_transform_and_clamp) {
    MultiLevelLayer identity;
    float uv[2];
    layerUv(identity, 0.25f, 0.75f, uv);
    CHECK(sameBits(uv[0], 0.25f));
    CHECK(sameBits(uv[1], 0.75f));

    // A transform whose two columns are NOT axis aligned, so an implementation
    // that transposed m0 and m1 fails rather than agreeing by symmetry.
    MultiLevelLayer skewed;
    skewed.m[0][0] = 2.0f;  skewed.m[0][1] = 0.5f;   // the x column
    skewed.m[1][0] = 0.0f;  skewed.m[1][1] = 4.0f;   // the y column
    skewed.m[2][0] = 0.125f; skewed.m[2][1] = -0.25f;
    skewed.m[3][0] = -10.0f; skewed.m[3][1] = -10.0f;
    skewed.m[4][0] = 10.0f;  skewed.m[4][1] = 10.0f;
    layerUv(skewed, 0.5f, 0.25f, uv);
    CHECK(sameBits(uv[0], 0.5f * 2.0f + 0.25f * 0.0f + 0.125f));
    CHECK(sameBits(uv[1], 0.5f * 0.5f + 0.25f * 4.0f + -0.25f));

    // The rect bites, and it bites per axis and on both ends.
    MultiLevelLayer boxed;
    boxed.m[3][0] = 0.2f; boxed.m[3][1] = 0.3f;
    boxed.m[4][0] = 0.8f; boxed.m[4][1] = 0.6f;
    layerUv(boxed, -5.0f, 0.45f, uv);
    CHECK(sameBits(uv[0], 0.2f));
    CHECK(sameBits(uv[1], 0.45f));
    layerUv(boxed, 0.45f, 99.0f, uv);
    CHECK(sameBits(uv[0], 0.45f));
    CHECK(sameBits(uv[1], 0.6f));
}

// The half narrowing that mod98 does before the log. Known values, computed by
// hand from the format rather than from another implementation of it -- two
// of them are exact TIES, which is the only place round-to-nearest-EVEN can be
// told from round-half-away.
TEST_CASE(mip_quantize_to_half) {
    CHECK(sameBits(quantizeToHalf(1.0f), 1.0f));
    CHECK(sameBits(quantizeToHalf(0.0f), 0.0f));
    CHECK(sameBits(quantizeToHalf(-2.5f), -2.5f));

    // 0.1 is not a half; the nearest one is 0x2E66.
    CHECK(sameBits(quantizeToHalf(0.1f), 0.0999755859375f));

    // Spacing is 2 between 2048 and 4096, so 2049 and 2051 are exact ties.
    // Even mantissa wins: 2049 goes DOWN to 2048, 2051 goes UP to 2052.
    CHECK(sameBits(quantizeToHalf(2049.0f), 2048.0f));
    CHECK(sameBits(quantizeToHalf(2051.0f), 2052.0f));

    // The ends. 65504 is the largest half; 2^-24 the smallest subnormal.
    CHECK(sameBits(quantizeToHalf(65504.0f), 65504.0f));
    CHECK(std::isinf(quantizeToHalf(70000.0f)));
    CHECK(sameBits(quantizeToHalf(6e-8f), 5.9604644775390625e-8f));
    CHECK(sameBits(quantizeToHalf(1e-8f), 0.0f));

    // The floor literal itself is a half, so it survives untouched. If it did
    // not, the transcription would be carrying a number the target cannot hold.
    CHECK(sameBits(quantizeToHalf(kBackdropRadiusFloor), kBackdropRadiusFloor));
}

// `[BIN]` lod = min(log2(max(1e-3, radius)) - bias, cap), mod98, with the floor
// and the log in HALF and the bias and cap in float.
TEST_CASE(mip_backdrop_lod_formula) {
    // Powers of two, where log2 is exact and half rounding cannot hide an error.
    CHECK(sameBits(backdropLod(1.0f, 0.0f, 100.0f), 0.0f));
    CHECK(sameBits(backdropLod(2.0f, 0.0f, 100.0f), 1.0f));
    CHECK(sameBits(backdropLod(4.0f, 0.0f, 100.0f), 2.0f));
    CHECK(sameBits(backdropLod(0.5f, 0.0f, 100.0f), -1.0f));
    CHECK(sameBits(backdropLod(1024.0f, 0.0f, 100.0f), 10.0f));

    // The bias is subtracted AFTER the log and BEFORE the cap.
    CHECK(sameBits(backdropLod(4.0f, 1.5f, 100.0f), 0.5f));
    CHECK(sameBits(backdropLod(4.0f, -1.0f, 100.0f), 3.0f));

    // A non-power of two, so the half rounding of the log is exercised: the
    // nearest half to log2(3) = 1.5849625 is 1.5849609375 (spacing 2^-10 in
    // [1, 2)).
    CHECK(sameBits(backdropLod(3.0f, 0.0f, 100.0f), 1.5849609375f));

    // The layer overload reads the SIXTH float2 and nothing else.
    MultiLevelLayer layer;
    layer.m[5][0] = 2.0f;
    layer.m[5][1] = 100.0f;
    CHECK(sameBits(backdropLod(layer, 16.0f), 2.0f));
}

// Both guards, both branches. An untested guard is not a guard.
TEST_CASE(mip_backdrop_lod_edges) {
    // THE FLOOR. radius 0 would be log2(0) = -inf without it, and -inf as an
    // explicit LOD is not a level, it is undefined behaviour at the sampler.
    const float atZero = backdropLod(0.0f, 0.0f, 100.0f);
    CHECK(!std::isinf(atZero));
    CHECK(!std::isnan(atZero));
    CHECK(sameBits(atZero, quantizeToHalf(std::log2(kBackdropRadiusFloor))));
    // And it is the value the half 0xH1419 actually gives: log2(2^-10 *
    // 1.0244140625) = -9.9652..., whose nearest half (spacing 2^-7 in [8, 16))
    // is -9.96875.
    CHECK(sameBits(atZero, -9.96875f));

    // A negative radius takes the same branch rather than producing a NaN.
    CHECK(sameBits(backdropLod(-5.0f, 0.0f, 100.0f), atZero));

    // Anything below the floor is pinned to it -- the floor is not a no-op that
    // only fires at exactly zero.
    CHECK(sameBits(backdropLod(1e-6f, 0.0f, 100.0f), atZero));

    // And the floor does NOT fire above itself.
    CHECK(!sameBits(backdropLod(0.01f, 0.0f, 100.0f), atZero));

    // THE CAP. It bites, and it bites after the bias.
    CHECK(sameBits(backdropLod(1024.0f, 0.0f, 3.0f), 3.0f));
    CHECK(sameBits(backdropLod(1024.0f, 2.0f, 3.0f), 3.0f));
    CHECK(sameBits(backdropLod(1024.0f, 8.0f, 3.0f), 2.0f));   // bias wins here
    // A cap below everything caps even the floored radius.
    CHECK(sameBits(backdropLod(0.0f, 0.0f, -20.0f), -20.0f));
    // And it does NOT bite when it is above the value.
    CHECK(sameBits(backdropLod(4.0f, 0.0f, 3.0f), 2.0f));
}

// ---- the pyramid, which is ours ------------------------------------------

TEST_CASE(mip_level_count_and_extents) {
    CHECK_EQ(nextMipExtent(8u), 4u);
    CHECK_EQ(nextMipExtent(2u), 1u);
    CHECK_EQ(nextMipExtent(1u), 1u);   // bottomed out, carried, never zero
    CHECK_EQ(nextMipExtent(7u), 3u);
    CHECK_EQ(nextMipExtent(5u), 2u);
    CHECK_EQ(nextMipExtent(3u), 1u);

    CHECK_EQ(mipLevelCount(1u, 1u), 1u);
    CHECK_EQ(mipLevelCount(2u, 2u), 2u);
    CHECK_EQ(mipLevelCount(1024u, 1024u), 11u);
    // A non-square chain runs until BOTH axes bottom out; the short one is
    // carried rather than stopping the chain.
    CHECK_EQ(mipLevelCount(8u, 1u), 4u);
    CHECK_EQ(mipLevelCount(5u, 3u), 3u);   // 5x3 -> 2x1 -> 1x1
    CHECK_EQ(mipLevelCount(0u, 4u), 0u);

    MipPyramid p = MipPyramid::build(patterned(5, 3).data(), 5, 3);
    REQUIRE(p.levelCount() == 3);
    CHECK_EQ(p.level(0).width, 5u);
    CHECK_EQ(p.level(0).height, 3u);
    CHECK_EQ(p.level(1).width, 2u);
    CHECK_EQ(p.level(1).height, 1u);
    CHECK_EQ(p.level(2).width, 1u);
    CHECK_EQ(p.level(2).height, 1u);
    CHECK_EQ(p.level(1).rgba.size(), std::size_t{8});
}

// The kernel, weight by weight. This is the choice, so it is read rather than
// inferred from a blurred image.
TEST_CASE(mip_reduce_taps_by_value) {
    int off[3];
    float w[3];

    // Extent 1: carried, not resampled.
    CHECK_EQ(mipReduceTaps(1, 0, off, w), 1);
    CHECK_EQ(off[0], 0);
    CHECK(sameBits(w[0], 1.0f));

    // Even: the plain 2-tap box, stepping by two.
    CHECK_EQ(mipReduceTaps(4, 0, off, w), 2);
    CHECK_EQ(off[0], 0);
    CHECK_EQ(off[1], 1);
    CHECK(sameBits(w[0], 0.5f) && sameBits(w[1], 0.5f));
    CHECK_EQ(mipReduceTaps(4, 1, off, w), 2);
    CHECK_EQ(off[0], 2);
    CHECK_EQ(off[1], 3);

    // Odd, the interesting case: three taps, and the last source texel is
    // REACHED. A 2-tap kernel here would step 0,1 and drop texel 2 entirely.
    CHECK_EQ(mipReduceTaps(3, 0, off, w), 3);
    CHECK_EQ(off[0], 0);
    CHECK_EQ(off[1], 1);
    CHECK_EQ(off[2], 2);
    CHECK(sameBits(w[0], 1.0f / 3.0f));
    CHECK(sameBits(w[1], 1.0f / 3.0f));
    CHECK(sameBits(w[2], 1.0f / 3.0f));

    // Extent 5, n = 2: the weights slide, and adjacent destinations SHARE
    // texel 2 -- which is what makes the filter continuous instead of blocky.
    CHECK_EQ(mipReduceTaps(5, 0, off, w), 3);
    CHECK_EQ(off[0], 0); CHECK_EQ(off[1], 1); CHECK_EQ(off[2], 2);
    CHECK(sameBits(w[0], 2.0f / 5.0f));
    CHECK(sameBits(w[1], 2.0f / 5.0f));
    CHECK(sameBits(w[2], 1.0f / 5.0f));
    CHECK_EQ(mipReduceTaps(5, 1, off, w), 3);
    CHECK_EQ(off[0], 2); CHECK_EQ(off[1], 3); CHECK_EQ(off[2], 4);
    CHECK(sameBits(w[0], 1.0f / 5.0f));
    CHECK(sameBits(w[1], 2.0f / 5.0f));
    CHECK(sameBits(w[2], 2.0f / 5.0f));
}

// THE ANTI-TRUNCATION GATE. Silently dropping the last row or column of an odd
// level is a real defect and an invisible one: the image still looks like the
// image, just shifted, and no mean-of-the-whole test notices. So the property
// asserted is coverage: every source texel is reached, and -- for every extent,
// even and odd -- reached with the SAME total weight, which is what makes the
// filter unbiased rather than merely non-lossy.
TEST_CASE(mip_reduce_taps_cover_every_source_texel) {
    for (std::uint32_t srcExtent = 2; srcExtent <= 17; ++srcExtent) {
        const std::uint32_t dstExtent = nextMipExtent(srcExtent);
        std::vector<double> total(srcExtent, 0.0);
        for (std::uint32_t d = 0; d < dstExtent; ++d) {
            int off[3];
            float w[3];
            const int n = mipReduceTaps(srcExtent, d, off, w);
            double sum = 0.0;
            for (int t = 0; t < n; ++t) {
                REQUIRE(off[t] >= 0 && off[t] < static_cast<int>(srcExtent));
                total[static_cast<std::size_t>(off[t])] += w[t];
                sum += w[t];
            }
            // Each destination texel is an AVERAGE: its weights sum to one.
            if (!near(static_cast<float>(sum), 1.0f, 1e-6f)) {
                std::printf("  FAIL extent %u dst %u weights sum to %f\n", srcExtent, d, sum);
                ++ictest::failures();
            }
        }
        const double expected = static_cast<double>(dstExtent) / static_cast<double>(srcExtent);
        for (std::uint32_t s = 0; s < srcExtent; ++s) {
            if (std::fabs(total[s] - expected) > 1e-6) {
                std::printf("  FAIL extent %u source texel %u carries %f, expected %f\n",
                            srcExtent, s, total[s], expected);
                ++ictest::failures();
            }
        }
    }
}

// A constant image reduces to that constant at EVERY level, exactly -- and that
// is an identity of the arrangement, not of the arithmetic: the sum is formed
// as `ref + sum(w * (s - ref))`, so every term is exactly zero whatever the
// weights round to. Written as `sum(w * s)` this would hold only to a
// tolerance for an odd extent, because 1/3 + 1/3 + 1/3 does not have to be 1.
//
// The odd chain is included on purpose: 13x7 -> 6x3 -> 3x1 -> 1x1 puts a
// three-tap kernel on both axes at once.
TEST_CASE(mip_constant_reduces_exactly) {
    const float colour[4] = {0.3f, 0.7f, 0.1f, 0.9f};  // premultiplied, alpha last
    const std::uint32_t sizes[][2] = {{8, 8}, {13, 7}, {5, 3}, {1024, 1}, {3, 3}};
    for (const auto& size : sizes) {
        std::vector<float> flat(static_cast<std::size_t>(size[0]) * size[1] * 4);
        for (std::size_t t = 0; t + 3 < flat.size(); t += 4) {
            for (int c = 0; c < 4; ++c) flat[t + c] = colour[c];
        }
        MipPyramid p = MipPyramid::build(flat.data(), size[0], size[1]);
        REQUIRE(p.levelCount() == mipLevelCount(size[0], size[1]));
        for (std::size_t li = 0; li < p.levelCount(); ++li) {
            const MipLevel& l = p.level(li);
            for (std::size_t t = 0; t + 3 < l.rgba.size(); t += 4) {
                for (int c = 0; c < 4; ++c) {
                    if (!sameBits(l.rgba[t + c], colour[c])) {
                        std::printf("  FAIL %ux%u level %zu texel %zu channel %d: %g not %g\n",
                                    size[0], size[1], li, t / 4, c,
                                    static_cast<double>(l.rgba[t + c]),
                                    static_cast<double>(colour[c]));
                        ++ictest::failures();
                    }
                }
            }
        }
        // And a constant survives the fetch too, at every LOD including the
        // fractional ones, which is where a level-index-off-by-one would show.
        for (float lod : {0.0f, 0.5f, 1.0f, 1.75f, 99.0f}) {
            float got[4];
            p.sample(lod, 0.5f, 0.5f, got);
            for (int c = 0; c < 4; ++c) CHECK(sameBits(got[c], colour[c]));
        }
    }
}

// A 2x2 checkerboard reduces to its mean, exactly. The values are chosen so the
// mean is exact in binary, so "exactly" is a claim about the reduction and not
// about the luck of the numbers.
TEST_CASE(mip_checkerboard_reduces_to_mean) {
    // Opaque throughout, so this test says nothing about premultiplication --
    // that is the next one's job, and mixing the two would let either pass for
    // the other.
    const std::vector<float> rgba = {
        1.0f, 0.0f, 0.0f, 1.0f,   0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 1.0f,   1.0f, 0.5f, 0.25f, 1.0f,
    };
    MipPyramid p = MipPyramid::build(rgba.data(), 2, 2);
    REQUIRE(p.levelCount() == 2);
    const MipLevel& top = p.level(1);
    CHECK_EQ(top.width, 1u);
    CHECK_EQ(top.height, 1u);
    CHECK(sameBits(top.rgba[0], 0.5f));      // (1 + 0 + 0 + 1) / 4
    CHECK(sameBits(top.rgba[1], 0.125f));    // (0 + 0 + 0 + 0.5) / 4
    CHECK(sameBits(top.rgba[2], 0.0625f));   // (0 + 0 + 0 + 0.25) / 4
    CHECK(sameBits(top.rgba[3], 1.0f));      // opaque stays opaque

    // A 1D checkerboard on an ODD extent, where the mean is over three.
    const std::vector<float> three = {
        0.0f, 0.0f, 0.0f, 1.0f,
        0.75f, 0.0f, 0.0f, 1.0f,
        1.5f, 0.0f, 0.0f, 1.0f,
    };
    MipLevel src = makeLevel(3, 1, three);
    MipLevel out = reduceMipLevel(src);
    CHECK_EQ(out.width, 1u);
    CHECK(sameBits(out.rgba[0], 0.75f));   // (0 + 0.75 + 1.5) / 3, exactly
    CHECK(sameBits(out.rgba[3], 1.0f));
}

// THE PREMULTIPLICATION QUESTION, HEAD ON.
//
// `[BIN]` mod98 divides the fetched rgb by `max(alpha, 1e-3)` immediately after
// every backdrop sample (`%387 = fmax(%386, 0xH1419)`, `%391 = fdiv %390,
// %389`). A divide by alpha AFTER the filter is only meaningful if what came
// out of the filter was premultiplied, so the target's backdrop is
// premultiplied and its levels are averages of premultiplied texels.
//
// It is also the only correct reduction on its own terms, which is what this
// test measures: across a footprint where alpha VARIES, the two conventions
// disagree by a factor of two, and only the colour shows it.
TEST_CASE(mip_premultiplied_average) {
    // Half opaque red, half fully transparent. Premultiplied, the transparent
    // texel is (0,0,0,0) and contributes nothing at all.
    const std::vector<float> half = {
        1.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
    };
    MipLevel out = reduceMipLevel(makeLevel(2, 1, half));
    CHECK(sameBits(out.rgba[0], 0.5f));
    CHECK(sameBits(out.rgba[1], 0.0f));
    CHECK(sameBits(out.rgba[2], 0.0f));
    CHECK(sameBits(out.rgba[3], 0.5f));

    // And now the part that the alpha check cannot see. Un-premultiplied, the
    // surviving colour is FULL red at half coverage:
    //
    //     right   (1, 0, 0) at alpha 0.5   -- 0.5 / 0.5
    //     wrong   (0.5, 0, 0) at alpha 0.5 -- what averaging STRAIGHT colour
    //                                         against a transparent black texel
    //                                         gives, i.e. half as bright
    //
    // That is the same failure the `IconRenderer` accumulator had: right alpha,
    // colour too dark by exactly the factor the un-multiply would have removed.
    float straight[3];
    const float texel[4] = {out.rgba[0], out.rgba[1], out.rgba[2], out.rgba[3]};
    unpremultiplyBackdrop(texel, straight);
    CHECK(sameBits(straight[0], 1.0f));
    CHECK(!sameBits(straight[0], 0.5f));
    CHECK(sameBits(straight[1], 0.0f));

    // A stronger version: two DIFFERENT colours at two different alphas, where
    // averaging straight colour gets the hue wrong and not merely the level.
    // Opaque red and half-covered green.
    const std::vector<float> mixed = {
        1.0f, 0.0f, 0.0f, 1.0f,    // straight (1,0,0), a = 1
        0.0f, 0.5f, 0.0f, 0.5f,    // straight (0,1,0), a = 0.5
    };
    MipLevel two = reduceMipLevel(makeLevel(2, 1, mixed));
    CHECK(sameBits(two.rgba[0], 0.5f));
    CHECK(sameBits(two.rgba[1], 0.25f));
    CHECK(sameBits(two.rgba[3], 0.75f));

    float hue[3];
    const float t2[4] = {two.rgba[0], two.rgba[1], two.rgba[2], two.rgba[3]};
    unpremultiplyBackdrop(t2, hue);
    // Alpha-weighted, so red carries twice green's vote: (2/3, 1/3, 0).
    CHECK(near(hue[0], 2.0f / 3.0f, 1e-6f));
    CHECK(near(hue[1], 1.0f / 3.0f, 1e-6f));
    // Averaging straight colour would have given (0.5, 0.5, 0) -- an even split
    // between a texel that covers the pixel and one that half covers it.
    CHECK(!near(hue[0], 0.5f, 1e-3f));

    // The un-multiply's own guard: a fully transparent texel divides by the
    // 1e-3 floor rather than by zero, so the result is finite.
    const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float nothing[3];
    unpremultiplyBackdrop(clear, nothing);
    for (int c = 0; c < 3; ++c) {
        CHECK(!std::isnan(nothing[c]));
        CHECK(sameBits(nothing[c], 0.0f));
    }
}

// A fractional LOD lies between the two levels, and an integral one is the
// level itself with nothing leaking in from its neighbour.
TEST_CASE(mip_fractional_lod) {
    // 2x1: level 0 is [0, 1], level 1 is [0.5]. Sampled at the CENTRE of texel
    // 0 the two levels disagree by exactly 0.5, so the interpolation is
    // readable at a glance and exact in binary.
    const std::vector<float> rgba = {
        0.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
    };
    MipPyramid p = MipPyramid::build(rgba.data(), 2, 1);
    REQUIRE(p.levelCount() == 2);

    float got[4];
    p.sample(0.0f, 0.25f, 0.5f, got);
    CHECK(sameBits(got[0], 0.0f));       // level 0, texel 0
    p.sample(1.0f, 0.25f, 0.5f, got);
    CHECK(sameBits(got[0], 0.5f));       // level 1, the mean
    p.sample(0.5f, 0.25f, 0.5f, got);
    CHECK(sameBits(got[0], 0.25f));      // exactly between
    p.sample(0.25f, 0.25f, 0.5f, got);
    CHECK(sameBits(got[0], 0.125f));
    p.sample(0.75f, 0.25f, 0.5f, got);
    CHECK(sameBits(got[0], 0.375f));

    // An integral LOD equals that level's bilinear sample bit for bit.
    for (std::size_t li = 0; li < p.levelCount(); ++li) {
        float direct[4], viaLod[4];
        p.sampleLevel(li, 0.25f, 0.5f, direct);
        p.sample(static_cast<float>(li), 0.25f, 0.5f, viaLod);
        for (int c = 0; c < 4; ++c) CHECK(sameBits(direct[c], viaLod[c]));
    }

    // The LOD is clamped into the chain at both ends, so a negative LOD is
    // level 0 and a LOD past the top is the 1x1 level -- not a read off the end
    // of the level table.
    float low[4], high[4], base[4], top[4];
    p.sample(-3.0f, 0.25f, 0.5f, low);
    p.sampleLevel(0, 0.25f, 0.5f, base);
    p.sample(50.0f, 0.25f, 0.5f, high);
    p.sampleLevel(p.levelCount() - 1, 0.25f, 0.5f, top);
    for (int c = 0; c < 4; ++c) {
        CHECK(sameBits(low[c], base[c]));
        CHECK(sameBits(high[c], top[c]));
    }
}

// The bilinear inside one level: half-texel centres and clamp at the edge.
//
// `[OBS]` The target's sampler descriptor word was not decoded, so this is OUR
// filter -- what is pinned here is that the two sides of the differential run
// the same one, not that Apple runs it.
TEST_CASE(mip_sample_level_bilinear) {
    const std::vector<float> rgba = {
        0.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
    };
    MipPyramid p = MipPyramid::build(rgba.data(), 2, 1);
    float got[4];

    // Texel centres are at u = 0.25 and 0.75.
    p.sampleLevel(0, 0.25f, 0.5f, got);
    CHECK(sameBits(got[0], 0.0f));
    p.sampleLevel(0, 0.75f, 0.5f, got);
    CHECK(sameBits(got[0], 1.0f));
    p.sampleLevel(0, 0.5f, 0.5f, got);
    CHECK(sameBits(got[0], 0.5f));

    // Outside the outermost centres the edge texel is held, not extrapolated
    // and not wrapped -- u = 0 must not read texel 1.
    p.sampleLevel(0, 0.0f, 0.5f, got);
    CHECK(sameBits(got[0], 0.0f));
    p.sampleLevel(0, 1.0f, 0.5f, got);
    CHECK(sameBits(got[0], 1.0f));

    // A level index past the end answers with zero rather than reading memory.
    p.sampleLevel(99, 0.5f, 0.5f, got);
    for (int c = 0; c < 4; ++c) CHECK(sameBits(got[c], 0.0f));
}

// An odd dimension end to end, through `build`, with the answer computed by
// hand from the kernel. 3x1 of a ramp reduces to the plain mean of three; a
// two-tap kernel would have produced the mean of the first two, 0.25.
TEST_CASE(mip_odd_dimension_end_to_end) {
    const std::vector<float> rgba = {
        0.0f, 0.0f, 0.0f, 1.0f,
        0.5f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
    };
    MipPyramid p = MipPyramid::build(rgba.data(), 3, 1);
    REQUIRE(p.levelCount() == 2);
    CHECK_EQ(p.level(1).width, 1u);
    CHECK(sameBits(p.level(1).rgba[0], 0.5f));
    CHECK(!sameBits(p.level(1).rgba[0], 0.25f));   // what dropping texel 2 gives

    // 5 wide, two destination texels, both computed from the sliding weights:
    //   dst0 = 0.4*s0 + 0.4*s1 + 0.2*s2
    //   dst1 = 0.2*s2 + 0.4*s3 + 0.4*s4
    const std::vector<float> five = {
        0.0f, 0, 0, 1,   1.0f, 0, 0, 1,   0.0f, 0, 0, 1,
        1.0f, 0, 0, 1,   0.0f, 0, 0, 1,
    };
    MipLevel out = reduceMipLevel(makeLevel(5, 1, five));
    CHECK_EQ(out.width, 2u);
    CHECK(near(out.rgba[0], 0.4f, 1e-6f));
    CHECK(near(out.rgba[4], 0.4f, 1e-6f));

    // An empty image has no pyramid, and asking for one is not a crash.
    MipPyramid none = MipPyramid::build(nullptr, 4, 4);
    CHECK(none.empty());
    CHECK_EQ(none.levelCount(), std::size_t{0});
    float got[4];
    none.sample(0.0f, 0.5f, 0.5f, got);
    for (int c = 0; c < 4; ++c) CHECK(sameBits(got[c], 0.0f));
}

// ---- GPU x CPU -----------------------------------------------------------

// The reduction, both chains, level by level. A power-of-two image where the
// kernel is the 2-tap box, and a 13x7 one where BOTH axes take the three-tap
// kernel at once.
TEST_CASE(mip_gpu_reduce_matches_oracle) {
    Device& d = gpu();
    if (!d.valid()) return;

    const std::uint32_t sizes[][2] = {{8, 8}, {13, 7}, {5, 3}, {16, 1}};
    for (const auto& size : sizes) {
        const std::vector<float> image = patterned(size[0], size[1]);
        MipPyramid cpu = MipPyramid::build(image.data(), size[0], size[1]);
        auto onGpu = MipPyramid::buildOnGpu(d, image.data(), size[0], size[1]);
        if (!onGpu) {
            std::printf("  FAIL buildOnGpu %ux%u: %s\n", size[0], size[1],
                        onGpu.error().c_str());
            ++ictest::failures();
            continue;
        }
        REQUIRE(onGpu->levelCount() == cpu.levelCount());
        for (std::size_t li = 0; li < cpu.levelCount(); ++li) {
            const MipLevel& a = cpu.level(li);
            const MipLevel& b = onGpu->level(li);
            CHECK_EQ(a.width, b.width);
            CHECK_EQ(a.height, b.height);
            REQUIRE(a.rgba.size() == b.rgba.size());
            for (std::size_t i = 0; i < a.rgba.size(); ++i) {
                // The kernel is a multiply and an add, and the GPU may keep
                // more intermediate precision than the host does. 2^-20 is the
                // allowance, and it is far below the smallest difference any
                // wrong tap or weight would produce -- the closest wrong answer
                // in the odd kernel is off by 1/5 of a texel's value.
                if (!near(a.rgba[i], b.rgba[i], 1.0f / 1048576.0f)) {
                    std::printf("  FAIL %ux%u level %zu slot %zu: cpu %g gpu %g\n", size[0],
                                size[1], li, i, static_cast<double>(a.rgba[i]),
                                static_cast<double>(b.rgba[i]));
                    ++ictest::failures();
                }
            }
        }
    }
}

// A constant image is required to survive the GPU chain BIT FOR BIT, not to a
// tolerance -- the difference form makes that an identity on either side, and
// a tolerance here would hide the one thing this claim is about.
TEST_CASE(mip_gpu_reduce_constant_is_exact) {
    Device& d = gpu();
    if (!d.valid()) return;

    const float colour[4] = {0.3f, 0.7f, 0.1f, 0.9f};
    std::vector<float> flat(13 * 7 * 4);
    for (std::size_t t = 0; t + 3 < flat.size(); t += 4) {
        for (int c = 0; c < 4; ++c) flat[t + c] = colour[c];
    }
    auto onGpu = MipPyramid::buildOnGpu(d, flat.data(), 13, 7);
    REQUIRE(onGpu.has_value());
    for (std::size_t li = 0; li < onGpu->levelCount(); ++li) {
        const MipLevel& l = onGpu->level(li);
        for (std::size_t t = 0; t + 3 < l.rgba.size(); t += 4) {
            for (int c = 0; c < 4; ++c) CHECK(sameBits(l.rgba[t + c], colour[c]));
        }
    }
}

// The LOD formula, bit for bit. This is a transcription, so a tolerance would
// defeat it: the half narrowing is the whole reason the function is written the
// way it is, and an implementation that skipped it would land within a
// thousandth of the right answer everywhere and still be the wrong function.
//
// It also pins the driver's `packHalf2x16` to round-to-nearest-even, which is
// what the CPU side does. If that ever disagrees on some adapter, this is the
// test that says so instead of a glass layer sampling the wrong mip.
TEST_CASE(mip_gpu_lod_matches_oracle) {
    Device& d = gpu();
    if (!d.valid()) return;

    std::vector<std::array<float, 3>> queries;
    // The edges first: the floor, a negative radius, and the cap on both sides.
    queries.push_back({0.0f, 0.0f, 100.0f});
    queries.push_back({-5.0f, 0.0f, 100.0f});
    queries.push_back({1e-6f, 0.0f, 100.0f});
    queries.push_back({1024.0f, 0.0f, 3.0f});
    queries.push_back({1024.0f, 8.0f, 3.0f});
    queries.push_back({0.0f, 0.0f, -20.0f});
    // Then powers of two, where log2 is exact, and a sweep where it is not.
    for (int e = -8; e <= 12; ++e) {
        queries.push_back({std::ldexp(1.0f, e), 0.0f, 100.0f});
    }
    for (int i = 1; i <= 64; ++i) {
        const float r = static_cast<float>(i) * 0.37f;
        queries.push_back({r, static_cast<float>(i % 5) * 0.5f, 100.0f});
    }

    auto onGpu = backdropLodOnGpu(d, queries);
    if (!onGpu) {
        std::printf("  FAIL lod on gpu: %s\n", onGpu.error().c_str());
        ++ictest::failures();
        return;
    }
    REQUIRE(onGpu->size() == queries.size());
    for (std::size_t i = 0; i < queries.size(); ++i) {
        const float want = backdropLod(queries[i][0], queries[i][1], queries[i][2]);
        if (!sameBits(want, (*onGpu)[i])) {
            std::printf("  FAIL lod r=%g bias=%g cap=%g: cpu %g gpu %g\n",
                        static_cast<double>(queries[i][0]), static_cast<double>(queries[i][1]),
                        static_cast<double>(queries[i][2]), static_cast<double>(want),
                        static_cast<double>((*onGpu)[i]));
            ++ictest::failures();
        }
    }
}

// The trilinear fetch, GPU against oracle, including the fractional LODs and
// both ends of the clamp.
TEST_CASE(mip_gpu_sample_matches_oracle) {
    Device& d = gpu();
    if (!d.valid()) return;

    const std::vector<float> image = patterned(13, 7);
    MipPyramid p = MipPyramid::build(image.data(), 13, 7);
    REQUIRE(!p.empty());

    std::vector<std::array<float, 3>> queries;
    for (int yi = 0; yi <= 6; ++yi) {
        for (int xi = 0; xi <= 6; ++xi) {
            const float u = static_cast<float>(xi) / 6.0f;
            const float v = static_cast<float>(yi) / 6.0f;
            for (float lod : {-1.0f, 0.0f, 0.5f, 1.0f, 2.25f, 3.0f, 9.0f}) {
                queries.push_back({u, v, lod});
            }
        }
    }

    auto onGpu = sampleOnGpu(d, p, queries);
    if (!onGpu) {
        std::printf("  FAIL sample on gpu: %s\n", onGpu.error().c_str());
        ++ictest::failures();
        return;
    }
    REQUIRE(onGpu->size() == queries.size());
    int reported = 0;
    for (std::size_t i = 0; i < queries.size(); ++i) {
        float want[4];
        p.sample(queries[i][2], queries[i][0], queries[i][1], want);
        for (int c = 0; c < 4; ++c) {
            if (!near(want[c], (*onGpu)[i][c], 1.0f / 1048576.0f) && reported < 8) {
                std::printf("  FAIL sample u=%g v=%g lod=%g ch %d: cpu %g gpu %g\n",
                            static_cast<double>(queries[i][0]),
                            static_cast<double>(queries[i][1]),
                            static_cast<double>(queries[i][2]), c,
                            static_cast<double>(want[c]),
                            static_cast<double>((*onGpu)[i][c]));
                ++ictest::failures();
                ++reported;
            }
        }
    }
}

// The chain a real canvas produces, at the size `IconRenderer` actually
// renders. Eleven levels, the last one 1x1, and the alpha of the top level is
// the mean coverage of the whole image -- which is a closed form and therefore
// an oracle rather than a self-comparison.
TEST_CASE(mip_canvas_sized_chain) {
    const std::uint32_t n = 512;
    std::vector<float> image(static_cast<std::size_t>(n) * n * 4, 0.0f);
    // The left half opaque white, the right half clear. Premultiplied.
    for (std::uint32_t y = 0; y < n; ++y) {
        for (std::uint32_t x = 0; x < n / 2; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * n + x) * 4;
            for (int c = 0; c < 4; ++c) image[at + c] = 1.0f;
        }
    }
    MipPyramid p = MipPyramid::build(image.data(), n, n);
    REQUIRE(p.levelCount() == 10);
    CHECK_EQ(p.level(9).width, 1u);
    CHECK_EQ(p.level(9).height, 1u);
    // Half the canvas is covered, so the 1x1 level is alpha 0.5 exactly -- and
    // premultiplied white at alpha 0.5, which un-multiplies back to full white.
    CHECK(sameBits(p.level(9).rgba[3], 0.5f));
    CHECK(sameBits(p.level(9).rgba[0], 0.5f));
    float straight[3];
    const float texel[4] = {p.level(9).rgba[0], p.level(9).rgba[1], p.level(9).rgba[2],
                            p.level(9).rgba[3]};
    unpremultiplyBackdrop(texel, straight);
    for (int c = 0; c < 3; ++c) CHECK(sameBits(straight[c], 1.0f));

    // And the level a radius picks is inside the chain. A blur radius of 16
    // canvas pixels with no bias is LOD 4, which is the 32x32 level.
    const float lod = backdropLod(16.0f, 0.0f, 100.0f);
    CHECK(sameBits(lod, 4.0f));
    CHECK_EQ(p.level(static_cast<std::size_t>(lod)).width, 32u);
}
