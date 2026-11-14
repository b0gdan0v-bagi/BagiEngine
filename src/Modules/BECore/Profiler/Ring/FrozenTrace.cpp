#include "FrozenTrace.h"

#include <BECore/Profiler/Backends/ChromeTraceJson.h>
#include <BECore/Profiler/Profiler.h>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <cerrno>
#include <cstdio>

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
            eastl::string name;
            uint32_t sortIndex = Profiler::kNoSortIndex;
            eastl::vector<ThreadZone> zones;
        };

        ThreadLane& LaneOf(eastl::vector<ThreadLane>& lanes, const RingThread& thread) {
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

    eastl::string WriteFrozenTraceJson(const eastl::vector<FrozenFrame>& frames) {
        uint64_t start = frames.empty() ? 0 : frames[0].beginTicks;
        uint64_t truncated = 0;
        eastl::vector<ThreadLane> lanes;
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

        eastl::string out;
        out.append("{\"traceEvents\":[");
        bool first = true;
        const auto beginEvent = [&out, &first] {
            out.append(first ? "\n" : ",\n");
            first = false;
        };

        for (ThreadLane& lane : lanes) {
            if (!lane.name.empty()) {
                beginEvent();
                out.append(Format("{{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":{},\"tid\":{},\"args\":{{\"name\":", kPid, lane.threadId));
                AppendJsonString(out, lane.name.c_str());
                out.append("}}");
                if (lane.sortIndex != Profiler::kNoSortIndex) {
                    beginEvent();
                    out.append(Format("{{\"name\":\"thread_sort_index\",\"ph\":\"M\",\"pid\":{},\"tid\":{},\"args\":{{\"sort_index\":{}}}}}", kPid, lane.threadId, lane.sortIndex));
                }
            }

            // Frames come oldest first but a zone may begin together with its parent: the parent goes first
            eastl::stable_sort(lane.zones.begin(), lane.zones.end(),
                               [](const ThreadZone& a, const ThreadZone& b) { return a.zone.beginTicks != b.zone.beginTicks ? a.zone.beginTicks < b.zone.beginTicks : a.zone.depth < b.zone.depth; });

            // The zones nest (a zone lies inside the one on top of the stack or after it), so a B/E pair per zone is a well-formed stack
            eastl::vector<const ThreadZone*> open;
            const auto endEvent = [&](const ThreadZone& zone, uint64_t endTicks) {
                beginEvent();
                out.append(Format("{{\"ph\":\"E\",\"pid\":{},\"tid\":{},\"ts\":{:.3f}", kPid, lane.threadId, timestamp(endTicks, zone.nanosecondsPerTick)));
                if constexpr (BE_PROFILER_ALLOC != 0) {
                    out.append(Format(",\"args\":{{\"allocs\":{},\"bytes\":{}}}", zone.zone.allocCount, zone.zone.allocBytes));
                }
                out.append("}");
            };
            for (const ThreadZone& entry : lane.zones) {
                while (!open.empty() && (open.back()->zone.endTicks < entry.zone.beginTicks || open.back()->zone.endTicks < entry.zone.endTicks)) {
                    endEvent(*open.back(), eastl::min(open.back()->zone.endTicks, entry.zone.beginTicks));
                    open.pop_back();
                }
                const ProfileSite* site = entry.zone.site;
                beginEvent();
                out.append("{\"name\":");
                AppendJsonString(out, site->name);
                out.append(
                    Format(",\"cat\":\"zone\",\"ph\":\"B\",\"pid\":{},\"tid\":{},\"ts\":{:.3f},\"args\":{{\"file\":", kPid, lane.threadId, timestamp(entry.zone.beginTicks, entry.nanosecondsPerTick)));
                AppendJsonString(out, site->file);
                out.append(Format(",\"line\":{}}}}}", site->line));
                open.push_back(&entry);
            }
            while (!open.empty()) {
                endEvent(*open.back(), open.back()->zone.endTicks);
                open.pop_back();
            }
        }

        for (const FrozenFrame& frame : frames) {
            beginEvent();
            out.append(Format("{{\"name\":\"Frozen frame\",\"ph\":\"i\",\"s\":\"g\",\"pid\":{},\"tid\":0,\"ts\":{:.3f},\"args\":{{\"frame\":{},\"ms\":{:.3f}}}}}", kPid,
                              timestamp(frame.beginTicks, frame.nanosecondsPerTick), frame.frameIndex, frame.frameMs));
        }

        out.append(Format("\n],\"displayTimeUnit\":\"ms\",\"otherData\":{{\"frozenFrames\":{},\"dropped\":{}}}}}\n", frames.size(), truncated));
        return out;
    }

    std::expected<void, eastl::string> WriteFrozenTraceFile(StringView path, const eastl::vector<FrozenFrame>& frames) {
        const eastl::string text = WriteFrozenTraceJson(frames);
        const eastl::string fileName(path.data(), path.size());
        std::FILE* file = std::fopen(fileName.c_str(), "wb");
        if (file == nullptr) {
            return std::unexpected(Format("cannot write '{}': {}", fileName, std::strerror(errno)));
        }
        const size_t written = std::fwrite(text.data(), 1, text.size(), file);
        const bool closed = std::fclose(file) == 0;
        if (written != text.size() || !closed) {
            return std::unexpected(Format("cannot write '{}': the disk refused part of it", fileName));
        }
        return {};
    }

}  // namespace BECore
