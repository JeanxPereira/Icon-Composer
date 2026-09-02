// The glass layer, joined and drawn.
//
// WHAT THIS GATE HAS TO CATCH, AND WHY EACH CHECK IS SHAPED THE WAY IT IS
// -----------------------------------------------------------------------
// Every stage of the icon's glass already has a gate: the material's eight
// fields and three formulas, the field generator against two closed forms, the
// displacement generator against AquaKit's independent CPU twin, and
// `displacementMap_v1` at 0 ULP GPU-against-CPU on all four tap modes. What had
// no gate is the JOIN, and a join fails in ways a stage gate cannot see.
//
// So the checks here are chosen against the failures that would still LOOK like
// a render:
//
//   1. "something changed" is not a test. A displacement of zero, a mask of
//      one, a sign flip and a scale of a hundred all change the picture. Every
//      end-to-end check below therefore names WHERE the backdrop must move,
//      where it must NOT, and -- for the sign -- which WAY.
//   2. Zero refraction must be the IDENTITY, bit for bit. `[ART]` It is also
//      the overwhelmingly common case: only 5 of the corpus's 271 groups carry
//      `refractivity` and only 2 of those a non-zero strength. A join that
//      blurred the backdrop by a quarter pixel on the other 269 would look
//      exactly like a render and be wrong 269 times.
//   3. The flattener is OUR code, so it is gated against a closed form and its
//      error is measured against the polygon sagitta -- the same argument
//      `test_rb_field.cpp` makes, for the same reason.
#include "check.h"
#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/IconComposerFoundation/Png.h"
#include "Source/RenderBox/GlassLayer.h"
#include "Source/RenderBox/IconRenderer.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

using namespace rb;

namespace {

namespace fs = std::filesystem;

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

// `[ART]` The corpus's two enabled `refractivity` entries, verbatim. Every
// end-to-end number here is driven by one of them rather than by a value chosen
// to make the arithmetic tidy.
constexpr double kRealStrength = -0.5269921875;   // CamilleScholtz/swmpc
constexpr double kRealDepth = 0.50046875;

PathGlobals identityGlobals() {
    PathGlobals g;
    g.m0[0] = 1.0f; g.m0[1] = 0.0f;
    g.m1[0] = 0.0f; g.m1[1] = 1.0f;
    g.m2[0] = 0.0f; g.m2[1] = 0.0f;
    return g;
}

// A circle as SVG cubics, in the one approximation everybody uses: the control
// arm is `kappa * r` and the quarter arc's radial error peaks at about 2.0e-4
// of the radius. That floor is part of the bound this test quotes, because it
// belongs to the AUTHOR's curve and not to our flattening of it.
constexpr double kKappa = 0.5522847498307936;
constexpr double kCubicCircleError = 2.8e-4;   // of r, the classic bound, rounded up

std::string circleSvg(double cx, double cy, double r, double side) {
    const double k = kKappa * r;
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
                  "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %g %g\">"
                  "<path fill=\"#ffffff\" d=\"M %.10f %.10f "
                  "C %.10f %.10f %.10f %.10f %.10f %.10f "
                  "C %.10f %.10f %.10f %.10f %.10f %.10f "
                  "C %.10f %.10f %.10f %.10f %.10f %.10f "
                  "C %.10f %.10f %.10f %.10f %.10f %.10f Z\"/></svg>",
                  side, side,
                  cx + r, cy,
                  cx + r, cy + k, cx + k, cy + r, cx, cy + r,
                  cx - k, cy + r, cx - r, cy + k, cx - r, cy,
                  cx - r, cy - k, cx - k, cy - r, cx, cy - r,
                  cx + k, cy - r, cx + r, cy - k, cx + r, cy);
    return std::string(buf);
}

