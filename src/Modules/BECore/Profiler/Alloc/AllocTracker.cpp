#include "AllocTracker.h"

#include <BECore/Profiler/Alloc/AllocPlatform.h>
#include <BECore/Profiler/Profiler.h>

namespace BECore {

    namespace {

        constinit std::atomic<uint64_t> g_count{0};
        constinit std::atomic<uint64_t> g_bytes{0};
        constinit std::atomic<uint64_t> g_frees{0};
        constinit std::atomic<uint64_t> g_liveBytes{0};
        constinit std::atomic<uint64_t> g_peakLiveBytes{0};
        constinit std::atomic<uint64_t> g_largest{0};
        constinit std::atomic<uint64_t> g_noAllocViolations{0};

        struct ThreadState {
            uint64_t count = 0;
            uint64_t bytes = 0;
            uint32_t noAllocDepth = 0;
            uint64_t noAllocViolations = 0;
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

    }  // namespace

    void AllocTracker::OnAlloc(void* ptr, size_t size) {
        ThreadState& state = t_state;
        if (state.inTracker) {
            return;
        }
        state.inTracker = true;
        ++state.count;
        state.bytes += size;
        g_count.fetch_add(1, std::memory_order_relaxed);
        g_bytes.fetch_add(size, std::memory_order_relaxed);
        RaiseTo(g_largest, size);
        const size_t usable = AllocPlatform::UsableSize(ptr);
        const uint64_t live = g_liveBytes.fetch_add(usable, std::memory_order_relaxed) + usable;
        RaiseTo(g_peakLiveBytes, live);
        if (state.noAllocDepth > 0) {
            ++state.noAllocViolations;
            g_noAllocViolations.fetch_add(1, std::memory_order_relaxed);
        }
        if (IProfilerBackend* backend = Profiler::GetBackend()) {
            backend->OnAlloc(ptr, size);
        }
        state.inTracker = false;
    }

    void AllocTracker::OnFree(void* ptr) {
        if (ptr == nullptr) {
            return;
        }
        ThreadState& state = t_state;
        if (state.inTracker) {
            return;
        }
        state.inTracker = true;
        g_frees.fetch_add(1, std::memory_order_relaxed);
        g_liveBytes.fetch_sub(AllocPlatform::UsableSize(ptr), std::memory_order_relaxed);
        if (IProfilerBackend* backend = Profiler::GetBackend()) {
            backend->OnFree(ptr);
        }
        state.inTracker = false;
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

}  // namespace BECore
