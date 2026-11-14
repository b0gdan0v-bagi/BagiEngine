#include "SpikeCapture.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace BECore {

    namespace {

        size_t MinMedianFrames(const SpikeSettings& settings) {
            return eastl::min(SpikeCapture::kMinMedianFrames, settings.medianWindow);
        }

        double MedianOf(const Vector<double>& values) {
            Vector<double> ordered = values;
            eastl::sort(ordered.begin(), ordered.end());
            const size_t middle = ordered.size() / 2;
            return ordered.size() % 2 == 1 ? ordered[middle] : (ordered[middle - 1] + ordered[middle]) * 0.5;
        }

    }  // namespace

    double SpikeCapture::GetMedianMs() const {
        const size_t needed = eastl::max<size_t>(MinMedianFrames(_settings), 1);
        return _recentMs.size() >= needed ? MedianOf(_recentMs) : 0.0;
    }

    void SpikeCapture::SetSettings(const SpikeSettings& settings) {
        _settings = settings;
        _settings.keep = eastl::max<size_t>(_settings.keep, 1);
        _settings.medianWindow = eastl::max<size_t>(_settings.medianWindow, 1);
        if (_spikes.size() > _settings.keep) {
            _spikes.erase(_spikes.begin(), _spikes.begin() + static_cast<ptrdiff_t>(_spikes.size() - _settings.keep));
        }
        if (_recentMs.size() > _settings.medianWindow) {
            _recentMs.erase(_recentMs.begin(), _recentMs.begin() + static_cast<ptrdiff_t>(_recentMs.size() - _settings.medianWindow));
        }
    }

    void SpikeCapture::Clear() {
        _spikes.clear();
        _recentMs.clear();
    }

    void SpikeCapture::Update(const RingSnapshot& snapshot) {
        for (const RingFrame& frame : snapshot.frames) {
            if (_hasJudged && frame.index <= _lastJudged) {
                continue;
            }
            Judge(snapshot, frame);
            _lastJudged = frame.index;
            _hasJudged = true;
        }
    }

    void SpikeCapture::Judge(const RingSnapshot& snapshot, const RingFrame& frame) {
        const double frameMs = snapshot.TicksToMs(frame.endTicks - frame.beginTicks);
        const double medianMs = GetMedianMs();
        const bool spike = _settings.mode == SpikeMode::AbsoluteMs ? frameMs > _settings.thresholdMs : (medianMs > 0.0 && frameMs > medianMs * _settings.medianFactor);

        if (spike) {
            _spikes.push_back(FreezeFrame(snapshot, frame, medianMs));
            if (_spikes.size() > _settings.keep) {
                _spikes.erase(_spikes.begin());
            }
        }

        // The median looks at the frames before a frame, so this one joins them after it has been judged
        if (_recentMs.size() == _settings.medianWindow) {
            _recentMs.erase(_recentMs.begin());
        }
        _recentMs.push_back(frameMs);
    }

    FrozenFrame FreezeFrame(const RingSnapshot& snapshot, const RingFrame& frame, double medianMs) {
        FrozenFrame frozen;
        frozen.frameIndex = frame.index;
        frozen.frameMs = snapshot.TicksToMs(frame.endTicks - frame.beginTicks);
        frozen.medianMs = medianMs;
        frozen.beginTicks = frame.beginTicks;
        frozen.endTicks = frame.endTicks;
        frozen.nanosecondsPerTick = snapshot.nanosecondsPerTick;

        size_t kept = 0;
        for (const RingThread& thread : snapshot.threads) {
            RingThread copy;
            for (const ZoneRecord& zone : thread.zones) {
                if (zone.beginTicks < frame.beginTicks || zone.beginTicks >= frame.endTicks) {
                    continue;
                }
                if (kept == SpikeCapture::kMaxFrozenZones) {
                    ++frozen.truncatedZones;
                    continue;
                }
                copy.zones.push_back(zone);
                ++kept;
            }
            if (!copy.zones.empty()) {
                copy.threadId = thread.threadId;
                copy.name = thread.name;
                copy.sortIndex = thread.sortIndex;
                frozen.threads.push_back(eastl::move(copy));
            }
        }
        return frozen;
    }

    RingSnapshot ToSnapshot(const FrozenFrame& frame) {
        RingSnapshot snapshot;
        snapshot.threads = frame.threads;
        snapshot.frames.push_back(RingFrame{frame.frameIndex, frame.beginTicks, frame.endTicks});
        snapshot.nanosecondsPerTick = frame.nanosecondsPerTick;
        return snapshot;
    }

}  // namespace BECore