// A bundle on disk, so `renderIcon` can be driven end to end on documents this
// test chose. The corpus has glass everywhere and a controlled backdrop
// nowhere, and a test that cannot isolate what it measures measures nothing.
class TempBundle {
public:
    explicit TempBundle(const std::string& document) {
        dir_ = fs::temp_directory_path() /
               ("ic-glass-" + std::to_string(std::hash<std::string>{}(document)));
        std::error_code ec;
        fs::remove_all(dir_, ec);
        fs::create_directories(dir_ / "Assets", ec);
        write(dir_ / "icon.json", document);

        // A 512-point square: centred on a 1024-point canvas it spans exactly
        // the middle half, so "inside", "in the band" and "outside" are all
        // arithmetic rather than eyeballing.
        write(dir_ / "Assets" / "square.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\">"
              "<path d=\"M0 0 L512 0 L512 512 L0 512 Z\" fill=\"#ffffff\"/></svg>");

        // THE BACKDROP, and it is a RAMP on purpose. A flat backdrop displaced
        // by any amount is the same flat backdrop: it would pass every check
        // below with the displacement zeroed. A monotone ramp turns "the
        // backdrop moved" into a number -- the red channel reads off WHERE the
        // sample came from.
        std::vector<float> px(static_cast<std::size_t>(kRamp) * kRamp * 4, 0.0f);
        for (std::uint32_t y = 0; y < kRamp; ++y) {
            for (std::uint32_t x = 0; x < kRamp; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * kRamp + x) * 4;
                px[i + 0] = static_cast<float>(x) / static_cast<float>(kRamp - 1);
                px[i + 1] = 0.25f;
                px[i + 2] = 0.75f;
                px[i + 3] = 1.0f;
            }
        }
        const std::vector<std::uint8_t> png = icf::encodePng(px, kRamp, kRamp);
        std::FILE* f = std::fopen((dir_ / "Assets" / "ramp.png").string().c_str(), "wb");
        if (f) {
            std::fwrite(png.data(), 1, png.size(), f);
            std::fclose(f);
        }
    }

    static constexpr std::uint32_t kRamp = 128;   // x 8 == the 1024-point canvas

    ~TempBundle() {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    const fs::path& path() const { return dir_; }

private:
    static void write(const fs::path& p, const std::string& text) {
        std::FILE* f = std::fopen(p.string().c_str(), "wb");
        if (!f) return;
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }
    fs::path dir_;
};

// A ramp behind, one glass layer in front. `art` names the glass layer's art so
// the raster gap can be exercised with the same document; `refractivity` is the
// group key verbatim, or empty for a group that carries none.
std::string rampAndGlass(const char* art, const std::string& refractivity, bool glass,
                         double opacity = 0.25) {
    std::string doc =
        "{\n  \"groups\" : [\n"
        "    { \"layers\" : [ { \"image-name\" : \"ramp.png\", \"name\" : \"bg\",\n"
        "        \"position\" : { \"scale\" : 8, \"translation-in-points\" : [0, 0] } } ] },\n"
        "    { ";
    if (!refractivity.empty()) doc += "\"refractivity\" : " + refractivity + ",\n      ";
    doc += "\"layers\" : [ { \"image-name\" : \"";
    doc += art;
    doc += "\", \"name\" : \"lens\",\n";
    doc += std::string("        \"glass\" : ") + (glass ? "true" : "false") + ",\n";
    doc += "        \"opacity\" : " + std::to_string(opacity) + " } ] }\n"
           "  ]\n}\n";
    return doc;
}

std::string realRefractivity(double strength) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "{ \"enabled\" : true, \"depth\" : %.17g, \"strength\" : %.17g }", kRealDepth,
                  strength);
    return std::string(buf);
}

const float* at(const RenderedIcon& img, std::uint32_t x, std::uint32_t y) {
    return img.rgba.data() + (static_cast<std::size_t>(y) * img.width + x) * 4;
}

// A half-plane field: inside is `x < edge`, so `d = x - edge` is negative
// inside, which is the sign convention the whole tower uses. The gradient is
// exact and constant, which is what makes the arithmetic in the sign test
// something a person can check by hand.
FieldImage halfPlaneField(std::uint32_t w, std::uint32_t h, float edge) {
    FieldImage f;
    f.width = w;
    f.height = h;
    f.rgba.assign(static_cast<std::size_t>(w) * h * 4, 0.0f);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const float d = static_cast<float>(x) + 0.5f - edge;
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
            f.rgba[i + 0] = d;
            f.rgba[i + 1] = 1.0f;   // d increases with x
            f.rgba[i + 2] = 0.0f;
            f.rgba[i + 3] = std::fmin(std::fmax(0.5f - d, 0.0f), 1.0f);
        }
    }
    return f;
}

// A premultiplied ramp buffer -- opaque, so premultiplied and straight agree
// and the red channel is directly the x it was sampled at.
std::vector<float> rampBuffer(std::uint32_t w, std::uint32_t h) {
    std::vector<float> v(static_cast<std::size_t>(w) * h * 4, 0.0f);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
            v[i + 0] = (static_cast<float>(x) + 0.5f) / static_cast<float>(w);
            v[i + 1] = 0.0f;
            v[i + 2] = 0.0f;
            v[i + 3] = 1.0f;
        }
    }
    return v;
}

}  // namespace

// ---- the flattener -------------------------------------------------------

