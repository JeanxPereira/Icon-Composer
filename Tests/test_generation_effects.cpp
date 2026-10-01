// THE SHADOW, THE GLOW AND THE COMPOSITING FLAGS, PER DESIGN GENERATION.
//
// `test_rendering_parameters.cpp` pins the two blocks and the highlight, fill
// and translucency consumers. This file pins the consumers that came after
// them: the shadow drawn with the generation's own `Shadow` block, the glow that
// only generation 26 has, the refraction maximum, the order of the root pass and
// what the two tinted-dark orderings are.
//
// Every expectation below is a reading of `IconRendering.arm64` (VA == file
// offset) or of the shader it installs, named at the case. None of them is a
// number taken off a picture: where a case needs a quantity only a render can
// give -- how much shadow reaches one pixel -- it measures that quantity in one
// render and predicts another from it by the formula under test.
#include "check.h"
#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/RenderBox/BlendFormula.h"
#include "Source/RenderBox/ChicletHighlights.h"
#include "Source/RenderBox/DistanceField.h"
#include "Source/RenderBox/GlassGlow.h"
#include "Source/RenderBox/GlassLayer.h"
#include "Source/RenderBox/GlassMaterial.h"
#include "Source/RenderBox/GlassShadow.h"
#include "Source/RenderBox/GpuGlass.h"
#include "Source/RenderBox/IconRenderer.h"
#include "Source/RenderBox/Mono.h"
#include "Source/RenderBox/RenderingParameters.h"
#include "Source/RenderBox/ViewportPlan.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace rb;

