#pragma once

#include <BECore/Profiler/Ring/RingSnapshot.h>
#include <BECore/Types/Types.h>
#include <expected>

namespace BECore {

    /// How many frames WriteRingTraceJson puts into the trace for the same arguments (0 for an empty snapshot)
    [[nodiscard]] size_t CountRingTraceFrames(const RingSnapshot& snapshot, double seconds);

    /**
     * @brief The last @p seconds of a ring snapshot as a Chrome trace, in the shape of WriteFrozenTraceJson (FrozenTrace.h).
     *
     * The window holds the frames that end within @p seconds before the end of the newest frame; the newest frame is always in, also
     * for @p seconds <= 0. Each frame is cut with FreezeFrame. The frames are consecutive here, so each frame's begin gets an instant
     * event named `Frame` and `trace_summary.py` reads the gaps as frame times (the frozen frames use `Frozen frame` for the opposite
     * reason). `otherData` holds `ringFrames`, `dropped` (zones the ring or a frame's zone limit left out) and `build` (`stats`).
     * An empty snapshot is a valid empty trace. Allocation counts are in the `E` events only with ENGINE_PROFILER_ALLOC.
     */
    [[nodiscard]] String WriteRingTraceJson(const RingSnapshot& snapshot, double seconds);

    /// Writes WriteRingTraceJson to @p path; the error names the file and the reason, as WriteFrozenTraceFile does
    [[nodiscard]] std::expected<void, String> WriteRingTraceFile(StringView path, const RingSnapshot& snapshot, double seconds);

}  // namespace BECore