// A rectangle written as three lines and a `Z` is FOUR segments, and the
// closing one is implied. A flattener that repeated the first point would build
// a zero-length segment; one that dropped the close would leave the shape open
// and its inside would leak out through the gap.
TEST_CASE(a_closed_rectangle_flattens_to_one_contour_with_the_close_implied) {
    auto svg = icf::svg::SvgDocument::parse(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\">"
        "<path d=\"M1 1 L9 1 L9 9 L1 9 Z\" fill=\"#000\"/></svg>");
    REQUIRE(svg.has_value());
    const GlassContours c = flattenSvgToContours(*svg, identityGlobals(), 16);
    REQUIRE(c.contours.size() == 1);
    CHECK_EQ(c.contours[0].xy.size(), std::size_t{8});   // four points, not five
    CHECK_EQ(c.contours[0].xy[0], 1.0f);
    CHECK_EQ(c.contours[0].xy[1], 1.0f);
    CHECK_EQ(c.contours[0].xy[6], 1.0f);
    CHECK_EQ(c.contours[0].xy[7], 9.0f);
    CHECK(!c.mixedRules);
}

// The same rectangle written with an EXPLICIT return to the start before the
// `Z`. The last point repeats the first and must not survive: `FieldContour`
// implies the closing segment, so keeping it would add a zero-length one.
TEST_CASE(an_explicit_return_to_the_start_is_not_kept_as_a_repeated_point) {
    auto svg = icf::svg::SvgDocument::parse(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\">"
        "<path d=\"M1 1 L9 1 L9 9 L1 9 L1 1 Z\" fill=\"#000\"/></svg>");
    REQUIRE(svg.has_value());
    const GlassContours c = flattenSvgToContours(*svg, identityGlobals(), 16);
    REQUIRE(c.contours.size() == 1);
    CHECK_EQ(c.contours[0].xy.size(), std::size_t{8});
}

// A shape painted `none` covers nothing, so it is not part of the glass's
// shape. Counting it would give the lens an outline the art does not have.
TEST_CASE(an_unpainted_shape_contributes_no_contour) {
    auto svg = icf::svg::SvgDocument::parse(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\">"
        "<path d=\"M1 1 L9 1 L9 9 Z\" fill=\"none\" stroke=\"#000\"/>"
        "<path d=\"M2 2 L8 2 L8 8 Z\" fill=\"#000\"/></svg>");
    REQUIRE(svg.has_value());
    const GlassContours c = flattenSvgToContours(*svg, identityGlobals(), 16);
    CHECK_EQ(c.contours.size(), std::size_t{1});
    CHECK_EQ(c.unpaintedShapes, std::size_t{1});
}

// Two painted shapes that disagree on the fill rule cannot both be signed
// correctly by one field, and the renderer is expected to NAME the layer rather
// than pick. Picking would invert the inside of part of the shape, and the
// picture would still be a picture.
TEST_CASE(disagreeing_fill_rules_are_flagged_rather_than_resolved) {
    auto svg = icf::svg::SvgDocument::parse(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\">"
        "<path d=\"M1 1 L9 1 L9 9 Z\" fill=\"#000\"/>"
        "<path d=\"M2 2 L8 2 L8 8 Z\" fill=\"#000\" fill-rule=\"evenodd\"/></svg>");
    REQUIRE(svg.has_value());
    const GlassContours c = flattenSvgToContours(*svg, identityGlobals(), 16);
    CHECK(c.mixedRules);
}

// The PLACEMENT is honoured, because the flattener runs the same affine
// `placeOnCanvas` builds and `PathVertex.glsl` applies. A glass shape one pixel
// off from the art it belongs to would refract the wrong edge, and at a canvas
// size where the scale is 1 nothing would notice.
TEST_CASE(the_flattener_honours_the_layers_placement_on_the_canvas) {
    auto svg = icf::svg::SvgDocument::parse(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\">"
        "<path d=\"M0 0 L512 0 L512 512 L0 512 Z\" fill=\"#000\"/></svg>");
    REQUIRE(svg.has_value());
    // 512 points of art centred on a 1024-point canvas, rendered at 1024
    // pixels: the square spans 256..768 with nothing left over.
    const GlassContours c =
        flattenSvgToContours(*svg, placeOnCanvas(svg->viewBox, LayerPlacement{}, 1024), 16);
    REQUIRE(c.contours.size() == 1);
    CHECK_EQ(c.contours[0].xy[0], 256.0f);
    CHECK_EQ(c.contours[0].xy[1], 256.0f);
    CHECK_EQ(c.contours[0].xy[2], 768.0f);
    CHECK_EQ(c.contours[0].xy[5], 768.0f);
}

