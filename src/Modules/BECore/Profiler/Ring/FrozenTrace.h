#pragma once

#include <BECore/Profiler/Ring/SpikeCapture.h>

namespace BECore {

    /**
     * @brief Frozen frames as a Chrome trace, in the format of ChromeTraceBackend::WriteJson, so Perfetto and tools/perf/trace_summary.py read it.
     *
     * Zones are `B` / `E` pairs per thread (`args` `file`, `line`, and with ENGINE_PROFILER_ALLOC on the `E` the inclusive `allocs` and
     * `bytes`), thread names and sort indexes are `M` events, time is in microseconds from the earliest begin tick of the frames, so
     * frames keep their real offsets. Each frame also gets an instant event named `Frozen frame` (`args` `frame`, `ms`) at its begin: not
     * `Frame`, which the summary tool turns into frame times, because the gap between two frozen frames is not a frame.
     * `otherData` holds `frozenFrames` and `dropped` (zones left out of the frames by SpikeCapture::kMaxFrozenZones). An empty list is a valid trace.
     * The time unit of each frame is its own nanosecondsPerTick.
     */
    [[nodiscard]] eastl::string WriteFrozenTraceJson(const eastl::vector<FrozenFrame>& frames);

    /// Writes WriteFrozenTraceJson to @p path; the error names the file and the reason, as ChromeTraceBackend::WriteFile does
    [[nodiscard]] std::expected<void, eastl::string> WriteFrozenTraceFile(eastl::string_view path, const eastl::vector<FrozenFrame>& frames);

}  // namespace BECore
