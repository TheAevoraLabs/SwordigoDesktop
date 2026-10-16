#pragma once
// ============================================================================
// parallel_for.h — a BOUNDED parallel-for for scene loading (shared base).
//
// Why this exists: the desktop scene loader used to fan out one
// std::async(std::launch::async, ...) per texture and per model. std::async
// with launch::async is NOT pooled — it creates a fresh OS thread every call —
// so a level with a few hundred unique ground-mesh textures spawned a few
// hundred threads at once. On a low-core machine (or one with a slow
// filesystem, where each worker then blocks on I/O) that is the "the world
// sometimes takes so long to load" report: thread creation + memory + context
// switching dwarf the actual decode work.
//
// The mobile loader already avoided this with its own bounded pool; this header
// is that helper, shared so Ruby GG and Ruby Touch load scenes the same way and
// neither can regress to an unbounded fan-out.
//
// Contract: `fn(i)` is invoked exactly once for every i in [0, count), across at
// most `max_threads` workers. It MUST be safe to run concurrently for different
// i (each worker owns its own output slot — no shared mutable state). Blocks
// until all work is done.
// ============================================================================

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <functional>
#include <thread>
#include <vector>

namespace ruby::tools {

/// Run `fn(i)` for i in [0, count) on at most `max_threads` worker threads.
/// max_threads == 0 means "use hardware_concurrency()".
inline void parallel_for(size_t count, unsigned max_threads,
                         const std::function<void(size_t)>& fn) {
    if (count == 0) return;
    const unsigned hw = std::thread::hardware_concurrency();
    unsigned n = std::min<unsigned>(max_threads ? max_threads : (hw ? hw : 1u),
                                    static_cast<unsigned>(count));
    if (n < 1u) n = 1u;
    if (n == 1u) {                 // tiny jobs (and single-core boxes): no threads
        for (size_t i = 0; i < count; ++i) fn(i);
        return;
    }
    std::atomic<size_t> cursor{0};
    std::vector<std::thread> pool;
    pool.reserve(n);
    for (unsigned t = 0; t < n; ++t) {
        pool.emplace_back([&]() {
            for (;;) {
                const size_t i = cursor.fetch_add(1, std::memory_order_relaxed);
                if (i >= count) return;
                fn(i);
            }
        });
    }
    for (auto& th : pool) th.join();
}

/// Thread budget for a scene load. Bounded (not hardware_concurrency) on
/// purpose: the work is I/O-heavy (file reads, decodes) and oversubscribing a
/// slow disk makes it slower, not faster. Six matches the mobile loader.
inline constexpr unsigned kSceneLoadThreads = 6;

} // namespace ruby::tools
