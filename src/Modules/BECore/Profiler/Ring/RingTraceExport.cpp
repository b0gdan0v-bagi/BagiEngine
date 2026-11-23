#include "RingTraceExport.h"

#include <BECore/Profiler/Backends/ChromeTraceJson.h>
#include <BECore/Profiler/Ring/FrozenTrace.h>
#include <BECore/Profiler/Ring/SpikeCapture.h>

namespace BECore {

    namespace {

        constexpr double kNanosecondsPerSecond = 1.0e9;

        /// The first tick a frame of the window may end at; 0 when the window reaches back past the ring's start
        uint64_t WindowStartTicks(const RingSnapshot& snapshot, double seconds) {
            const uint64_t newestEnd = snapshot.frames.back().endTicks;
            if (seconds <= 0.0 || snapshot.nanosecondsPerTick <= 0.0) {
                return newestEnd;
            }
            const double windowTicks = seconds * kNanosecondsPerSecond / snapshot.nanosecondsPerTick;
            return windowTicks >= static_cast<double>(newestEnd) ? 0 : newestEnd - static_cast<uint64_t>(windowTicks);
        }

    }  // namespace

    String WriteRingTraceJson(const RingSnapshot& snapshot, double seconds) {
        Vector<FrozenFrame> frames;
        if (!snapshot.frames.empty()) {
            const uint64_t windowStart = WindowStartTicks(snapshot, seconds);
            const RingFrame& newest = snapshot.frames.back();
            for (const RingFrame& frame : snapshot.frames) {
                if (&frame == &newest || frame.endTicks >= windowStart) {
                    frames.push_back(FreezeFrame(snapshot, frame, 0.0));
                }
            }
        }
        return WriteFramesTraceJson(frames, FramesTraceFormat{.markName = "Frame", .countKey = "ringFrames", .extraDropped = snapshot.dropped, .extraMembers = ",\"build\":\"stats\""});
    }

    std::expected<void, String> WriteRingTraceFile(StringView path, const RingSnapshot& snapshot, double seconds) {
        return WriteTextFile(path, WriteRingTraceJson(snapshot, seconds));
    }

}  // namespace BECore