namespace {

namespace fs = std::filesystem;

bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

Device& device() {
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

// A bundle in a temporary directory: the document given, and two pieces of art.
// `square.svg` is a white 512-point square, so at 256 px it covers the pixels
// 64..191 on both axes; `full.svg` is a white square the size of the canvas.
class Bundle {
public:
    explicit Bundle(const std::string& document) {
        dir_ = fs::temp_directory_path() /
               ("ic-gen-effects-" + std::to_string(std::hash<std::string>{}(document)));
        std::error_code ec;
        fs::remove_all(dir_, ec);
        fs::create_directories(dir_ / "Assets", ec);
        write(dir_ / "icon.json", document);
        write(dir_ / "Assets" / "square.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\">"
              "<path d=\"M0 0 L512 0 L512 512 L0 512 Z\" fill=\"#ffffff\"/></svg>");
        write(dir_ / "Assets" / "full.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1024 1024\">"
              "<path d=\"M0 0 L1024 0 L1024 1024 L0 1024 Z\" fill=\"#ffffff\"/></svg>");
        write(dir_ / "Assets" / "red.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1024 1024\">"
              "<path d=\"M0 0 L1024 0 L1024 1024 L0 1024 Z\" fill=\"#ff0000\"/></svg>");
    }
    ~Bundle() {
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

float at(const RenderedIcon& img, std::uint32_t x, std::uint32_t y, int c) {
    return img.rgba[(static_cast<std::size_t>(y) * img.width + x) * 4 + c];
}

std::optional<RenderedIcon> render(const std::string& document, DesignGeneration generation,
                                   bool onGpu = false, std::uint32_t size = 256,
                                   const std::function<void(IconRenderOptions&)>& more = {}) {
    Device& d = device();
    if (!d.valid()) return std::nullopt;
    const Bundle b(document);
    auto bundle = icf::IconBundle::open(b.path());
    if (!bundle) return std::nullopt;
    IconRenderOptions o;
    o.size = size;
    o.generation = generation;
    if (more) more(o);
    auto icon = onGpu ? renderIconGpu(d, *bundle, o) : renderIcon(d, *bundle, o);
    if (!icon) {
        std::printf("  render: %s\n", icon.error().c_str());
        return std::nullopt;
    }
    return std::move(*icon);
}

bool said(const RenderedIcon& icon, const char* note) {
    for (const std::string& n : icon.notes) {
        if (n == note) return true;
    }
    return false;
}

// Whether some note of the render carries `text`.
bool mentions(const RenderedIcon& icon, const char* text) {
    for (const std::string& n : icon.notes) {
        if (n.find(text) != std::string::npos) return true;
    }
    return false;
}

// The two things `prepareMono` does to the options, spelled out: the Clear
// renditions and Tinted Light ask for the mask (and carry an identity
// recolouring), Tinted Dark carries the recolouring alone.
void askForClearMask(IconRenderOptions& o) {
    o.tint = IconRenderOptions::TintRecolour{};
    o.clearMask = true;
}

// One group -- `groupKeys` and one `square.svg` layer carrying `layerKeys` -- in
// front of an opaque red layer that fills the canvas and takes no part in any
// glass.
std::string overRed(const std::string& groupKeys, const std::string& layerKeys = "") {
    return "{\n  \"groups\" : [\n    { " + groupKeys +
           "\"layers\" : [ { \"image-name\" : \"square.svg\", \"name\" : \"front\"" + layerKeys +
           " } ] },\n"
           "    { \"shadow\" : { \"kind\" : \"none\", \"opacity\" : 0.5 },\n"
           "      \"layers\" : [ { \"image-name\" : \"red.svg\", \"name\" : \"back\","
           " \"glass\" : false } ] }\n"
           "  ]\n}\n";
}

// The same front group over a document background of one grey.
std::string overGrey(const char* grey, const std::string& groupKeys,
                     const std::string& layerKeys = "") {
    return std::string("{\n  \"fill\" : { \"solid\" : \"srgb:") + grey + "," + grey + "," + grey +
           ",1.00000\" },\n  \"groups\" : [\n    { " + groupKeys +
           "\"layers\" : [ { \"image-name\" : \"square.svg\", \"name\" : \"front\"" + layerKeys +
           " } ] }\n  ]\n}\n";
}

}  // namespace

// ---- S1: the shadow ------------------------------------------------------------------

// `[BIN]` THE ALPHA AND THE BLEND BYTE OF `0x49ED4`, under each generation's
// `Shadow` block.
//
// The alpha is `shadowOpacity x table[3 - sizeClass] x group opacity`
// (`0x4A06C`/`0x4A070`), the table being `neutralOpacity` (`ctx+0x4580`..) with
// a black tint or `vibrantOpacity` (`ctx+0x4560`..) with a white one. Generation
// 27 has `[0.375 x4]` and `[0.75 x4]`; `0x76FC0` writes `[0.1 x4]` and
// `[0.5 x4]` (`0x77EB0`, `0x77EA4`).
//
// The blend byte: the neutral branch takes `Shadow.blendMode` (`0x49F94`); the
// vibrant one takes `blendModeForVibrantOnDim` when `ctx+0x463` -- the
// `iconBrightness` the highlight selectors read at `config+0x5B` -- is 2, `dim`,
// and `blendMode` otherwise (`0x49FF4`-`0x4A000`); the overdraw call replaces
// either with `overdrawBlendMode` (`tst w1, #1`). Generation 27 is
// multiply / normal / multiply; generation 26 writes plusDarker into the first
// two (`0x77EBC`, `0x77EC0`) and leaves the third.
TEST_CASE(the_shadow_alpha_and_blend_per_style_generation_and_brightness_class) {
    const ShadowParameters& s27 = renderingParameters(DesignGeneration::G27).shadow;
    const ShadowParameters& s26 = renderingParameters(DesignGeneration::G26).shadow;

    for (IconSizeClass c : {IconSizeClass::Small, IconSizeClass::Medium, IconSizeClass::Large,
                            IconSizeClass::Display}) {
        ShadowInputs in;
        in.shadowOpacity = 1.0;
        in.layerOpacity = 1.0;
        in.sizeClass = c;
        in.style = ShadowStyle::Neutral;
        CHECK(near(shadowAlpha(in, s27), 0.375));
        CHECK(near(shadowAlpha(in, s26), 0.1));
        in.style = ShadowStyle::Vibrant;
        CHECK(near(shadowAlpha(in, s27), 0.75));
        CHECK(near(shadowAlpha(in, s26), 0.5));
        in.style = ShadowStyle::Automatic;   // grouped with vibrant, `0x4A254`/`0x4A260`
        CHECK(near(shadowAlpha(in, s27), 0.75));
        CHECK(near(shadowAlpha(in, s26), 0.5));
        // The three factors, and no clamp, in generation 26 as well.
        in.style = ShadowStyle::Neutral;
        in.shadowOpacity = 2.4;
        in.layerOpacity = 0.5;
        CHECK(near(shadowAlpha(in, s26), 0.5 * (2.4 * 0.1), 1e-12));
    }

    // Generation 27: multiply everywhere, except the vibrant shadow of a dim icon.
    CHECK(shadowBlendMode(ShadowStyle::Neutral, false, s27) == BlendMode::Multiply);
    CHECK(shadowBlendMode(ShadowStyle::Neutral, true, s27) == BlendMode::Multiply);
    CHECK(shadowBlendMode(ShadowStyle::Vibrant, false, s27) == BlendMode::Multiply);
    CHECK(shadowBlendMode(ShadowStyle::Vibrant, true, s27) == BlendMode::Normal);
    CHECK(shadowBlendMode(ShadowStyle::Automatic, true, s27) == BlendMode::Normal);
    CHECK(s27.overdrawBlendMode == BlendMode::Multiply);

    // Generation 26: plusDarker in every case, and the overdraw byte untouched.
    for (ShadowStyle style : {ShadowStyle::Neutral, ShadowStyle::Vibrant, ShadowStyle::Automatic}) {
        CHECK(shadowBlendMode(style, false, s26) == BlendMode::PlusDarker);
        CHECK(shadowBlendMode(style, true, s26) == BlendMode::PlusDarker);
    }
    CHECK(s26.overdrawBlendMode == BlendMode::Multiply);
}

// `[BIN]` THE GEOMETRY, `0x20B14`, under each block. `s = size / 1024`; the
// offset is `s x (offsetX, offsetY)`, the blur `s x blurStrengthMax x
// clamp(radius[3 - c], 0, 1)` and the ring `s x ringWidth[3 - c]` when the
// Optional is present. Generation 26 writes (16, 16), `[0.35 x4]` and the nil
// tag (`0x77E80`, `0x77E98`, `0x77E88`); `blurStrengthMax` is 64 in both.
TEST_CASE(the_shadow_geometry_of_each_generation) {
    const RenderingParameters& p27 = renderingParameters(DesignGeneration::G27);
    const RenderingParameters& p26 = renderingParameters(DesignGeneration::G26);

    const ShadowGeometry g27 = shadowGeometry(1024, IconSizeClass::Large, p27.shadow, p27.glass);
    CHECK(near(g27.offsetX, 0.0));
    CHECK(near(g27.offsetY, 32.0));
    CHECK(near(g27.blurRadius, 0.3 * 64.0, 1e-12));
    REQUIRE(g27.ringWidth.has_value());
    CHECK(near(*g27.ringWidth, 16.0));

    const ShadowGeometry g26 = shadowGeometry(512, IconSizeClass::Display, p26.shadow, p26.glass);
    CHECK(near(g26.offsetX, 8.0));
    CHECK(near(g26.offsetY, 8.0));
    CHECK(near(g26.blurRadius, 0.5 * 0.35 * 64.0, 1e-12));
    CHECK(!g26.ringWidth.has_value());   // no ring: `0x20C48` skips the clip

    // `vibrantBrightness` is 1.0 in generation 26 (`0x77ECC`), and `0x20BC0`
    // skips the colour filter at exactly that value: the vibrant shadow keeps
    // the art's colour undimmed. Under generation 27 it is multiplied by 0.75.
    const std::vector<float> art = {0.8f, 0.4f, 0.2f, 1.0f};
    ShadowGeometry still;
    const std::vector<float> v27 = shadowImage(art, 1, 1, ShadowStyle::Vibrant, still, p27.shadow);
    const std::vector<float> v26 = shadowImage(art, 1, 1, ShadowStyle::Vibrant, still, p26.shadow);
    REQUIRE(v27.size() == 4 && v26.size() == 4);
    CHECK(near(v27[0], 0.8 * 0.75, 1e-6));
    CHECK_EQ(v26[0], 0.8f);
    CHECK_EQ(v26[1], 0.4f);
    CHECK_EQ(v26[2], 0.2f);
}

// `[BIN]` A VIBRANT SHADOW ON A DIM ICON IS BLENDED `normal` IN GENERATION 27
// (`0x49FF4`: `ldrb w8, [x20, #0x463]; cmp w8, #2; csel w8, w12, w11, eq`, with
// `w12` the byte at `Shadow+0x99`). Until 2026-10-01 the renderer never passed
// the class and the shadow multiplied on every chiclet.
//
// How much shadow reaches the probe is measured on a chiclet that classifies as
// `default` (grey 0.5), where the blend is multiply and the backdrop is opaque:
// `out = d x (1 - a x (1 - c))`, `c` being the vibrant shadow's colour -- the
// white art times `vibrantBrightness`, 0.75. That gives `a`. On the dim chiclet
// (grey 0.1: lightness below `maxDimChicletLuminance`, 0.2) the same shadow,
// source-over, is `c x a + d x (1 - a)`.
TEST_CASE(a_vibrant_shadow_on_a_dim_chiclet_is_blended_normal_in_generation_27) {
    const std::string group =
        "\"specular\" : false,\n"
        "      \"shadow\" : { \"kind\" : \"layer-color\", \"opacity\" : 1.0 },\n      ";
    auto mid = render(overGrey("0.50000", group), DesignGeneration::G27);
    auto dim = render(overGrey("0.10000", group), DesignGeneration::G27);
    REQUIRE(mid && dim);
    CHECK(mid->backgroundPainted && dim->backgroundPainted);
    CHECK_EQ(mid->glassShadowed, std::size_t{1});
    CHECK_EQ(dim->glassShadowed, std::size_t{1});

    // The probe: four pixels under the square, where only the shadow reaches.
    // The backdrop is read well away from the shadow and from the chiclet's rim.
    const std::uint32_t px = 128, py = 196, bx = 30, by = 128;
    const double c = 0.75;
    const double d1 = at(*mid, bx, by, 0);
    const double a = (1.0 - at(*mid, px, py, 0) / d1) / (1.0 - c);
    CHECK(a > 0.05);   // there is a shadow to speak of
    CHECK(a < 0.75 + 1e-6);

    const double d2 = at(*dim, bx, by, 0);
    const double normal = c * a + d2 * (1.0 - a);
    const double multiply = d2 * (1.0 - a * (1.0 - c));
    CHECK(near(at(*dim, px, py, 0), normal, 2e-5));
    CHECK(std::fabs(normal - multiply) > 0.01);   // the two blends are apart here

    // A NEUTRAL shadow on the same dim chiclet still multiplies: the neutral
    // branch (`0x49F94`) does not read the class. Its colour is black, so
    // multiply is `d x (1 - a)` and `normal` would be the same number -- the
    // case that can tell them apart is the vibrant one above; this one only
    // holds the alpha table: 0.375 against 0.75 for the same coverage.
    const std::string neutral =
        "\"specular\" : false,\n"
        "      \"shadow\" : { \"kind\" : \"neutral\", \"opacity\" : 1.0 },\n      ";
    auto dimNeutral = render(overGrey("0.10000", neutral), DesignGeneration::G27);
    REQUIRE(dimNeutral.has_value());
    CHECK(near(at(*dimNeutral, px, py, 0), d2 * (1.0 - a * 0.5), 2e-5));
}

// `[BIN]` THE OVERDRAW GATE IS TWO TESTS (`0x4ADBC`-`0x4ADCC`):
// `ldrb w8, [x24, #0x5b1]` -- `Shadow.drawOverContent` -- `cmp w8, #1`, then
// `ccmp w9, #0, #0, eq` on the group's blend byte, and `b.ne` past the whole
// pass. Generation 26 clears the flag (`0x77ED0`), so a translucent glass group
// that blends normally is overdrawn in 27 and not in 26.
TEST_CASE(the_shadow_overdraw_needs_draw_over_content_and_a_normal_group_blend) {
    auto doc = [](const char* blend) {
        return overRed(std::string("\"specular\" : false,\n      \"blend-mode\" : \"") + blend +
                       "\",\n      \"shadow\" : { \"kind\" : \"neutral\", \"opacity\" : 1.0 },\n"
                       "      \"translucency\" : { \"enabled\" : true, \"value\" : 0.5 },\n      ");
    };
    for (bool gpu : {false, true}) {
        auto n27 = render(doc("normal"), DesignGeneration::G27, gpu, 128);
        auto b27 = render(doc("plus-lighter"), DesignGeneration::G27, gpu, 128);
        auto n26 = render(doc("normal"), DesignGeneration::G26, gpu, 128);
        auto b26 = render(doc("plus-lighter"), DesignGeneration::G26, gpu, 128);
        REQUIRE(n27 && b27 && n26 && b26);
        for (const auto* icon : {&*n27, &*b27, &*n26, &*b26}) {
            CHECK_EQ(icon->glassShadowed, std::size_t{1});   // the main shadow has no such gate
        }
        CHECK_EQ(n27->glassShadowOverdrawn, std::size_t{1});
        CHECK_EQ(b27->glassShadowOverdrawn, std::size_t{0});
        CHECK_EQ(n26->glassShadowOverdrawn, std::size_t{0});
        CHECK_EQ(b26->glassShadowOverdrawn, std::size_t{0});
    }
}

// `[BIN]` WITH NO RING THE SHADOW IS CAST FROM EVERY ELEMENT. `0x18338`:
// `ldrb w8, [x23, #0x40]` -- the tag of `Shadow.ringWidth` -- `cmp w8, #1`, and
// when it is nil the glass-filtered list is released and the list of ALL the
// group's elements takes its place (`mov x20, x22`, `0x18354`). So a group
// whose only layer says `"glass" : false` casts nothing in generation 27 and a
// shadow in generation 26 -- displaced by the (16, 16) points `0x77E80` writes,
// which at 256 px is four pixels right and four down.
TEST_CASE(without_a_ring_every_element_of_the_group_casts_the_shadow) {
    const std::string doc = overRed(
        "\"specular\" : false,\n"
        "      \"shadow\" : { \"kind\" : \"neutral\", \"opacity\" : 1.0 },\n      ",
        ", \"glass\" : false");
    for (bool gpu : {false, true}) {
        auto g27 = render(doc, DesignGeneration::G27, gpu);
        auto g26 = render(doc, DesignGeneration::G26, gpu);
        REQUIRE(g27 && g26);
        CHECK_EQ(g27->glassShadowed, std::size_t{0});
        CHECK_EQ(g26->glassShadowed, std::size_t{1});
        CHECK(!said(*g26, kShadowRingNote));   // nothing was clipped to a ring

        // Beside the square, 6.5 px from its left and its right edge.
        const std::uint32_t left = 57, right = 198, row = 128;
        CHECK(near(at(*g27, left, row, 0), 1.0, 1e-6));    // the red backdrop, untouched
        CHECK(near(at(*g27, right, row, 0), 1.0, 1e-6));
        const float dl = 1.0f - at(*g26, left, row, 0);
        const float dr = 1.0f - at(*g26, right, row, 0);
        CHECK(dr > 0.005f);
        CHECK(dr > dl + 0.002f);   // the offset is to the RIGHT...
        const float du = 1.0f - at(*g26, 128, 57, 0);
        const float dd = 1.0f - at(*g26, 128, 198, 0);
        CHECK(dd > du + 0.002f);   // ...and DOWN, by the same amount
        CHECK(near(dr, dd, 1e-3));
        CHECK(near(dl, du, 1e-3));
    }
}

// `[BIN]` `Shadow.ignoreFillOpacity` (`0x1C260`: `ldrb w8, [x20, #0xb8]`,
// `cmp w8, #1`). Set, the list the shadow is cast from is drawn with every
// fill's alpha rewritten to 1.0 -- `0x1C3E8`-`0x1C430` copies each stop's three
// colour components and its location and stores `0x3FF0000000000000` in the
// alpha slot. Generation 27 sets it; `0x77ED0` clears it in 26.
//
// One glass layer whose fill is white at alpha 0.5, and the same with the fill
// opaque. In generation 27 the two cast THE SAME shadow -- to the float, it is
// the same source -- while the art itself is half as opaque. In generation 26
// the shadow follows the fill: plusDarker with a black source is
// `d - a` over an opaque backdrop, so half the alpha is half the darkening.
TEST_CASE(a_translucent_fill_casts_an_opaque_shadow_in_generation_27_and_its_own_in_26) {
    auto doc = [](const char* alpha) {
        return overRed(
            "\"specular\" : false,\n"
            "      \"shadow\" : { \"kind\" : \"neutral\", \"opacity\" : 1.0 },\n      ",
            std::string(", \"fill\" : { \"solid\" : \"srgb:1.00000,1.00000,1.00000,") + alpha +
                "\" }");
    };
    const std::uint32_t px = 128, py = 200;   // under the square: shadow only
    for (bool gpu : {false, true}) {
        auto solid27 = render(doc("1.00000"), DesignGeneration::G27, gpu);
        auto faint27 = render(doc("0.50000"), DesignGeneration::G27, gpu);
        auto solid26 = render(doc("1.00000"), DesignGeneration::G26, gpu);
        auto faint26 = render(doc("0.50000"), DesignGeneration::G26, gpu);
        REQUIRE(solid27 && faint27 && solid26 && faint26);

        // The art IS half transparent: green shows the white through the red.
        // (Read in generation 27, where the ring keeps the shadow off the middle
        // of the square; in 26 the whole silhouette casts and the middle is
        // darkened under the art.)
        CHECK(near(at(*solid27, 128, 128, 1), 1.0, 1e-5));
        CHECK(near(at(*faint27, 128, 128, 1), 0.5, 1e-4));
        CHECK(at(*faint26, 128, 128, 1) < 0.5f);
        CHECK(at(*faint26, 128, 128, 1) > 0.4f);

        const float dark27 = 1.0f - at(*solid27, px, py, 0);
        CHECK(dark27 > 0.02f);
        for (int k = 0; k < 4; ++k) {
            CHECK(near(at(*faint27, px, py, k), at(*solid27, px, py, k), gpu ? 1e-6 : 0.0));
        }
        CHECK(!said(*faint27, kShadowSourceNote));

        const float darkSolid = 1.0f - at(*solid26, px, py, 0);
        const float darkFaint = 1.0f - at(*faint26, px, py, 0);
        CHECK(darkSolid > 0.005f);
        CHECK(near(darkFaint, 0.5 * darkSolid, 1e-5));
    }
}

// ---- S2: the glow of generation 26 ---------------------------------------------------

// `[BIN]` THE FRAGMENT (`metallib-iconrendering/default_mod10.ll`, `@glow`):
//
//     x    = depth / innerRadius                 (both negated in the shader)
//     g    = exp(-0.5 x^2)
//     den  = max(1 + (1 - g) x (1/bias - 2), 2^-10)
//     edge = saturate(x / (clamp(fwidth(x), 2^-10, 2) x 0.8330078125) + 0.5)
//     out  = edge x g / den
//
// with the arguments `0xF534` installs: the radius scaled to SDF texels
// (`0xF620`), `1/bias - 2` (`0xF650`), and the alpha `innerOpacity x group
// opacity` (`0x48D8C`). Generation 26 writes bias 0.5, radius 42.38 and opacity
// 0.03 (`0x77E5C`-`0x77E64`).
TEST_CASE(the_glow_fragment_on_the_contour_at_the_inner_radius_and_deep_inside) {
    const RenderingParameters& p26 = renderingParameters(DesignGeneration::G26);
    CHECK(!renderingParameters(DesignGeneration::G27).glow.has_value());
    REQUIRE(p26.glow.has_value());
    const double frame[4] = {0.0, 0.0, 512.0, 512.0};

    // A field whose reach is far beyond anything probed, to read the fragment
    // without the saturation of the depth.
    const GlowArguments a = glowArguments(*p26.glow, 1.0, 0.5, 1e6, frame);
    CHECK(near(a.radius, 42.38 * 0.5, 1e-12));
    CHECK(near(a.biasAmount, 0.0));           // bias 0.5: the denominator is 1
    CHECK(near(a.alpha, 0.03));
    CHECK(glowDraws(a));

    // On the contour: `x = 0`, `g = 1`, and the edge is half way up.
    CHECK_EQ(glowFragment(0.0, a), 0.5);
    // One pixel band: `edge = saturate(depth / 0.8330078125 + 0.5)`.
    CHECK(near(glowFragment(0.2, a),
               (0.2 / 0.8330078125 + 0.5) * std::exp(-0.5 * (0.2 / a.radius) * (0.2 / a.radius)),
               1e-14));
    CHECK_EQ(glowFragment(-0.5, a), 0.0);     // outside: the edge has closed
    // At the inner radius: one sigma in.
    CHECK(near(glowFragment(a.radius, a), std::exp(-0.5), 1e-15));
    // Deep inside: ten sigmas.
    const double deep = glowFragment(10.0 * a.radius, a);
    CHECK(deep > 0.0);
    CHECK(near(deep / std::exp(-50.0), 1.0, 1e-12));

    // The bias. `bias 0.25` is `bias_amount 2`: `g / (1 + 2 (1 - g))`.
    GlowParameters biased = *p26.glow;
    biased.bias = 0.25;
    const GlowArguments b = glowArguments(biased, 1.0, 0.5, 1e6, frame);
    CHECK(near(b.biasAmount, 2.0));
    const double g1 = std::exp(-0.5);
    CHECK(near(glowFragment(b.radius, b), g1 / (1.0 + 2.0 * (1.0 - g1)), 1e-15));
    // And the floor under the denominator, `2^-10`: a bias amount of -1.5
    // (`bias = 2`) takes `1 + (1 - g) x -1.5` below it three sigmas in.
    biased.bias = 2.0;
    const GlowArguments c = glowArguments(biased, 1.0, 0.5, 1e6, frame);
    CHECK(near(c.biasAmount, -1.5));
    const double g3 = std::exp(-4.5);
    CHECK(1.0 + (1.0 - g3) * -1.5 < 0.0009765625);
    CHECK(near(glowFragment(3.0 * c.radius, c) / (g3 / 0.0009765625), 1.0, 1e-12));

    // The alpha is the group's too, and a group at zero opacity draws nothing
    // (`0xF570`: `fcmp d3, #0.0; b.le`).
    CHECK(near(glowArguments(*p26.glow, 0.5, 0.5, 6.0, frame).alpha, 0.015));
    CHECK(!glowDraws(glowArguments(*p26.glow, 0.0, 0.5, 6.0, frame)));
}

// `[BIN]` THE DEPTH THE FRAGMENT SEES SATURATES AT THE FIELD'S REACH. The SDF is
// what RenderBox's distance filter wrote, `saturate(distance x scale + bias)`
// (`metallib-renderbox/default_mod74.ll`, `%94`-`%99`), installed with
// `zeroDistance = +maxDistance, oneDistance = -maxDistance` (RenderBox
// `0x3F354`), and the shader decodes it with `sdfScale = -2 x maxDistance`
// (`0xF6B0`). Generation 26's glyph highlights reach 12 points (`distance`,
// `0x77870`...), so at any depth past that the fragment answers
// `exp(-0.5 (12 / 42.38)^2)`: the glow is nearly flat over the inside.
TEST_CASE(the_glow_depth_saturates_at_the_reach_of_the_groups_field) {
    const RenderingParameters& p26 = renderingParameters(DesignGeneration::G26);
    REQUIRE(p26.glow.has_value());
    const double frame[4] = {0.0, 0.0, 512.0, 512.0};
    const double reach = 12.0 * 0.5;   // twelve points, at 512 px
    const GlowArguments a = glowArguments(*p26.glow, 1.0, 0.5, reach, frame);
    const double floor = std::exp(-0.5 * (12.0 / 42.38) * (12.0 / 42.38));
    CHECK(near(floor, 0.96070, 1e-5));
    CHECK(near(glowFragment(reach, a), floor, 1e-15));
    CHECK(near(glowFragment(40.0, a), floor, 1e-15));
    CHECK(near(glowFragment(4000.0, a), floor, 1e-15));
    // Short of the reach it is still the Gaussian of the depth.
    CHECK(near(glowFragment(3.0, a), std::exp(-0.5 * (3.0 / a.radius) * (3.0 / a.radius)),
               1e-15));
    // Outside, past the reach: the derivative is zero there, the edge is a step
    // and it is on the dark side.
    CHECK_EQ(glowFragment(-40.0, a), 0.0);
    // A field with no reach decodes nothing, and the pass is not drawn.
    CHECK(!glowDraws(glowArguments(*p26.glow, 1.0, 0.5, 0.0, frame)));

    // The exponential both render paths compute, against the library's.
    for (int i = 0; i <= 1200; ++i) {
        const double x = i * 0.01;
        CHECK(near(glowGaussian(x) / std::exp(-0.5 * x * x), 1.0, 1e-13));
    }
    CHECK_EQ(glowGaussian(0.0), 1.0);
    CHECK_EQ(glowGaussian(1e6), 0.0);
}

// `[BIN]` THE CLIP: the group's `effectsFrame` on the canvas, outset by one
// `pixelUnit` (`0x48D50`-`0x48D5C`: `ldr d4, [x20, #0x46a8]; fneg d4, d4;
// CGRectInset`), handed to `clipShape:alpha:1 mode:0` (`0x48D78`). `[INF]` The
// coverage of a pixel the rect's edge crosses is the area of it inside.
TEST_CASE(the_glow_is_clipped_to_the_effects_frame_outset_by_one_pixel) {
    const RenderingParameters& p26 = renderingParameters(DesignGeneration::G26);
    REQUIRE(p26.glow.has_value());
    const double frame[4] = {10.5, 20.0, 100.0, 50.0};
    const GlowArguments a = glowArguments(*p26.glow, 1.0, 0.5, 6.0, frame);
    CHECK(near(a.clip[0], 9.5));
    CHECK(near(a.clip[1], 19.0));
    CHECK(near(a.clip[2], 111.5));
    CHECK(near(a.clip[3], 71.0));
    CHECK(near(glowClipCoverage(50.0, 30.0, a), 1.0));
    CHECK(near(glowClipCoverage(9.0, 30.0, a), 0.5));    // the left edge cuts it
    CHECK(near(glowClipCoverage(8.0, 30.0, a), 0.0));
    CHECK(near(glowClipCoverage(111.0, 30.0, a), 0.5));
    CHECK(near(glowClipCoverage(112.0, 30.0, a), 0.0));
    CHECK(near(glowClipCoverage(50.0, 19.0, a), 1.0));   // the outset row
    CHECK(near(glowClipCoverage(50.0, 18.0, a), 0.0));
    CHECK(near(glowClipCoverage(50.0, 70.0, a), 1.0));
    CHECK(near(glowClipCoverage(50.0, 71.0, a), 0.0));
    CHECK(near(glowClipCoverage(9.0, 19.0, a), 0.5));
}

// `[BIN]` THE PASS IN A WHOLE RENDER (`0x48C20`-`0x48DA8`): per group, after the
// content and before the highlights, white at `innerOpacity x group opacity`,
// plus-lighter raw (`blendMode: 0x1B`, no blend shader) -- and only in
// generation 26, whose block has a `Glow`.
//
// A white glass square over red. Its middle is 64 px from the contour, past the
// reach of the field (twelve points: 3 px at 256), so the glow there is
// `opacity x exp(-0.5 (12 / 42.38)^2)` and nothing else of the glass reaches it
// -- the two highlights of generation 26 live within 4.5 px of the contour. The
// same document with the specular off has a field with no reach and no glow,
// and gives the pixel the glow is added to.
TEST_CASE(the_glow_is_drawn_per_group_in_generation_26_and_never_in_27) {
    auto doc = [](const char* specular, const char* opacity) {
        return overRed(std::string("\"specular\" : ") + specular + ",\n      \"opacity\" : " +
                       opacity +
                       ",\n      \"shadow\" : { \"kind\" : \"none\", \"opacity\" : 0.5 },\n      ");
    };
    const double g = std::exp(-0.5 * (12.0 / 42.38) * (12.0 / 42.38));
    for (bool gpu : {false, true}) {
        for (const char* opacity : {"1.0", "0.5"}) {
            const double groupOpacity = std::atof(opacity);
            auto lit = render(doc("true", opacity), DesignGeneration::G26, gpu);
            auto plain = render(doc("false", opacity), DesignGeneration::G26, gpu);
            REQUIRE(lit && plain);
            CHECK_EQ(lit->glassGlowed, std::size_t{1});
            CHECK_EQ(plain->glassGlowed, std::size_t{0});
            CHECK(said(*lit, kGlowNote));
            CHECK(!said(*plain, kGlowNote));
            CHECK(said(*plain, kGlowNoReachNote));

            // The middle of the square: the art, plus the glow on every channel.
            for (int c = 0; c < 3; ++c) {
                CHECK(near(at(*lit, 128, 128, c) - at(*plain, 128, 128, c),
                           0.03 * groupOpacity * g, 2e-6));
            }
            CHECK_EQ(at(*lit, 128, 128, 3), 1.0f);
            // Outside the square the backdrop is untouched: the edge is closed.
            CHECK_EQ(at(*lit, 30, 128, 1), 0.0f);
            CHECK_EQ(at(*lit, 30, 128, 0), at(*plain, 30, 128, 0));
        }

        auto g27 = render(doc("true", "1.0"), DesignGeneration::G27, gpu);
        auto g27plain = render(doc("false", "1.0"), DesignGeneration::G27, gpu);
        REQUIRE(g27 && g27plain);
        CHECK_EQ(g27->glassGlowed, std::size_t{0});
        CHECK(!said(*g27, kGlowNote));
        CHECK(!said(*g27plain, kGlowNoReachNote));
        // No glow: the middle is the art, with or without the specular.
        CHECK(near(at(*g27, 128, 128, 1), at(*g27plain, 128, 128, 1), 1e-6));
    }
}

// THE TWO RENDER PATHS DRAW THE SAME GLOW, FLOAT FOR FLOAT. `icon_glow.comp` is
// `drawGlow` again in double -- the exponential included, which is not the
// device's `exp` but `glowGaussian` -- so there is no ceiling here: every float
// has to be the CPU's.
TEST_CASE(gpu_glow_is_the_cpu_glow_bit_for_bit) {
    Device& d = device();
    if (!d.valid()) return;
    if (!d.float64()) {
        std::printf("  (sem shaderFloat64 neste aparelho: o brilho fica na CPU)\n");
        return;
    }
    auto resident = gpu::Resident::of(d);
    REQUIRE(resident.has_value());
    gpu::Resident& r = **resident;

    const PixelGrid grid{512, 37, 21, 300, 260};
    FieldContour star;
    for (int k = 0; k < 10; ++k) {
        const double a = k * 3.14159265358979 / 5.0;
        const double rr = (k % 2) ? 90.0 : 200.0;
        star.xy.push_back(static_cast<float>(250.0 + rr * std::sin(a)));
        star.xy.push_back(static_cast<float>(240.0 - rr * std::cos(a)));
    }
    FieldOptions fo;
    fo.originX = grid.originX;
    fo.originY = grid.originY;
    const FieldImage field = generateFieldFromContours({star}, grid.width, grid.height, fo, 1);
    REQUIRE(field.width == grid.width);

    std::vector<float> acc(grid.texels() * 4);
    for (std::uint32_t y = 0; y < grid.height; ++y) {
        for (std::uint32_t x = 0; x < grid.width; ++x) {
            float* p = &acc[(static_cast<std::size_t>(y) * grid.width + x) * 4];
            const float a = 0.25f + 0.75f * static_cast<float>((x * 7 + y * 3) % 97) / 96.0f;
            p[0] = a * static_cast<float>(x) / grid.width;
            p[1] = a * 0.5f;
            p[2] = a * static_cast<float>(y) / grid.height;
            p[3] = a;
        }
    }

    std::lock_guard<std::mutex> lock(r.mutex());
    auto fieldSlab = r.acquire(field.rgba.size() * sizeof(float));
    REQUIRE(fieldSlab.has_value());
    REQUIRE(r.upload(*fieldSlab, field.rgba.data(), field.rgba.size() * sizeof(float)).has_value());

    struct Case {
        const char* name;
        double bias, opacity, reach;
    };
    // The frame cuts through the star on all four sides, on fractional edges.
    const double frame[4] = {120.25, 90.5, 250.5, 230.25};
    for (const Case c : {Case{"geracao 26", 0.5, 1.0, 6.0}, Case{"sem saturar", 0.5, 0.7, 1e6},
                         Case{"bias 0.25", 0.25, 1.0, 30.0}, Case{"bias 2", 2.0, 0.4, 80.0}}) {
        GlowParameters glow = *renderingParameters(DesignGeneration::G26).glow;
        glow.bias = c.bias;
        const GlowArguments args = glowArguments(glow, c.opacity, 0.5, c.reach, frame);
        REQUIRE(glowDraws(args));

        std::vector<float> want = acc;
        const std::size_t touched = drawGlow(want, field, args);
        CHECK(touched > 1000);

        auto target = r.acquire(acc.size() * sizeof(float));
        REQUIRE(target.has_value());
        REQUIRE(r.upload(*target, acc.data(), acc.size() * sizeof(float)).has_value());
        REQUIRE(gpu::glow(r, *target, *fieldSlab, grid.width, grid.height, grid.originX,
                          grid.originY, args)
                    .has_value());
        std::vector<float> got(acc.size());
        REQUIRE(r.download(*target, got.data(), got.size() * sizeof(float)).has_value());
        std::size_t differ = 0;
        float worst = 0.0f;
        for (std::size_t i = 0; i < got.size(); ++i) {
            if (std::memcmp(&got[i], &want[i], sizeof(float)) != 0) {
                ++differ;
                worst = std::max(worst, std::fabs(got[i] - want[i]));
            }
        }
        if (differ) {
            std::printf("  brilho (%s): %zu floats diferem, pior %g\n", c.name, differ,
                        static_cast<double>(worst));
        }
        CHECK_EQ(differ, std::size_t{0});
    }
}

// ---- S3: the refraction maximum, the clear mode ---------------------------------------

// `[BIN]` `refractionStrengthMax` (`ICRRenderingParameters+0x208`): 640 in
// generation 27, and `0x77F64` is `str xzr, [x19, #0x208]` -- zero in 26. The
// denormalisation is `max x sign x pow(|s|, power)` (`0x4A708`), so under
// generation 26 every strength a document can spell comes out zero and the
// refraction is the identity.
TEST_CASE(the_refraction_strength_maximum_is_zero_in_generation_26) {
    const RenderingParameters& p27 = renderingParameters(DesignGeneration::G27);
    const RenderingParameters& p26 = renderingParameters(DesignGeneration::G26);
    CHECK_EQ(p27.glass.refractionStrengthMax, 640.0);
    CHECK_EQ(p26.glass.refractionStrengthMax, 0.0);
    // Nothing else of the block moves.
    CHECK_EQ(p26.glass.refractionHeightMax, p27.glass.refractionHeightMax);
    CHECK_EQ(p26.glass.refractionHeightMin, p27.glass.refractionHeightMin);
    CHECK_EQ(p26.glass.blurStrengthMax, p27.glass.blurStrengthMax);

    GlassMaterial m;
    m.refractionStrength = -0.5269921875;   // one of the corpus's two
    m.refractionHeight = 0.25;
    const DenormalisedGlass d27 = denormaliseGlass(m, p27.glass);
    const DenormalisedGlass d26 = denormaliseGlass(m, p26.glass);
    CHECK(near(d27.refractionStrengthPoints, -640.0 * 0.5269921875, 1e-9));
    CHECK(d26.refractionStrengthPoints == 0.0);
    CHECK(near(d26.refractionHeightPoints, d27.refractionHeightPoints));
    CHECK(!glassRefractionIsIdentity(glassRefractionFor(d27, 512)));
    CHECK(glassRefractionIsIdentity(glassRefractionFor(d26, 512)));

    // In a whole render, on both paths: the group refracts in 27 and not in 26.
    const std::string doc = overRed(
        "\"specular\" : false,\n"
        "      \"shadow\" : { \"kind\" : \"none\", \"opacity\" : 0.5 },\n"
        "      \"refractivity\" : { \"enabled\" : true, \"depth\" : 0.25, \"strength\" : -0.1 },\n"
        "      ");
    for (bool gpu : {false, true}) {
        auto g27 = render(doc, DesignGeneration::G27, gpu, 128);
        auto g26 = render(doc, DesignGeneration::G26, gpu, 128);
        REQUIRE(g27 && g26);
        CHECK_EQ(g27->glassRefracted, std::size_t{1});
        CHECK_EQ(g26->glassRefracted, std::size_t{0});
    }

    // And the viewport's margin follows: the chained reach of a refraction is
    // nothing when nothing refracts.
    const Bundle b(doc);
    auto bundle = icf::IconBundle::open(b.path());
    REQUIRE(bundle.has_value());
    const DocumentReach r27 =
        documentReach(bundle->document(), icf::Context{}, IconSizeClass::Large, p27);
    const DocumentReach r26 =
        documentReach(bundle->document(), icf::Context{}, IconSizeClass::Large, p26);
    CHECK(r27.chainedPoints > 0.0);
    CHECK_EQ(r26.chainedPoints, 0.0);
}

// `[BIN]` `ICRRenderingParameters.clearMode` IS NIL IN GENERATION 26
// (`0x77064`), so the resolved clear mode is nil in every rendering mode: the
// root pass of that generation (`0x43150`) installs the headroom and the colour
// clamp and no Clear total matrix, which belongs to `0x47D2C`. A render asked
// for the Clear mask therefore draws the mask in generation 27 and the icon in
// generation 26, and `finishMono` reads the mask only where there is one.
//
// The mask is recognisable: the document's background becomes the solid
// (0, 1, 0, 1) (`0x486D0`-`0x48964`) and white content goes through the content
// matrix to (0.85, 1, 0) (`0x4AF20`).
TEST_CASE(a_generation_without_a_clear_mode_draws_no_clear_mask) {
    const std::string doc = overGrey(
        "0.50000",
        "\"specular\" : false,\n"
        "      \"shadow\" : { \"kind\" : \"none\", \"opacity\" : 0.5 },\n      ");
    CHECK(renderingParameters(DesignGeneration::G27).clearMode.has_value());
    CHECK(!renderingParameters(DesignGeneration::G26).clearMode.has_value());
    for (bool gpu : {false, true}) {
        auto g27 = render(doc, DesignGeneration::G27, gpu, 256, askForClearMask);
        auto g26 = render(doc, DesignGeneration::G26, gpu, 256, askForClearMask);
        REQUIRE(g27 && g26);
        CHECK(g27->clearMask);
        CHECK(!g26->clearMask);
        CHECK(!said(*g27, kClearModeNilNote));
        CHECK(said(*g26, kClearModeNilNote));

        // The background, away from the rim and from the square.
        CHECK(near(at(*g27, 30, 128, 0), 0.0, 1e-6));
        CHECK(near(at(*g27, 30, 128, 1), 1.0, 1e-6));
        CHECK(near(at(*g27, 30, 128, 2), 0.0, 1e-6));
        CHECK(at(*g26, 30, 128, 0) > 0.05f);                              // the grey itself
        CHECK(near(at(*g26, 30, 128, 0), at(*g26, 30, 128, 1), 1e-6));
        CHECK(near(at(*g26, 30, 128, 0), at(*g26, 30, 128, 2), 1e-6));
        // The white square.
        CHECK(near(at(*g27, 128, 128, 0), 0.85, 1e-6));
        CHECK(near(at(*g27, 128, 128, 1), 1.0, 1e-6));
        CHECK(near(at(*g27, 128, 128, 2), 0.0, 1e-6));
        for (int c = 0; c < 3; ++c) CHECK(near(at(*g26, 128, 128, c), 1.0, 1e-6));

        // The second phase: over a white backdrop, with the icon's square given.
        MonoLook mono;
        mono.kind = MonoLook::Kind::ClearLight;
        mono.squareX = 0.0;
        mono.squareY = 0.0;
        mono.squareSide = 64.0;
        ClearBackdrop back;
        back.width = 64;
        back.height = 64;
        back.rgba.assign(64 * 64 * 4, 255);
        const std::string why26 = finishMono(*g26, mono, back, icf::Idiom{}, 256);
        CHECK(why26.empty());
        // No mask was read: the opaque white of the art is still white.
        for (int c = 0; c < 4; ++c) CHECK(near(at(*g26, 128, 128, c), 1.0, 1e-5));
        const std::string why27 = finishMono(*g27, mono, back, icf::Idiom{}, 256);
        CHECK(why27.empty());
        // The mask was consumed: the pixel is a colour now, not (0.85, 1, 0).
        CHECK(!(near(at(*g27, 128, 128, 0), 0.85, 1e-3) && near(at(*g27, 128, 128, 2), 0.0, 1e-3)));
    }
}

// `[BIN]` THE CHICLET SET OUTSIDE `.color`, generation 27 (`0x62588`, the
// `chicletLuminance` branch): `0x625C0`-`0x625E8` tests the rendering mode, and
// when it is not `.color` `0x62684`-`0x62740` compares the effective clear mode
// with nil -- nil takes `Highlights+0x19D8`, `chicletScreened`, and a clear
// mode takes `+0x13A8`, `chicletClear`. Until 2026-10-01 the renderer drew
// `chicletDefault` in all three.
//
// Generation 26 is in the other branch (`0x62698`): the style's appearance
// alone -- dark is `chicletDim`, anything else `chicletDefault`.
TEST_CASE(outside_the_colour_mode_the_chiclet_takes_the_clear_or_the_screened_set) {
    const std::string doc = overGrey(
        "0.50000",
        "\"specular\" : false,\n"
        "      \"shadow\" : { \"kind\" : \"none\", \"opacity\" : 0.5 },\n      ");
    auto tintedDark = [](IconRenderOptions& o) { o.tint = IconRenderOptions::TintRecolour{}; };

    auto colour = render(doc, DesignGeneration::G27);
    auto clear = render(doc, DesignGeneration::G27, false, 256, askForClearMask);
    auto dark = render(doc, DesignGeneration::G27, false, 256, tintedDark);
    REQUIRE(colour && clear && dark);
    CHECK(mentions(*colour, "`chicletDefault`"));
    CHECK(mentions(*clear, "`chicletClear`"));
    CHECK(mentions(*dark, "`chicletScreened`"));
    CHECK(!mentions(*clear, "`chicletDefault`"));
    CHECK(!mentions(*dark, "`chicletDefault`"));

    // Under the mask the bright highlights are painted (0.7, 0, 0), plus-lighter
    // (`0x478B4`), over the mask's green: red rises on the lit top rim and the
    // other two channels stay where the solid put them.
    CHECK(at(*clear, 128, 1, 0) > 0.05f);
    CHECK(near(at(*clear, 128, 1, 1), 1.0, 1e-6));
    CHECK(near(at(*clear, 128, 1, 2), 0.0, 1e-6));

    auto clear26 = render(doc, DesignGeneration::G26, false, 256, askForClearMask);
    auto dark26 = render(doc, DesignGeneration::G26, false, 256, tintedDark);
    REQUIRE(clear26 && dark26);
    CHECK(mentions(*clear26, "`chicletDefault`"));
    CHECK(mentions(*dark26, "`chicletDim`"));
}

// ---- S6: the order of the root pass ---------------------------------------------------

// `[BIN]` CONTENT, THEN THE CHICLET'S HIGHLIGHTS, in both generations. In
// generation 27's body `0x47D2C` calls the content `0x435A0` at `0x48268` and
// the chiclet highlights `0x475A0` at `0x48274`; in generation 26's, `0x42FBC`
// calls the content (`0x469E8`) at `0x431A8` and `0x475A0` at `0x43338`. And
// `0x435A0` is the background AND the groups (`0x48524`, then `0x48E20` /
// `0x48B74`). So the highlights land on top of whatever the groups drew along
// the pastille's rim. Until 2026-10-01 they were drawn before the groups, and
// an opaque layer covering the rim buried them.
//
// A grey background under an opaque red layer the size of the canvas. What the
// pass adds is read off a render of the background alone, at the same pixel of
// the lit top rim:
//
//   generation 27, plus-lighter: the source is added whatever is under it, so
//     over the red it is `lit - unlit` of the bare background on every channel;
//   generation 26, `blendModeOverride == .normal`: each highlight is a
//     source-over, and a chain of those is linear in what is under it,
//     `out = K + d x T`. Two bare backgrounds of different greys give `K` and
//     `T`; over the red the green channel -- zero in the art -- is `K` and the
//     red, one in the art, `K + T`.
TEST_CASE(the_chiclet_highlights_are_drawn_after_the_groups_in_both_generations) {
    const char* const bare = "{\n  \"fill\" : { \"solid\" : \"srgb:0.50000,0.50000,0.50000,1.00000\" },\n"
                             "  \"groups\" : [ ]\n}\n";
    const char* const darker = "{\n  \"fill\" : { \"solid\" : \"srgb:0.25000,0.25000,0.25000,1.00000\" },\n"
                               "  \"groups\" : [ ]\n}\n";
    const char* const covered =
        "{\n  \"fill\" : { \"solid\" : \"srgb:0.50000,0.50000,0.50000,1.00000\" },\n"
        "  \"groups\" : [\n    { \"layers\" : [ { \"image-name\" : \"red.svg\", \"name\" : \"cover\","
        " \"glass\" : false } ] }\n  ]\n}\n";
    const std::uint32_t x = 128, rim = 1, middle = 128;
    for (bool gpu : {false, true}) {
        const double eps = gpu ? 1e-5 : 2e-6;
        auto bare27 = render(bare, DesignGeneration::G27, gpu);
        auto over27 = render(covered, DesignGeneration::G27, gpu);
        REQUIRE(bare27 && over27);
        const double added = at(*bare27, x, rim, 1) - at(*bare27, x, middle, 1);
        CHECK(added > 0.02);   // the key lights the top
        // The middle of the picture is the art alone: red.
        CHECK(near(at(*over27, x, middle, 0), 1.0, 1e-6));
        CHECK(near(at(*over27, x, middle, 1), 0.0, 1e-6));
        // And along the rim the highlight is ON it.
        CHECK(near(at(*over27, x, rim, 1), added, eps));
        CHECK(near(at(*over27, x, rim, 2), added, eps));
        CHECK(near(at(*over27, x, rim, 0), 1.0 + added, eps));
        CHECK(mentions(*over27, "realces do chiclet desenhados"));

        auto bare26 = render(bare, DesignGeneration::G26, gpu);
        auto darker26 = render(darker, DesignGeneration::G26, gpu);
        auto over26 = render(covered, DesignGeneration::G26, gpu);
        REQUIRE(bare26 && darker26 && over26);
        const double d1 = at(*bare26, x, middle, 1), o1 = at(*bare26, x, rim, 1);
        const double d2 = at(*darker26, x, middle, 1), o2 = at(*darker26, x, rim, 1);
        REQUIRE(std::fabs(d1 - d2) > 0.1);
        const double T = (o1 - o2) / (d1 - d2);
        const double K = o1 - d1 * T;
        CHECK(K > 0.02);        // something was laid over the rim
        CHECK(T < 0.98);
        CHECK(near(at(*over26, x, rim, 1), K, 5.0 * eps));
        CHECK(near(at(*over26, x, rim, 0), K + T, 5.0 * eps));
    }
}

// ---- S3: the tinted-dark layer and the chiclet's highlights ---------------------------

// `[BIN]` WHERE THE TINTED-DARK LAYER CLOSES. The saturation filter and the
// duotone wrap a layer the content is drawn in (`0x43210`-`0x4332C`,
// `0x482C8`-`0x483CC`). Generation 27's body tests
// `darkTintHighlightsBlendWithContent` (`ldrb w8, [x20, #0xb0]` at `0x483D4`)
// and, set -- its default --, calls the chiclet highlights `0x475A0` BEFORE
// closing the layer (`0x483E0`): they are recoloured with the content.
// Generation 26's body has no such test: `drawLayer` at `0x4332C`, `restore`,
// and only then `bl 0x475A0` at `0x43338`: they are never tinted.
//
// A grey pastille and nothing else, under a tint of (1, 0.5, 0.25). The duotone
// with `lo = black`, `hi = tint` multiplies each channel by the tint's, so:
//
//   * away from the rim the pixel is `grey x tint` in both generations;
//   * on the lit rim, generation 27: the highlight was added to the grey and
//     the sum was tinted -- the three channels are still in the ratio of the
//     tint, 1 : 0.5 : 0.25;
//   * on the lit rim, generation 26: the highlights (`blendModeOverride ==
//     .normal`, a chain of source-overs) went on OVER the tinted grey,
//     `out_c = K + T x grey x tint_c` with the same `K` and `T` on every
//     channel -- affine in the tint's component, and NOT through the origin:
//     `K`, what the untinted highlight itself contributes, is the value the
//     three channels extrapolate to at a tint of zero.
TEST_CASE(the_chiclet_highlights_are_inside_the_dark_tint_in_27_and_outside_it_in_26) {
    CHECK(renderingParameters(DesignGeneration::G27).darkTintHighlightsBlendWithContent);
    CHECK(!renderingParameters(DesignGeneration::G27).useOS26Compositing);
    CHECK(!renderingParameters(DesignGeneration::G26).darkTintHighlightsBlendWithContent);
    CHECK(renderingParameters(DesignGeneration::G26).useOS26Compositing);

    const char* const doc = "{\n  \"fill\" : { \"solid\" : \"srgb:0.50000,0.50000,0.50000,1.00000\" },\n"
                            "  \"groups\" : [ ]\n}\n";
    MonoLook mono;
    mono.kind = MonoLook::Kind::TintedDark;
    mono.tint.r = 1.0;
    mono.tint.g = 0.5;
    mono.tint.b = 0.25;
    mono.tint.saturation = 1.0;
    const std::uint32_t x = 128, rim = 1, middle = 128;
    for (bool gpu : {false, true}) {
        auto finished = [&](DesignGeneration generation) -> std::optional<RenderedIcon> {
            auto icon = render(doc, generation, gpu, 256,
                               [&](IconRenderOptions& o) { prepareMono(o, mono); });
            if (!icon) return std::nullopt;
            // No square: no simulated glass, only the recolouring.
            const std::string why = finishMono(*icon, mono, ClearBackdrop{}, icf::Idiom{}, 256);
            CHECK(why.empty());
            return icon;
        };
        auto g27 = finished(DesignGeneration::G27);
        auto g26 = finished(DesignGeneration::G26);
        REQUIRE(g27 && g26);
        CHECK(!g27->tintApplied);   // recoloured afterwards, over the finished picture
        CHECK(g26->tintApplied);    // recoloured inside the render, under the highlights
        CHECK(mentions(*g27, "`chicletScreened`"));
        CHECK(mentions(*g26, "`chicletDim`"));

        for (const RenderedIcon* icon : {&*g27, &*g26}) {
            // Away from the rim: the grey, times the tint.
            const double grey = at(*icon, x, middle, 0);
            CHECK(grey > 0.05);
            CHECK(near(at(*icon, x, middle, 1), 0.5 * grey, 1e-6));
            CHECK(near(at(*icon, x, middle, 2), 0.25 * grey, 1e-6));
        }

        // Generation 27: the lit rim keeps the tint's ratio.
        const double r27 = at(*g27, x, rim, 0);
        CHECK(r27 > at(*g27, x, middle, 0) + 0.02);   // it IS lit
        CHECK(near(at(*g27, x, rim, 1), 0.5 * r27, 1e-6));
        CHECK(near(at(*g27, x, rim, 2), 0.25 * r27, 1e-6));

        // Generation 26: affine in the tint, with the untinted highlight as the
        // intercept.
        const double r26 = at(*g26, x, rim, 0), gg26 = at(*g26, x, rim, 1), b26 = at(*g26, x, rim, 2);
        const double slope = (r26 - b26) / (1.0 - 0.25);
        const double intercept = b26 - 0.25 * slope;
        CHECK(near(gg26, intercept + 0.5 * slope, 2e-6));
        CHECK(intercept > 0.02);
        // ...which a highlight tinted with the content would not have.
        CHECK(std::fabs(gg26 - 0.5 * r26) > 0.01);
    }
}

// ---- S4: the plus-lighter clamp --------------------------------------------------------

// `[BIN]` `shouldClampPlusLBlending` AND THE ONE DRAW IT REACHES. `0x4B530`:
// `cmp w24, #8` on the group's blend byte, then `ldrb w8, [x20, #0x288]` -- the
// flag, `true` in generation 27 and cleared by `0x77080` in 26 -- then
// `ldrb w8, [x20, #0x528]`, and only with all three does `setBlendShader:` hand
// the composite to `clampedPlusL` (`metallib-iconrendering/default_mod8.ll`):
// `max(dest, (min(1, source + dest).rgb, saturate(source.a + dest.a)))`.
//
// `[OBS]` The byte at `+0x528` is not read, so it is an option of the render,
// off by default. A white group blended plus-lighter over opaque red: the plain
// sum takes red to 2; the clamp stops it at 1 and leaves green and blue, which
// were 0, at 1. Only generation 27 with the option set clamps.
TEST_CASE(the_plus_lighter_clamp_reaches_a_groups_image_behind_two_gates) {
    CHECK(renderingParameters(DesignGeneration::G27).shouldClampPlusLBlending);
    CHECK(!renderingParameters(DesignGeneration::G26).shouldClampPlusLBlending);
    const std::string doc = overRed(
        "\"specular\" : false,\n"
        "      \"blend-mode\" : \"plus-lighter\",\n"
        "      \"shadow\" : { \"kind\" : \"none\", \"opacity\" : 0.5 },\n      ",
        ", \"glass\" : false");
    auto open = [](IconRenderOptions& o) { o.drawingContextClampsPlusLighter = true; };
    for (bool gpu : {false, true}) {
        auto plain27 = render(doc, DesignGeneration::G27, gpu, 64);
        auto gated27 = render(doc, DesignGeneration::G27, gpu, 64, open);
        auto plain26 = render(doc, DesignGeneration::G26, gpu, 64);
        auto gated26 = render(doc, DesignGeneration::G26, gpu, 64, open);
        REQUIRE(plain27 && gated27 && plain26 && gated26);
        for (const RenderedIcon* icon : {&*plain27, &*plain26, &*gated26}) {
            CHECK(near(at(*icon, 32, 32, 0), 2.0, 1e-6));   // the plain sum
            CHECK(near(at(*icon, 32, 32, 1), 1.0, 1e-6));
        }
        CHECK(near(at(*gated27, 32, 32, 0), 1.0, 1e-6));    // `min(1, 1 + 1)`
        CHECK(near(at(*gated27, 32, 32, 1), 1.0, 1e-6));
        CHECK(near(at(*gated27, 32, 32, 2), 1.0, 1e-6));
        CHECK(near(at(*gated27, 32, 32, 3), 1.0, 1e-6));
        // Outside the square nothing was blended: the backdrop.
        CHECK(near(at(*gated27, 4, 4, 0), 1.0, 1e-6));
        CHECK(near(at(*gated27, 4, 4, 1), 0.0, 1e-6));
        // The gap is named exactly where it could have changed the picture.
        CHECK(said(*plain27, kPlusLighterClampNote));
        CHECK(!said(*gated27, kPlusLighterClampNote));
        CHECK(!said(*plain26, kPlusLighterClampNote));
        CHECK(!said(*gated26, kPlusLighterClampNote));
    }
}

// ---- the house invariant, under generation 26 -------------------------------------------

// A VIEWPORT RENDER IS THE CROP OF THE FULL ONE, under generation 26 as under
// 27. What is new for the plan: the shadow's reach comes from generation 26's
// `Shadow` block -- an offset of (16, 16) points and a wider blur, no ring --
// and the two passes this front added (the glow, the tinted-dark recolouring
// inside the render) work in absolute canvas coordinates.
//
// The tile sits over the lower right corner of the square, where the displaced
// shadow, the glow's edge, both highlights and the pastille's own rim are all
// near. On the CPU path every float is the full render's; the resident path is
// held to half an 8-bit level.
TEST_CASE(a_generation_26_viewport_is_the_crop_of_the_full_render) {
    const std::string doc = overGrey(
        "0.50000",
        "\"specular\" : true,\n"
        "      \"shadow\" : { \"kind\" : \"neutral\", \"opacity\" : 1.0 },\n"
        "      \"translucency\" : { \"enabled\" : true, \"value\" : 0.5 },\n      ");
    MonoLook mono;
    mono.kind = MonoLook::Kind::TintedDark;
    mono.tint.g = 0.5;
    mono.tint.b = 0.25;
    for (bool tinted : {false, true}) {
        for (bool gpu : {false, true}) {
            auto options = [&](IconRenderOptions& o) {
                if (tinted) prepareMono(o, mono);
            };
            auto full = render(doc, DesignGeneration::G26, gpu, 256, options);
            auto part = render(doc, DesignGeneration::G26, gpu, 256, [&](IconRenderOptions& o) {
                options(o);
                o.viewport = IconViewport{150, 140, 100, 110};
            });
            REQUIRE(full && part);
            REQUIRE(part->width == 100 && part->height == 110);
            CHECK_EQ(full->glassGlowed, std::size_t{1});
            CHECK_EQ(full->glassShadowed, std::size_t{1});
            CHECK_EQ(full->tintApplied, tinted);
            std::size_t differ = 0;
            float worst = 0.0f;
            for (std::uint32_t y = 0; y < part->height; ++y) {
                for (std::uint32_t x = 0; x < part->width; ++x) {
                    for (int c = 0; c < 4; ++c) {
                        const float a = at(*full, x + 150, y + 140, c);
                        const float b = at(*part, x, y, c);
                        if (a != b) {
                            ++differ;
                            worst = std::max(worst, std::fabs(a - b));
                        }
                    }
                }
            }
            if (differ) {
                std::printf("  %s%s: %zu floats diferem, pior %g\n", gpu ? "gpu" : "cpu",
                            tinted ? " tingido" : "", differ, static_cast<double>(worst));
            }
            if (gpu) {
                CHECK(worst <= 0.5f / 255.0f);
            } else {
                CHECK_EQ(differ, std::size_t{0});
            }
        }
    }
}
