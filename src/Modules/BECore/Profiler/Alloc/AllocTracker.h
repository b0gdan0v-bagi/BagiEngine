#pragma once

#include <BECore/Memory/ArenaStats.h>
#include <BECore/Memory/MemTag.h>
#include <BECore/Profiler/ProfileSite.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /// Who made an allocation: the global operator new or one of the libraries routed through AllocRouting (Alloc/CLAUDE.md)
    CORE_ENUM(AllocSource, uint8_t, GlobalNew, Pugixml, ImGui, SDL, Stb, DrLibs)

    /// The heap since the start of the process (ENGINE_PROFILER_ALLOC; all zero without it)
    struct AllocTotals {
        uint64_t count = 0;              // allocations made
        uint64_t bytes = 0;              // bytes asked for by them
        uint64_t frees = 0;              // blocks given back
        uint64_t liveBytes = 0;          // usable bytes of the blocks alive now
        uint64_t peakLiveBytes = 0;      // the most liveBytes ever was
        uint64_t largest = 0;            // the largest single allocation asked for
        uint64_t noAllocViolations = 0;  // allocations inside a BE_PROFILE_NO_ALLOC scope
    };

    /// What one AllocSource allocated; the AllocTotals fields of the same name, summed over every source
    struct AllocSourceTotals {
        uint64_t count = 0;
        uint64_t bytes = 0;
        uint64_t frees = 0;
        uint64_t liveBytes = 0;
    };

    /// The allocations made inside the zones of one ProfileSite, inclusive of the zones inside them, since the last reset
    struct ZoneAllocStats {
        const ProfileSite* site = nullptr;
        uint64_t calls = 0;
        uint64_t count = 0;
        uint64_t bytes = 0;
    };

    /**
     * @brief Counts the allocations of the global operator new / delete (Alloc/GlobalOperatorNew.cpp, ENGINE_PROFILER_ALLOC) and of the
     * libraries routed through AllocRouting.
     *
     * Every counted block passes one place (RecordAlloc / RecordFree in the .cpp), whatever its AllocSource: the totals, the thread
     * counters, the zone statistics, the no-alloc violations and the backend see them alike.
     *
     * Static and `constinit`, like Profiler: it runs before main and on every thread. Nothing it does on the allocation path
     * allocates, locks or logs. Without ENGINE_PROFILER_ALLOC the operators are not replaced and every number stays zero.
     */
    class AllocTracker {
    public:
        AllocTracker() = delete;

        static constexpr bool kEnabled = BE_PROFILER_ALLOC != 0;
        static constexpr uint32_t kZoneSlots = 1024;  // distinct zone sites with statistics; more are not counted

        /// The calling thread's allocations since it started (monotonic)
        struct ThreadCounters {
            uint64_t count = 0;
            uint64_t bytes = 0;
        };

        /// From the operator new: @p ptr of @p size bytes asked for was just allocated
        static void OnAlloc(void* ptr, size_t size);
        /// From the operator delete: @p ptr is about to be freed
        static void OnFree(void* ptr);
        /// From AllocRouting: @p ptr of @p size bytes asked for, @p usable bytes in all, was just allocated by @p source
        static void OnRoutedAlloc(AllocSource source, void* ptr, size_t size, size_t usable);
        /// From AllocRouting: the block @p ptr of @p usable bytes is about to be freed. A block that was made before the routing
        /// started has no matching alloc: the live bytes subtract down to zero and no further
        static void OnRoutedFree(AllocSource source, void* ptr, size_t usable);

        [[nodiscard]] static AllocTotals GetTotals();
        [[nodiscard]] static AllocSourceTotals GetSourceTotals(AllocSource source);
        [[nodiscard]] static ThreadCounters GetThreadCounters();

        /// Adds one zone of @p site that made @p count allocations of @p bytes (ProfileZone)
        static void AddZone(const ProfileSite& site, uint64_t count, uint64_t bytes);
        /// The zones with statistics, the most bytes first
        [[nodiscard]] static Vector<ZoneAllocStats> GetZoneStats();
        /// Zeroes the zone statistics (the totals stay)
        static void ResetZoneStats();

        /// BE_PROFILE_NO_ALLOC: allocations of the calling thread count as violations until the matching Leave. Returns the violations of this thread so far
        static uint64_t EnterNoAlloc(const ProfileSite& site);
        static uint64_t LeaveNoAlloc();

        /// The calling thread's memory tag (MemTagScope); Untagged when no scope is open
        [[nodiscard]] static MemTag GetCurrentTag();
        /// Sets the calling thread's memory tag and returns the previous one
        static MemTag SwapTag(MemTag tag);

        /// The counters of every arena alive (Memory/CLAUDE.md, "Counters"); allocates
        [[nodiscard]] static Vector<ArenaStatsEntry> GetArenaStats();
    };

}  // namespace BECore
