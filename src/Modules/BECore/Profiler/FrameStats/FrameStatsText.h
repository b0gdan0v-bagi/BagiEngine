#pragma once

#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>

namespace BECore {

    /// The lines of the Game overlay for @p stats (the last frame) and the heap @p totals; @p allocTracking: whether the build counts
    /// allocations (AllocTracker::kEnabled). Pure, so a test reads the text without a renderer
    [[nodiscard]] eastl::vector<eastl::string> FormatOverlayLines(const FrameStats& stats, const AllocTotals& totals, bool allocTracking);

    /// @p bytes as "512 B", "3.4 KB", "12.3 MB"
    [[nodiscard]] eastl::string FormatBytes(uint64_t bytes);

}  // namespace BECore
