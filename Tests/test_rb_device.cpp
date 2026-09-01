#include "check.h"
#include "Source/RenderBox/Buffer.h"
#include "Source/RenderBox/Device.h"

#include <cstring>
#include <vector>

using namespace rb;

namespace {

// The device is built once and shared. Creating a Vulkan device per test case is
// seconds of driver work for no coverage: every case here exercises what the
// device DOES, not that it can be made twice.
Device& gpu() {
    static Device* d = [] {
        auto made = Device::create();
        // A machine with no Vulkan device FAILS the suite; it does not skip it.
        // Same rule as the corpus: a gate that goes green because it could not
        // look is the failure mode this project exists to avoid.
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

}  // namespace

TEST_CASE(device_creates_and_names_the_adapter) {
    Device& d = gpu();
    REQUIRE(d.valid());
    // The name is not decoration: when a render differs between machines, the
    // first question is which adapter drew it.
    CHECK(!d.name().empty());
    CHECK(d.handle() != VK_NULL_HANDLE);
    CHECK(d.queue() != VK_NULL_HANDLE);
}

TEST_CASE(a_host_visible_buffer_round_trips) {
    Device& d = gpu();
    REQUIRE(d.valid());
    auto buf = Buffer::create(d, 256, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    REQUIRE(buf.has_value());
    REQUIRE(buf->mapped() != nullptr);

    std::vector<std::uint8_t> sent(256);
    for (std::size_t i = 0; i < sent.size(); ++i) sent[i] = static_cast<std::uint8_t>(i * 7 + 1);
    std::memcpy(buf->mapped(), sent.data(), sent.size());

    std::vector<std::uint8_t> back(256);
    std::memcpy(back.data(), buf->mapped(), back.size());
    CHECK(back == sent);
}

// The one that proves the tower actually talks to the GPU: the bytes go to
// DEVICE-LOCAL memory, which the CPU cannot address, and come back only because
// a command buffer was recorded, submitted and waited on.
TEST_CASE(a_device_local_buffer_round_trips_through_staging) {
    Device& d = gpu();
    REQUIRE(d.valid());
    constexpr VkDeviceSize kSize = 1024;

    auto onDevice = Buffer::create(d, kSize,
                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    REQUIRE(onDevice.has_value());
    CHECK(onDevice->mapped() == nullptr);  // device-local is not addressable from here

    std::vector<std::uint8_t> sent(kSize);
    for (std::size_t i = 0; i < sent.size(); ++i) sent[i] = static_cast<std::uint8_t>(i ^ 0x5A);

    auto up = upload(d, *onDevice, sent.data(), sent.size());
    REQUIRE(up.has_value());

    auto back = download(d, *onDevice);
    REQUIRE(back.has_value());
    CHECK(*back == sent);
}

// A refusal has to say what it refused. `std::optional` is the convention in the
// towers below this one, and it is the wrong one here: "could not allocate" with
// no reason is a bug report nobody can act on.
TEST_CASE(an_impossible_memory_type_is_refused_by_name) {
    Device& d = gpu();
    REQUIRE(d.valid());
    // Every bit set at once: no memory type satisfies lazily-allocated AND
    // host-coherent AND protected on any real adapter.
    const VkMemoryPropertyFlags impossible =
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT |
        VK_MEMORY_PROPERTY_PROTECTED_BIT;
    auto idx = d.memoryTypeIndex(0xFFFFFFFFu, impossible);
    REQUIRE(!idx.has_value());
    CHECK(idx.error().find("memory type") != std::string::npos);
}

TEST_CASE(a_zero_sized_buffer_is_refused_rather_than_allocated) {
    Device& d = gpu();
    REQUIRE(d.valid());
    auto buf = Buffer::create(d, 0, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    REQUIRE(!buf.has_value());
    CHECK(buf.error().find("size") != std::string::npos);
}
