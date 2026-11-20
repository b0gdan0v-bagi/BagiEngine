#include "AllocTracker.h"

#include <BECore/Profiler/Alloc/AllocPlatform.h>
#include <BECore/Profiler/Profiler.h>
#include <cstring>

namespace BECore {

    namespace {

        constinit std::atomic<uint64_t> g_count{0};
        constinit std::atomic<uint64_t> g_bytes{0};
        constinit std::atomic<uint64_t> g_frees{0};
        constinit std::atomic<uint64_t> g_liveBytes{0};
        constinit std::atomic<uint64_t> g_peakLiveBytes{0};
        constinit std::atomic<uint64_t> g_largest{0};
        constinit std::atomic<uint64_t> g_noAllocViolations{0};

        struct SourceSlot {
            std::atomic<uint64_t> count{0};
            std::atomic<uint64_t> bytes{0};
            std::atomic<uint64_t> frees{0};
            std::atomic<uint64_t> liveBytes{0};
        };
        constinit SourceSlot g_sources[EnumUtils<AllocSource>::Count()];

        struct TagSlot {
            std::atomic<uint64_t> count{0};
            std::atomic<uint64_t> bytes{0};
            std::atomic<uint64_t> frees{0};
            std::atomic<uint64_t> liveBytes{0};
            std::atomic<uint64_t> peakLiveBytes{0};
        };
        constinit TagSlot g_tags[kMemTagCount];
        constinit std::atomic<uint64_t> g_untrackedBlocks{0};

        struct ThreadState {
            uint64_t count = 0;
            uint64_t bytes = 0;
            uint32_t noAllocDepth = 0;
            uint32_t pauseDepth = 0;  // ScopedAllocCountersPause: nothing of this thread is counted while it is above zero
            uint64_t noAllocViolations = 0;
            MemTag tag = MemTag::Untagged;
            bool inTracker = false;  // the backend's OnAlloc may allocate: that allocation is not reported again
        };
        constinit thread_local ThreadState t_state;

        struct ZoneSlot {
            std::atomic<const ProfileSite*> site{nullptr};
            std::atomic<uint64_t> calls{0};
            std::atomic<uint64_t> count{0};
            std::atomic<uint64_t> bytes{0};
        };
        constinit ZoneSlot g_zoneSlots[AllocTracker::kZoneSlots];

        struct NameSlot {
            std::atomic<const char*> name{nullptr};
            std::atomic<uint64_t> count{0};
            std::atomic<uint64_t> bytes{0};
        };
        constinit NameSlot g_nameSlots[AllocTracker::kNameSlots];

        void RaiseTo(std::atomic<uint64_t>& value, uint64_t candidate) {
            uint64_t current = value.load(std::memory_order_relaxed);
            while (candidate > current && !value.compare_exchange_weak(current, candidate, std::memory_order_relaxed)) {
            }
        }

        /// The slot of @p site, claimed on first use (open addressing, never removed); nullptr when the table is full
        ZoneSlot* FindSlot(const ProfileSite& site) {
            const auto hash = reinterpret_cast<uintptr_t>(&site) >> 4;
            for (uint32_t probe = 0; probe < AllocTracker::kZoneSlots; ++probe) {
                ZoneSlot& slot = g_zoneSlots[(hash + probe) % AllocTracker::kZoneSlots];
                const ProfileSite* key = slot.site.load(std::memory_order_acquire);
                if (key == &site) {
                    return &slot;
                }
                if (key == nullptr) {
                    if (slot.site.compare_exchange_strong(key, &site, std::memory_order_acq_rel) || key == &site) {
                        return &slot;
                    }
                }
            }
            return nullptr;
        }

