// The colour conversion, held to the constants it was transcribed from.
//
// What is pinned here is chosen the way `test_auto_gradient.cpp` chooses: what
// would a plausible TEXTBOOK version have got wrong? Three things. It would have
// clamped; it would have used the chromaticity-derived D65 matrix, whose third
// column is zero; and it would have re-encoded a gamma-2.2 grey onto the sRGB
// curve. The target does none of the three (`ColorSpace.h`), and each has a case
// below that the textbook version fails.
//
// The expected numbers are re-derived HERE, in `double`, from the decimal
// reading of the same constants -- not copied out of the function under test.
#include "check.h"
#include "Source/RenderBox/AutomaticGradient.h"
#include "Source/RenderBox/ColorSpace.h"

#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <vector>

using namespace rb;

namespace {

// `[BIN]` `RenderBox 0x15E7C0`.. as decimals, ROW by row: out = M * in.
const double kP3ToSrgb[3][3] = {
    {1.22500253, -0.224975437, -2.72393227e-05},
    {-0.0420922078, 1.04207087, 2.14669853e-05},
    {-0.0196249187, -0.0786099434, 1.09823489},
};
// `[BIN]` `RenderBox 0x15E7F0`..
const double kSrgbToP3[3][3] = {
    {0.8224262, 0.177556992, 1.68979168e-05},
    {0.0332198255, 0.966798306, -1.80695206e-05},
    {0.017074177, 0.0723747835, 0.910551071},
};

double decode(double v) {
    const double a = std::fabs(v);
    const double m = a <= 0.04045 ? a / 12.92 : std::pow((a + 0.055) / 1.055, 2.4);
    return v < 0 ? -m : m;
}

double encode(double v) {
    const double a = std::fabs(v);
    // 0.41667 and not 1/2.4: the target's exponent is the float 0x3ED555C5.
    const double m = a <= 0.0031308 ? a * 12.92 : 1.055 * std::pow(a, 0.4166699946) - 0.055;
    return v < 0 ? -m : m;
}

void expected(const double (&m)[3][3], const double (&in)[3], double (&out)[3]) {
    const double lin[3] = {decode(in[0]), decode(in[1]), decode(in[2])};
    for (int r = 0; r < 3; ++r) {
        out[r] = encode(m[r][0] * lin[0] + m[r][1] * lin[1] + m[r][2] * lin[2]);
    }
}

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// The PNG's conversion (`toByte`, `Png.cpp`): clamp, then `v * 255 + 0.5`
// truncated.
int byteOf(double v) {
    if (!(v > 0.0)) return 0;
    if (v >= 1.0) return 255;
    return static_cast<int>(v * 255.0 + 0.5);
}

icf::Color colour(icf::ColorSpace space, double r, double g, double b, double a = 1.0) {
    icf::Color c;
    c.space = space;
    c.count = 4;
    c.components[0] = r;
    c.components[1] = g;
    c.components[2] = b;
    c.components[3] = a;
    return c;
}

icf::Color grey(icf::ColorSpace space, double l, double a = 1.0) {
    icf::Color c;
    c.space = space;
    c.count = 2;
    c.components[0] = l;
    c.components[1] = a;
    return c;
}

}  // namespace

// `[BIN]` `0x76CB4` / `0x76E10`: both run on |x| and put the sign back.
TEST_CASE(colour_space_transfer_is_odd_and_is_not_clamped) {
    for (float x : {0.001f, 0.02f, 0.04045f, 0.2f, 0.5f, 1.0f, 1.25f, 3.0f}) {
        CHECK(srgbDecode(-x) == -srgbDecode(x));
        CHECK(srgbEncode(-x) == -srgbEncode(x));
    }
    // Above one stays above one, in both directions: no clamp.
    CHECK(srgbDecode(1.25f) > 1.0f);
    CHECK(srgbEncode(1.25f) > 1.0f);
    CHECK(srgbDecode(0.0f) == 0.0f);
    CHECK(srgbEncode(0.0f) == 0.0f);
}

