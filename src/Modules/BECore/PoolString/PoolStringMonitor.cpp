#include "PoolStringMonitor.h"

#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <EASTL/sort.h>

namespace BECore {

    PoolStringMonitor::PoolStringMonitor(Details::PoolStringStorage& storage) : _storage(storage), _previous(storage.GetTotals()) {}

    PoolStringMonitor::~PoolStringMonitor() {
        SetRecording(false);
    }

    void PoolStringMonitor::BeginFrame(uint64_t frame) {
        _frame.store(frame, std::memory_order_relaxed);
    }

    void PoolStringMonitor::EndFrame() {
        const Details::PoolStringTotals totals = _storage.GetTotals();
        const uint64_t frame = _frame.load(std::memory_order_relaxed);
        _ring[_frames % kFrameHistory] = {.frame = frame,
                                          .runtimeStrings = totals.runtimeStrings - _previous.runtimeStrings,
                                          .runtimeBytes = totals.runtimeBytes - _previous.runtimeBytes,
                                          .otherStrings = totals.otherStrings - _previous.otherStrings};
        ++_frames;
        _previous = totals;
        _frame.store(frame + 1, std::memory_order_relaxed);
    }

    size_t PoolStringMonitor::RecentCount(size_t count) const {
        return std::min({count, _frames, kFrameHistory});
    }

    void PoolStringMonitor::ForEachRecent(size_t count, FunctionRef<void(const PoolGrowthFrame&)> visit) const {
        for (size_t i = _frames - RecentCount(count); i < _frames; ++i) {
            visit(_ring[i % kFrameHistory]);
        }
    }

    void PoolStringMonitor::SetRecording(bool recording) {
        if (recording == _recording) {
            return;
        }
        _recording = recording;
        if (!recording) {
            _storage.RemoveObserver(*this);
            return;
        }
        {
            // At once, so that nothing is allocated under the pool's lock later
            LockGuard lock(_mutex);
            _records.reserve(kMaxRecords);
        }
        _storage.SetObserver(*this);
    }

    void PoolStringMonitor::ClearRecords() {
        LockGuard lock(_mutex);
        _records.clear();
        _dropped = 0;
        ++_clears;
    }

    size_t PoolStringMonitor::GetDroppedCount() const {
        LockGuard lock(_mutex);
        return _dropped;
    }

    PoolRecordCounts PoolStringMonitor::GetRecordCounts() const {
        LockGuard lock(_mutex);
        return {.records = _records.size(), .dropped = _dropped, .clears = _clears};
    }

    void PoolStringMonitor::GroupByZone(Vector<PoolStringRecord>& records, Vector<PoolZoneGroup>& groups) const {
        {
            LockGuard lock(_mutex);
            records.assign(_records.begin(), _records.end());
        }
        eastl::sort(records.begin(), records.end(), [](const PoolStringRecord& left, const PoolStringRecord& right) {
            const uintptr_t leftZone = reinterpret_cast<uintptr_t>(left.zone);
            const uintptr_t rightZone = reinterpret_cast<uintptr_t>(right.zone);
            return leftZone != rightZone ? leftZone < rightZone : left.order < right.order;
        });
        groups.clear();
        for (size_t i = 0; i < records.size(); ++i) {
            const PoolStringRecord& record = records[i];
            if (groups.empty() || groups.back().zone != record.zone) {
                groups.push_back({.zone = record.zone, .first = i});
            }
            PoolZoneGroup& group = groups.back();
            ++group.count;
            group.bytes += Details::PoolEntryBytes(record.text.Length());
            group.lastFrame = std::max(group.lastFrame, record.frame);
        }
        eastl::sort(groups.begin(), groups.end(),
                    [](const PoolZoneGroup& left, const PoolZoneGroup& right) { return left.bytes != right.bytes ? left.bytes > right.bytes : left.first < right.first; });
    }

    void PoolStringMonitor::OnRuntimeString(PoolString text) {
        const ProfileSite* zone = AllocTracker::GetCurrentZone();
        LockGuard lock(_mutex);
        if (_records.size() >= kMaxRecords) {
            ++_dropped;
            return;
        }
        _records.push_back({.text = text, .zone = zone, .frame = _frame.load(std::memory_order_relaxed), .order = static_cast<uint32_t>(_records.size())});
    }

}  // namespace BECore
