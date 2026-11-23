#include "RingTraceExport.h"

#include <BECore/Profiler/Backends/ChromeTraceJson.h>
#include <BECore/Profiler/ProfilerToggle.h>
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

        /// The newest frame is always in, also when it ended before the window start
        bool IsInWindow(const RingSnapshot& snapshot, const RingFrame& frame, uint64_t windowStart) {
            return &frame == &snapshot.frames.back() || frame.endTicks >= windowStart;
        }

    }  // namespace

    size_t CountRingTraceFrames(const RingSnapshot& snapshot, double seconds) {
        if (snapshot.frames.empty()) {
            return 0;
        }
        const uint64_t windowStart = WindowStartTicks(snapshot, seconds);
        size_t count = 0;
        for (const RingFrame& frame : snapshot.frames) {
            count += IsInWindow(snapshot, frame, windowStart) ? 1 : 0;
        }
        return count;
    }

    String WriteRingTraceJson(const RingSnapshot& snapshot, double seconds) {
        Vector<FrozenFrame> frames;
        if (!snapshot.frames.empty()) {
            const uint64_t windowStart = WindowStartTicks(snapshot, seconds);
            for (const RingFrame& frame : snapshot.frames) {
                if (IsInWindow(snapshot, frame, windowStart)) {
                    frames.push_back(FreezeFrame(snapshot, frame, 0.0));
                }
            }
        }
        return WriteFramesTraceJson(
            frames, FramesTraceFormat{
                        .markName = "Frame", .countKey = "ringFrames", .extraDropped = snapshot.dropped, .extraMembers = kProfilerStatsBuild ? ",\"build\":\"stats\"" : ",\"build\":\"profile\""});
    }

    std::expected<void, String> WriteRingTraceFile(StringView path, const RingSnapshot& snapshot, double seconds) {
        return WriteTextFile(path, WriteRingTraceJson(snapshot, seconds));
    }

}  // namespace BECore
