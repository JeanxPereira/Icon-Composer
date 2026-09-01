#include "check.h"
#include "Source/CoreSVG/Path.h"
#include "Source/RenderBox/Compute.h"
#include "Source/RenderBox/PathBuffer.h"
#include "Source/RenderBox/PathVertexOracle.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// The compiled compute shader, produced by glslc at build time. `-mfmt=c` emits
// the braces and the 32-bit words, so the include IS the initialiser.
static const std::uint32_t kPathProbeSpirv[] =
#include "path_probe.comp.inc"
    ;

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

// The push-constant block of path_probe.comp: PathGlobals (52 bytes) then the
// invocation count and which pass to run.
struct ProbeConstants {
    PathGlobals globals;
    std::uint32_t count = 0;
    std::uint32_t pass = 0;
};
static_assert(sizeof(ProbeConstants) == 60, "52 bytes of globals plus two uints");

std::vector<ProbeVertex> onGpu(Device& d, const PathBuffer& buffer, const PathGlobals& globals,
                               PathPass pass) {
    const std::uint32_t count = invocationCount(buffer, pass);
    if (count == 0) return {};

    auto segments = Buffer::create(d, buffer.entries.size() * sizeof(CubicSegment),
                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!segments) return {};
    std::memcpy(segments->mapped(), buffer.entries.data(),
                buffer.entries.size() * sizeof(CubicSegment));

    auto out = Buffer::create(d, count * sizeof(ProbeVertex), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!out) return {};
    std::memset(out->mapped(), 0, count * sizeof(ProbeVertex));

    auto probe = ComputePass::create(d, kPathProbeSpirv, sizeof kPathProbeSpirv,
                                     sizeof(ProbeConstants));
    if (!probe) {
        std::printf("  FAIL compute pass: %s\n", probe.error().c_str());
        ++ictest::failures();
        return {};
    }

    ProbeConstants pc;
    pc.globals = globals;
    pc.count = count;
    pc.pass = static_cast<std::uint32_t>(pass);
    auto ran = probe->run(d, *segments, *out, &pc, sizeof pc, count);
    if (!ran) {
        std::printf("  FAIL dispatch: %s\n", ran.error().c_str());
        ++ictest::failures();
        return {};
    }

    std::vector<ProbeVertex> results(count);
    std::memcpy(results.data(), out->mapped(), count * sizeof(ProbeVertex));
    return results;
}

PathGlobals testGlobals() {
    PathGlobals g;
    g.m0[0] = 1.5f;
    g.m1[1] = 1.25f;
    g.m2[0] = 3.0f;
    g.m2[1] = -2.0f;
    g.origin[0] = 7.5f;
    g.origin[1] = -11.25f;
    g.depth = 1024.0f;
    g.urx = 233.75f;
    return g;
}

// The gate: the same arithmetic, twice, on two machines. Bit-identical
// everywhere a multiply and an add can reach; four ULP on the two fields that
// descend from a divide, because Vulkan allows 2.5 ULP there and no SPIR-V
// control makes it exact. PathVertexOracle.h carries the measurement.
void agreeOn(const char* d_, PathPass pass, int subdivisions) {
    Device& d = gpu();
    if (!d.valid()) return;

    auto parsed = parsePath(d_);
    REQUIRE(parsed.has_value());
    BuildOptions o;
    o.subdivisions = subdivisions;
    auto buffer = buildPathBuffer(*parsed, o);
    REQUIRE(buffer.has_value());

    const PathGlobals globals = testGlobals();
    const auto gpuSide = onGpu(d, *buffer, globals, pass);
    REQUIRE(!gpuSide.empty());

    int disagreements = 0;
    for (std::uint32_t i = 0; i < gpuSide.size(); ++i) {
        const ProbeVertex want = pathProbeVertex(*buffer, globals, pass, i);
        if (!matches(gpuSide[i], want)) {
            if (disagreements < 3) {
                std::printf("  FAIL pass=%u i=%u\n    gpu pos %.9g %.9g  line %.9g %.9g %.9g %.9g"
                            "  v %.9g\n    cpu pos %.9g %.9g  line %.9g %.9g %.9g %.9g  v %.9g\n",
                            static_cast<unsigned>(pass), i, gpuSide[i].position[0],
                            gpuSide[i].position[1], gpuSide[i].line[0], gpuSide[i].line[1],
                            gpuSide[i].line[2], gpuSide[i].line[3], gpuSide[i].extra[0],
                            want.position[0], want.position[1], want.line[0], want.line[1],
                            want.line[2], want.line[3], want.extra[0]);
            }
            ++disagreements;
        }
    }
    CHECK_EQ(disagreements, 0);
}

