#pragma once

#include <BECore/Memory/LinearArena.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /**
     * @brief The calling thread's frame arena: memory handed on inside a frame and given back after the present, all at once.
     *
     * A `constinit thread_local LinearArena` like ScratchArena, not a service: it works on any thread and allocates nothing until its
     * first Allocate. Memory of one thread's arena must not be read by another (the arena is not thread-safe).
     *
     * - **The frame number** is one global counter. Application calls Advance() once per frame, after the present.
     * - **The main thread resets at once** (inside Advance), **every other thread lazily:** Get() compares the frame its arena was last used
     *   in with the current number and resets first when they differ. An arena may be reset only by its owner, and a pool thread cannot be
     *   reached without a lock or a hook in the pool; a sleeping thread holds nothing it must give back. So the counters of an idle thread's
     *   arena show the last frame it worked in.
     * - **A task that can outlive the frame must not use the frame arena** (a delayed or background task, anything TaskManager keeps past the
     *   call): its pool thread resets the arena at its next Get() in a later frame, under the task's data. FrameAllocator asserts a container
     *   grown in a later frame or on another thread (Debug).
     */
    class FrameArena {
    public:
        /// The size of a chunk; the first one is made by the thread's first Allocate, so a thread that never uses the arena pays nothing.
        /// It is not a limit: a frame that needs more takes further chunks, counts one overflow each the first time and keeps them. It is
        /// sized so that a screen of about ten boxes with a blurred shadow (the biggest user, about 100 KB each with the growth of its lists)
        /// fits in one chunk
        static constexpr size_t kChunkSize = 1024 * 1024;

        FrameArena() = delete;

        /// The calling thread's arena, reset first when the frame it was last used in has ended
        [[nodiscard]] static LinearArena& Get();

        /// The number of the running frame; starts at 1
        [[nodiscard]] static uint64_t GetFrameNumber();

        /// The frame is over: starts the next one and resets the calling thread's arena. Called by the main thread after the present
        static void Advance();
    };

}  // namespace BECore