TEST_CASE(colour_space_transfer_matches_the_constants_as_read) {
    for (double x : {0.0, 0.003, 0.0031308, 0.01, 0.04045, 0.05, 0.192, 0.5, 0.75, 1.0, 1.1}) {
        CHECK(near(srgbDecode(static_cast<float>(x)), decode(x), 2e-6));
        CHECK(near(srgbEncode(static_cast<float>(x)), encode(x), 2e-6));
    }
    // The two knees are the two branches' meeting point, and the linear branch
    // is the one taken AT the knee (`fcmge knee, a`).
    CHECK(near(srgbDecode(0.04045f), 0.04045 / 12.92, 1e-8));
    CHECK(near(srgbEncode(0.0031308f), 0.0031308 * 12.92, 1e-7));
    // And they undo each other, to what a float holds.
    for (float x = -0.5f; x <= 1.5f; x += 0.0625f) {
        CHECK(near(srgbEncode(srgbDecode(x)), x, 2e-5));
    }
}

// `[BIN]` The matrix is the profile-derived one. Its rows still sum to one, so
// a neutral stays neutral -- which is what lets the canned system ramps and
// every grey ignore this whole file.
TEST_CASE(colour_space_neutrals_pass_through_the_matrix) {
    for (float v : {0.0f, 0.078f, 0.192f, 0.5f, 0.9607843f, 1.0f}) {
        float a[4] = {v, v, v, 0.5f};
        displayP3ToSrgb(a);
        float b[4] = {v, v, v, 0.5f};
        srgbToDisplayP3(b);
        for (int k = 0; k < 3; ++k) {
            CHECK(near(a[k], v, 1e-5));
            CHECK(near(b[k], v, 1e-5));
        }
        CHECK(a[3] == 0.5f);
        CHECK(b[3] == 0.5f);
    }
}

// THE THING A TEXTBOOK MATRIX WOULD HAVE GOT WRONG. The D65 chromaticity
// matrix has an exactly zero third column in its first two rows, so P3 blue
// would come out with r == g == 0. The target's does not.
TEST_CASE(colour_space_the_matrix_is_the_targets_and_not_the_d65_one) {
    float blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    displayP3ToSrgb(blue);
    const double in[3] = {0, 0, 1};
    double want[3];
    expected(kP3ToSrgb, in, want);
    for (int k = 0; k < 3; ++k) CHECK(near(blue[k], want[k], 2e-6));
    CHECK(blue[0] < 0.0f);                       // -2.72e-05 linear, x 12.92
    CHECK(near(blue[0], -2.72393227e-05 * 12.92, 1e-7));
    CHECK(blue[1] > 0.0f);
    CHECK(near(blue[1], 2.14669853e-05 * 12.92, 1e-7));
    CHECK(blue[2] > 1.04f);                      // out of the sRGB gamut, and kept
}

TEST_CASE(colour_space_out_of_gamut_components_are_kept) {
    float green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    displayP3ToSrgb(green);
    CHECK(green[0] < -0.5f);    // encode(-0.224975)
    CHECK(green[1] > 1.0f);     // encode(1.04207)
    CHECK(green[2] < -0.3f);    // encode(-0.0786)
    // And the trip back recovers the P3 colour, sign and all -- which a clamp
    // anywhere on the way would have made impossible.
    // To 1e-4 and not tighter: a `float` residue of a few 1e-6 in linear light
    // lands on the 12.92 slope of the curve's foot.
    srgbToDisplayP3(green);
    CHECK(near(green[0], 0.0, 1e-4));
    CHECK(near(green[1], 1.0, 1e-4));
    CHECK(near(green[2], 0.0, 1e-4));
}