// Every pass over one shape. The packings differ -- 32, 21 and 13 indices per
// instance -- so a shape that sits inside one instance for one pass can straddle
// a boundary in another.
void agreeOnAllPasses(const char* d_, int subdivisions = 4) {
    agreeOn(d_, PathPass::Edges, subdivisions);
    agreeOn(d_, PathPass::Interior, subdivisions);
    agreeOn(d_, PathPass::Exterior, subdivisions);
}

PathBuffer buildOrDie(const char* d_, int subdivisions) {
    auto parsed = parsePath(d_);
    if (!parsed) return PathBuffer{};
    BuildOptions o;
    o.subdivisions = subdivisions;
    auto b = buildPathBuffer(*parsed, o);
    return b ? std::move(*b) : PathBuffer{};
}

}  // namespace

TEST_CASE(gpu_and_cpu_agree_on_a_straight_line) { agreeOnAllPasses("M0 0 L100 0"); }

TEST_CASE(gpu_and_cpu_agree_on_a_cubic) { agreeOnAllPasses("M10 10 C40 0 60 80 90 30"); }

// More than one instance in every packing, which is what makes the index
// arithmetic and the binary search do any work at all.
TEST_CASE(gpu_and_cpu_agree_across_several_instances) {
    agreeOnAllPasses("M0 0 C10 20 30 40 50 0 C70 -40 90 40 110 0 C130 -20 150 20 170 0", 16);
}

// Several subpaths, so the non-finite p1.x branch is taken and some vertices
// come back clipped away.
TEST_CASE(gpu_and_cpu_agree_with_subpath_breaks) {
    agreeOnAllPasses("M0 0 L20 0 M40 0 L60 20 M80 80 C90 90 100 70 110 80", 8);
}

// THE UGLY NUMBERS ARE THE POINT -- do not round them.
//
// The target factors the Bezier as `u3*p0 + (3tu)*(u*p1 + t*p2) + t3*p3`, and
// PathVertex.glsl transcribes that order rather than the textbook expansion.
// The mutation sweep tried the textbook form and SURVIVED: with coordinates like
// 0, 10, 50 and 90 the two orders land on the same float every time.
//
// Measured over 620 000 random quadruples at the t values these tests use, the
// two forms disagree in 21.7% of cases -- so the claim is true and the paths
// above were simply too kind. These coordinates disagree at t = 0.375 and
// t = 0.625. Rounding them would put the claim back to being unproven.
TEST_CASE(gpu_and_cpu_agree_where_the_factoring_order_is_visible) {
    agreeOnAllPasses("M12.3457 -87.6543 C143.219 -8.7654 -76.5432 191.357 88.8888 -33.3333", 8);
}

TEST_CASE(gpu_and_cpu_agree_on_a_closed_shape) {
    agreeOnAllPasses("M0 0 L50 0 L50 50 L0 50 Z", 8);
}

// A vertex past the declared total must land outside the clip volume, which is
// how the target drops the tail of the last instance without a branch.
TEST_CASE(a_vertex_past_the_total_is_placed_outside_the_clip_volume) {
    Device& d = gpu();
    REQUIRE(d.valid());
    PathBuffer buffer = buildOrDie("M0 0 L10 0", 4);
    REQUIRE(!buffer.entries.empty());
    CHECK_EQ(buffer.vertexCount(), 4);

    const auto gpuSide = onGpu(d, buffer, testGlobals(), PathPass::Edges);
    REQUIRE(gpuSide.size() == static_cast<std::size_t>(64));

    // vid 0..7 cover vertex indices 0..3 -- drawn. vid 8 is index 4, past the end.
    ProbeVertex away;
    away.position[0] = -2.0f;
    away.position[1] = -2.0f;
    away.position[3] = 1.0f;
    CHECK(!(gpuSide[0] == away));
    CHECK(gpuSide[8] == away);
    CHECK(gpuSide[63] == away);
}

