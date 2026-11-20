#include "PoolStats.h"

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
            std::atomic<uint64_t> blockSize{0};
            std::atomic<uint64_t> live{0};
            std::atomic<uint64_t> peak{0};
            std::atomic<uint64_t> pages{0};
            std::atomic<uint64_t> pageBytes{0};
            std::atomic<uint64_t> allocations{0};
        };
        constinit Slot g_slots[PoolStats::kMaxPools];

        // Single writer: a plain store after a load, no read-modify-write on the pool's hot path
        void AddSingle(std::atomic<uint64_t>& value, uint64_t delta) {
            value.store(value.load(std::memory_order_relaxed) + delta, std::memory_order_relaxed);
        }

        void SubSingle(std::atomic<uint64_t>& value, uint64_t delta) {
            value.store(value.load(std::memory_order_relaxed) - delta, std::memory_order_relaxed);
        }

        void RaiseTo(std::atomic<uint64_t>& peak, uint64_t value) {
            uint64_t seen = peak.load(std::memory_order_relaxed);
            while (value > seen && !peak.compare_exchange_weak(seen, value, std::memory_order_relaxed)) {
            }
        }

        /// Copies the slot when it is live and stayed the same pool while copying
        bool TryCopy(const Slot& slot, PoolStatsEntry& entry) {
            if (slot.state.load(std::memory_order_acquire) != Live) {
                return false;
            }
            const uint32_t epoch = slot.epoch.load(std::memory_order_acquire);
            entry.name = slot.name.load(std::memory_order_relaxed);
            entry.blockSize = slot.blockSize.load(std::memory_order_relaxed);
            entry.live = slot.live.load(std::memory_order_relaxed);
            entry.peak = slot.peak.load(std::memory_order_relaxed);
            entry.pages = slot.pages.load(std::memory_order_relaxed);
            entry.pageBytes = slot.pageBytes.load(std::memory_order_relaxed);
            entry.allocations = slot.allocations.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            return slot.epoch.load(std::memory_order_relaxed) == epoch && slot.state.load(std::memory_order_relaxed) == Live;
        }

    }  // namespace

    PoolStatsSlot PoolStatsSlot::Claim(const char* name, uint64_t blockSize, bool concurrent) {
        for (uint32_t index = 0; index < PoolStats::kMaxPools; ++index) {
            Slot& slot = g_slots[index];
            uint32_t expected = Free;
            if (!slot.state.compare_exchange_strong(expected, Claiming, std::memory_order_acq_rel)) {
                continue;
            }
            slot.epoch.fetch_add(1, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_release);
            slot.name.store(name, std::memory_order_relaxed);
            slot.blockSize.store(blockSize, std::memory_order_relaxed);
            slot.live.store(0, std::memory_order_relaxed);
            slot.peak.store(0, std::memory_order_relaxed);
            slot.pages.store(0, std::memory_order_relaxed);
            slot.pageBytes.store(0, std::memory_order_relaxed);
            slot.allocations.store(0, std::memory_order_relaxed);
            slot.state.store(Live, std::memory_order_release);
            return PoolStatsSlot(index, concurrent);
        }
        return {};
    }

    void PoolStatsSlot::Release() {
        if (_index == kNone) {
            return;
        }
        Slot& slot = g_slots[_index];
        slot.epoch.fetch_add(1, std::memory_order_release);
        slot.state.store(Free, std::memory_order_release);
        _index = kNone;
    }

    void PoolStatsSlot::OnAllocate() {
        if (_index == kNone) {
            return;
        }
        Slot& slot = g_slots[_index];
        if (_concurrent) {
            slot.allocations.fetch_add(1, std::memory_order_relaxed);
            RaiseTo(slot.peak, slot.live.fetch_add(1, std::memory_order_relaxed) + 1);
        } else {
            AddSingle(slot.allocations, 1);
            const uint64_t live = slot.live.load(std::memory_order_relaxed) + 1;
            slot.live.store(live, std::memory_order_relaxed);
            if (live > slot.peak.load(std::memory_order_relaxed)) {
                slot.peak.store(live, std::memory_order_relaxed);
            }
        }
    }

    void PoolStatsSlot::OnFree() {
        if (_index == kNone) {
            return;
        }
        Slot& slot = g_slots[_index];
        if (_concurrent) {
            slot.live.fetch_sub(1, std::memory_order_relaxed);
        } else {
            SubSingle(slot.live, 1);
        }
    }

    void PoolStatsSlot::SetCounts(uint64_t live, uint64_t allocations) {
        if (_index == kNone) {
            return;
        }
        Slot& slot = g_slots[_index];
        slot.live.store(live, std::memory_order_relaxed);
        slot.allocations.store(allocations, std::memory_order_relaxed);
        RaiseTo(slot.peak, live);
    }

    void PoolStatsSlot::OnPage(uint64_t pageBytes) {
        if (_index == kNone) {
            return;
        }
        Slot& slot = g_slots[_index];
        if (_concurrent) {
            slot.pages.fetch_add(1, std::memory_order_relaxed);
            slot.pageBytes.fetch_add(pageBytes, std::memory_order_relaxed);
        } else {
            AddSingle(slot.pages, 1);
            AddSingle(slot.pageBytes, pageBytes);
        }
    }

    uint32_t PoolStats::GetLiveCount() {
        uint32_t live = 0;
        for (const Slot& slot : g_slots) {
            if (slot.state.load(std::memory_order_acquire) == Live) {
                ++live;
            }
        }
        return live;
    }

    Vector<PoolStatsEntry> GetPoolStats() {
        Vector<PoolStatsEntry> entries;
        for (const Slot& slot : g_slots) {
            PoolStatsEntry entry;
            if (TryCopy(slot, entry)) {
                entries.push_back(entry);
            }
        }
        return entries;
    }

    PoolTotals GetPoolTotals() {
        PoolTotals totals;
        for (const Slot& slot : g_slots) {
            PoolStatsEntry entry;
            if (TryCopy(slot, entry)) {
                totals.liveBlocks += entry.live;
                totals.liveBytes += entry.live * entry.blockSize;
                totals.pages += entry.pages;
                totals.pageBytes += entry.pageBytes;
            }
        }
        return totals;
    }

}  // namespace BECore
