#pragma once

#include <BECore/Profiler/Ring/RingSnapshot.h>

namespace BECore {

    CORE_ENUM(SpikeMode, uint8_t, AbsoluteMs, TimesMedian)

    struct SpikeSettings {
        static constexpr double kDefaultThresholdMs = 50.0;
        static constexpr double kDefaultMedianFactor = 3.0;
        static constexpr size_t kDefaultKeep = 8;
        static constexpr size_t kDefaultMedianWindow = 60;

        SpikeMode mode = SpikeMode::AbsoluteMs;
        double thresholdMs = kDefaultThresholdMs;    // AbsoluteMs: a frame longer than this is a spike
        double medianFactor = kDefaultMedianFactor;  // TimesMedian: a frame longer than this times the median of the recent frames
        size_t keep = kDefaultKeep;                  // how many frozen frames are kept, the newest
        size_t medianWindow = kDefaultMedianWindow;  // how many previous frames the median is taken over
    };

    /// One frame kept for a look afterwards: only the zones that began inside it, with the names of their threads
    struct FrozenFrame {
        uint64_t frameIndex = 0;
        double frameMs = 0.0;
        double medianMs = 0.0;              // the median of the frames before it, 0 when there were not enough to take one
        eastl::vector<RingThread> threads;  // threads with at least one zone in the frame
        uint64_t beginTicks = 0;
        uint64_t endTicks = 0;
        double nanosecondsPerTick = 1.0;
        uint64_t truncatedZones = 0;  // zones of the frame left out because of SpikeCapture::kMaxFrozenZones
    };

    /**
     * @brief Watches the frames of the ring's snapshots and freezes the long ones with all their zones, so a spike can be looked at
     * after the ring has overwritten it.
     *
     * Call Update with a fresh snapshot often enough that a frame is still in the ring (the ring holds a few hundred frames).
     * Each frame is judged once, by its duration (end minus begin tick of the frame): the first Update judges every frame of the
     * snapshot, the next ones only those with a greater index than any judged before. Frames the ring no longer has when Update
     * finally runs are skipped without error. Changing the settings does not re-judge frames already seen. Main thread only.
     */
    class SpikeCapture {
    public:
        static constexpr size_t kMaxFrozenZones = 20000;  // per frozen frame; the rest are counted in truncatedZones
        static constexpr size_t kMinMedianFrames = 10;    // TimesMedian does not fire before it has this many previous frames (or a full medianWindow when that is smaller)

        void Update(const RingSnapshot& snapshot);

        /// The frozen frames, the oldest first (at most settings.keep)
        [[nodiscard]] const eastl::vector<FrozenFrame>& GetSpikes() const {
            return _spikes;
        }

        /// Forgets the frozen frames and the median's frames; frames already judged are not judged again
        void Clear();

        void SetSettings(const SpikeSettings& settings);
        [[nodiscard]] const SpikeSettings& GetSettings() const {
            return _settings;
        }

        /// The median of the recent frame durations, 0 when there are fewer than the minimum
        [[nodiscard]] double GetMedianMs() const;

    private:
        void Judge(const RingSnapshot& snapshot, const RingFrame& frame);

        SpikeSettings _settings;
        eastl::vector<FrozenFrame> _spikes;
        eastl::vector<double> _recentMs;  // durations of the last medianWindow judged frames, oldest first
        uint64_t _lastJudged = 0;
        bool _hasJudged = false;
    };

    /// A frame of @p snapshot with the zones that began inside it, as the capture freezes a spike (at most SpikeCapture::kMaxFrozenZones of
    /// them); @p medianMs is stored as given. For a look at a frame that is not a spike, such as the one chosen for the export
    [[nodiscard]] FrozenFrame FreezeFrame(const RingSnapshot& snapshot, const RingFrame& frame, double medianMs);

    /// One frozen frame as a one-frame snapshot, so the timeline shows it through the code that shows a live one
    [[nodiscard]] RingSnapshot ToSnapshot(const FrozenFrame& frame);

}  // namespace BECore
