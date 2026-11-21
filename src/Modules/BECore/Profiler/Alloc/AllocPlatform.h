#pragma once

#include <cstddef>

namespace BECore {

    /**
     * @brief The C allocator under the replaced global operator new / delete (ENGINE_PROFILER_ALLOC, and on Windows every build).
     *
     * No header goes in front of a block: plain and over-aligned blocks are both freed by Free(), whichever operator delete is
     * called. That keeps a mismatched pair harmless (EASTL allocates over-aligned arrays with the aligned operator new[] and frees
     * them with the plain operator delete[]). On macOS and Linux an aligned block is posix_memalign memory, which free() takes; on
     * Windows every block, plain or aligned, is _aligned_malloc memory, freed by _aligned_free, because the MSVC runtime's plain
     * operator delete (free) does not take an aligned block.
     */
    class AllocPlatform {
    public:
        AllocPlatform() = delete;

        /// nullptr when the system has no memory
        [[nodiscard]] static void* Allocate(size_t size);
        [[nodiscard]] static void* AllocateAligned(size_t size, size_t alignment);
        static void Free(void* ptr);
        /// The usable size of a live block of Allocate / AllocateAligned (at least what was asked for). Only AllocTracker calls it. On
        /// Windows it reads the release CRT's block header, which the Debug CRT's _aligned_malloc does not write: valid only in a
        /// build with ENGINE_PROFILER_ALLOC (a release-runtime preset), never in a plain Debug build.
        [[nodiscard]] static size_t UsableSize(void* ptr);
        /// The usable size of a live block of plain malloc / calloc / realloc (AllocRouting). Unlike UsableSize it is the system's own
        /// measure on every platform, Windows included
        [[nodiscard]] static size_t LibcUsableSize(void* ptr);
    };

}  // namespace BECore
