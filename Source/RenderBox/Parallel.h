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
//
// THE THREADS ARE KEPT, NOT CREATED PER CALL (29/09). `[INF]` Measured on the
// machine above: creating and joining fifteen `std::thread`s costs ~0.8 ms per
// call, and a 1024 px render of the Apollo document makes well over a hundred
// calls. The pool below starts its workers once and hands them the SAME ranges
// the loop above used to hand fresh threads, so the split -- and therefore
// every number -- is exactly what it was. A caller that is waiting runs queued
// ranges itself instead of sleeping, which is what keeps a `parallelRanges`
// issued from inside another one (or from two render threads at once) from
// waiting on workers that are all waiting too.
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <system_error>
#include <thread>
#include <type_traits>

namespace rb {

// `hardware_concurrency` is allowed to answer 0 when it does not know. A floor
// of 1 turns that into "run it here", which is also what a single-core machine
// gets, so neither case needs a second code path.
inline unsigned parallelThreadCount() {
    const unsigned n = std::thread::hardware_concurrency();
    return n > 0 ? n : 1;
}

inline constexpr std::size_t kParallelChunkWork = 262144;

namespace detail {

// One range of one `parallelRanges` call.
struct ParallelTask {
    void (*run)(void* body, std::size_t begin, std::size_t end);
    void* body;
    std::size_t begin;
    std::size_t end;
    std::atomic<std::size_t>* pending;
};

class ParallelPool {
public:
    // Leaked on purpose: its workers sleep for the life of the process, and
    // joining them from a static destructor would race the runtime's own exit.
    static ParallelPool& instance() {
        static ParallelPool* pool = new ParallelPool(parallelThreadCount() - 1);
        return *pool;
    }

    void push(const ParallelTask& task) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(task);
        }
        cv_.notify_all();
    }

    // Returns once every task counted by `pending` has run, running queued
    // tasks -- its own or anybody's -- while it waits.
    void wait(std::atomic<std::size_t>& pending) {
        std::unique_lock<std::mutex> lock(mutex_);
        while (pending.load(std::memory_order_acquire) != 0) {
            if (!queue_.empty()) {
                const ParallelTask task = queue_.front();
                queue_.pop_front();
                lock.unlock();
                execute(task);
                lock.lock();
                continue;
            }
            cv_.wait(lock, [&] {
                return pending.load(std::memory_order_acquire) == 0 || !queue_.empty();
            });
        }
    }

private:
    explicit ParallelPool(unsigned workers) {
        for (unsigned i = 0; i < workers; ++i) {
            // A machine that cannot give out another thread must still draw
            // the picture: fewer workers, and the waiting caller runs what
            // they do not.
            try {
                std::thread([this] { loop(); }).detach();
            } catch (const std::system_error&) {
                break;
            }
        }
    }

    void execute(const ParallelTask& task) {
        task.run(task.body, task.begin, task.end);
        if (task.pending->fetch_sub(1, std::memory_order_acq_rel) == 1) {
            // Taking the lock orders this wake after the waiter's check.
            { std::lock_guard<std::mutex> lock(mutex_); }
            cv_.notify_all();
        }
    }

    void loop() {
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            cv_.wait(lock, [&] { return !queue_.empty(); });
            const ParallelTask task = queue_.front();
            queue_.pop_front();
            lock.unlock();
            execute(task);
            lock.lock();
        }
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<ParallelTask> queue_;
};

}  // namespace detail

// Splits `[0, count)` into contiguous ranges and calls `body(begin, end)` once
// per range. The LAST range runs on the calling thread, and the others go to
// the pool's workers.
//
// `work` is the caller's estimate of the scalar operations the whole loop does
// -- pixels times the cost of a pixel, in whatever unit the caller finds
// natural, as long as it is roughly one machine operation. It decides HOW MANY
// ranges there are, and when there is not enough of it the whole loop runs on
// the calling thread with nothing queued at all. That serial path is not a
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

    using BodyT = std::remove_reference_t<Body>;
    detail::ParallelPool& pool = detail::ParallelPool::instance();
    std::atomic<std::size_t> pending{chunks - 1};
    const std::size_t base = count / chunks;
    const std::size_t rest = count % chunks;
    std::size_t begin = 0;
    for (std::size_t c = 0; c < chunks; ++c) {
        // The first `rest` ranges get one extra item, so the split is even to
        // within a single item however `count` divides.
        const std::size_t end = begin + base + (c < rest ? 1 : 0);
        if (c + 1 == chunks) {
            body(begin, end);
        } else {
            pool.push(detail::ParallelTask{
                [](void* b, std::size_t x0, std::size_t x1) { (*static_cast<BodyT*>(b))(x0, x1); },
                const_cast<void*>(static_cast<const void*>(&body)), begin, end, &pending});
        }
        begin = end;
    }
    pool.wait(pending);
}

}  // namespace rb
