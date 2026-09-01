#include "check.h"
#include "Source/RenderBox/Compute.h"
#include "Source/RenderBox/PathCoverageOracle.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

static const std::uint32_t kCoverageSpirv[] =
#include "path_coverage.comp.inc"
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

struct Control {
    std::uint32_t count = 0;
    std::uint32_t stage = 0;
};

std::vector<Coverage> onGpu(Device& d, const std::vector<CoverageInput>& in, CoverageStage stage) {
    if (in.empty()) return {};
    auto input = Buffer::create(d, in.size() * sizeof(CoverageInput),
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!input) return {};
    std::memcpy(input->mapped(), in.data(), in.size() * sizeof(CoverageInput));

    auto out = Buffer::create(d, in.size() * 4 * sizeof(float),
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!out) return {};
    std::memset(out->mapped(), 0, in.size() * 4 * sizeof(float));

    auto pass = ComputePass::create(d, kCoverageSpirv, sizeof kCoverageSpirv, sizeof(Control));
    if (!pass) {
        std::printf("  FAIL compute pass: %s\n", pass.error().c_str());
        ++ictest::failures();
        return {};
    }
    Control c;
    c.count = static_cast<std::uint32_t>(in.size());
    c.stage = static_cast<std::uint32_t>(stage);
    auto ran = pass->run(d, *input, *out, &c, sizeof c, c.count);
    if (!ran) {
        std::printf("  FAIL dispatch: %s\n", ran.error().c_str());
        ++ictest::failures();
        return {};
    }

    std::vector<float> raw(in.size() * 4);
    std::memcpy(raw.data(), out->mapped(), raw.size() * sizeof(float));
    std::vector<Coverage> results(in.size());
    for (std::size_t i = 0; i < results.size(); ++i) {
        results[i].x = raw[i * 4];
        results[i].y = raw[i * 4 + 1];
    }
    return results;
}

void agreeOn(const std::vector<CoverageInput>& in, CoverageStage stage) {
    Device& d = gpu();
    if (!d.valid()) return;
    const auto gpuSide = onGpu(d, in, stage);
    REQUIRE(gpuSide.size() == in.size());
    int bad = 0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        const Coverage want = pathCoverage(in[i], stage);
        if (!matches(gpuSide[i], want, stage)) {
            if (bad < 3) {
                std::printf("  FAIL stage=%u i=%zu  gpu %.9g %.9g   cpu %.9g %.9g\n",
                            static_cast<unsigned>(stage), i, gpuSide[i].x, gpuSide[i].y,
                            want.x, want.y);
            }
            ++bad;
        }
    }
    CHECK_EQ(bad, 0);
}

CoverageInput edge(float fx, float fy, float yLow, float yHigh, float slope, float intercept,
                   float value) {
    CoverageInput in;
    in.position[0] = fx;
    in.position[1] = fy;
    in.line[0] = yLow;
    in.line[1] = yHigh;
    in.line[2] = slope;
    in.line[3] = intercept;
    in.extra[0] = value;
    return in;
}

}  // namespace

// `[BIN]` mod62, four instructions: the winding number, and its sign is the
// triangle's facing.
TEST_CASE(the_interior_fragment_is_the_winding_sign) {
    std::vector<CoverageInput> in(2);
    in[0].extra[1] = 1.0f;  // front facing
    in[1].extra[1] = 0.0f;
    agreeOn(in, CoverageStage::Interior);

    CHECK_EQ(pathCoverage(in[0], CoverageStage::Interior).y, 1.0f);
    CHECK_EQ(pathCoverage(in[1], CoverageStage::Interior).y, -1.0f);
    CHECK_EQ(pathCoverage(in[0], CoverageStage::Interior).x, 0.0f);
}

// A vertical edge crossing the whole pixel: the area to its right is exactly
// what is left of the pixel's width, and no trapezoid correction applies.
TEST_CASE(a_vertical_edge_covers_the_pixel_to_its_right) {
    // pixel (10, 20), edge at x = 10.25 for the pixel's whole height
    const CoverageInput in = edge(10.5f, 20.5f, 0.0f, 100.0f, 0.0f, 10.25f, 1.0f);
    const Coverage c = pathCoverage(in, CoverageStage::Exterior);
    CHECK_EQ(c.y, 0.75f);
    agreeOn({in}, CoverageStage::Exterior);
}

