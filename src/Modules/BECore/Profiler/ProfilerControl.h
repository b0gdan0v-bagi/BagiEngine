#pragma once

namespace BECore {

    class ControlService;

    /**
     * @brief Registers the profiler's methods of the control API (CoreManager::InitializeCoreServices).
     *
     * | Method | Params | Result |
     * |--------|--------|--------|
     * | frame_stats | frames? (1..FrameStatsCollector::kFrameHistory, default 1) | {frames: [...oldest first], allocTracking} |
     * | profiler_dump | path? (relative, no "..", default "trace.json") | {path, dropped}; `not_recording` when the Chrome trace is not the backend |
     * | zone_allocs | top? (1..1000, default 20), reset? | {enabled, zones: [{name, file, line, calls, allocs, bytes}], totals}; reset zeroes the zone statistics after the answer |
     */
    void RegisterProfilerControl(ControlService& service);

}  // namespace BECore