// THE CLOSED FORM. A circle drawn as four cubics, flattened at N chords each,
// generates a field whose error against the true circle is bounded by the sum
// of two things and nothing else:
//
//   - the POLYGON SAGITTA of our flattening, `r * (1 - cos(pi / (4N)))`;
//   - the AUTHOR's own curve error, ~2e-4 * r, which is the cubic
//     approximation of a quarter circle and is not ours to fix.
//
// The same argument `test_rb_field.cpp` makes for a contour handed in directly.
// Measured at three N so the bound is seen to TIGHTEN: a flattener that ignored
// `subdivisions` would sit at one error for all three and still pass a single
// loose threshold.
TEST_CASE(the_flattened_circle_matches_its_closed_form_to_the_polygon_sagitta) {
    const double side = 128.0, cx = 64.0, cy = 64.0, r = 40.0;
    auto svg = icf::svg::SvgDocument::parse(circleSvg(cx, cy, r, side));
    REQUIRE(svg.has_value());

    double previous = 1e9;
    for (int n : {4, 16, 64}) {
        const GlassContours c = flattenSvgToContours(*svg, identityGlobals(), n);
        REQUIRE(c.contours.size() == 1);
        const FieldImage f = generateField(c.contours, 128, 128);

        double worst = 0.0;
        for (std::uint32_t y = 0; y < 128; ++y) {
            for (std::uint32_t x = 0; x < 128; ++x) {
                const double px = x + 0.5, py = y + 0.5;
                const double truth = std::hypot(px - cx, py - cy) - r;
                worst = std::fmax(worst, std::fabs(f.at(x, y)[0] - truth));
            }
        }
        const double sagitta = r * (1.0 - std::cos(3.141592653589793 / (4.0 * n)));
        const double bound = sagitta + kCubicCircleError * r;
        std::printf("  circle at %2d subdivisions: max |error| = %.4g px, bound = %.4g px\n", n,
                    worst, bound);
        // 1.5x, the same headroom `test_rb_field.cpp` allows: the sagitta is a
        // bound on the geometry and the samples land where they land.
        CHECK(worst < bound * 1.5);
        // And it must actually improve. This is the half that a flattener
        // ignoring `subdivisions` fails.
        CHECK(worst < previous);
        previous = worst;
    }
}

// ---- the ruler, and the gap it leaves ------------------------------------

// `[INF]` At the canvas size the points ARE the pixels, and the note is silent
// because there is nothing to declare. At any other size the height is scaled
// and `[OBS]` the band cutoff inside the transcription is not -- so the render
// has to SAY so.
TEST_CASE(the_ruler_is_one_at_the_canvas_size_and_is_declared_everywhere_else) {
    CHECK_EQ(glassPointsToPixels(1024), 1.0);
    CHECK_EQ(glassPointsToPixels(512), 0.5);
    CHECK(glassRulerNote(1024).empty());

    const std::string note = glassRulerNote(512);
    CHECK(!note.empty());
    // The note must name the part that is NOT scaled, or it is a note about
    // half the problem.
    CHECK(note.find("-5.0") != std::string::npos);

    GlassMaterial m;
    m.refractionHeight = kRealDepth;
    m.refractionStrength = kRealStrength;
    const DenormalisedGlass g = denormaliseGlass(m);
    // `[ART]` 134.51 points, the corpus's own value.
    CHECK(std::fabs(g.refractionHeightPoints - 134.5140) < 1e-3);
    CHECK(std::fabs(glassRefractionFor(g, 1024).heightPixels - g.refractionHeightPoints) < 1e-3);
    // Half the canvas, half the pixels. Linear, and marked `[INF]` at the site.
    CHECK(std::fabs(glassRefractionFor(g, 512).heightPixels -
                    g.refractionHeightPoints * 0.5) < 1e-3);
    CHECK(std::fabs(glassRefractionFor(g, 512).scalePixels -
                    g.displacementShaderArgument * 0.5) < 1e-3);
}

// ---- zero refraction is the identity -------------------------------------