        /// The slot of the literal @p name, claimed on first use (open addressing keyed by the pointer, never removed); nullptr when full
        NameSlot* FindNameSlot(const char* name) {
            const auto hash = reinterpret_cast<uintptr_t>(name) >> 2;
            for (uint32_t probe = 0; probe < AllocTracker::kNameSlots; ++probe) {
                NameSlot& slot = g_nameSlots[(hash + probe) % AllocTracker::kNameSlots];
                const char* key = slot.name.load(std::memory_order_acquire);
                if (key == name) {
                    return &slot;
                }
                if (key == nullptr) {
                    if (slot.name.compare_exchange_strong(key, name, std::memory_order_acq_rel) || key == name) {
                        return &slot;
                    }
                }
            }
            return nullptr;
        }

#if BE_PROFILER_ALLOC
        // The block-tag table: the tag a block was allocated under, found again by its address when it is freed (blocks carry no
        // header: Alloc/CLAUDE.md). Open addressing over fixed zero-initialised arrays, which cost untouched BSS only. A key is 0 (never
        // used), kTombstoneKey (erased: an insert may reuse it, a lookup goes on past it) or kReservedKey (an insert wrote the claim and
        // not yet the tag: a lookup of another key goes on past it) or the block's address, published after its tag byte.
        constexpr uint32_t kBlockTableBits = 20;
        constexpr size_t kBlockTableSize = size_t{1} << kBlockTableBits;
        constexpr uint32_t kBlockProbeLimit = 64;
        constexpr uint32_t kInsertAttempts = 4;
        constexpr uint64_t kHashMultiplier = 0x9E3779B97F4A7C15ull;
        constexpr uintptr_t kEmptyKey = 0;
        constexpr uintptr_t kTombstoneKey = 1;
        constexpr uintptr_t kReservedKey = 2;
        constexpr size_t kNoSlot = ~size_t{0};

        // Plain arrays, zero in BSS, read through atomic_ref: a million `constinit` atomics exceed the compiler's constant-evaluation limit
        alignas(std::atomic_ref<uintptr_t>::required_alignment) uintptr_t g_blockKeyStorage[kBlockTableSize];
        uint8_t g_blockTagStorage[kBlockTableSize];

        std::atomic_ref<uintptr_t> BlockKey(size_t index) {
            return std::atomic_ref<uintptr_t>(g_blockKeyStorage[index]);
        }

        std::atomic_ref<uint8_t> BlockTag(size_t index) {
            return std::atomic_ref<uint8_t>(g_blockTagStorage[index]);
        }

        constinit std::atomic<uint32_t> g_insertProbeLimit{kBlockProbeLimit};

        size_t BlockHome(uintptr_t key) {
            return static_cast<size_t>((static_cast<uint64_t>(key >> 4) * kHashMultiplier) >> (64 - kBlockTableBits));
        }

        /// Registers @p key under @p tag; false when no slot was found within the probe limit (the block stays unregistered)
        bool InsertBlock(uintptr_t key, MemTag tag) {
            const uint32_t limit = g_insertProbeLimit.load(std::memory_order_relaxed);
            for (uint32_t attempt = 0; attempt < kInsertAttempts; ++attempt) {
                size_t index = BlockHome(key);
                size_t reusable = kNoSlot;
                uintptr_t reusableKey = kEmptyKey;
                for (uint32_t probe = 0; probe < limit; ++probe, index = (index + 1) & (kBlockTableSize - 1)) {
                    const uintptr_t current = BlockKey(index).load(std::memory_order_acquire);
                    if (current == key) {
                        // The address of a block whose free was never seen: it is this block now
                        BlockTag(index).store(static_cast<uint8_t>(tag), std::memory_order_relaxed);
                        return true;
                    }
                    if ((current == kEmptyKey || current == kTombstoneKey) && reusable == kNoSlot) {
                        reusable = index;
                        reusableKey = current;
                    }
                    if (current == kEmptyKey) {
                        break;
                    }
                }
                if (reusable == kNoSlot) {
                    return false;
                }
                uintptr_t expected = reusableKey;
                if (BlockKey(reusable).compare_exchange_strong(expected, kReservedKey, std::memory_order_acq_rel)) {
                    BlockTag(reusable).store(static_cast<uint8_t>(tag), std::memory_order_relaxed);
                    BlockKey(reusable).store(key, std::memory_order_release);
                    return true;
                }
            }
            return false;
        }

