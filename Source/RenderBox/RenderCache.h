// RenderCache -- what one render leaves for the next.
//
// `Docs/Laudos/2026-09-29-perfil-do-render.md` §3.3: between two edits of the
// same document almost nothing a layer costs changes. An opacity or colour edit
// to one group leaves the art, the placement and the size of every other layer
// alone, and those are all the SVG render, the distance field and the shadow
// read. Before this, `renderIcon` threw all of it away and paid it again.
//
// THE KEY IS THE CONTENT, NOT A GUESS ABOUT WHAT AN EDIT TOUCHED. Every cached
// step is a pure function of its inputs, and its key is a 128-bit hash of ALL
// of them -- the art's bytes, the images it reads, every field of every
// parameter struct. So a hit is the same inputs, and the same inputs give the
// same output: the cache cannot serve a stale picture without a hash collision,
// and it does not have to know which document keys feed which step. Hashing a
// 1024 px buffer costs a few milliseconds; the steps it stands in for cost tens
// to hundreds.
//
// The cost of that design is a hasher per parameter struct that has to name
// every field. `IconRenderer.cpp` pins each struct's size with a static_assert
// beside its hasher, so a field added to one of them stops the build there
// instead of quietly dropping out of the key.
//
// One render at a time. `JobScheduler` never runs two, but the one it runs
// moves between worker threads, so the table is locked anyway; the lock is
// taken per lookup, never around a step.
#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace rb {

struct CacheKey {
    std::uint64_t a = 0;
    std::uint64_t b = 0;
    bool operator==(const CacheKey&) const = default;
};

// 128 bits over everything fed to it, in order. Not cryptographic: nothing
// here is adversarial, and 128 bits put an accidental collision out of reach.
class KeyHasher {
public:
    // The domain separates the steps, so a field and a shadow built from the
    // same bytes can never answer for each other.
    explicit KeyHasher(std::string_view domain);

    KeyHasher& bytes(const void* data, std::size_t size);

    // Numbers and enums only: a struct would hash its padding, which is not
    // part of its value. Structs go through a hasher that names each field.
    template <class T>
    KeyHasher& value(const T& v) {
        static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>,
                      "hash a struct field by field, not by its bytes");
        return bytes(&v, sizeof v);
    }

    template <class T>
    KeyHasher& span(const T* data, std::size_t count) {
        static_assert(std::is_arithmetic_v<T>, "a span of numbers");
        value(count);
        return bytes(data, count * sizeof(T));
    }

    CacheKey finish() const;

private:
    std::uint64_t lane_[4];
    std::uint64_t length_ = 0;
};

class RenderCache {
public:
    // A heavy document at 1024 px leaves ~50 MB per glass layer (the SVG
    // render, the field and the shadow, a float4 buffer each); the heaviest
    // one in the corpus has seven such layers.
    static constexpr std::size_t kDefaultBudget = std::size_t{768} << 20;

    explicit RenderCache(std::size_t budgetBytes = kDefaultBudget) : budget_(budgetBytes) {}

    RenderCache(const RenderCache&) = delete;
    RenderCache& operator=(const RenderCache&) = delete;

    template <class T>
    std::shared_ptr<const T> find(const CacheKey& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(key);
        if (it == entries_.end() || it->second.type != std::type_index(typeid(T))) {
            ++stats_.misses;
            return nullptr;
        }
        ++stats_.hits;
        order_.splice(order_.begin(), order_, it->second.use);
        return std::static_pointer_cast<const T>(it->second.value);
    }

    // `bytes` is what the value holds, for the budget. An entry bigger than the
    // whole budget is handed back and not kept.
    template <class T>
    std::shared_ptr<const T> store(const CacheKey& key, T value, std::size_t bytes) {
        auto kept = std::make_shared<const T>(std::move(value));
        std::lock_guard<std::mutex> lock(mutex_);
        if (bytes > budget_) return kept;
        auto it = entries_.find(key);
        if (it != entries_.end()) {
            held_ -= it->second.bytes;
            order_.erase(it->second.use);
            entries_.erase(it);
        }
        order_.push_front(key);
        entries_.emplace(key, Entry{kept, std::type_index(typeid(T)), bytes, order_.begin()});
        held_ += bytes;
        evict();
        return kept;
    }

    struct Stats {
        std::size_t hits = 0;
        std::size_t misses = 0;
    };
    Stats stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }
    std::size_t heldBytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return held_;
    }
    std::size_t entries() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }

private:
    struct KeyHash {
        std::size_t operator()(const CacheKey& k) const noexcept {
            return static_cast<std::size_t>(k.a ^ (k.b * 0x9E3779B97F4A7C15ull));
        }
    };
    struct Entry {
        std::shared_ptr<const void> value;
        std::type_index type;
        std::size_t bytes;
        std::list<CacheKey>::iterator use;
    };

    void evict();

    mutable std::mutex mutex_;
    std::size_t budget_;
    std::size_t held_ = 0;
    std::list<CacheKey> order_;   // most recently used first
    std::unordered_map<CacheKey, Entry, KeyHash> entries_;
    Stats stats_;
};

}  // namespace rb