// THE TOLERANCE HAS TO BITE, and until this test existed nothing proved it did.
//
// The sweep replaced the ULP comparison with `return true` and the suite stayed
// GREEN -- because disabling a check that currently passes breaks nothing. A
// bound nobody tests is a bound that is not there: it would accept a slope that
// is wrong by a mile just as happily as one that is wrong by a bit.
//
// So: one ULP is accepted, because that is what the driver's approximate divide
// actually costs (PathVertexOracle.h carries the measurement). Anything past the
// bound is rejected.
TEST_CASE(the_ulp_bound_accepts_a_drivers_divide_and_rejects_a_wrong_slope) {
    ProbeVertex a;
    a.line[0] = 1.0f;
    a.line[1] = 2.0f;
    a.line[2] = 1.19575274f;   // the slope the GPU actually returned
    a.line[3] = 154.926147f;

    // One ULP away: what the divide costs, and it must pass.
    ProbeVertex oneUlp = a;
    oneUlp.line[2] = std::nextafter(a.line[2], 2.0f);
    CHECK_EQ(ulpsApart(a.line[2], oneUlp.line[2]), 1u);
    CHECK(matches(a, oneUlp));

    // Five ULP away: past the bound, and it must NOT.
    ProbeVertex fiveUlp = a;
    for (int i = 0; i < 5; ++i) fiveUlp.line[2] = std::nextafter(fiveUlp.line[2], 2.0f);
    CHECK_EQ(ulpsApart(a.line[2], fiveUlp.line[2]), 5u);
    CHECK(!matches(a, fiveUlp));

    // A slope wrong by a percent -- the shape of every mutation in the sweep.
    ProbeVertex wrong = a;
    wrong.line[2] = a.line[2] * 1.01f;
    CHECK(!matches(a, wrong));

    // And the intercept is held to the same bound.
    ProbeVertex badIntercept = a;
    badIntercept.line[3] = a.line[3] + 1.0f;
    CHECK(!matches(a, badIntercept));
}

// The tolerance is for the divide ONLY. Everything else is bit-exact, and a
// bound that leaked into those fields would hide a real error.
TEST_CASE(the_tolerance_does_not_reach_position_or_path_y) {
    ProbeVertex a;
    a.position[0] = 0.5f;
    a.line[0] = 3.0f;
    a.extra[0] = 1.0f;

    ProbeVertex nudged = a;
    nudged.position[0] = std::nextafter(a.position[0], 1.0f);
    CHECK(!matches(a, nudged));

    ProbeVertex nudgedY = a;
    nudgedY.line[0] = std::nextafter(a.line[0], 4.0f);
    CHECK(!matches(a, nudgedY));

    ProbeVertex nudgedValue = a;
    nudgedValue.extra[0] = std::nextafter(a.extra[0], 2.0f);
    CHECK(!matches(a, nudgedValue));
}

TEST_CASE(ulps_apart_counts_representable_floats_not_a_relative_epsilon) {
    CHECK_EQ(ulpsApart(1.0f, 1.0f), 0u);
    CHECK_EQ(ulpsApart(1.0f, std::nextafter(1.0f, 2.0f)), 1u);
    // Near zero a relative test accepts everything; this one does not.
    CHECK(ulpsApart(0.0f, 1e-30f) > 4u);
    // And across the sign, adjacent floats stay adjacent.
    CHECK_EQ(ulpsApart(0.0f, -0.0f), 0u);
}

// A buffer whose header disagrees with its segments is REFUSED, not allocated
// from. This is the guard that a missing check taught: under one of the sweep's
// own mutations the header's vertex total came back as 1 090 519 040 -- the bits
// of the float 8.0 read as an int -- and the harness asked the machine for a
// hundred gigabytes. It got it wrong enough to reboot the machine.
TEST_CASE(a_header_that_lies_about_its_total_is_refused_not_allocated_from) {
    PathBuffer buffer = buildOrDie("M0 0 L10 0 L10 10", 4);
    REQUIRE(!buffer.entries.empty());
    CHECK(headerAgreesWithSegments(buffer));
    CHECK(invocationCount(buffer, PathPass::Edges) > 0u);

    // Exactly the corruption the mutation produces: the total written as a float.
    PathBuffer corrupt = buildOrDie("M0 0 L10 0 L10 10", 4);
    corrupt.entries.front().recip_n = static_cast<float>(corrupt.vertexCount());
    CHECK(headerVertexCount(corrupt.entries.front()) > 1000000);  // the absurd number
    CHECK(!headerAgreesWithSegments(corrupt));
    CHECK_EQ(invocationCount(corrupt, PathPass::Edges), 0u);
}

TEST_CASE(a_header_whose_segment_count_is_wrong_is_refused) {
    PathBuffer buffer = buildOrDie("M0 0 L10 0", 4);
    REQUIRE(!buffer.entries.empty());
    buffer.entries.front().count += 1;
    CHECK(!headerAgreesWithSegments(buffer));
}

