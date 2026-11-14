#pragma once

#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Profiler/Ring/RingSnapshot.h>

namespace BECore {

    /// One row of the zone table: every call of one site on every thread inside the window
    struct ZoneStatRow {
        const ProfileSite* site = nullptr;
        uint64_t calls = 0;
        double totalMs = 0.0;
        double selfMs = 0.0;
        double meanMs = 0.0;
        double maxMs = 0.0;
        double p95Ms = 0.0;
        uint64_t allocCount = 0;  // inclusive of the zones inside, as ZoneAllocs is (zero without ENGINE_PROFILER_ALLOC)
        uint64_t allocBytes = 0;
    };

    CORE_ENUM(ZoneStatColumn, uint8_t, Name, Calls, Total, Self, Mean, Max, P95, Allocations)

    /**
     * @brief The statistics per zone site over the frames @p firstFrame to @p lastFrame (inclusive values of RingFrame::index).
     *
     * Frames of the snapshot are consecutive, so the window is one tick range, and a zone belongs to it when its begin tick is in it
     * (the rule of RingSnapshot). Frames of the window the ring no longer has are skipped; with none left the result is empty.
     *
     * **Total** is the sum of the durations of the calls: a zone inside itself (recursion) counts once per call, as the Chrome trace
     * shows it. **Self** is a call's duration minus the durations of the zones directly inside it on its thread (found by walking the
     * zones in begin order with a stack; a child is a zone that lies inside the interval of the one on top), also for a call whose
     * children begin in a later frame. **p95** is the nearest-rank percentile of the per-call durations: the value at rank
     * ceil(0.95 * n) of the n sorted calls, so one call is its own duration. The rows come in the order the sites were first met.
     */
    [[nodiscard]] eastl::vector<ZoneStatRow> ComputeZoneStats(const RingSnapshot& snapshot, uint64_t firstFrame, uint64_t lastFrame);

    /// Orders the rows by @p column; equal values are ordered by name, then file and line, in either direction of the sort, so the order is stable
    void SortZoneStats(eastl::vector<ZoneStatRow>& rows, ZoneStatColumn column, bool ascending);

    /// The rows whose zone name or `file:line` contains @p text, ignoring case; empty text keeps every row
    [[nodiscard]] eastl::vector<ZoneStatRow> FilterZoneStats(const eastl::vector<ZoneStatRow>& rows, StringView text);

    /// The segments of the stacked stage bar, in the order they are stacked from the bottom
    CORE_ENUM(ExclusiveStage, uint8_t, UpdateOnly, SceneUpdateStage, RenderOnly, SceneDrawStage, PresentStage, OtherStage)

    inline constexpr size_t kExclusiveStageCount = 6;

    /**
     * @brief The stage times of a frame without double counting (FrameStats::stageMs has SceneUpdate inside Update and SceneDraw inside
     * Render): Update and Render lose their inner stage, and Other is what is left of cpuMs. A stage longer than the one around it (a
     * clock hiccup) and a sum over cpuMs give 0 for that segment, never a negative one, so the segments sum to cpuMs unless the stages
     * together exceed it.
     */
    [[nodiscard]] eastl::array<double, kExclusiveStageCount> ExclusiveStageMs(const FrameStats& frame);

}  // namespace BECore
