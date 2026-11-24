#include "ProfilerControl.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlParams.h>
#include <BECore/Control/ControlService.h>
#include <BECore/File/Path.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/Backends/ChromeTraceBackend.h>
#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Profiler/Profiler.h>
#include <BECore/Profiler/ProfilerAnswers.h>
#include <EASTL/sort.h>

namespace BECore {

    namespace {

        constexpr StringView kNotRecording = "not_recording";
        constexpr StringView kDefaultTracePath = "trace.json";
        constexpr int64_t kDefaultTopZones = 20;
        constexpr int64_t kMaxTopZones = 1000;

        /// Writes an object of @p count members whose names @p nameOf gives in no particular order, in byte order of the names as a `Json` object keeps them
        /// (the bytes a client has always seen). Up to kInlineMembers names sort on the stack
        template <typename TName, typename TWrite>
        void WriteMembersByName(JsonWriter& writer, size_t count, TName nameOf, TWrite writeValue) {
            constexpr size_t kInlineMembers = 16;
            SmallVector<size_t, kInlineMembers> order;
            order.resize(count);
            for (size_t i = 0; i < count; ++i) {
                order[i] = i;
            }
            eastl::sort(order.begin(), order.end(), [&](size_t left, size_t right) { return nameOf(left) < nameOf(right); });
            writer.BeginObject();
            for (const size_t index : order) {
                writer.Key(nameOf(index));
                writeValue(index);
            }
            writer.EndObject();
        }

        void WriteFrame(JsonWriter& writer, const FrameStats& stats) {
            writer.BeginObject();
            writer.Member("allocBytes", stats.allocBytes);
            writer.Member("allocCount", stats.allocCount);
            writer.Member("arenaOverflows", stats.arenaOverflows);
            writer.Member("arenaPeakBytes", stats.arenaPeakBytes);
            writer.Member("arenaUsedBytes", stats.arenaUsedBytes);
            writer.Key("counters");
            WriteMembersByName(writer, stats.counters.size(), [&](size_t i) { return stats.counters[i].name.ToStringView(); }, [&](size_t i) { writer.Value(stats.counters[i].value); });
            writer.Member("cpuMs", stats.cpuMs);
            writer.Member("drawCalls", stats.draws.drawCalls);
            writer.Member("frame", stats.frame);
            writer.Member("frameMs", stats.frameMs);
            writer.Member("liveBytes", stats.liveBytes);
            writer.Member("paused", stats.paused);
            writer.Member("poolLiveBlocks", stats.poolLiveBlocks);
            writer.Member("poolPageBytes", stats.poolPageBytes);
            writer.Key("stagesMs");
            WriteMembersByName(writer, kFrameStageCount, [](size_t i) { return EnumUtils<FrameStage>::ToString(static_cast<FrameStage>(i)); }, [&](size_t i) { writer.Value(stats.stageMs[i]); });
            writer.Key("tagAllocCount");
            WriteMembersByName(writer, kMemTagCount, [](size_t i) { return EnumUtils<MemTag>::ToString(static_cast<MemTag>(i)); }, [&](size_t i) { writer.Value(stats.tagAllocCount[i]); });
            writer.Key("tagLiveBytes");
            WriteMembersByName(writer, kMemTagCount, [](size_t i) { return EnumUtils<MemTag>::ToString(static_cast<MemTag>(i)); }, [&](size_t i) { writer.Value(stats.tagLiveBytes[i]); });
            writer.Member("textureSwitches", stats.draws.textureSwitches);
            writer.Member("triangles", stats.draws.triangles);
            writer.EndObject();
        }

        /// A positive integer member of at most @p max, or @p fallback when absent
        std::expected<int64_t, ControlError> ReadCount(JsonView params, StringView key, int64_t fallback, int64_t max) {
            if (!FindParam(params, key)) {
                return fallback;
            }
            const auto value = GetParamInt(params, key);
            if (!value || *value < 1 || *value > max) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'{}' has to be an integer from 1 to {}", key, max)));
            }
            return *value;
        }

