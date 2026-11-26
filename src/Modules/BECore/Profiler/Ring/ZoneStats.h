#pragma once

#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Profiler/Ring/RingSnapshot.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstddef>
#include <cstdint>

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
     * shows it. **Self** is a call's duration minus the durations of the zones directly inside it on its thread (a child is a zone
     * inside its interval that no other zone inside it contains), also for a call whose
     * children begin in a later frame. **p95** is the nearest-rank percentile of the per-call durations: the value at rank
     * ceil(0.95 * n) of the n sorted calls, so one call is its own duration. The rows come in the order the sites were first met.
     */
    [[nodiscard]] Vector<ZoneStatRow> ComputeZoneStats(const RingSnapshot& snapshot, uint64_t firstFrame, uint64_t lastFrame);

    /// The same into @p rows, whose previous content is dropped and whose capacity is kept; the working lists are on the thread's scratch
    /// arena, so a caller that keeps @p rows allocates nothing once the rows and the arena have grown to the window
    void ComputeZoneStats(const RingSnapshot& snapshot, uint64_t firstFrame, uint64_t lastFrame, Vector<ZoneStatRow>& rows);

    /// <0, 0, >0 by zone name, then file, then line: the order that settles equal values
    [[nodiscard]] int CompareZoneStatIdentity(const ZoneStatRow& a, const ZoneStatRow& b);

    /// Orders the rows by @p column; equal values are ordered by name, then file and line, in either direction of the sort, so the order is stable
    void SortZoneStats(Vector<ZoneStatRow>& rows, ZoneStatColumn column, bool ascending);

    /// The rows whose zone name or `file:line` contains @p text, ignoring case; empty text keeps every row
    [[nodiscard]] Vector<ZoneStatRow> FilterZoneStats(const Vector<ZoneStatRow>& rows, StringView text);

    /// The segments of the stacked stage bar, in the order they are stacked from the bottom. WaitStage is last on purpose: "work only"
    /// is every segment but the last
    CORE_ENUM(ExclusiveStage, uint8_t, UpdateOnly, SceneUpdateStage, RenderOnly, SceneDrawStage, PresentStage, OtherStage, WaitStage)

    inline constexpr size_t kExclusiveStageCount = 7;
    static_assert(kExclusiveStageCount == EnumUtils<ExclusiveStage>::Count(), "kExclusiveStageCount follows ExclusiveStage");

    /**
     * @brief The stage times of a frame without double counting (FrameStats::stageMs has SceneUpdate inside Update and SceneDraw inside
     * Render): Update and Render lose their inner stage, and Other is what is left of cpuMs after every other segment, Wait included.
     * Submit is nested inside whatever stage flushed, so it is no segment of its own and stays part of that stage. A stage longer than the
     * one around it (a clock hiccup) and a sum over cpuMs give 0 for that segment, never a negative one, so the segments sum to cpuMs
     * unless the stages together exceed it.
     */
    [[nodiscard]] Array<double, kExclusiveStageCount> ExclusiveStageMs(const FrameStats& frame);

    /// What the CPU did in the frame: cpuMs without the time the backend waited for the display (never negative)
    [[nodiscard]] double FrameWorkMs(const FrameStats& frame);

    /// The mean work and the mean wait of a history
    struct WorkWaitMean {
        double workMs = 0.0;
        double waitMs = 0.0;
    };

    /// The means of FrameWorkMs and of the Wait stage over the frames of @p history that are not paused (a held frame is no game frame);
    /// zeros when none is left
    [[nodiscard]] WorkWaitMean SummarizeWorkWait(const Vector<FrameStats>& history);

}  // namespace BECore
