#pragma once

#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Renderer/RenderCounters.h>
#include <EASTL/array.h>

namespace BECore {

    /// The timed stages of a frame (CoreManager::OnGameCycle, Scene::Draw, the present in Application::RunFrame)
    CORE_ENUM(FrameStage, uint8_t, Update, SceneUpdate, Render, SceneDraw, Present)

    inline constexpr size_t kFrameStageCount = 5;
    inline constexpr size_t kMaxFrameCounters = 16;

    struct FrameCounter {
        PoolString name;
        double value = 0.0;
    };

    /// What one frame cost
    struct FrameStats {
        uint64_t frame = 0;                                // 1, 2, ... in the order the collector saw them
        bool paused = false;                               // a held frame: the game did not advance (a stepped frame is a game frame)
        double frameMs = 0.0;                              // from the end of the frame before to the end of this one (the first frame: its cpuMs)
        double cpuMs = 0.0;                                // from BeginFrame to EndFrame
        eastl::array<double, kFrameStageCount> stageMs{};  // by FrameStage; SceneUpdate is inside Update, SceneDraw inside Render
        RenderCounters draws;
        uint64_t allocCount = 0;  // the allocations of the frame (ENGINE_PROFILER_ALLOC; 0 without it)
        uint64_t allocBytes = 0;
        uint64_t liveBytes = 0;  // at the end of the frame
        eastl::fixed_vector<FrameCounter, kMaxFrameCounters, false> counters;

        [[nodiscard]] double GetStageMs(FrameStage stage) const {
            return stageMs[+stage];
        }
        /// The value of the counter @p name this frame, nullopt when nobody set it
        [[nodiscard]] std::optional<double> FindCounter(PoolString name) const;
    };

    /**
     * @brief The statistics of the last kFrameHistory frames: times of the stages, draws, allocations and named counters.
     *
     * Always on (it costs a few clock reads per frame), main thread only. Application::RunFrame opens and closes the frame and times
     * the present; CoreManager::OnGameCycle and Scene::Draw time their stages through StageScope; a module adds a named value with
     * SetCounter during the frame (ImGuiWidget: its own draw calls, which the renderer's counters do not see).
     * Read by the control method `frame_stats` and the overlays.
     */
    class FrameStatsCollector {
    public:
        static constexpr size_t kFrameHistory = 240;

        using NowFn = uint64_t (*)();  // nanoseconds of a monotonic clock

        FrameStatsCollector();

        /// Times StageScope with @p now instead of the real clock (tests); nullptr: the real clock
        void SetClock(NowFn now);

        /// Opens a frame; @p held marks one in which the game stood still (FrameKind::Held), so tools do not average it with game frames
        void BeginFrame(bool held = false);
        void BeginStage(FrameStage stage);
        void EndStage(FrameStage stage);
        /// Sets @p name to @p value for the open frame (the last value wins); dropped past kMaxFrameCounters names
        void SetCounter(PoolString name, double value);
        /// Closes the frame with what the renderer counted and the allocation totals now; no effect without BeginFrame
        void EndFrame(const RenderCounters& draws, const AllocTotals& allocs);

        /// The last @p count frames (fewer when fewer were seen), the oldest first
        [[nodiscard]] eastl::vector<FrameStats> GetHistory(size_t count) const;
        /// The last finished frame, nullptr before the first
        [[nodiscard]] const FrameStats* GetLast() const;
        [[nodiscard]] uint64_t GetFrameCount() const {
            return _frames;
        }

        /// Times one stage of the collector for the length of a scope
        class StageScope {
        public:
            StageScope(FrameStatsCollector& collector, FrameStage stage) : _collector(collector), _stage(stage) {
                _collector.BeginStage(_stage);
            }
            ~StageScope() {
                _collector.EndStage(_stage);
            }
            StageScope(const StageScope&) = delete;
            StageScope& operator=(const StageScope&) = delete;

        private:
            FrameStatsCollector& _collector;
            FrameStage _stage;
        };

    private:
        [[nodiscard]] uint64_t Now() const;

        NowFn _now = nullptr;
        eastl::vector<FrameStats> _ring;  // kFrameHistory entries, the newest at (_frames - 1) % kFrameHistory
        uint64_t _frames = 0;
        bool _open = false;
        FrameStats _current;
        uint64_t _frameBegin = 0;
        uint64_t _lastFrameEnd = 0;
        eastl::array<uint64_t, kFrameStageCount> _stageBegin{};
        AllocTotals _allocsAtBegin;
    };

}  // namespace BECore