        void WriteArenas(JsonWriter& writer, const Vector<ArenaStatsEntry>& arenas) {
            writer.BeginArray();
            for (const ArenaStatsEntry& entry : arenas) {
                ProfilerArenaRow::Read(entry).WriteJson(writer);
            }
            writer.EndArray();
        }

        void WriteTags(JsonWriter& writer) {
            const Array<AllocTagTotals, kMemTagCount> totals = AllocTracker::GetTagTotals();
            writer.BeginArray();
            for (size_t i = 0; i < kMemTagCount; ++i) {
                ProfilerTagRow::Read(totals[i], static_cast<MemTag>(i)).WriteJson(writer);
            }
            writer.EndArray();
        }

        void WritePools(JsonWriter& writer, const Vector<PoolStatsEntry>& pools) {
            writer.BeginArray();
            for (const PoolStatsEntry& entry : pools) {
                ProfilerPoolRow::Read(entry).WriteJson(writer);
            }
            writer.EndArray();
        }

        /// Streamed by hand, not a reflected struct: `frames` is written straight from the history copy (up to a few hundred frames), and each frame holds objects keyed
        /// by names a module chooses (`counters`, `stagesMs`, `tagAllocCount`, `tagLiveBytes`), which a reflected map cannot write (it is an array of `{key, value}`).
        /// The rows of `arenas`, `pools` and `tags` are reflected structs written one at a time: a `Vector` of them would allocate, and the answer allocates nothing
        /// beyond the stats it reads. `allocTracking` is one flag of the same object, written beside the streamed `frames`
        void HandleFrameStats(ControlCall& call) {
            const auto frames = ReadCount(call.Params(), "frames", 1, static_cast<int64_t>(FrameStatsCollector::kFrameHistory));
            if (!frames) {
                call.Fail(frames.error());
                return;
            }
            // The stats are read first: they allocate (Vector of copies), the writing does not
            const Vector<FrameStats> history = CoreManager::GetFrameStats().GetHistory(static_cast<size_t>(*frames));
            const Vector<ArenaStatsEntry> arenas = AllocTracker::GetArenaStats();
            const Vector<PoolStatsEntry> pools = AllocTracker::GetPoolStats();
            call.Reply([&](JsonWriter& writer) {
                writer.BeginObject();
                writer.Member("allocTracking", AllocTracker::kEnabled);
                writer.Key("arenas");
                WriteArenas(writer, arenas);
                writer.Key("frames");
                writer.BeginArray();
                for (const FrameStats& stats : history) {
                    WriteFrame(writer, stats);
                }
                writer.EndArray();
                writer.Key("pools");
                WritePools(writer, pools);
                writer.Key("tags");
                WriteTags(writer);
                writer.EndObject();
            });
        }

        /// @p text as a trace path the game may write: relative and inside the working folder
        bool IsSafeRelativePath(StringView text) {
            // Through Path so that a backslash is a separator on Windows, as it is for the file that gets written
            const Path path(text);
            const StringView view = path.View();
            if (view.empty() || PathIsAbsolute(view) || PathIsSeparator(view.front())) {
                return false;
            }
#ifdef _WIN32
            // `C:file` is not absolute but names the working folder of another drive
            constexpr size_t kDriveColon = 1;
            if (view.size() > kDriveColon && view[kDriveColon] == ':') {
                return false;
            }
#endif
            constexpr StringView kParentName = "..";
            for (size_t begin = 0; begin <= view.size();) {
                size_t end = begin;
                while (end < view.size() && !PathIsSeparator(view[end])) {
                    ++end;
                }
                if (view.substr(begin, end - begin) == kParentName) {
                    return false;
                }
                begin = end + 1;
            }
            return true;
        }