        /// Unregisters @p key and returns its tag; Untagged when it was never registered (made before the tracker, untagged or unregistered)
        MemTag EraseBlock(uintptr_t key) {
            size_t index = BlockHome(key);
            for (uint32_t probe = 0; probe < kBlockProbeLimit; ++probe, index = (index + 1) & (kBlockTableSize - 1)) {
                const uintptr_t current = BlockKey(index).load(std::memory_order_acquire);
                if (current == key) {
                    const auto tag = static_cast<MemTag>(BlockTag(index).load(std::memory_order_relaxed));
                    BlockKey(index).store(kTombstoneKey, std::memory_order_release);
                    return tag;
                }
                if (current == kEmptyKey) {
                    break;
                }
            }
            return MemTag::Untagged;
        }
#endif

        /// Takes @p amount off @p value, stopping at zero: a block made before the routing started is freed without an alloc
        void SubtractSaturating(std::atomic<uint64_t>& value, uint64_t amount) {
            uint64_t current = value.load(std::memory_order_relaxed);
            while (!value.compare_exchange_weak(current, current > amount ? current - amount : 0, std::memory_order_relaxed)) {
            }
        }

        /// The one place every counted allocation passes, whatever its source. The thread's MemTag is read here (a field of t_state)
        /// and kept next to the source; it must not allocate or lock.
        void RecordAlloc(AllocSource source, void* ptr, size_t size, size_t usable) {
            ThreadState& state = t_state;
            if (state.inTracker) {
                return;
            }
            state.inTracker = true;
            if (state.pauseDepth > 0) {
                // The backend still sees the block: its memory events must stay balanced with the real memory
                if (IProfilerBackend* backend = Profiler::GetBackend()) {
                    backend->OnAlloc(ptr, size);
                }
                state.inTracker = false;
                return;
            }
            SourceSlot& slot = g_sources[static_cast<size_t>(source)];
            ++state.count;
            state.bytes += size;
            g_count.fetch_add(1, std::memory_order_relaxed);
            g_bytes.fetch_add(size, std::memory_order_relaxed);
            slot.count.fetch_add(1, std::memory_order_relaxed);
            slot.bytes.fetch_add(size, std::memory_order_relaxed);
            slot.liveBytes.fetch_add(usable, std::memory_order_relaxed);
            RaiseTo(g_largest, size);
            const uint64_t live = g_liveBytes.fetch_add(usable, std::memory_order_relaxed) + usable;
            RaiseTo(g_peakLiveBytes, live);
            MemTag tag = state.tag;
#if BE_PROFILER_ALLOC
            // An untagged block needs no entry: a free that finds none is subtracted from Untagged
            if (tag != MemTag::Untagged && !InsertBlock(reinterpret_cast<uintptr_t>(ptr), tag)) {
                g_untrackedBlocks.fetch_add(1, std::memory_order_relaxed);
                tag = MemTag::Untagged;
            }
#endif
            TagSlot& tagSlot = g_tags[static_cast<size_t>(tag)];
            tagSlot.count.fetch_add(1, std::memory_order_relaxed);
            tagSlot.bytes.fetch_add(size, std::memory_order_relaxed);
            RaiseTo(tagSlot.peakLiveBytes, tagSlot.liveBytes.fetch_add(usable, std::memory_order_relaxed) + usable);
            if (state.noAllocDepth > 0) {
                ++state.noAllocViolations;
                g_noAllocViolations.fetch_add(1, std::memory_order_relaxed);
            }
            if (IProfilerBackend* backend = Profiler::GetBackend()) {
                backend->OnAlloc(ptr, size);
            }
            state.inTracker = false;
        }

