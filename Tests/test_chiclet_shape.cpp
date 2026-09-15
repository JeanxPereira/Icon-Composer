// The chiclet's curve, and the numbers the reading committed to.
//
// This file exists to PIN, not to drive: the geometry was transcribed from
// `RenderBox.arm64` and the cases below are the measurements that would notice
// if the transcription drifted. Two of them are the fork the older laudo could
// not resolve -- which regime the chiclet's corner falls in -- and one is the
// area the clip removes, which is what the corpus gate now compares.
#include "check.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/RenderBox/ChicletShape.h"
#include "Source/RenderBox/IconRenderer.h"

using icf::svg::SegmentKind;

namespace {

namespace fs = std::filesystem;

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

rb::Device& gpu() {
    static rb::Device* d = [] {
        auto made = rb::Device::create();
        if (!made) {
            std::printf("  FAIL no Vulkan device: %s\n", made.error().c_str());
            ++ictest::failures();
            return static_cast<rb::Device*>(nullptr);
        }
        return new rb::Device(std::move(*made));
    }();
    static rb::Device dead;
    return d ? *d : dead;
}

// A bundle whose only content is an opaque background, so every pixel of the
// canvas is the background's and a transparent one can only be the clip.
class SolidBundle {
public:
    SolidBundle() {
        dir_ = fs::temp_directory_path() / "ic-chiclet-solid";
        std::error_code ec;
        fs::remove_all(dir_, ec);
        fs::create_directories(dir_ / "Assets", ec);
        const std::string doc =
            "{\n  \"fill\" : { \"solid\" : \"srgb:1.00000,0.00000,0.00000,1.00000\" },\n"
            "  \"groups\" : []\n}\n";
        std::FILE* f = std::fopen((dir_ / "icon.json").string().c_str(), "wb");
        if (f) {
            std::fwrite(doc.data(), 1, doc.size(), f);
            std::fclose(f);
        }
    }
    ~SolidBundle() {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    const fs::path& path() const { return dir_; }

private:
    fs::path dir_;
};

float alphaAt(const rb::RenderedIcon& img, std::uint32_t x, std::uint32_t y) {
    return img.rgba[(static_cast<std::size_t>(y) * img.width + x) * 4 + 3];
}

}  // namespace

// THE FORK, CLOSED. `[BIN]` `add_rounded_rect 0x7F664`-`0x7F690` divides the
// edge's leftover by `(r_near + r_far) * 0.528664947`; the chiclet's 1024 edge
// against two 266.24 radii gives 1.746, comfortably over the cliff. Had the
// stored `x1.275` reached this formula the same edge would have given 0.9615 and
// the corner would have been the BLENDED one -- a visibly different curve, and
// the reason no implementation was allowed before `Coverage::Primitive::add_path`
// was read.
TEST_CASE(the_chiclets_edge_slack_lands_in_the_canonical_regime) {
    const rb::ContinuousCornerParams p = rb::continuousCornerParams(1024.0, 266.24, 266.24);
    CHECK(near(p.extent, 1.5286649465560913, 1e-15));
    CHECK(near(p.control, 1.0884900093078613, 1e-15));
    CHECK(near(p.shoulder, 0.8684070110321045, 1e-15));

    // And the value itself, so the margin over the cliff is on the record.
    const float t = (1024.0f - 532.48f) / (532.48f * 0.528664947f);
    CHECK(near(t, 1.746052861213684, 1e-6));
    CHECK(t > 1.0f);

    // The radius the target would have used had the multiply survived, for
    // contrast: it lands on the other side.
    const float t2 = (1024.0f - 678.912f) / (678.912f * 0.528664947f);
    CHECK(t2 < 1.0f);
}

// `[BIN]` The blend of `0x7F6DC`-`0x7F6F0` REPRODUCES the canonical triple at
// `t == 1`, which is what validates the two constant pools against each other:
// `1.0 + 0.528664947`, `0.96 + 0.128490031`, `0.82 + 0.0484070182`. An edge with
// exactly no slack is the boundary case that makes them meet.
TEST_CASE(the_corner_blend_meets_the_canonical_triple_at_the_cliff) {
    // t == 1 exactly: leftover == (r + r) * 0.528664947.
    const double r = 100.0;
    const double edge = 2 * r + (2 * r) * 0.528664947;
    const rb::ContinuousCornerParams p = rb::continuousCornerParams(edge, r, r);
    CHECK(near(p.extent, 1.5286649465560913, 1e-6));
    CHECK(near(p.control, 1.0884900093078613, 1e-6));
    CHECK(near(p.shoulder, 0.8684070110321045, 1e-6));

    // And a genuinely cramped edge blends DOWN, towards the circular corner.
    const rb::ContinuousCornerParams tight = rb::continuousCornerParams(200.0, 100.0, 100.0);
    CHECK(near(tight.extent, 1.0, 1e-6));
    CHECK(near(tight.control, 0.959999979, 1e-6));
    CHECK(near(tight.shoulder, 0.819999993, 1e-6));
}

// `[BIN]` The structural signature of the continuous corner, counted from the
// emissions in `0x7F664`-`0x7FC50`: four `elt_lineto` and THREE `elt_cubeto` per
// corner. A circular corner would be four and four.
TEST_CASE(the_continuous_outline_is_four_lines_and_twelve_cubics) {
    const icf::svg::Path p = rb::continuousRoundedRect(0, 0, 1024, 1024, 266.24, 266.24);
    int moves = 0, lines = 0, cubics = 0, closes = 0;
    for (const icf::svg::Segment& s : p.segments) {
        switch (s.kind) {
            case SegmentKind::Move: ++moves; break;
            case SegmentKind::Line: ++lines; break;
            case SegmentKind::Cubic: ++cubics; break;
            case SegmentKind::Close: ++closes; break;
        }
    }
    CHECK_EQ(moves, 1);
    CHECK_EQ(cubics, 12);
    CHECK_EQ(closes, 1);
    // Four edges, plus the one that closes the seam back to the middle of the
    // right edge where the subpath started (`0x7F634`).
    CHECK_EQ(lines, 5);

    // The seam is the middle of the right edge, not a corner.
    CHECK(near(p.segments[0].p[0].x, 1024.0, 1e-12));
    CHECK(near(p.segments[0].p[0].y, 512.0, 1e-12));

    // The corner eats `extent * r` of each edge, so 1024 - 2 * 1.5286649 * 266.24
    // of straight edge survives between two corners.
    CHECK(near(1024.0 - 2 * 1.5286649465560913 * 266.24, 210.0164892578125, 1e-6));
}

// WHAT THE CLIP COSTS THE PIXEL, measured rather than described. The outline's
// signed area is 984696.005 against the canvas's 1048576, so the chiclet keeps
// 93.908% of the square and the clip removes 6.092% of it -- all of it in the
// four corners. That is the whole of the difference this front makes to what the
// corpus gate compares.
TEST_CASE(the_chiclet_clip_removes_six_percent_of_the_canvas) {
    const std::uint32_t n = 256;
    const std::vector<float> cov = rb::chicletCoverage(n);
    REQUIRE(cov.size() == static_cast<std::size_t>(n) * n);

    double sum = 0.0;
    for (float c : cov) sum += c;
    const double fraction = sum / (static_cast<double>(n) * n);
    CHECK(near(fraction, 0.93907873, 2e-3));

    // The middle is untouched and the extreme corner is gone: the clip is a
    // corner cut, not a global fade.
    CHECK(near(cov[(static_cast<std::size_t>(n) / 2) * n + n / 2], 1.0f, 1e-6));
    CHECK(near(cov[0], 0.0f, 1e-6));
    CHECK(near(cov[n - 1], 0.0f, 1e-6));
    CHECK(near(cov[static_cast<std::size_t>(n - 1) * n], 0.0f, 1e-6));

    // The middle of every edge stays fully covered -- which is why the ramp
    // tests that sample the centre column at the top and bottom rows still read
    // the ramp's ends and not a hole.
    CHECK(near(cov[n / 2], 1.0f, 1e-6));
    CHECK(near(cov[static_cast<std::size_t>(n - 1) * n + n / 2], 1.0f, 1e-6));
}

// AND THE ICON ITSELF IS NO LONGER SQUARE. The case above measures the mask;
// this one measures what `renderIcon` actually writes, which is the claim the
// front was opened for.
TEST_CASE(a_rendered_icon_has_transparent_corners_and_an_opaque_middle) {
    rb::Device& d = gpu();
    if (!d.valid()) return;
    const SolidBundle b;
    auto bundle = icf::IconBundle::open(b.path());
    REQUIRE(bundle.has_value());

    const std::uint32_t n = 128;
    rb::IconRenderOptions o;
    o.size = n;
    auto icon = rb::renderIcon(d, *bundle, o);
    REQUIRE(icon.has_value());
    CHECK(icon->backgroundPainted);

    // Every corner gone, the middle and the middle of every edge intact.
    CHECK(near(alphaAt(*icon, 0, 0), 0.0, 1e-5));
    CHECK(near(alphaAt(*icon, n - 1, 0), 0.0, 1e-5));
    CHECK(near(alphaAt(*icon, 0, n - 1), 0.0, 1e-5));
    CHECK(near(alphaAt(*icon, n - 1, n - 1), 0.0, 1e-5));
    CHECK(near(alphaAt(*icon, n / 2, n / 2), 1.0, 1e-5));
    CHECK(near(alphaAt(*icon, n / 2, 0), 1.0, 1e-5));
    CHECK(near(alphaAt(*icon, 0, n / 2), 1.0, 1e-5));

    // The corner is a CURVE and not a bevel: on the 45 degree diagonal the shape
    // reaches further out than a straight cut between the two points where the
    // corner leaves the edges would.
    const double r = rb::chicletCornerRadius(n);
    const double eaten = 1.5286649465560913 * r;   // where the corner meets the edge
    const auto diag = static_cast<std::uint32_t>(std::floor(eaten * 0.5));
    CHECK(alphaAt(*icon, diag, diag) > 0.5f);
}

// The radius scales with the canvas because 266.24 / 1024 is exactly 0.26, so a
// 16-pixel test render and a 1024-pixel one are the same shape.
TEST_CASE(the_chiclet_radius_is_a_fixed_fraction_of_the_canvas) {
    CHECK(near(rb::chicletCornerRadius(1024), 266.24, 1e-12));
    CHECK(near(rb::chicletCornerRadius(512), 133.12, 1e-12));
    CHECK(near(rb::chicletCornerRadius(100), 26.0, 1e-12));
}
