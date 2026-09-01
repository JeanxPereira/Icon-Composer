#include "check.h"
#include "Source/CoreSVG/Path.h"
#include "Source/RenderBox/Compute.h"
#include "Source/RenderBox/PathBuffer.h"
#include "Source/RenderBox/PathVertexOracle.h"

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

// The push-constant block of path_probe.comp: PathGlobals (52 bytes) followed by
// how many vertex ids to evaluate.
struct ProbeConstants {
    PathGlobals globals;
    std::uint32_t vertexPairs = 0;
};
static_assert(sizeof(ProbeConstants) == 56, "52 bytes of globals plus one uint");

// Runs the shader over every vertex id the buffer needs and returns what the GPU
// computed.
std::vector<ClipPosition> onGpu(Device& d, const PathBuffer& buffer, const PathGlobals& globals) {
    const std::uint32_t count = vertexIdCount(buffer);
    if (count == 0) return {};

    auto segments = Buffer::create(d, buffer.entries.size() * sizeof(CubicSegment),
                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!segments) return {};
    std::memcpy(segments->mapped(), buffer.entries.data(),
                buffer.entries.size() * sizeof(CubicSegment));

    auto out = Buffer::create(d, count * sizeof(ClipPosition),
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!out) return {};
    std::memset(out->mapped(), 0, count * sizeof(ClipPosition));

    auto pass = ComputePass::create(d, kPathProbeSpirv, sizeof kPathProbeSpirv,
                                    sizeof(ProbeConstants));
    if (!pass) {
        std::printf("  FAIL compute pass: %s\n", pass.error().c_str());
        ++ictest::failures();
        return {};
    }

    ProbeConstants pc;
    pc.globals = globals;
    pc.vertexPairs = count;
    auto ran = pass->run(d, *segments, *out, &pc, sizeof pc, count);
    if (!ran) {
        std::printf("  FAIL dispatch: %s\n", ran.error().c_str());
        ++ictest::failures();
        return {};
    }

    std::vector<ClipPosition> positions(count);
    std::memcpy(positions.data(), out->mapped(), count * sizeof(ClipPosition));
    return positions;
}

// The gate: the same arithmetic, twice, on two machines, required to agree
// EXACTLY. Not "close enough" -- see PathVertexOracle.h for why a tolerance
// would be the thing that lets a real error through.
void agreeOn(const char* d_, int subdivisions = 4) {
    Device& d = gpu();
    if (!d.valid()) return;

    auto parsed = parsePath(d_);
    REQUIRE(parsed.has_value());
    BuildOptions o;
    o.subdivisions = subdivisions;
    auto buffer = buildPathBuffer(*parsed, o);
    REQUIRE(buffer.has_value());

    PathGlobals globals;
    globals.m0[0] = 1.5f;
    globals.m1[1] = 1.25f;
    globals.m2[0] = 3.0f;
    globals.m2[1] = -2.0f;
    globals.depth = 1024.0f;

    const auto gpuSide = onGpu(d, *buffer, globals);
    REQUIRE(!gpuSide.empty());

    int disagreements = 0;
    for (std::uint32_t i = 0; i < gpuSide.size(); ++i) {
        const ClipPosition want = pathEdgeVertex(*buffer, globals, i % 64u, i / 64u);
        if (!(gpuSide[i] == want)) {
            if (disagreements < 3) {
                std::printf("  FAIL vid=%u iid=%u\n    gpu: %.9g %.9g %.9g %.9g\n"
                            "    cpu: %.9g %.9g %.9g %.9g\n",
                            i % 64u, i / 64u, gpuSide[i].x, gpuSide[i].y, gpuSide[i].z,
                            gpuSide[i].w, want.x, want.y, want.z, want.w);
            }
            ++disagreements;
        }
    }
    CHECK_EQ(disagreements, 0);
}

}  // namespace

TEST_CASE(gpu_and_cpu_agree_on_a_straight_line) { agreeOn("M0 0 L100 0"); }

TEST_CASE(gpu_and_cpu_agree_on_a_cubic) { agreeOn("M10 10 C40 0 60 80 90 30"); }

// More than 32 vertices, so more than one instance -- which is what makes the
// (iid * 32 + vid/2) index and the binary search do any work at all.
TEST_CASE(gpu_and_cpu_agree_across_several_instances) {
    agreeOn("M0 0 C10 20 30 40 50 0 C70 -40 90 40 110 0 C130 -20 150 20 170 0", 16);
}

// Several subpaths, so the non-finite p1.x branch is taken and some vertices
// come back clipped away.
TEST_CASE(gpu_and_cpu_agree_with_subpath_breaks) {
    agreeOn("M0 0 L20 0 M40 0 L60 20 M80 80 C90 90 100 70 110 80", 8);
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
    agreeOn("M12.3457 -87.6543 C143.219 -8.7654 -76.5432 191.357 88.8888 -33.3333", 8);
}

TEST_CASE(gpu_and_cpu_agree_on_a_closed_shape) {
    agreeOn("M0 0 L50 0 L50 50 L0 50 Z", 8);
}

// A vertex past the declared total must land outside the clip volume, which is
// how the target drops the tail of the last instance without a branch.
TEST_CASE(a_vertex_past_the_total_is_placed_outside_the_clip_volume) {
    Device& d = gpu();
    REQUIRE(d.valid());
    auto parsed = parsePath("M0 0 L10 0");
    REQUIRE(parsed.has_value());
    BuildOptions o;
    o.subdivisions = 4;
    auto buffer = buildPathBuffer(*parsed, o);
    REQUIRE(buffer.has_value());
    CHECK_EQ(buffer->vertexCount(), 4);

    PathGlobals globals;
    const auto gpuSide = onGpu(d, *buffer, globals);
    REQUIRE(gpuSide.size() == static_cast<std::size_t>(64));

    // vid 0..7 cover vertex indices 0..3 -- drawn. vid 8 is index 4, past the end.
    const ClipPosition away{-2.0f, -2.0f, 0.0f, 1.0f};
    CHECK(!(gpuSide[0] == away));
    CHECK(gpuSide[8] == away);
    CHECK(gpuSide[63] == away);
}