        void HandleProfilerDump(ControlCall& call) {
            String path(kDefaultTracePath.data(), kDefaultTracePath.size());
            const JsonView params = call.Params();
            if (FindParam(params, "path")) {
                const auto given = GetParamString(params, "path");
                if (!given || !IsSafeRelativePath(*given)) {
                    call.Fail(ControlErrors::kInvalidParams, "'path' has to be a relative file name without '..'");
                    return;
                }
                path = *given;
            }
            ChromeTraceBackend& recorder = Profiler::GetChromeTrace();
            if (Profiler::GetPrimaryBackend() != &recorder) {
                call.Fail(kNotRecording, "the Chrome trace is not recording: start the game with --trace=<file> or --profiler=chrome (a build with ENGINE_PROFILER)");
                return;
            }
            const auto written = recorder.WriteFile(path);
            if (!written) {
                call.Fail(ControlErrors::kInternalError, written.error());
                return;
            }
            const uint64_t dropped = recorder.GetDroppedCount();
            ProfilerDumpAnswer answer;
            answer.dropped = dropped;
            answer.path = TextView(path);
            call.ReplyObject(answer);
        }

        void HandleZoneAllocs(ControlCall& call) {
            const auto top = ReadCount(call.Params(), "top", kDefaultTopZones, kMaxTopZones);
            if (!top) {
                call.Fail(top.error());
                return;
            }
            bool reset = false;
            if (FindParam(call.Params(), "reset")) {
                const auto value = GetParamBool(call.Params(), "reset");
                if (!value) {
                    call.Fail(ControlErrors::kInvalidParams, "'reset' has to be a boolean");
                    return;
                }
                reset = *value;
            }

            const Vector<ZoneAllocStats> zones = AllocTracker::GetZoneStats();
            const Vector<AllocSiteStats> names = AllocTracker::GetNameStats();
            const Vector<AllocSizeStats> sizes = AllocTracker::GetSizeStats();
            const AllocTotals totals = AllocTracker::GetTotals();
            ZoneAllocsAnswer answer;
            answer.enabled = AllocTracker::kEnabled;
            const size_t limit = static_cast<size_t>(*top);
            answer.names.reserve(std::min(names.size(), limit));
            answer.sizes.reserve(std::min(sizes.size(), limit));
            answer.zones.reserve(std::min(zones.size(), limit));
            for (size_t i = 0; i < names.size() && i < limit; ++i) {
                answer.names.push_back(ProfilerAllocName::Read(names[i]));
            }
            for (size_t i = 0; i < sizes.size() && i < limit; ++i) {
                answer.sizes.push_back(ProfilerAllocSizes::Read(sizes[i]));
            }
            answer.totals = ProfilerAllocTotals::Read(totals);
            for (size_t i = 0; i < zones.size() && i < limit; ++i) {
                answer.zones.push_back(ProfilerAllocZone::Read(zones[i]));
            }
            call.ReplyObject(answer);
            if (reset) {
                AllocTracker::ResetZoneStats();
            }
        }

    }  // namespace

    void RegisterProfilerControl(ControlService& service) {
        service.RegisterHandler("frame_stats", HandleFrameStats);
        service.RegisterHandler("profiler_dump", HandleProfilerDump);
        service.RegisterHandler("zone_allocs", HandleZoneAllocs);

        service.DescribeMethod("frame_stats", {"The last frames with stage times, draws, allocations, arena and pool totals and counters, oldest first; arenas, pools: the counters of every arena and "
                                               "block pool; tags: the totals of every memory tag, each frame's tagAllocCount and tagLiveBytes by tag name",
                                               {{"frames", "integer", false, Format("How many frames, 1 to {} (default 1)", FrameStatsCollector::kFrameHistory)}}});
        service.DescribeMethod("profiler_dump", {"Writes the Chrome trace of the run (the game has to record one)",
                                                 {{"path", "string", false, Format("A relative file name without '..' (default {})", kDefaultTracePath)}}});
        service.DescribeMethod("zone_allocs", {"Allocations per profiler zone, per zone and allocation name (names), per zone and size bucket (sizes) and the totals",
                                               {{"top", "integer", false, Format("How many zones, names and size histograms, 1 to {} (default {})", kMaxTopZones, kDefaultTopZones)},
                                                {"reset", "boolean", false, "Clear the per-zone, per-name and per-size counts after answering"}}});
    }

}  // namespace BECore
