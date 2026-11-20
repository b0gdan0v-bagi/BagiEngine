#include "ArenaStats.h"


namespace BECore {

    namespace {

        enum SlotState : uint32_t {
            Free,
            Claiming,  // the claimer is filling the slot: readers skip it
            Live,
        };

        struct Slot {
            std::atomic<uint32_t> state{Free};
            // Bumped on every claim and release; a reader that sees it change while copying drops the entry (a slot reused mid-copy)
            std::atomic<uint32_t> epoch{0};
            std::atomic<const char*> name{nullptr};
            std::atomic<uint64_t> capacity{0};
            std::atomic<uint64_t> used{0};
            std::atomic<uint64_t> peak{0};
            std::atomic<uint64_t> overflows{0};
            std::atomic<uint64_t> chunks{0};
            std::atomic<uint64_t> resets{0};
        };
        constinit Slot g_slots[ArenaStats::kMaxArenas];

        // Single writer: a plain store after a load, no read-modify-write on the arena's bump path
        void Add(std::atomic<uint64_t>& value, uint64_t delta) {
            value.store(value.load(std::memory_order_relaxed) + delta, std::memory_order_relaxed);
        }

        /// Copies the slot when it is live and stayed the same arena while copying
        bool TryCopy(const Slot& slot, ArenaStatsEntry& entry) {
            if (slot.state.load(std::memory_order_acquire) != Live) {
                return false;
            }
            const uint32_t epoch = slot.epoch.load(std::memory_order_acquire);
            entry.name = slot.name.load(std::memory_order_relaxed);
            entry.capacity = slot.capacity.load(std::memory_order_relaxed);
            entry.used = slot.used.load(std::memory_order_relaxed);
            entry.peak = slot.peak.load(std::memory_order_relaxed);
            entry.overflows = slot.overflows.load(std::memory_order_relaxed);
            entry.chunks = slot.chunks.load(std::memory_order_relaxed);
            entry.resets = slot.resets.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            return slot.epoch.load(std::memory_order_relaxed) == epoch && slot.state.load(std::memory_order_relaxed) == Live;
        }

    }  // namespace

    ArenaStatsSlot ArenaStatsSlot::Claim(const char* name) {
        for (uint32_t index = 0; index < ArenaStats::kMaxArenas; ++index) {
            Slot& slot = g_slots[index];
            uint32_t expected = Free;
            if (!slot.state.compare_exchange_strong(expected, Claiming, std::memory_order_acq_rel)) {
                continue;
            }
            slot.epoch.fetch_add(1, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_release);
            slot.name.store(name, std::memory_order_relaxed);
            slot.capacity.store(0, std::memory_order_relaxed);
            slot.used.store(0, std::memory_order_relaxed);
            slot.peak.store(0, std::memory_order_relaxed);
            slot.overflows.store(0, std::memory_order_relaxed);
            slot.chunks.store(0, std::memory_order_relaxed);
            slot.resets.store(0, std::memory_order_relaxed);
            slot.state.store(Live, std::memory_order_release);
            return ArenaStatsSlot(index);
        }
        return {};
    }

    void ArenaStatsSlot::Release() {
        if (_index == kNone) {
            return;
        }
        Slot& slot = g_slots[_index];
        slot.epoch.fetch_add(1, std::memory_order_release);
        slot.state.store(Free, std::memory_order_release);
        _index = kNone;
    }

    void ArenaStatsSlot::PublishCapacity(uint64_t capacity, uint64_t chunks) {
        if (_index == kNone) {
            return;
        }
        Slot& slot = g_slots[_index];
        slot.capacity.store(capacity, std::memory_order_relaxed);
        slot.chunks.store(chunks, std::memory_order_relaxed);
    }

    void ArenaStatsSlot::PublishUsed(uint64_t used) {
        if (_index == kNone) {
            return;
        }
        Slot& slot = g_slots[_index];
        slot.used.store(used, std::memory_order_relaxed);
        if (used > slot.peak.load(std::memory_order_relaxed)) {
            slot.peak.store(used, std::memory_order_relaxed);
        }
    }

    void ArenaStatsSlot::CountOverflow() {
        if (_index != kNone) {
            Add(g_slots[_index].overflows, 1);
        }
    }

    void ArenaStatsSlot::CountReset() {
        if (_index != kNone) {
            Add(g_slots[_index].resets, 1);
        }
    }

    uint32_t ArenaStats::GetLiveCount() {
        uint32_t live = 0;
        for (const Slot& slot : g_slots) {
            if (slot.state.load(std::memory_order_acquire) == Live) {
                ++live;
            }
        }
        return live;
    }

    Vector<ArenaStatsEntry> GetArenaStats() {
        Vector<ArenaStatsEntry> entries;
        for (const Slot& slot : g_slots) {
            ArenaStatsEntry entry;
            if (TryCopy(slot, entry)) {
                entries.push_back(entry);
            }
        }
        return entries;
    }

    ArenaTotals GetArenaTotals() {
        ArenaTotals totals;
        for (const Slot& slot : g_slots) {
            ArenaStatsEntry entry;
            if (TryCopy(slot, entry)) {
                totals.capacity += entry.capacity;
                totals.used += entry.used;
                totals.peak += entry.peak;
                totals.overflows += entry.overflows;
            }
        }
        return totals;
    }

}  // namespace BECore