// `[BIN]` `refractionStrength` defaults to 0 -- the five-argument init at
// `0x38E58` fills it from the constant at `0x93B30` -- and `[ART]` 269 of the
// corpus's 271 groups end up there. `fma(disp, 2s, -s)` at `s == 0` is exactly
// zero for EVERY map value, so the whole stage is the identity.
TEST_CASE(the_read_default_strength_makes_the_displacement_the_identity) {
    const DenormalisedGlass g = denormaliseGlass(GlassMaterial{});
    CHECK_EQ(g.refractionStrengthPoints, 0.0);
    CHECK_EQ(g.displacementShaderArgument, -0.0);
    CHECK(glassRefractionIsIdentity(glassRefractionFor(g, 1024)));

    // And the decode itself, across the map's whole range: no value of the map
    // moves a sample when the scale is zero.
    for (int i = 0; i <= 20; ++i) {
        const float d = static_cast<float>(i) / 20.0f;
        float offset[2];
        displacementDecodeOffset(0.0f, d, 1.0f - d, offset);
        CHECK_EQ(offset[0], 0.0f);
        CHECK_EQ(offset[1], 0.0f);
    }

    // A group that carries `refractivity` DISABLED with a zero strength is the
    // corpus's third real shape (`videolan/vlc-ios`, 3 groups). `[OBS]` The
    // `enabled` bit is not applied anywhere in this tower, so what makes this
    // the identity is the VALUE being zero, not the bit being false.
    GlassMaterial off;
    off.refractionStrength = 0.0;
    CHECK(glassRefractionIsIdentity(glassRefractionFor(denormaliseGlass(off), 1024)));
}

// THE STRONGEST CHECK AVAILABLE, end to end and BIT FOR BIT. At `size == 1024`
// the ruler is exactly 1, so nothing here is confounded by the `[INF]` scaling.
// A glass layer whose group carries no `refractivity` must produce EXACTLY the
// same image as the same layer with the glass bit cleared -- not "nearly", not
// "within a tolerance". `displacementMap_v1` supersamples four jittered taps,
// so a join that ran the stage anyway would come back blurred by a quarter of a
// pixel and every tolerance-based check would let it through.
TEST_CASE(a_glass_layer_with_no_refractivity_leaves_the_backdrop_bit_identical) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 1024;   // the ruler is exactly 1 here, and only here

    const TempBundle withGlass(rampAndGlass("square.svg", "", true));
    const TempBundle without(rampAndGlass("square.svg", "", false));
    auto a = icf::IconBundle::open(withGlass.path());
    auto b = icf::IconBundle::open(without.path());
    REQUIRE(a.has_value() && b.has_value());
    auto ga = renderIcon(d, *a, o);
    auto gb = renderIcon(d, *b, o);
    REQUIRE(ga.has_value() && gb.has_value());

    // The glass layer still DRAWS -- this is the whole point of the task. It
    // just refracts nothing.
    CHECK_EQ(ga->drawn, std::size_t{2});
    CHECK_EQ(ga->glassRefracted, std::size_t{0});
    CHECK(ga->skipped.empty());

    std::size_t differing = 0;
    REQUIRE(ga->rgba.size() == gb->rgba.size());
    for (std::size_t i = 0; i < ga->rgba.size(); ++i) {
        if (ga->rgba[i] != gb->rgba[i]) ++differing;
    }
    std::printf("  zero-refraction identity: %zu of %zu components differ\n", differing,
                ga->rgba.size());
    CHECK_EQ(differing, std::size_t{0});
}

// ---- the refraction actually moves the backdrop --------------------------