// `[BIN]` The interior pass fans from `origin`, and corner 0 IS the apex -- it
// exists whether or not the index it belongs to is in range.
TEST_CASE(the_interior_fan_starts_every_triangle_at_the_origin) {
    Device& d = gpu();
    REQUIRE(d.valid());
    PathBuffer buffer = buildOrDie("M0 0 L40 0 L40 40 Z", 4);
    REQUIRE(!buffer.entries.empty());

    const PathGlobals g = testGlobals();
    const auto gpuSide = onGpu(d, buffer, g, PathPass::Interior);
    REQUIRE(!gpuSide.empty());

    const ProbeVertex apex = pathProbeVertex(buffer, g, PathPass::Interior, 0);
    for (std::uint32_t i = 0; i < gpuSide.size(); i += 4) {
        CHECK(gpuSide[i] == apex);
    }
    CHECK(apex.position[0] != -2.0f);
}

// `[BIN]` A near-horizontal edge is discarded rather than divided by: the
// threshold is float(1e-4). A path of nothing but horizontal lines must produce
// nothing but clipped-away vertices from the exterior pass.
TEST_CASE(the_exterior_pass_drops_a_horizontal_edge) {
    Device& d = gpu();
    REQUIRE(d.valid());
    PathBuffer buffer = buildOrDie("M0 50 L100 50", 4);
    REQUIRE(!buffer.entries.empty());

    const auto gpuSide = onGpu(d, buffer, testGlobals(), PathPass::Exterior);
    REQUIRE(!gpuSide.empty());
    for (const auto& v : gpuSide) {
        CHECK_EQ(v.position[0], -2.0f);
    }
}

// `[BIN]` path_value is the winding direction: +1 for an edge whose world-space
// y increases, -1 for one that decreases.
TEST_CASE(the_exterior_pass_signs_an_edge_by_its_direction) {
    Device& d = gpu();
    REQUIRE(d.valid());
    PathBuffer buffer = buildOrDie("M0 0 L0 60 L20 0", 4);
    REQUIRE(!buffer.entries.empty());

    const auto gpuSide = onGpu(d, buffer, testGlobals(), PathPass::Exterior);
    REQUIRE(!gpuSide.empty());
    int up = 0, down = 0;
    for (const auto& v : gpuSide) {
        if (v.position[0] == -2.0f) continue;
        if (v.extra[0] == 1.0f) ++down;
        if (v.extra[0] == -1.0f) ++up;
    }
    CHECK(down > 0);
    CHECK(up > 0);
}

// `[BIN]` The quad reaches out to `urx` -- that is what bounds "everything to the
// right of this edge", and it is why the pass needs the bound at all. Corners 1
// and 2 sit on it.
TEST_CASE(the_exterior_quad_reaches_the_right_bound) {
    Device& d = gpu();
    REQUIRE(d.valid());
    PathBuffer buffer = buildOrDie("M10 0 L10 60", 4);
    REQUIRE(!buffer.entries.empty());

    const PathGlobals g = testGlobals();
    const auto gpuSide = onGpu(d, buffer, g, PathPass::Exterior);
    REQUIRE(!gpuSide.empty());

    const float rightEdge = (g.urx + 0.5f) * g.twoOverSize[0] + -1.0f;
    int atRight = 0;
    for (std::uint32_t i = 0; i < gpuSide.size(); ++i) {
        if (gpuSide[i].position[0] == -2.0f) continue;
        const std::uint32_t corner = i & 3u;
        if (corner == 1u || corner == 2u) {
            CHECK_EQ(gpuSide[i].position[0], rightEdge);
            ++atRight;
        }
    }
    CHECK(atRight > 0);
}

// `[BIN]` The line is carried as x = slope*y + intercept, with slope = dx/dy --
// the reciprocal of the usual one, because the coverage integral runs along y.
TEST_CASE(the_exterior_line_is_x_as_a_function_of_y) {
    Device& d = gpu();
    REQUIRE(d.valid());
    // Identity transform, so world space is path space and the numbers are
    // readable: a segment from (10,0) to (30,40) has dx/dy = 0.5.
    PathBuffer buffer = buildOrDie("M10 0 L30 40", 1);
    REQUIRE(!buffer.entries.empty());

    PathGlobals g;
    g.urx = 100.0f;
    const auto gpuSide = onGpu(d, buffer, g, PathPass::Exterior);
    REQUIRE(!gpuSide.empty());

    bool checked = false;
    for (const auto& v : gpuSide) {
        if (v.position[0] == -2.0f) continue;
        CHECK_EQ(v.line[2], 0.5f);    // slope = dx/dy
        CHECK_EQ(v.line[3], 10.0f);   // x where y = 0
        CHECK_EQ(v.line[0], 0.0f);    // path_y, low end first
        CHECK_EQ(v.line[1], 40.0f);
        checked = true;
        break;
    }
    CHECK(checked);
}