        /// The counterpart of RecordAlloc; @p usable is read from the block before it is freed
        void RecordFree(AllocSource source, void* ptr, size_t usable) {
            ThreadState& state = t_state;
            if (state.inTracker) {
                return;
            }
            state.inTracker = true;
            if (state.pauseDepth > 0) {
#if BE_PROFILER_ALLOC
                // Not counted, but a tagged block made outside the pause must not leave its entry in the table
                EraseBlock(reinterpret_cast<uintptr_t>(ptr));
#endif
                if (IProfilerBackend* backend = Profiler::GetBackend()) {
                    backend->OnFree(ptr);
                }
                state.inTracker = false;
                return;
            }
            SourceSlot& slot = g_sources[static_cast<size_t>(source)];
            g_frees.fetch_add(1, std::memory_order_relaxed);
            slot.frees.fetch_add(1, std::memory_order_relaxed);
            SubtractSaturating(g_liveBytes, usable);
            SubtractSaturating(slot.liveBytes, usable);
#if BE_PROFILER_ALLOC
            const MemTag tag = EraseBlock(reinterpret_cast<uintptr_t>(ptr));
#else
            const MemTag tag = MemTag::Untagged;
#endif
            TagSlot& tagSlot = g_tags[static_cast<size_t>(tag)];
            tagSlot.frees.fetch_add(1, std::memory_order_relaxed);
            SubtractSaturating(tagSlot.liveBytes, usable);
            if (IProfilerBackend* backend = Profiler::GetBackend()) {
                backend->OnFree(ptr);
            }
            state.inTracker = false;
        }

    }  // namespace

    void AllocTracker::OnAlloc(void* ptr, size_t size) {
        RecordAlloc(AllocSource::GlobalNew, ptr, size, AllocPlatform::UsableSize(ptr));
    }

    void AllocTracker::OnFree(void* ptr) {
        if (ptr != nullptr) {
            RecordFree(AllocSource::GlobalNew, ptr, AllocPlatform::UsableSize(ptr));
        }
    }

    void AllocTracker::OnNamedAlloc(const char* name, size_t size) {
        if (name == nullptr) {
            return;
        }
        if (NameSlot* slot = FindNameSlot(name)) {
            slot->count.fetch_add(1, std::memory_order_relaxed);
            slot->bytes.fetch_add(size, std::memory_order_relaxed);
        }
    }

    void AllocTracker::OnRoutedAlloc(AllocSource source, void* ptr, size_t size, size_t usable) {
        RecordAlloc(source, ptr, size, usable);
    }

    void AllocTracker::OnRoutedFree(AllocSource source, void* ptr, size_t usable) {
        if (ptr != nullptr) {
            RecordFree(source, ptr, usable);
        }
    }

    AllocTotals AllocTracker::GetTotals() {
        AllocTotals totals;
        totals.count = g_count.load(std::memory_order_relaxed);
        totals.bytes = g_bytes.load(std::memory_order_relaxed);
        totals.frees = g_frees.load(std::memory_order_relaxed);
        totals.liveBytes = g_liveBytes.load(std::memory_order_relaxed);
        totals.peakLiveBytes = g_peakLiveBytes.load(std::memory_order_relaxed);
        totals.largest = g_largest.load(std::memory_order_relaxed);
        totals.noAllocViolations = g_noAllocViolations.load(std::memory_order_relaxed);
        totals.untrackedBlocks = g_untrackedBlocks.load(std::memory_order_relaxed);
        return totals;
    }

    AllocSourceTotals AllocTracker::GetSourceTotals(AllocSource source) {
        const SourceSlot& slot = g_sources[static_cast<size_t>(source)];
        return {slot.count.load(std::memory_order_relaxed), slot.bytes.load(std::memory_order_relaxed), slot.frees.load(std::memory_order_relaxed), slot.liveBytes.load(std::memory_order_relaxed)};
    }

    AllocTagTotals AllocTracker::GetTagTotals(MemTag tag) {
        const TagSlot& slot = g_tags[static_cast<size_t>(tag)];
        return {slot.count.load(std::memory_order_relaxed), slot.bytes.load(std::memory_order_relaxed), slot.frees.load(std::memory_order_relaxed), slot.liveBytes.load(std::memory_order_relaxed),
                slot.peakLiveBytes.load(std::memory_order_relaxed)};
    }

