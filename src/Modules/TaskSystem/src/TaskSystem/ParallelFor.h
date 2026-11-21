#pragma once

#include <BECore/Utils/FunctionRef.h>
#include <TaskSystem/ThreadPool.h>
#include <cstddef>

namespace BECore {

    /**
     * Runs `body(begin, end)` over the chunks of `[0, count)` on the workers of @p pool and on the calling thread, and returns when every chunk is done.
     * A fork-join over a pool for a pass with no task, handle or coroutine: no heap allocation in a steady call (`TaskSystem/CLAUDE.md`, "ParallelFor").
     *
     * - **Inline.** `count == 0` does nothing. An inline pool, a pool without workers, a stopped pool and `count < 2 * minChunk` call `body(0, count)` once on the calling thread
     *   and submit no job.
     * - **Chunks.** Otherwise there are `min(count / minChunk, 2 * (workers + 1))` chunks (at least 2, `minChunk` 0 counts as 1), so a chunk holds at least `minChunk` indices.
     *   They are a partition of `[0, count)` into runs of `count / chunks` or one more (the first `count % chunks` chunks are the longer ones), so the ranges depend only on
     *   `count`, `minChunk` and the worker count, never on timing; which thread runs which chunk does depend on timing.
     * - **Who runs them.** The caller submits `min(workers, chunks - 1)` helper jobs and claims chunks in the same loop as they do. When none is left it waits (yielding)
     *   only for the chunks others have claimed and not finished, never on the pool, so a pool whose workers are all busy still finishes by the caller's own chunks, and a call from
     *   inside a chunk or a pool job works. A helper that starts late finds nothing to claim and never touches @p body or the caller's stack.
     * - **The body** runs on several threads at once: it must write only the data of its own range `[begin, end)`, read what no chunk writes, and never block on the caller or on
     *   another chunk. Each chunk is a profiler zone `ParallelFor chunk`.
     *
     * @example
     * ParallelFor(pool, particles.size(), 256, [&](size_t begin, size_t end) { Simulate(begin, end); });
     *
     * @param minChunk the smallest worth of one chunk: below `2 * minChunk` indices the call stays on the caller
     */
    void ParallelFor(ThreadPool& pool, size_t count, size_t minChunk, FunctionRef<void(size_t begin, size_t end)> body);

}  // namespace BECore
