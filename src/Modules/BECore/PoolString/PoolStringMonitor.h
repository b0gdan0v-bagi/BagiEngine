#pragma once

#include <BECore/PoolString/PoolStringObserver.h>
#include <BECore/PoolString/PoolStringStorage.h>
#include <BECore/Profiler/ProfileSite.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/FunctionRef.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /// What one frame added to a pool: the change since the end of the frame before, so what is interned between two
    /// frames counts in the second
    struct PoolGrowthFrame {
        uint64_t frame = 0;
        uint64_t runtimeStrings = 0;  ///< Copied by `Intern` and `InternBatch`: what should be 0 in a steady frame
        uint64_t runtimeBytes = 0;    ///< Their aligned entry sizes
        uint64_t otherStrings = 0;    ///< Registered and adopted entries
    };

    /// A runtime string that appeared while the monitor was recording
    struct PoolStringRecord {
        PoolString text;
        const ProfileSite* zone = nullptr;  ///< The innermost profiler zone of the interning thread; null: none, or zones are not tracked
        uint64_t frame = 0;                 ///< The frame that was open, or the next one between two frames
        uint32_t order = 0;                 ///< Its place among the records, in the order the strings came
    };

    /// The records of one zone: `records[first, first + count)` of the vector `GroupByZone` filled with them
    struct PoolZoneGroup {
        const ProfileSite* zone = nullptr;
        size_t first = 0;
        size_t count = 0;
        size_t bytes = 0;  ///< Aligned entry sizes of the strings
        uint64_t lastFrame = 0;
    };

    /**
     * @brief Watches one pool grow: the strings of each of the last frames and, while recording, each new runtime string with
     * the profiler zone that interned it
     *
     * Zones are those of `AllocTracker::GetCurrentZone`, which `ProfileZone` sets only with ENGINE_PROFILER_ALLOC.
     * The frames and the switches belong to one thread (the main one); strings may be interned on any. The observer of the
     * pool is installed only while recording, and takes the monitor's mutex under the pool's lock, so nothing here calls
     * the pool while it holds that mutex.
     */
    class PoolStringMonitor final : private IPoolStringObserver {
    public:
        static constexpr size_t kFrameHistory = 240;
        static constexpr size_t kMaxRecords = 16384;  ///< Strings past it are counted as dropped, not recorded

        /// `storage` is not owned and outlives the monitor. What it holds now is the baseline of the first frame.
        explicit PoolStringMonitor(Details::PoolStringStorage& storage);
        ~PoolStringMonitor() override;

        PoolStringMonitor(const PoolStringMonitor&) = delete;
        PoolStringMonitor& operator=(const PoolStringMonitor&) = delete;

        /// The pool it watches
        [[nodiscard]] Details::PoolStringStorage& GetStorage() const {
            return _storage;
        }

        void BeginFrame(uint64_t frame);
        /// Stores the growth of the frame; allocates nothing
        void EndFrame();

        /// How many frames `ForEachRecent(count, ...)` visits
        [[nodiscard]] size_t RecentCount(size_t count) const;
        /// Calls `visit` for the last `count` ended frames (fewer when fewer were seen), the oldest first
        void ForEachRecent(size_t count, FunctionRef<void(const PoolGrowthFrame&)> visit) const;

        /// Switching on keeps the records of earlier recordings and takes the memory for `kMaxRecords` of them at once
        void SetRecording(bool recording);
        [[nodiscard]] bool IsRecording() const {
            return _recording;
        }
        /// Drops the records and the dropped count
        void ClearRecords();
        [[nodiscard]] size_t GetDroppedCount() const;

        /// Refills `records` with a copy of the records, the ones of a zone side by side in the order they came, and `groups`
        /// with one entry per zone, the most bytes first
        void GroupByZone(Vector<PoolStringRecord>& records, Vector<PoolZoneGroup>& groups) const;

    private:
        void OnRuntimeString(PoolString text) override;

        Details::PoolStringStorage& _storage;
        Details::PoolStringTotals _previous;
        Array<PoolGrowthFrame, kFrameHistory> _ring{};  // the newest at (_frames - 1) % kFrameHistory
        size_t _frames = 0;
        bool _recording = false;
        std::atomic<uint64_t> _frame{1};  // read by the interning threads

        mutable Mutex _mutex{BE_LOCK_SITE("PoolString monitor")};
        Vector<PoolStringRecord> _records;  // guarded by _mutex
        size_t _dropped = 0;                // guarded by _mutex
    };

}  // namespace BECore
