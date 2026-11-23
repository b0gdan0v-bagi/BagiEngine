#pragma once

#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/FunctionRef.h>
#include <cstdint>

namespace BECore {

    /// The lines of the Game overlay for @p stats (the last frame) and the heap @p totals; @p allocTracking: whether the build counts
    /// allocations (AllocTracker::kEnabled). Pure, so a test reads the text without a renderer
    [[nodiscard]] Vector<String> FormatOverlayLines(const FrameStats& stats, const AllocTotals& totals, bool allocTracking);

    /// The longest overlay line FormatOverlayLinesTo hands out; a longer one is cut at this length
    inline constexpr size_t kOverlayLineCapacity = 256;

    /// FormatOverlayLines without a container: each line is formatted into a stack buffer and given to @p emit, whose view is valid only
    /// during the call. A line longer than kOverlayLineCapacity is cut. Allocates nothing
    void FormatOverlayLinesTo(const FrameStats& stats, const AllocTotals& totals, bool allocTracking, FunctionRef<void(StringView)> emit);

    /// @p bytes as "512 B", "3.4 KB", "12.3 MB"
    [[nodiscard]] String FormatBytes(uint64_t bytes);

}  // namespace BECore