// Displaced WHERE THE SHAPE IS and untouched OUTSIDE it, at `size == 1024` and
// on the corpus's own strength and depth.
//
// The outside check is bit-for-bit, and it is the half that a mask error fails.
// The inside check reads the ramp back as a POSITION: the red channel is a
// monotone function of x, so "the backdrop moved by this many pixels" is a
// number this test computes rather than a difference it merely notices. A join
// with the displacement zeroed passes neither half.
TEST_CASE(the_glass_displaces_the_backdrop_inside_the_shape_and_nowhere_else) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 1024;

    const TempBundle lens(rampAndGlass("square.svg", realRefractivity(kRealStrength), true));
    const TempBundle flat(rampAndGlass("square.svg", realRefractivity(kRealStrength), false));
    auto a = icf::IconBundle::open(lens.path());
    auto b = icf::IconBundle::open(flat.path());
    REQUIRE(a.has_value() && b.has_value());
    auto ga = renderIcon(d, *a, o);
    auto gb = renderIcon(d, *b, o);
    REQUIRE(ga.has_value() && gb.has_value());
    CHECK_EQ(ga->drawn, std::size_t{2});
    CHECK_EQ(ga->glassRefracted, std::size_t{1});
    CHECK(ga->skipped.empty());
    // At 1024 the ruler is 1, so nothing has to be declared.
    CHECK(ga->notes.empty());

    // OUTSIDE. The square spans pixels 256..768; a generous margin either side
    // keeps the ~1px mask ramp out of it. Bit for bit.
    std::size_t outsideDiffering = 0;
    for (std::uint32_t y = 8; y < 1024; y += 37) {
        for (std::uint32_t x = 8; x < 1024; x += 37) {
            if (x >= 250 && x <= 774 && y >= 250 && y <= 774) continue;
            for (int k = 0; k < 4; ++k) {
                if (at(*ga, x, y)[k] != at(*gb, x, y)[k]) ++outsideDiffering;
            }
        }
    }
    CHECK_EQ(outsideDiffering, std::size_t{0});

    // INSIDE THE BAND. 24 pixels in from the left edge, so `s / height` is about
    // 0.18 and the profile is nowhere near either end of the band.
    //
    // The glass layer's art is white at opacity 0.25, so the refracted backdrop
    // survives at 75% and the two renders differ by 0.75 * (moved - still).
    const float moved = at(*ga, 280, 512)[0];
    const float still = at(*gb, 280, 512)[0];
    const float backdrop = at(*gb, 280, 512)[0];
    CHECK(std::fabs(moved - still) > 0.02f);

    // And WHICH WAY. The gradient at the left edge points OUT of the shape, i.e.
    // toward -x; the scale is `-strength` and the strength is negative, so the
    // scale is positive and the sample is taken to the LEFT -- from a darker
    // part of the ramp. Reading the ramp back: red falls.
    std::printf("  band pixel: backdrop %.4f -> refracted-and-tinted %.4f\n", backdrop, moved);
    CHECK(moved < still);

    // The right edge is the mirror image, and it has to move the OTHER way --
    // a displacement stuck on one axis direction would pass the check above.
    CHECK(at(*ga, 744, 512)[0] > at(*gb, 744, 512)[0]);

    // The middle of the square is deeper than the band (`height` is 134.5 px and
    // the square's half-width is 256), so the fade has closed and the backdrop
    // is untouched there. Bit for bit again: this is what proves the band has a
    // reach rather than the whole interior being displaced.
    for (int k = 0; k < 4; ++k) {
        CHECK_EQ(at(*ga, 512, 512)[k], at(*gb, 512, 512)[k]);
    }
}

// THE SIGN, on the corpus's own magnitude. `[ART]` Both enabled `refractivity`
// entries in all 145 documents carry a NEGATIVE strength, and
// `denormaliseRefractionStrength` carries the sign THROUGH the power rather than
// discarding it. If the join lost that sign the picture would still look like
// glass -- it would refract the wrong way, and nothing about a lens says which
// way is right by eye.
//
// Held at the composite rather than end to end so the arithmetic is checkable by
// hand: a half-plane field, a ramp backdrop, and the same map driven with the
// scale's two signs.
TEST_CASE(a_negative_strength_displaces_the_opposite_way_from_a_positive_one) {
    const std::uint32_t w = 512, h = 8;
    const float edge = 300.0f;
    const FieldImage field = halfPlaneField(w, h, edge);

    GlassMaterial negative;
    negative.refractionHeight = kRealDepth;
    negative.refractionStrength = kRealStrength;
    GlassMaterial positive = negative;
    positive.refractionStrength = -kRealStrength;

    GlassRefraction rn = glassRefractionFor(denormaliseGlass(negative), 1024);
    GlassRefraction rp = glassRefractionFor(denormaliseGlass(positive), 1024);
    // Same magnitude, opposite sign, and nothing else different.
    CHECK(std::fabs(rn.scalePixels + rp.scalePixels) < 1e-3f);
    CHECK_EQ(rn.heightPixels, rp.heightPixels);

    // The map does not depend on the sign -- the sign lives entirely in the
    // shader's one argument -- so both composites run the SAME map.
    const DisplacementImage map = glassDisplacementMap(field, rn);

    const std::vector<float> base = rampBuffer(w, h);
    std::vector<float> an = base, ap = base;
    glassOver(an, w, h, map, rn);
    glassOver(ap, w, h, map, rp);

    // 50 pixels inside the edge: `s / height` is about 0.37, well inside the
    // band, and the mask is saturated so the composite is a full replacement.
    const std::size_t i = (static_cast<std::size_t>(4) * w + 250) * 4;
    const float v0 = base[i], vn = an[i], vp = ap[i];
    std::printf("  sign: still %.5f, negative strength %.5f, positive %.5f\n", v0, vn, vp);
    CHECK(std::fabs(vn - v0) > 0.01f);
    CHECK((vn - v0) * (vp - v0) < 0.0f);                     // opposite ways
    CHECK(std::fabs((vn - v0) + (vp - v0)) < 0.02f);         // same distance

    // And outside the shape the backdrop is untouched, bit for bit, for both.
    const std::size_t out = (static_cast<std::size_t>(4) * w + 400) * 4;
    CHECK_EQ(an[out], base[out]);
    CHECK_EQ(ap[out], base[out]);
}