// An edge to the LEFT of the pixel covers all of it; one to the right covers none.
TEST_CASE(an_edge_outside_the_pixel_covers_all_or_nothing) {
    const CoverageInput left = edge(10.5f, 20.5f, 0.0f, 100.0f, 0.0f, 5.0f, 1.0f);
    const CoverageInput right = edge(10.5f, 20.5f, 0.0f, 100.0f, 0.0f, 40.0f, 1.0f);
    CHECK_EQ(pathCoverage(left, CoverageStage::Exterior).y, 1.0f);
    CHECK_EQ(pathCoverage(right, CoverageStage::Exterior).y, 0.0f);
    agreeOn({left, right}, CoverageStage::Exterior);
}

// The edge's y span clips the pixel's, and BOTH ends of the clip have to be
// tested. An edge that ENDS inside the pixel exercises the `min`; one that
// STARTS inside exercises the `max`.
//
// The mutation sweep found that only the first of those was covered: every case
// here, and every case in the thousand-edge sweep, had the edge starting exactly
// at the pixel's lower boundary -- where `max(py, yLow)` returns the same thing
// whether the max is there or not. A systematic blind spot, in the sweep as much
// as in the hand-written cases.
TEST_CASE(the_edge_ending_inside_the_pixel_clips_the_span) {
    const CoverageInput ends = edge(10.5f, 20.5f, 20.0f, 20.5f, 0.0f, 10.0f, 1.0f);
    CHECK_EQ(pathCoverage(ends, CoverageStage::Exterior).y, 0.5f);
    agreeOn({ends}, CoverageStage::Exterior);
}

TEST_CASE(the_edge_starting_inside_the_pixel_clips_the_span) {
    // yLow is ABOVE the pixel's floor: the edge covers only the top 0.25 of it.
    const CoverageInput starts = edge(10.5f, 20.5f, 20.75f, 100.0f, 0.0f, 10.0f, 1.0f);
    CHECK_EQ(pathCoverage(starts, CoverageStage::Exterior).y, 0.25f);
    agreeOn({starts}, CoverageStage::Exterior);
}

// `[BIN]` path_value is the winding sign, and it multiplies the area -- which is
// how two opposite edges cancel.
TEST_CASE(the_winding_sign_multiplies_the_area) {
    const CoverageInput up = edge(10.5f, 20.5f, 0.0f, 100.0f, 0.0f, 10.25f, 1.0f);
    const CoverageInput down = edge(10.5f, 20.5f, 0.0f, 100.0f, 0.0f, 10.25f, -1.0f);
    CHECK_EQ(pathCoverage(up, CoverageStage::Exterior).y,
             -pathCoverage(down, CoverageStage::Exterior).y);
    agreeOn({up, down}, CoverageStage::Exterior);
}

// A diagonal edge cutting the pixel corner to corner covers exactly half of it,
// and THAT is the case the trapezoid correction exists for -- without it the
// answer would be the rectangle, 0 or 1.
TEST_CASE(a_diagonal_across_the_pixel_covers_exactly_half) {
    // slope dx/dy = 1, passing through the pixel's lower-left corner
    const CoverageInput in = edge(10.5f, 20.5f, 0.0f, 100.0f, 1.0f, 10.0f - 20.0f, 1.0f);
    const Coverage c = pathCoverage(in, CoverageStage::Exterior);
    CHECK_EQ(c.y, 0.5f);
    agreeOn({in}, CoverageStage::Exterior);
}

// The same diagonal with the sign of the slope flipped: still half, because the
// ordering step puts the ends back in order before the area is taken.
TEST_CASE(a_negative_slope_is_ordered_before_the_area_is_taken) {
    const CoverageInput in = edge(10.5f, 20.5f, 0.0f, 100.0f, -1.0f, 10.0f + 21.0f, 1.0f);
    CHECK_EQ(pathCoverage(in, CoverageStage::Exterior).y, 0.5f);
    agreeOn({in}, CoverageStage::Exterior);
}

