#include "FrozenTrace.h"

#include <BECore/Profiler/Backends/ChromeTraceJson.h>
#include <BECore/Profiler/Profiler.h>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace BECore {

    namespace {

        constexpr int kPid = 1;
        constexpr double kNanosecondsPerMicrosecond = 1000.0;

        /// One zone of a thread with the factor that turns its ticks into time
        struct ThreadZone {
            ZoneRecord zone;
            double nanosecondsPerTick;
        };

        struct ThreadLane {
            uint32_t threadId = 0;
            String name;
            uint32_t sortIndex = Profiler::kNoSortIndex;
            Vector<ThreadZone> zones;
        };

        ThreadLane& LaneOf(Vector<ThreadLane>& lanes, const RingThread& thread) {
            for (ThreadLane& lane : lanes) {
                if (lane.threadId == thread.threadId) {
                    return lane;
                }
            }
            ThreadLane& lane = lanes.emplace_back();
            lane.threadId = thread.threadId;
            lane.name = thread.name;
            lane.sortIndex = thread.sortIndex;
            return lane;
        }

    }  // namespace

    String WriteFrozenTraceJson(const Vector<FrozenFrame>& frames) {
        uint64_t start = frames.empty() ? 0 : frames[0].beginTicks;
        uint64_t truncated = 0;
        Vector<ThreadLane> lanes;
        for (const FrozenFrame& frame : frames) {
            start = eastl::min(start, frame.beginTicks);
            truncated += frame.truncatedZones;
            for (const RingThread& thread : frame.threads) {
                ThreadLane& lane = LaneOf(lanes, thread);
                for (const ZoneRecord& zone : thread.zones) {
                    lane.zones.push_back({zone, frame.nanosecondsPerTick});
                }
            }
        }
        const auto timestamp = [start](uint64_t ticks, double nanosecondsPerTick) {
            return ticks > start ? static_cast<double>(ticks - start) * nanosecondsPerTick / kNanosecondsPerMicrosecond : 0.0;
        };

        String out;
        out.append("{\"traceEvents\":[");
        bool first = true;
        const auto beginEvent = [&out, &first] {
            out.append(first ? "\n" : ",\n");
            first = false;
        };

        for (ThreadLane& lane : lanes) {
            if (!lane.name.empty()) {
                beginEvent();
                AppendThreadNameEvent(out, kPid, lane.threadId, lane.name.c_str());
                if (lane.sortIndex != Profiler::kNoSortIndex) {
                    beginEvent();
                    AppendThreadSortIndexEvent(out, kPid, lane.threadId, lane.sortIndex);
                }
            }

            // Frames come oldest first but a zone may begin together with its parent: the parent goes first
            //-NoStd: equal zones keep the order they were recorded in; one sort per frozen trace, not per frame
            eastl::stable_sort(lane.zones.begin(), lane.zones.end(),
                               [](const ThreadZone& a, const ThreadZone& b) { return a.zone.beginTicks != b.zone.beginTicks ? a.zone.beginTicks < b.zone.beginTicks : a.zone.depth < b.zone.depth; });

            // The zones nest (a zone lies inside the one on top of the stack or after it), so a B/E pair per zone is a well-formed stack
            Vector<const ThreadZone*> open;
            const auto endEvent = [&](const ThreadZone& zone, uint64_t endTicks) {
                beginEvent();
                AppendZoneEndEvent(out, kPid, lane.threadId, timestamp(endTicks, zone.nanosecondsPerTick), zone.zone.allocCount, zone.zone.allocBytes);
            };
            for (const ThreadZone& entry : lane.zones) {
                while (!open.empty() && (open.back()->zone.endTicks < entry.zone.beginTicks || open.back()->zone.endTicks < entry.zone.endTicks)) {
                    endEvent(*open.back(), eastl::min(open.back()->zone.endTicks, entry.zone.beginTicks));
                    open.pop_back();
                }
                beginEvent();
                AppendZoneBeginEvent(out, kPid, lane.threadId, timestamp(entry.zone.beginTicks, entry.nanosecondsPerTick), *entry.zone.site);
                open.push_back(&entry);
            }
            while (!open.empty()) {
                endEvent(*open.back(), open.back()->zone.endTicks);
                open.pop_back();
            }
        }

        for (const FrozenFrame& frame : frames) {
            beginEvent();
            AppendFormat(out, "{{\"name\":\"Frozen frame\",\"ph\":\"i\",\"s\":\"g\",\"pid\":{},\"tid\":0,\"ts\":{:.3f},\"args\":{{\"frame\":{},\"ms\":{:.3f}}}}}", kPid,
                         timestamp(frame.beginTicks, frame.nanosecondsPerTick), frame.frameIndex, frame.frameMs);
        }

        AppendFormat(out, "\n],\"displayTimeUnit\":\"ms\",\"otherData\":{{\"frozenFrames\":{},\"dropped\":{}}}}}\n", frames.size(), truncated);
        return out;
    }

    std::expected<void, String> WriteFrozenTraceFile(StringView path, const Vector<FrozenFrame>& frames) {
        return WriteTextFile(path, WriteFrozenTraceJson(frames));
    }

}  // namespace BECore
