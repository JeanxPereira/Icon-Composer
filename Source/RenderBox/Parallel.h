#pragma once
// THE ONE PARALLEL PRIMITIVE THIS TOWER HAS, AND WHY IT IS THIS SMALL.
//
// Rule 2 of the architecture spec keeps ONYX -- and therefore Onyx's `JobQueue`
// -- out of everything `ic_tests` links, and `Source/RenderBox` is linked by
// `ic_tests`. So the standard library is the whole toolbox: `std::thread`,
// nothing else.
//
// WHAT IT IS FOR. The distance field's two Euclidean transforms sweep COLUMNS
// and then ROWS, and each column of the first pass and each row of the second
// reads only its own line and writes only its own line. That is parallelism
// with no sharing at all, which is the only kind this project can take: a
// reduction done in a different order moves the last bit of a `double`, and
// every pixel this tower draws is gated byte for byte against a stored render.
// A split that merely divides an index range cannot move a bit, because each
// output is computed by exactly the same arithmetic in exactly the same order
// as before -- only on a different thread.
//
// THE FLOOR IS MEASURED, NOT GUESSED, AND THE FIRST VALUE I PICKED WAS WRONG.
// `kParallelChunkWork` is the smallest amount of work a worker must carry
// before the thread pays for itself. It matters because the small grids are
// not hypothetical: the blur's own quality ladder reduces a 128 px render to a
// 32x32 grid and then runs up to 32 passes over it, so a floor that is too low
// spawns dozens of threads to move four thousand texels.
//
// `[INF]` MEASURED, Release, this machine (Ryzen 7 5700X, 8 cores / 16
// threads), `Apollo-Reborn__Apollo-Reborn__AppIcon`, three interleaved runs per
// point, seconds, against a build of this same tree whose floor was set to
// `SIZE_MAX` -- i.e. the serial code, from the same compiler:
//
//   side   serial              floor 65536         floor 262144
//    64    0.057-0.059         0.060-0.063         0.059-0.101
//   128    0.087-0.090         0.101-0.120  WORSE  0.088-0.092
//   256    0.216-0.222         0.195-0.201         0.199-0.201
//   512    0.662-0.671         0.495-0.504         0.498-0.509
//  1024    2.402-2.431         1.658-1.684         1.682-1.689
//
// 65536 units is about 65 us of this kind of work and it LOST 15 % at 128 px.
// 262144 gives the small grids back -- they run serially, exactly as they did
// -- and keeps every bit of the win from 256 px up. That is why the number is
// this one. The full table is in
// `.superpowers/sdd/2026-09-19-paridade/task-7-report.md`.
#include <algorithm>
#include <cstddef>
#include <system_error>
#include <thread>
#include <vector>

namespace rb {

// `hardware_concurrency` is allowed to answer 0 when it does not know. A floor
// of 1 turns that into "run it here", which is also what a single-core machine
// gets, so neither case needs a second code path.
inline unsigned parallelThreadCount() {
    const unsigned n = std::thread::hardware_concurrency();
    return n > 0 ? n : 1;
}

inline constexpr std::size_t kParallelChunkWork = 262144;

// Splits `[0, count)` into contiguous ranges and calls `body(begin, end)` once
// per range. The LAST range runs on the calling thread, so a two-way split
// spawns one thread and not two.
//
// `work` is the caller's estimate of the scalar operations the whole loop does
// -- pixels times the cost of a pixel, in whatever unit the caller finds
// natural, as long as it is roughly one machine operation. It decides HOW MANY
// ranges there are, and when there is not enough of it the whole loop runs on
// the calling thread with no thread created at all. That serial path is not a
// fallback for correctness; it is the same code, and a caller can force it by
// passing `work = 0`.
template <typename Body>
void parallelRanges(std::size_t count, std::size_t work, Body&& body) {
    if (count == 0) return;

    std::size_t chunks = 1;
    if (count > 1 && work >= kParallelChunkWork) {
        const std::size_t byWork = work / kParallelChunkWork;
        chunks = std::min<std::size_t>(parallelThreadCount(), std::min(count, byWork));
        if (chunks < 1) chunks = 1;
    }
    if (chunks <= 1) {
        body(std::size_t{0}, count);
        return;
    }

    const std::size_t base = count / chunks;
    const std::size_t rest = count % chunks;
    std::vector<std::thread> workers;
    workers.reserve(chunks - 1);
    std::size_t begin = 0;
    for (std::size_t c = 0; c < chunks; ++c) {
        // The first `rest` ranges get one extra item, so the split is even to
        // within a single item however `count` divides.
        const std::size_t end = begin + base + (c < rest ? 1 : 0);
        if (c + 1 == chunks) {
            body(begin, end);
        } else {
            // A machine that cannot give out another thread must still draw
            // the picture. `std::thread`'s constructor throws `system_error`
            // when it cannot, and the range runs HERE instead -- the same
            // work, the same numbers, just not concurrently. Letting the
            // exception out would leave the threads already started unjoined,
            // which is `std::terminate`.
            try {
                workers.emplace_back([&body, begin, end] { body(begin, end); });
            } catch (const std::system_error&) {
                body(begin, end);
            }
        }
        begin = end;
    }
    for (std::thread& t : workers) t.join();
}

}  // namespace rb
