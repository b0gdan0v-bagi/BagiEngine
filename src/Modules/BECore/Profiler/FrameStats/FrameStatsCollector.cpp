#include "FrameStatsCollector.h"

#include <BECore/Profiler/ProfilerClock.h>

namespace BECore {

    namespace {

        constexpr double kNanosecondsPerMillisecond = 1.0e6;

        double ToMs(uint64_t nanoseconds) {
            return static_cast<double>(nanoseconds) / kNanosecondsPerMillisecond;
        }

    }  // namespace

    Optional<double> FrameStats::FindCounter(PoolString name) const {
        for (const FrameCounter& counter : counters) {
            if (counter.name == name) {
                return counter.value;
            }
        }
        return std::nullopt;
    }

    FrameStatsCollector::FrameStatsCollector() {
        _ring.resize(kFrameHistory);
    }

    void FrameStatsCollector::SetClock(NowFn now) {
        _now = now;
    }

    uint64_t FrameStatsCollector::Now() const {
        return _now != nullptr ? _now() : static_cast<uint64_t>(ProfilerClock::ToNanoseconds(ProfilerClock::Now()));
    }

    void FrameStatsCollector::BeginFrame(bool held) {
        _open = true;
        _current = FrameStats{};
        _current.paused = held;
        _frameBegin = Now();
        _stageBegin = {};
        _allocsAtBegin = AllocTracker::GetTotals();
    }

    void FrameStatsCollector::BeginStage(FrameStage stage) {
        if (_open) {
            _stageBegin[+stage] = Now();
        }
    }

    void FrameStatsCollector::EndStage(FrameStage stage) {
        if (_open) {
            _current.stageMs[+stage] += ToMs(Now() - _stageBegin[+stage]);
        }
    }

    void FrameStatsCollector::SetCounter(PoolString name, double value) {
        if (!_open) {
            return;
        }
        for (FrameCounter& counter : _current.counters) {
            if (counter.name == name) {
                counter.value = value;
                return;
            }
        }
        if (_current.counters.size() < kMaxFrameCounters) {
            _current.counters.push_back({name, value});
        }
    }

    void FrameStatsCollector::EndFrame(const RenderCounters& draws, const AllocTotals& allocs) {
        if (!_open) {
            return;
        }
        _open = false;
        const uint64_t end = Now();
        _current.frame = ++_frames;
        _current.cpuMs = ToMs(end - _frameBegin);
        _current.frameMs = _lastFrameEnd != 0 ? ToMs(end - _lastFrameEnd) : _current.cpuMs;
        _lastFrameEnd = end;
        _current.draws = draws;
        _current.allocCount = allocs.count - _allocsAtBegin.count;
        _current.allocBytes = allocs.bytes - _allocsAtBegin.bytes;
        _current.liveBytes = allocs.liveBytes;
        _ring[(_frames - 1) % kFrameHistory] = _current;

        BE_PROFILE_COUNTER("Draw calls", draws.drawCalls);
        BE_PROFILE_COUNTER("Allocations", _current.allocCount);
        BE_PROFILE_COUNTER("Live bytes", _current.liveBytes);
    }

    eastl::vector<FrameStats> FrameStatsCollector::GetHistory(size_t count) const {
        const uint64_t available = _frames < kFrameHistory ? _frames : kFrameHistory;
        const uint64_t taken = count < available ? count : available;
        eastl::vector<FrameStats> history;
        history.reserve(taken);
        for (uint64_t frame = _frames - taken + 1; frame <= _frames; ++frame) {
            history.push_back(_ring[(frame - 1) % kFrameHistory]);
        }
        return history;
    }

    const FrameStats* FrameStatsCollector::GetLast() const {
        return _frames == 0 ? nullptr : &_ring[(_frames - 1) % kFrameHistory];
    }

}  // namespace BECore