    Array<AllocTagTotals, kMemTagCount> AllocTracker::GetTagTotals() {
        Array<AllocTagTotals, kMemTagCount> totals;
        for (size_t i = 0; i < kMemTagCount; ++i) {
            totals[i] = GetTagTotals(static_cast<MemTag>(i));
        }
        return totals;
    }

    AllocNameTotals AllocTracker::GetNameTotals(const char* name) {
        AllocNameTotals totals;
        for (const NameSlot& slot : g_nameSlots) {
            const char* key = slot.name.load(std::memory_order_acquire);
            if (key != nullptr && std::strcmp(key, name) == 0) {
                totals.count += slot.count.load(std::memory_order_relaxed);
                totals.bytes += slot.bytes.load(std::memory_order_relaxed);
            }
        }
        return totals;
    }

    AllocTracker::ThreadCounters AllocTracker::GetThreadCounters() {
        const ThreadState& state = t_state;
        return {state.count, state.bytes};
    }

    void AllocTracker::AddZone(const ProfileSite& site, uint64_t count, uint64_t bytes) {
        ZoneSlot* slot = FindSlot(site);
        if (slot == nullptr) {
            return;
        }
        slot->calls.fetch_add(1, std::memory_order_relaxed);
        slot->count.fetch_add(count, std::memory_order_relaxed);
        slot->bytes.fetch_add(bytes, std::memory_order_relaxed);
    }

    Vector<ZoneAllocStats> AllocTracker::GetZoneStats() {
        Vector<ZoneAllocStats> stats;
        for (const ZoneSlot& slot : g_zoneSlots) {
            const ProfileSite* site = slot.site.load(std::memory_order_acquire);
            const uint64_t calls = slot.calls.load(std::memory_order_relaxed);
            if (site != nullptr && calls > 0) {
                stats.push_back({site, calls, slot.count.load(std::memory_order_relaxed), slot.bytes.load(std::memory_order_relaxed)});
            }
        }
        std::sort(stats.begin(), stats.end(), [](const ZoneAllocStats& a, const ZoneAllocStats& b) { return a.bytes != b.bytes ? a.bytes > b.bytes : a.count > b.count; });
        return stats;
    }

    void AllocTracker::ResetZoneStats() {
        for (ZoneSlot& slot : g_zoneSlots) {
            slot.calls.store(0, std::memory_order_relaxed);
            slot.count.store(0, std::memory_order_relaxed);
            slot.bytes.store(0, std::memory_order_relaxed);
        }
    }

    uint64_t AllocTracker::EnterNoAlloc(const ProfileSite& /*site*/) {
        ThreadState& state = t_state;
        ++state.noAllocDepth;
        return state.noAllocViolations;
    }

    uint64_t AllocTracker::LeaveNoAlloc() {
        ThreadState& state = t_state;
        if (state.noAllocDepth > 0) {
            --state.noAllocDepth;
        }
        return state.noAllocViolations;
    }

    uint32_t AllocTracker::SetBlockProbeLimitForTests([[maybe_unused]] uint32_t limit) {
#if BE_PROFILER_ALLOC
        return g_insertProbeLimit.exchange(limit, std::memory_order_relaxed);
#else
        return 0;
#endif
    }

    MemTag AllocTracker::GetCurrentTag() {
        return t_state.tag;
    }

    MemTag AllocTracker::SwapTag(MemTag tag) {
        ThreadState& state = t_state;
        const MemTag previous = state.tag;
        state.tag = tag;
        return previous;
    }

    void AllocTracker::PauseCounting() {
        ++t_state.pauseDepth;
    }

    void AllocTracker::ResumeCounting() {
        ThreadState& state = t_state;
        if (state.pauseDepth > 0) {
            --state.pauseDepth;
        }
    }

    Vector<ArenaStatsEntry> AllocTracker::GetArenaStats() {
        return BECore::GetArenaStats();
    }

    Vector<PoolStatsEntry> AllocTracker::GetPoolStats() {
        return BECore::GetPoolStats();
    }

}  // namespace BECore