// A sweep, so the agreement is not three lucky points. Every combination of a
// pixel, an edge position, a slope and a sign.
TEST_CASE(gpu_and_cpu_agree_on_a_sweep_of_edges) {
    std::vector<CoverageInput> in;
    for (int px = 0; px < 4; ++px) {
        for (float x = -0.5f; x <= 1.75f; x += 0.25f) {
            for (float slope : {-2.0f, -0.75f, -0.125f, 0.0f, 0.125f, 0.75f, 2.0f}) {
                for (float value : {1.0f, -1.0f}) {
                    for (float yLow : {-2.0f, 0.0f, 0.25f, 0.75f}) {
                        for (float yHigh : {0.25f, 0.5f, 1.0f, 7.0f}) {
                            const float fx = static_cast<float>(px) + 0.5f;
                            // yLow varies too: with it pinned to the pixel's
                            // floor the `max` that clips it is never exercised.
                            in.push_back(edge(fx, 3.5f, 3.0f + yLow, 3.0f + yHigh, slope,
                                              static_cast<float>(px) + x - 3.0f * slope,
                                              value));
                        }
                    }
                }
            }
        }
    }
    CHECK(in.size() > 1000);
    agreeOn(in, CoverageStage::Exterior);
}

// `[BIN]` mod64: the perpendicular distance to the segment, narrowed to HALF
// before the floor. A point on the segment does not come back as full coverage,
// because the floor is 0xH1626.
TEST_CASE(the_distance_fragment_floors_at_the_half_constant) {
    CoverageInput on;
    on.dist[0] = 0.0f;
    on.dist[1] = 0.0f;
    on.dist[2] = 1.0f;
    on.dist[3] = 0.0f;
    on.extra[2] = 1.0f;
    const Coverage c = pathCoverage(on, CoverageStage::Distance);
    CHECK(c.y < 1.0f);
    // The result is narrowed too, so the answer is the nearest half to
    // 1 - 0.0015010833740234375, not the float.
    CHECK_EQ(c.y, static_cast<float>(static_cast<_Float16>(1.0f - 0.0015010833740234375f)));
    agreeOn({on}, CoverageStage::Distance);
}

// THE TOLERANCE HAS TO BITE. The sweep replaced it with `return true` and the
// suite stayed green -- disabling a check that currently passes breaks nothing.
// Same lesson as the vertex oracle's, and it had to be learnt twice.
TEST_CASE(the_coverage_tolerance_accepts_one_half_ulp_and_rejects_more) {
    Coverage a;
    a.y = 0.5f;

    Coverage within = a;
    within.y = 0.5f + 0.00048828125f;        // exactly the derived bound
    CHECK(matches(a, within, CoverageStage::Distance));

    Coverage past = a;
    past.y = 0.5f + 0.00048828125f * 2.0f;   // twice it
    CHECK(!matches(a, past, CoverageStage::Distance));

    Coverage wrong = a;
    wrong.y = 0.6f;
    CHECK(!matches(a, wrong, CoverageStage::Distance));

    // The interior stage gets NO allowance at all: it cannot round.
    Coverage nudged = a;
    nudged.y = std::nextafter(a.y, 1.0f);
    CHECK(!matches(a, nudged, CoverageStage::Interior));

    // And `x` is bit-exact for every stage.
    Coverage otherX = a;
    otherX.x = std::nextafter(0.0f, 1.0f);
    CHECK(!matches(a, otherX, CoverageStage::Distance));
}

TEST_CASE(gpu_and_cpu_agree_on_a_sweep_of_distances) {
    std::vector<CoverageInput> in;
    for (float x = -1.5f; x <= 1.5f; x += 0.125f) {
        for (float y = -1.5f; y <= 1.5f; y += 0.125f) {
            for (float scale : {0.25f, 1.0f, 4.0f}) {
                CoverageInput c;
                c.dist[0] = x;
                c.dist[1] = y;
                c.dist[2] = 1.0f;
                c.dist[3] = 0.5f;
                c.extra[2] = scale;
                in.push_back(c);
            }
        }
    }
    CHECK(in.size() > 1000);
    agreeOn(in, CoverageStage::Distance);
}