TEST_CASE(colour_space_display_p3_to_srgb_matches_the_rederived_values) {
    const double samples[][3] = {
        {0.315, 0.678, 0.464},                    // SF Symbols `Color-7`
        {166 / 255.0, 212 / 255.0, 1.0},          // #A6D4FF
        {0.9, 0.2, 0.3},  {0.1, 0.3, 0.9},        // `test_viewport_render`'s ramp
        {0.695, 0.153, 0.477},                    // Apollo's pink
        {1.0, 0.0, 0.0},  {0.0, 0.5058, 1.0},
    };
    for (const auto& s : samples) {
        float got[4] = {static_cast<float>(s[0]), static_cast<float>(s[1]),
                        static_cast<float>(s[2]), 1.0f};
        displayP3ToSrgb(got);
        double want[3];
        expected(kP3ToSrgb, s, want);
        for (int k = 0; k < 3; ++k) CHECK(near(got[k], want[k], 4e-6));

        float back[4] = {got[0], got[1], got[2], 1.0f};
        srgbToDisplayP3(back);
        for (int k = 0; k < 3; ++k) CHECK(near(back[k], s[k], 1e-4));
    }
    // One of them in the units a person compares pictures in.
    float c7[4] = {0.315f, 0.678f, 0.464f, 0.78f};
    displayP3ToSrgb(c7);
    CHECK(near(c7[0] * 255.0, 16.05, 0.02));
    CHECK(near(c7[1] * 255.0, 175.51, 0.02));
    CHECK(near(c7[2] * 255.0, 113.19, 0.02));
    CHECK(c7[3] == 0.78f);
}

TEST_CASE(colour_space_to_working_covers_every_document_space) {
    // sRGB and extended sRGB ARE the working space: not one bit moves, in or
    // out of range.
    for (auto space : {icf::ColorSpace::SRGB, icf::ColorSpace::ExtendedSRGB}) {
        float rgba[4];
        toWorking(colour(space, 1.2, -0.1, 0.3, 0.4), rgba);
        CHECK(rgba[0] == 1.2f);
        CHECK(rgba[1] == -0.1f);
        CHECK(rgba[2] == 0.3f);
        CHECK(rgba[3] == 0.4f);
    }
    // A grey is widened and nothing else.
    for (auto space : {icf::ColorSpace::Gray, icf::ColorSpace::ExtendedGray}) {
        float rgba[4];
        toWorking(grey(space, 0.192, 0.6), rgba);
        CHECK(rgba[0] == 0.192f);
        CHECK(rgba[1] == 0.192f);
        CHECK(rgba[2] == 0.192f);
        CHECK(rgba[3] == 0.6f);
    }
    // Display P3 is the one that converts, and the alpha is not a component.
    float p3[4];
    toWorking(colour(icf::ColorSpace::DisplayP3, 0.315, 0.678, 0.464, 0.78), p3);
    float direct[4] = {0.315f, 0.678f, 0.464f, 0.78f};
    displayP3ToSrgb(direct);
    for (int k = 0; k < 4; ++k) CHECK(p3[k] == direct[k]);

    // The overload on four floats agrees with the one on the document value.
    float again[4] = {0.315f, 0.678f, 0.464f, 0.78f};
    toWorking(icf::ColorSpace::DisplayP3, again);
    for (int k = 0; k < 4; ++k) CHECK(again[k] == direct[k]);
}

// `[OBS]` THE GREY IS NOT RE-ENCODED, and Apple's own bitmap is the witness:
// the Icon Composer icon's background runs `gray 0.192 -> 0.078` and the baked
// 8-bit rendition reads 49 at the top and 20 at the bottom. A reader that took
// "GenericGrayGamma2_2" at its word would have produced 45 and 12.
TEST_CASE(colour_space_a_grey_keeps_its_number_as_apples_bitmap_does) {
    float top[4];
    float bottom[4];
    toWorking(grey(icf::ColorSpace::Gray, 0.192), top);
    toWorking(grey(icf::ColorSpace::Gray, 0.078), bottom);
    CHECK_EQ(byteOf(top[0]), 49);
    CHECK_EQ(byteOf(bottom[0]), 20);

    // What the re-encode would have given, so the difference is on the page.
    CHECK_EQ(byteOf(encode(std::pow(0.192, 2.2))), 45);
    CHECK_EQ(byteOf(encode(std::pow(0.078, 2.2))), 12);
}

