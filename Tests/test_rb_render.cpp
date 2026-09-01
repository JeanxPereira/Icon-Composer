#include "check.h"
#include "Source/CoreSVG/Path.h"
#include "Source/RenderBox/CoveragePass.h"
#include "Source/RenderBox/Image.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace rb;
using icf::svg::parsePath;

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

constexpr std::uint32_t kSize = 32;

PathGlobals identityGlobals() {
    PathGlobals g;
    g.m0[0] = 1.0f; g.m0[1] = 0.0f;
    g.m1[0] = 0.0f; g.m1[1] = 1.0f;
    g.m2[0] = 0.0f; g.m2[1] = 0.0f;
    g.twoOverSize[0] = 2.0f / static_cast<float>(kSize);
    g.twoOverSize[1] = 2.0f / static_cast<float>(kSize);
    // Everything to the right of an edge must be reachable, so the bound is the
    // image's own right-hand side.
    g.urx = static_cast<float>(kSize);
    return g;
}

// Renders a path and hands back the coverage image, row major.
std::vector<Texel> render(Device& d, const char* pathData, int subdivisions = 1) {
    auto parsed = parsePath(pathData);
    if (!parsed) return {};
    BuildOptions o;
    o.subdivisions = subdivisions;
    auto buffer = buildPathBuffer(*parsed, o);
    if (!buffer) {
        std::printf("  FAIL path buffer: %s\n", buffer.error().c_str());
        ++ictest::failures();
        return {};
    }
    auto image = Image::create(d, kSize, kSize);
    if (!image) {
        std::printf("  FAIL image: %s\n", image.error().c_str());
        ++ictest::failures();
        return {};
    }
    auto pass = CoveragePass::create(d);
    if (!pass) {
        std::printf("  FAIL pass: %s\n", pass.error().c_str());
        ++ictest::failures();
        return {};
    }
    auto drew = pass->draw(d, *image, *buffer, identityGlobals());
    if (!drew) {
        std::printf("  FAIL draw: %s\n", drew.error().c_str());
        ++ictest::failures();
        return {};
    }
    auto pixels = readBack(d, *image);
    if (!pixels) {
        std::printf("  FAIL readback: %s\n", pixels.error().c_str());
        ++ictest::failures();
        return {};
    }
    return *pixels;
}

float at(const std::vector<Texel>& p, std::uint32_t x, std::uint32_t y) {
    return p[static_cast<std::size_t>(y) * kSize + x].y;
}

// The overlap of one pixel with an axis-aligned rectangle, in closed form.
//
// THIS IS THE ORACLE, and it is not the target's -- it is geometry's. There is no
// pixel oracle from Apple for this project, and for a shape whose coverage has a
// closed form there does not need to be one: the exact answer is computable
// without reference to any shader, and a render that disagrees with it is wrong
// no matter how plausible it looks.
float exactRectCoverage(std::uint32_t px, std::uint32_t py, float x0, float y0, float x1,
                        float y1) {
    const float l = std::fmax(static_cast<float>(px), x0);
    const float r = std::fmin(static_cast<float>(px) + 1.0f, x1);
    const float b = std::fmax(static_cast<float>(py), y0);
    const float t = std::fmin(static_cast<float>(py) + 1.0f, y1);
    const float w = r > l ? r - l : 0.0f;
    const float h = t > b ? t - b : 0.0f;
    return w * h;
}

std::string rect(float x0, float y0, float x1, float y1) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "M%g %g L%g %g L%g %g L%g %g Z", x0, y0, x1, y0, x1, y1, x0,
                  y1);
    return buf;
}

