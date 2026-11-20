#pragma once

#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <cstddef>
#include <cstdlib>

namespace BECore {

    /**
     * @brief malloc / calloc / realloc / free that report to AllocTracker under an AllocSource (ENGINE_PROFILER_ALLOC).
     *
     * For the libraries that allocate on their own: the include sites of stb and dr_libs and the setters of pugixml, ImGui and SDL
     * (Install) call these. Without ENGINE_PROFILER_ALLOC they are the plain C functions, inline, with no tracker call and no cost.
     *
     * A routed block is a plain libc block, never an AllocPlatform one (on Windows those are _aligned_malloc memory): a block made
     * before the routing started and freed after, or the other way round, still goes to the right free. Its free is then reported
     * without a matching alloc, which the tracker's live-byte counters take (they stop at zero).
     */
    class AllocRouting {
    public:
        AllocRouting() = delete;

        [[nodiscard]] static void* Malloc([[maybe_unused]] AllocSource source, size_t size) {
            if constexpr (AllocTracker::kEnabled) {
                return TrackedMalloc(source, size);
            } else {
                return std::malloc(size);
            }
        }

        /// Zeroed memory for @p count objects of @p size bytes
        [[nodiscard]] static void* Calloc([[maybe_unused]] AllocSource source, size_t count, size_t size) {
            if constexpr (AllocTracker::kEnabled) {
                return TrackedCalloc(source, count, size);
            } else {
                return std::calloc(count, size);
            }
        }

        /// Like std::realloc: nullptr @p ptr is Malloc. A block that moved or changed size is one free (its old usable size) and one
        /// alloc; a failed one leaves the block as it was and reports nothing
        [[nodiscard]] static void* Realloc([[maybe_unused]] AllocSource source, void* ptr, size_t size) {
            if constexpr (AllocTracker::kEnabled) {
                return TrackedRealloc(source, ptr, size);
            } else {
                return std::realloc(ptr, size);
            }
        }

        /// nullptr is ignored
        static void Free([[maybe_unused]] AllocSource source, void* ptr) {
            if constexpr (AllocTracker::kEnabled) {
                TrackedFree(source, ptr);
            } else {
                std::free(ptr);
            }
        }

        /// Points pugixml, ImGui and SDL at these functions with the sources Pugixml, ImGui and SDL. Idempotent and thread-safe. It runs
        /// from a static initializer of BECore (AllocRouting.cpp), before main and before any SDL or ImGui call: the libraries'
        /// setters are not safe once they have allocated. Does nothing without ENGINE_PROFILER_ALLOC, so the libraries keep malloc.
        static void Install();
        [[nodiscard]] static bool IsInstalled();

    private:
        static void* TrackedMalloc(AllocSource source, size_t size);
        static void* TrackedCalloc(AllocSource source, size_t count, size_t size);
        static void* TrackedRealloc(AllocSource source, void* ptr, size_t size);
        static void TrackedFree(AllocSource source, void* ptr);
    };

}  // namespace BECore
