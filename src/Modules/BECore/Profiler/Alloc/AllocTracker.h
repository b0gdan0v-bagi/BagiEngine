#pragma once

#include <BECore/Memory/ArenaStats.h>
#include <BECore/Memory/MemTag.h>
#include <BECore/Memory/PoolStats.h>
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
        uint64_t untrackedBlocks = 0;    // tagged blocks the block-tag table could not hold: counted under Untagged
    };

    /// What was allocated under one MemTag (Memory/CLAUDE.md, "Memory tags"); the AllocTotals fields of the same name, summed over every tag
    struct AllocTagTotals {
        uint64_t count = 0;
        uint64_t bytes = 0;
        uint64_t frees = 0;  // blocks given back that were allocated under this tag
        uint64_t liveBytes = 0;
        uint64_t peakLiveBytes = 0;
    };

    /// What one AllocSource allocated; the AllocTotals fields of the same name, summed over every source
    struct AllocSourceTotals {
        uint64_t count = 0;
        uint64_t bytes = 0;
        uint64_t frees = 0;
        uint64_t liveBytes = 0;
    };

    /// What the containers of one debug name allocated (EngineAllocator::get_name): the blocks asked for, not the live ones
    struct AllocNameTotals {
        uint64_t count = 0;
        uint64_t bytes = 0;
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
     * Every counted block also lands in the slot of the thread's MemTag (MemTagScope); a free is subtracted from the tag its block was
     * allocated under, found in a block-tag table (Alloc/CLAUDE.md, "Tags").
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
        static constexpr uint32_t kNameSlots = 256;   // distinct container names with statistics; more are not counted

        /// The calling thread's allocations since it started (monotonic)
        struct ThreadCounters {
            uint64_t count = 0;
            uint64_t bytes = 0;
        };

        /// From the operator new: @p ptr of @p size bytes asked for was just allocated
        static void OnAlloc(void* ptr, size_t size);
        /// From the operator delete: @p ptr is about to be freed
        static void OnFree(void* ptr);
        /// From EngineAllocator, after its block was allocated (OnAlloc counted it already): the container that asked for @p size bytes
        /// is named @p name. @p name is kept by pointer and must be a literal; no allocation. When the name table is full the name is dropped
        static void OnNamedAlloc(const char* name, size_t size);
        /// From AllocRouting: @p ptr of @p size bytes asked for, @p usable bytes in all, was just allocated by @p source
        static void OnRoutedAlloc(AllocSource source, void* ptr, size_t size, size_t usable);
        /// From AllocRouting: the block @p ptr of @p usable bytes is about to be freed. A block that was made before the routing
        /// started has no matching alloc: the live bytes subtract down to zero and no further
        static void OnRoutedFree(AllocSource source, void* ptr, size_t usable);

        [[nodiscard]] static AllocTotals GetTotals();
        [[nodiscard]] static AllocSourceTotals GetSourceTotals(AllocSource source);
        /// The blocks EngineAllocator allocated under @p name (compared by text); zero unless the build has both ENGINE_PROFILER_ALLOC and
        /// EASTL_NAME_ENABLED (EASTL keeps names only in a `_DEBUG` build)
        [[nodiscard]] static AllocNameTotals GetNameTotals(const char* name);
        [[nodiscard]] static ThreadCounters GetThreadCounters();
        [[nodiscard]] static AllocTagTotals GetTagTotals(MemTag tag);
        /// Every tag, in the order of the enum; no allocation
        [[nodiscard]] static Array<AllocTagTotals, kMemTagCount> GetTagTotals();

        /// Adds one zone of @p site that made @p count allocations of @p bytes (ProfileZone)
        static void AddZone(const ProfileSite& site, uint64_t count, uint64_t bytes);
        /// The zones with statistics, the most bytes first
        [[nodiscard]] static Vector<ZoneAllocStats> GetZoneStats();
        /// Zeroes the zone statistics (the totals stay)
        static void ResetZoneStats();

        /// BE_PROFILE_NO_ALLOC: allocations of the calling thread count as violations until the matching Leave. Returns the violations of this thread so far
        static uint64_t EnterNoAlloc(const ProfileSite& site);
        static uint64_t LeaveNoAlloc();

        /// Test hook: how many slots an insert into the block-tag table may probe (0: every block stays unregistered, as in a full table).
        /// Returns the previous limit; restore it when done
        static uint32_t SetBlockProbeLimitForTests(uint32_t limit);

        /// The calling thread's memory tag (MemTagScope); Untagged when no scope is open
        [[nodiscard]] static MemTag GetCurrentTag();
        /// Sets the calling thread's memory tag and returns the previous one
        static MemTag SwapTag(MemTag tag);

        /// While a pause of the calling thread is open its allocations and frees are not counted (ScopedAllocCountersPause); pauses nest
        static void PauseCounting();
        static void ResumeCounting();

        /// The counters of every arena alive (Memory/CLAUDE.md, "Counters"); allocates
        [[nodiscard]] static Vector<ArenaStatsEntry> GetArenaStats();

        /// The counters of every block pool alive (Memory/CLAUDE.md, "Pools"); allocates
        [[nodiscard]] static Vector<PoolStatsEntry> GetPoolStats();
    };

    /// Keeps the allocations and frees of the calling thread out of every count for the length of a scope: the profiler's own views
    /// (Alloc/CLAUDE.md, "Pause"). The memory backend is still told, so its events stay balanced. A no-op without ENGINE_PROFILER_ALLOC
    class ScopedAllocCountersPause {
    public:
        ScopedAllocCountersPause() {
            if constexpr (AllocTracker::kEnabled) {
                AllocTracker::PauseCounting();
            }
        }
        ~ScopedAllocCountersPause() {
            if constexpr (AllocTracker::kEnabled) {
                AllocTracker::ResumeCounting();
            }
        }

        ScopedAllocCountersPause(const ScopedAllocCountersPause&) = delete;
        ScopedAllocCountersPause& operator=(const ScopedAllocCountersPause&) = delete;
    };

}  // namespace BECore