// `fwidth` on a pixel grid is `|dFdx| + |dFdy|`, and `t` differs from the field
// by a constant, so it is `|gx| + |gy|`. Pinned because the transcription takes
// it as an ARGUMENT -- a CPU has no fragment quad -- and an argument nobody
// checks is a magic number with a nice name.
TEST_CASE(the_filter_width_is_the_gradients_manhattan_length) {
    FieldSample s;
    s.gx = 1.0f;
    s.gy = 0.0f;
    CHECK_EQ(glassFilterWidth(s), 1.0f);      // an axis-aligned edge
    s.gx = -0.6f;
    s.gy = 0.8f;
    CHECK(std::fabs(glassFilterWidth(s) - 1.4f) < 1e-6f);   // signs do not cancel
}

// The map's neutral point. `DisplacementOracle.h` states it from the other end:
// "a map of exactly 0.5 gives an offset of exactly zero". `[OBS]` The ENCODING
// is not read, and 0.5 is the only bias that makes a displacement of zero and
// the decode's neutral point the same thing.
TEST_CASE(a_zero_displacement_encodes_to_the_maps_neutral_half) {
    GlassRefraction r;
    r.heightPixels = 100.0f;
    r.scalePixels = 42.0f;

    // DEEP INSIDE, past the end of the band: `s / height` saturates at 1, the
    // circular profile reaches 1, `1 - mag` is 0 and the fade has closed on top
    // of that. Zero displacement, and the mask is full.
    FieldImage deep;
    deep.width = deep.height = 1;
    deep.rgba = {-500.0f, 1.0f, 0.0f, 1.0f};
    const DisplacementImage m = glassDisplacementMap(deep, r);
    CHECK_EQ(m.rgba[0], 0.5f);
    CHECK_EQ(m.rgba[1], 0.5f);
    // `.z` is the shader's PER-TAP weight, splatted over the colour at every
    // tap. The shader already divides by the tap count, so anything but 1 here
    // darkens the refracted backdrop.
    CHECK_EQ(m.rgba[2], 1.0f);
    CHECK_EQ(m.rgba[3], 1.0f);   // the mask, and it is not the weight

    // OUTSIDE THE SHAPE, and this is the half worth knowing: the displacement
    // there is NOT zero. `bandCoordinate` saturates at 0, the circular profile
    // is 0, `1 - mag` is 1 and the fade is wide open -- so the shader hands back
    // a FULL-magnitude displacement outside the shape. What keeps the backdrop
    // untouched out there is the MASK, which is what `glassOver` gates on. A
    // composite that ignored the mask and trusted the displacement to be zero
    // would smear the whole canvas.
    FieldImage outside;
    outside.width = outside.height = 1;
    outside.rgba = {500.0f, 1.0f, 0.0f, 0.0f};
    const DisplacementImage o = glassDisplacementMap(outside, r);
    CHECK_EQ(o.rgba[3], 0.0f);                 // the mask is what is zero
    CHECK(std::fabs(o.rgba[0] - 0.5f) > 0.4f); // and the displacement is not
}

// ---- the gap that is carried, not filled ---------------------------------

// `[ART]` 45 of the corpus's 171 glass layers name `.png` art and one names
// `.heic`. A raster has no path to flatten. That gets its OWN sentence: folding
// it into "the glass is not transcribed" would hide the fact that the glass now
// draws, and a reader counting blockers would never learn what is actually
// missing (a field generator that reads a raster's alpha).
TEST_CASE(a_glass_layer_over_raster_art_is_skipped_with_its_own_reason) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 64;
    const TempBundle b(rampAndGlass("ramp.png", realRefractivity(kRealStrength), true));
    auto bundle = icf::IconBundle::open(b.path());
    REQUIRE(bundle.has_value());
    auto icon = renderIcon(d, *bundle, o);
    REQUIRE(icon.has_value());
    CHECK_EQ(icon->drawn, std::size_t{1});          // the backdrop, not the lens
    REQUIRE(icon->skipped.size() == 1);
    const std::string& why = icon->skipped[0].why;
    std::printf("  raster glass skip: %s\n", why.c_str());
    CHECK(why.find("raster") != std::string::npos);
    // And it must NOT be the old sentence, which said the effect was not
    // transcribed. It is.
    CHECK(why.find("nao foi transcrito") == std::string::npos);
}

