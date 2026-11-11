#pragma once

#include <cstddef>

namespace BECore {

    /**
     * @brief The C allocator under the replaced global operator new / delete (ENGINE_PROFILER_ALLOC).
     *
     * No header goes in front of a block: plain and over-aligned blocks are both freed by Free(), whichever operator delete is
     * called. That keeps a mismatched pair harmless (EASTL allocates over-aligned arrays with the aligned operator new[] and frees
     * them with the plain operator delete[]). On macOS and Linux an aligned block is posix_memalign memory, which free() takes; on
     * Windows every block, plain or aligned, is _aligned_malloc memory, freed by _aligned_free.
     */
    class AllocPlatform {
    public:
        AllocPlatform() = delete;

        /// nullptr when the system has no memory
        [[nodiscard]] static void* Allocate(size_t size);
        [[nodiscard]] static void* AllocateAligned(size_t size, size_t alignment);
        static void Free(void* ptr);
        /// The usable size of a live block of Allocate / AllocateAligned (at least what was asked for)
        [[nodiscard]] static size_t UsableSize(void* ptr);
    };

}  // namespace BECore