// What `IconColor.init(_: CGColor)` leaves behind: Display P3 components.
TEST_CASE(colour_space_to_display_p3_is_what_an_icon_colour_holds) {
    float red[4];
    toDisplayP3(colour(icf::ColorSpace::SRGB, 1, 0, 0), red);
    const double in[3] = {1, 0, 0};
    double want[3];
    expected(kSrgbToP3, in, want);
    for (int k = 0; k < 3; ++k) CHECK(near(red[k], want[k], 4e-6));
    CHECK(near(red[0], 0.9175, 1e-3));   // the familiar sRGB red in Display P3
    CHECK(near(red[1], 0.2003, 1e-3));
    CHECK(near(red[2], 0.1387, 1e-3));

    // A P3 colour is already there; a grey is replicated.
    float same[4];
    toDisplayP3(colour(icf::ColorSpace::DisplayP3, 0.2, 0.4, 0.6, 0.5), same);
    CHECK(same[0] == 0.2f);
    CHECK(same[1] == 0.4f);
    CHECK(same[2] == 0.6f);
    CHECK(same[3] == 0.5f);
    float g[4];
    toDisplayP3(grey(icf::ColorSpace::ExtendedGray, 0.078, 1.0), g);
    CHECK(g[0] == 0.078f);
    CHECK(g[1] == 0.078f);
    CHECK(g[2] == 0.078f);

    // `[INF]` The identity `toWorking` takes for an sRGB colour is the
    // target's sRGB -> P3 -> sRGB, and in `float` that round trip comes back
    // within 1e-4 -- a fortieth of an 8-bit level, at worst, on a zero
    // component of a saturated colour.
    for (const auto& c : {colour(icf::ColorSpace::SRGB, 0.1, 0.2, 0.4),
                          colour(icf::ColorSpace::SRGB, 0.9, 0.2, 0.2),
                          colour(icf::ColorSpace::SRGB, 0.0, 1.0, 0.0)}) {
        float trip[4];
        toDisplayP3(c, trip);
        displayP3ToSrgb(trip);
        for (int k = 0; k < 3; ++k) CHECK(near(trip[k], c.components[k], 1e-4));
    }
}

// The rule is untouched; only where it runs and where its answer lands moved.
TEST_CASE(colour_space_automatic_gradient_derives_on_display_p3) {
    // A Display P3 base: the plain rule on the document's numbers, then each
    // stop converted.
    const icf::Color p3 = colour(icf::ColorSpace::DisplayP3, 0.0, 0.50588, 1.0, 0.9);
    const std::vector<RampStop> plain = automaticGradient(p3);
    const std::vector<RampStop> working = automaticGradientInWorkingSpace(p3);
    REQUIRE(plain.size() == 2);
    REQUIRE(working.size() == 2);
    for (std::size_t i = 0; i < 2; ++i) {
        float want[4];
        for (int k = 0; k < 4; ++k) want[k] = static_cast<float>(plain[i].rgba[k]);
        displayP3ToSrgb(want);
        for (int k = 0; k < 3; ++k) CHECK(working[i].rgba[k] == static_cast<double>(want[k]));
        CHECK(working[i].rgba[3] == plain[i].rgba[3]);   // the alpha, untouched
        CHECK(working[i].location == plain[i].location);
    }

    // A neutral base in ANY space: P3 and sRGB agree on a grey, so the wrapper
    // and the plain rule agree too.
    const icf::Color g = colour(icf::ColorSpace::SRGB, 0.4, 0.4, 0.4);
    const std::vector<RampStop> gPlain = automaticGradient(g);
    const std::vector<RampStop> gWorking = automaticGradientInWorkingSpace(g);
    for (std::size_t i = 0; i < 2; ++i) {
        for (int k = 0; k < 3; ++k) CHECK(near(gWorking[i].rgba[k], gPlain[i].rgba[k], 1e-4));
    }

    // A SATURATED sRGB base is where the two part: the rule sees the colour's
    // P3 numbers. The base stop comes back to where it started; the derived one
    // is not the one the sRGB numbers would have given.
    const icf::Color red = colour(icf::ColorSpace::SRGB, 0.8, 0.2, 0.2);
    const std::vector<RampStop> rPlain = automaticGradient(red);
    const std::vector<RampStop> rWorking = automaticGradientInWorkingSpace(red);
    REQUIRE(rPlain.size() == 2);
    REQUIRE(rWorking.size() == 2);
    double baseDrift = 0.0;
    double derivedDrift = 0.0;
    for (std::size_t i = 0; i < 2; ++i) {
        CHECK(rWorking[i].location == rPlain[i].location);
        // The base stop is the one that still carries the document's numbers.
        const bool isBase = near(rPlain[i].rgba[0], 0.8, 1e-12) &&
                            near(rPlain[i].rgba[1], 0.2, 1e-12);
        for (int k = 0; k < 3; ++k) {
            const double d = std::fabs(rWorking[i].rgba[k] - rPlain[i].rgba[k]);
            if (isBase) {
                baseDrift = std::fmax(baseDrift, d);
            } else {
                derivedDrift = std::fmax(derivedDrift, d);
            }
        }
    }
    CHECK(baseDrift < 1e-4);
    CHECK(derivedDrift > 1e-3);
}