// A render at any size other than 1024 refracts, and SAYS what it had to
// assume. `[OBS]` The band cutoff inside `sdf_glass_displacement` is Apple's
// constant in field units and this project does not get to rescale it.
TEST_CASE(a_render_away_from_the_canvas_size_declares_the_ruler_it_assumed) {
    Device& d = gpu();
    if (!d.valid()) return;
    IconRenderOptions o;
    o.size = 128;
    const TempBundle b(rampAndGlass("square.svg", realRefractivity(kRealStrength), true));
    auto bundle = icf::IconBundle::open(b.path());
    REQUIRE(bundle.has_value());
    auto icon = renderIcon(d, *bundle, o);
    REQUIRE(icon.has_value());
    CHECK_EQ(icon->glassRefracted, std::size_t{1});
    REQUIRE(icon->notes.size() == 1);
    CHECK(icon->notes[0].find("[INF]") != std::string::npos);
}

// ---- the corpus ----------------------------------------------------------

// `[ART]` The two bundles in all 145 documents whose `refractivity` is enabled
// with a non-zero strength -- and THE MEASUREMENT THAT CAME OUT OF RUNNING
// THEM, which is not what this task expected:
//
//   CamilleScholtz/swmpc  -- its one glass layer names `12.png`. Raster art,
//                            so the flattener has nothing to flatten.
//   StikDebug/StikPair    -- all four of its layers are dangling references;
//                            the corpus bundle ships an `Assets/` folder that
//                            does not contain them.
//
// So the corpus exercises the refraction on ZERO real layers. The whole chain
// is gated -- stage by stage against the IR, and end to end against a closed
// form on this file's own documents -- but nothing in the corpus drives it. That
// is reported rather than papered over, and it is the reason the reach this task
// delivers is what it is: the corpus's glass is a PARTICIPATION bit on layers
// whose groups carry no refraction at all.
TEST_CASE(the_two_corpus_bundles_with_a_real_refraction_render_end_to_end) {
    Device& d = gpu();
    if (!d.valid()) return;
    const char* dir = std::getenv("IC_CORPUS_DIR");
    REQUIRE(dir && *dir);

    for (const char* name : {"CamilleScholtz__swmpc__swmpc", "StikDebug__StikPair__StikPair"}) {
        auto bundle = icf::IconBundle::open(fs::path(dir) / name);
        if (!bundle) continue;   // a partial corpus is not this test's failure
        IconRenderOptions o;
        o.size = 64;
        auto icon = renderIcon(d, *bundle, o);
        if (!icon) {
            std::printf("  FAIL %s: %s\n", name, icon.error().c_str());
            ++ictest::failures();
            continue;
        }
        std::printf("  %s: %zu/%zu drawn, %zu refracted, %zu skipped\n", name, icon->drawn,
                    icon->total, icon->glassRefracted, icon->skipped.size());
        for (const auto& s : icon->skipped) std::printf("      %s\n", s.why.c_str());
        CHECK(icon->drawn + icon->skipped.size() <= icon->total);
        // Away from 1024, so the ruler must have been declared.
        if (icon->glassRefracted > 0) CHECK(!icon->notes.empty());
    }
}

// THE CORPUS GATE for the glass specifically. Every glass layer in every bundle
// that ships art is either DRAWN or NAMED, and no skip reason is the retired
// "the effect was not transcribed" -- if that sentence comes back, the join has
// silently stopped joining.
TEST_CASE(corpus_glass_layers_are_drawn_or_named_and_never_the_old_sentence) {
    Device& d = gpu();
    if (!d.valid()) return;
    const char* dir = std::getenv("IC_CORPUS_DIR");
    REQUIRE(dir && *dir);

    std::vector<fs::path> bundles;
    std::error_code ec;
    for (const auto& b : fs::directory_iterator(fs::path(dir), ec)) {
        if (fs::is_directory(b.path() / "Assets", ec)) bundles.push_back(b.path());
    }
    REQUIRE(!bundles.empty());

    std::size_t drawn = 0, total = 0, refracted = 0;
    std::vector<std::string> offenders;
    for (const auto& b : bundles) {
        auto bundle = icf::IconBundle::open(b);
        if (!bundle) continue;
        IconRenderOptions o;
        o.size = 48;
        auto icon = renderIcon(d, *bundle, o);
        if (!icon) {
            offenders.push_back(b.filename().string() + ": " + icon.error());
            continue;
        }
        drawn += icon->drawn;
        total += icon->total;
        refracted += icon->glassRefracted;
        for (const auto& s : icon->skipped) {
            if (s.why.find("o efeito nao foi transcrito") != std::string::npos) {
                offenders.push_back(b.filename().string() + ": " + s.why);
            }
        }
    }
    std::printf("  corpus with glass joined: %zu of %zu layers drawn, %zu refracted\n", drawn,
                total, refracted);
    for (const auto& o : offenders) std::printf("    %s\n", o.c_str());
    CHECK(offenders.empty());
    CHECK(drawn > 0);
}
