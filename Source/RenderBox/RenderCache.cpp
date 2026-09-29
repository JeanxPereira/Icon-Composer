#include "Source/RenderBox/RenderCache.h"

#include <cstring>

namespace rb {

namespace {

constexpr std::uint64_t kPrime1 = 0x9E3779B185EBCA87ull;
constexpr std::uint64_t kPrime2 = 0xC2B2AE3D27D4EB4Full;
constexpr std::uint64_t kPrime3 = 0x165667B19E3779F9ull;

std::uint64_t rotl(std::uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }

// One xxHash64 round: it is what keeps four independent lanes fast over a
// 16 MB buffer, which is the size this hashes on every lookup.
std::uint64_t round(std::uint64_t acc, std::uint64_t word) {
    acc += word * kPrime2;
    acc = rotl(acc, 31);
    return acc * kPrime1;
}

std::uint64_t avalanche(std::uint64_t h) {
    h ^= h >> 33;
    h *= kPrime2;
    h ^= h >> 29;
    h *= kPrime3;
    h ^= h >> 32;
    return h;
}

}  // namespace

KeyHasher::KeyHasher(std::string_view domain)
    : lane_{kPrime1 + kPrime2, kPrime2, 0, 0 - kPrime1} {
    bytes(domain.data(), domain.size());
    // The length closes the domain, so "ab" + "c" and "a" + "bc" differ.
    value(domain.size());
}

KeyHasher& KeyHasher::bytes(const void* data, std::size_t size) {
    // Every call is framed by its own length and restarts the lane rotation,
    // so the split of a stream into calls is part of the key and two
    // different field lists cannot run together into the same bytes.
    const auto* p = static_cast<const unsigned char*>(data);
    std::size_t i = 0;
    for (; i + 32 <= size; i += 32) {
        std::uint64_t w[4];
        std::memcpy(w, p + i, 32);
        lane_[0] = round(lane_[0], w[0]);
        lane_[1] = round(lane_[1], w[1]);
        lane_[2] = round(lane_[2], w[2]);
        lane_[3] = round(lane_[3], w[3]);
    }
    for (int k = 0; i < size; ++k) {
        std::uint64_t w = 0;
        const std::size_t n = size - i < 8 ? size - i : 8;
        std::memcpy(&w, p + i, n);
        lane_[k & 3] = round(lane_[k & 3], w ^ (static_cast<std::uint64_t>(n) << 56));
        i += n;
    }
    lane_[0] = round(lane_[0], static_cast<std::uint64_t>(size));
    length_ += size;
    return *this;
}

CacheKey KeyHasher::finish() const {
    const std::uint64_t mixed = rotl(lane_[0], 1) + rotl(lane_[1], 7) + rotl(lane_[2], 12) +
                                rotl(lane_[3], 18) + length_;
    CacheKey k;
    k.a = avalanche(mixed ^ lane_[0] ^ (lane_[2] * kPrime3));
    k.b = avalanche(mixed + lane_[1] + (lane_[3] * kPrime1));
    return k;
}

void RenderCache::evict() {
    while (held_ > budget_ && !order_.empty()) {
        auto it = entries_.find(order_.back());
        held_ -= it->second.bytes;
        entries_.erase(it);
        order_.pop_back();
    }
}

}  // namespace rb