// The whole-image disagreement against the closed form, in MAGNITUDE.
//
// `[BIN]` The coverage the target accumulates is SIGNED: each edge contributes
// `path_value * area`, and the sign is the winding direction. A rectangle wound
// one way fills to +1 and the same rectangle wound the other fills to -1. What
// turns that into an alpha is a RESOLVE stage applying the fill rule, and that
// stage is not decoded -- so this compares what has been built, which is the
// magnitude, and does not pretend to know the sign convention the resolve wants.
//
// `half` is what the target stores, so the allowance is one step of that grid at
// the magnitude coverage lives in -- 2^-11 over [0.5, 1).
void agreesWithRectangle(const std::vector<Texel>& p, float x0, float y0, float x1, float y1) {
    REQUIRE(p.size() == static_cast<std::size_t>(kSize) * kSize);
    int bad = 0;
    float worst = 0.0f;
    for (std::uint32_t y = 0; y < kSize; ++y) {
        for (std::uint32_t x = 0; x < kSize; ++x) {
            const float want = exactRectCoverage(x, y, x0, y0, x1, y1);
            const float got = std::fabs(at(p, x, y));
            const float gap = std::fabs(got - want);
            if (gap > worst) worst = gap;
            if (gap > 0.0025f) {
                if (bad < 4) {
                    std::printf("  FAIL (%u,%u) rendered %.6f, exact %.6f\n", x, y, got, want);
                }
                ++bad;
            }
        }
    }
    if (bad != 0) std::printf("  worst disagreement %.6f\n", worst);
    CHECK_EQ(bad, 0);
}

}  // namespace

// The first pixel this project has ever drawn, and it is checked against a
// closed form rather than against a picture somebody looked at.
TEST_CASE(a_pixel_aligned_rectangle_renders_its_exact_area) {
    Device& d = gpu();
    REQUIRE(d.valid());
    const auto pixels = render(d, rect(8.0f, 8.0f, 20.0f, 20.0f).c_str());
    REQUIRE(!pixels.empty());
    agreesWithRectangle(pixels, 8.0f, 8.0f, 20.0f, 20.0f);
    // and the inside really is full, not merely close to the oracle everywhere
    CHECK(std::fabs(at(pixels, 12, 12)) > 0.99f);
    CHECK(std::fabs(at(pixels, 2, 2)) < 0.01f);
}

// Off the pixel grid on every side: now every boundary pixel is a fraction, and
// the analytic coverage is what separates a correct renderer from one that
// merely fills the right pixels.
TEST_CASE(a_fractional_rectangle_renders_its_exact_area) {
    Device& d = gpu();
    REQUIRE(d.valid());
    const auto pixels = render(d, rect(6.25f, 9.5f, 21.75f, 18.125f).c_str());
    REQUIRE(!pixels.empty());
    agreesWithRectangle(pixels, 6.25f, 9.5f, 21.75f, 18.125f);
}

// A rectangle narrower than one pixel: both its edges land in the same column,
// and the coverage there is the width. Nothing about this works unless the
// signed contributions of the two edges actually cancel.
TEST_CASE(a_sub_pixel_rectangle_renders_as_a_fraction) {
    Device& d = gpu();
    REQUIRE(d.valid());
    const auto pixels = render(d, rect(10.25f, 5.0f, 10.75f, 25.0f).c_str());
    REQUIRE(!pixels.empty());
    agreesWithRectangle(pixels, 10.25f, 5.0f, 10.75f, 25.0f);
    CHECK(std::fabs(std::fabs(at(pixels, 10, 12)) - 0.5f) < 0.0025f);
}

// And the sign IS carried, whichever way it points: a rectangle and the same
// rectangle wound the other way must come back opposite. This is the test that
// keeps `agreesWithRectangle`'s absolute value honest -- without it, dropping
// `path_value` entirely would still pass every magnitude check above.
TEST_CASE(reversing_the_winding_reverses_the_sign) {
    Device& d = gpu();
    REQUIRE(d.valid());
    const auto forward = render(d, "M8 8 L20 8 L20 20 L8 20 Z");
    const auto reverse = render(d, "M8 8 L8 20 L20 20 L20 8 Z");
    REQUIRE(!forward.empty());
    REQUIRE(!reverse.empty());
    const float a = at(forward, 12, 12);
    const float b = at(reverse, 12, 12);
    CHECK(std::fabs(a) > 0.99f);
    CHECK(std::fabs(a + b) < 0.0025f);   // equal and opposite
    CHECK(a * b < 0.0f);
}