// `[OBS]` SF Symbols 8, measured against the catalog's own 8-bit rendition and
// recorded in `scripts/car_to_icon.py`. Two pixels, and they pull opposite
// ways -- which is what makes them a check on the RULE and not on a constant:
//
//   - where the art shows alone, Apple's pixel is the art's raw hex: an
//     untagged SVG colour is sRGB unless the document says otherwise;
//   - where `Color-7` -- `display-p3:0.315,0.678,0.464,0.78` -- covers the art,
//     Apple's pixel is the CONVERTED colour over it.
//
// `[INF]` The stop under `Color-7` is taken to be `#60ECB7` because it is the
// one stop of `5.a.back.1.svg` that reproduces both recorded predictions
// (34,189,129 converted and 84,187,133 unconverted); the pixel's own
// coordinates were not recorded.
TEST_CASE(colour_space_agrees_with_the_sf_symbols_catalog_pixels) {
    const int apple[3] = {36, 191, 130};
    const double under[3] = {0x60 / 255.0, 0xEC / 255.0, 0xB7 / 255.0};
    const double alpha = 0.78;

    float converted[4];
    toWorking(colour(icf::ColorSpace::DisplayP3, 0.315, 0.678, 0.464, alpha), converted);
    const double raw[3] = {0.315, 0.678, 0.464};

    const int wantConverted[3] = {34, 189, 129};
    const int wantRaw[3] = {84, 187, 133};
    for (int k = 0; k < 3; ++k) {
        const int withConversion = byteOf(converted[k] * alpha + under[k] * (1.0 - alpha));
        const int without = byteOf(raw[k] * alpha + under[k] * (1.0 - alpha));
        CHECK_EQ(withConversion, wantConverted[k]);
        CHECK_EQ(without, wantRaw[k]);
        CHECK(std::abs(withConversion - apple[k]) <= 2);
    }
    // Unconverted, the red channel alone is 48 levels out.
    CHECK(byteOf(raw[0] * alpha + under[0] * (1.0 - alpha)) - apple[0] >= 40);

    // The heart, art alone: #73AEE8 drawn as it is written lands on Apple's
    // 115,175,233; read as Display P3 it would have landed on 95,176,237.
    const int heartApple[3] = {115, 175, 233};
    const int heartArt[3] = {115, 174, 232};
    float asP3[4] = {115 / 255.0f, 174 / 255.0f, 232 / 255.0f, 1.0f};
    displayP3ToSrgb(asP3);
    const int wantAsP3[3] = {95, 176, 237};
    int errorAsWritten = 0;
    int errorAsP3 = 0;
    for (int k = 0; k < 3; ++k) {
        CHECK_EQ(byteOf(asP3[k]), wantAsP3[k]);
        errorAsWritten += std::abs(heartArt[k] - heartApple[k]);
        errorAsP3 += std::abs(byteOf(asP3[k]) - heartApple[k]);
    }
    CHECK(errorAsWritten <= 2);
    CHECK(errorAsP3 >= 20);
}