// The winding cancels: a rectangle drawn inside another, in the OPPOSITE
// direction, is a hole. Nothing in the fragment knows about holes -- the sign of
// `path_value` and the additive blend are the whole mechanism.
TEST_CASE(an_opposite_wound_rectangle_cuts_a_hole) {
    Device& d = gpu();
    REQUIRE(d.valid());
    const std::string outer = rect(6.0f, 6.0f, 24.0f, 24.0f);
    // Reversed: the corners in the other order.
    const std::string inner = "M12 12 L12 18 L18 18 L18 12 Z";
    const auto pixels = render(d, (outer + " " + inner).c_str());
    REQUIRE(!pixels.empty());
    CHECK(std::fabs(at(pixels, 8, 8)) > 0.99f);    // in the ring
    CHECK(std::fabs(at(pixels, 15, 15)) < 0.01f);  // in the hole
    CHECK(std::fabs(at(pixels, 2, 2)) < 0.01f);    // outside
}

// A shape drawn entirely outside the image leaves it clear -- the edges are
// still rasterised, and their contributions still have to sum to nothing.
TEST_CASE(a_shape_off_the_image_leaves_it_clear) {
    Device& d = gpu();
    REQUIRE(d.valid());
    const auto pixels = render(d, rect(40.0f, 40.0f, 50.0f, 50.0f).c_str());
    REQUIRE(!pixels.empty());
    for (std::uint32_t y = 0; y < kSize; ++y) {
        for (std::uint32_t x = 0; x < kSize; ++x) {
            REQUIRE(std::fabs(at(pixels, x, y)) < 0.0025f);
        }
    }
}

// The draw REFUSES a buffer whose header disagrees with its segments, and it has
// to be tested here as well as in the oracle: this is the call that would size a
// draw from the lie. It is the guard that a hundred-gigabyte allocation and a
// machine reboot bought, and the sweep found that nothing was exercising it on
// this side -- the third guard today that existed and was never fired.
TEST_CASE(the_draw_refuses_a_buffer_whose_header_lies) {
    Device& d = gpu();
    REQUIRE(d.valid());
    auto parsed = parsePath("M8 8 L20 8 L20 20 L8 20 Z");
    REQUIRE(parsed.has_value());
    BuildOptions o;
    o.subdivisions = 1;
    auto buffer = buildPathBuffer(*parsed, o);
    REQUIRE(buffer.has_value());

    auto image = Image::create(d, kSize, kSize);
    REQUIRE(image.has_value());
    auto pass = CoveragePass::create(d);
    REQUIRE(pass.has_value());

    // A valid buffer draws.
    REQUIRE(pass->draw(d, *image, *buffer, identityGlobals()).has_value());

    // The same corruption the sweep applies: the vertex total written as a float,
    // which reads back as a number in the billions.
    PathBuffer corrupt = std::move(*buffer);
    corrupt.entries.front().recip_n = static_cast<float>(corrupt.vertexCount());
    auto drew = pass->draw(d, *image, corrupt, identityGlobals());
    REQUIRE(!drew.has_value());
    CHECK(drew.error().find("header") != std::string::npos);
}

// `[BIN]` The target leaves `.x` at zero in every path fragment. A render that
// wrote something there would be writing to a channel the target reserves.
TEST_CASE(the_x_channel_of_the_coverage_target_stays_zero) {
    Device& d = gpu();
    REQUIRE(d.valid());
    const auto pixels = render(d, rect(8.0f, 8.0f, 20.0f, 20.0f).c_str());
    REQUIRE(!pixels.empty());
    for (const auto& t : pixels) REQUIRE(t.x == 0.0f);
}
