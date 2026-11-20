#include "ProfilerControl.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/Backends/ChromeTraceBackend.h>
#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Profiler/Profiler.h>

namespace BECore {

    namespace {

        constexpr StringView kNotRecording = "not_recording";
        constexpr StringView kDefaultTracePath = "trace.json";
        constexpr int64_t kDefaultTopZones = 20;
        constexpr int64_t kMaxTopZones = 1000;

        /// The zone x name rows of EASTL's allocations (Alloc/CLAUDE.md, "Names"), the most bytes first; empty without ENGINE_PROFILER_ALLOC
        Json DescribeNames(size_t top) {
            Json names = Json::array();
            for (const AllocSiteStats& stats : AllocTracker::GetNameStats()) {
                if (names.size() >= top) {
                    break;
                }
                Json row = Json::object();
                row["zone"] = stats.ZoneLabel();
                row["name"] = stats.name != nullptr ? stats.name : "";
                row["allocs"] = stats.count;
                row["bytes"] = stats.bytes;
                names.push_back(std::move(row));
            }
            return names;
        }

        Json DescribeFrame(const FrameStats& stats) {
            Json frame = Json::object();
            frame["frame"] = stats.frame;
            frame["paused"] = stats.paused;
            frame["frameMs"] = stats.frameMs;
            frame["cpuMs"] = stats.cpuMs;
            Json stages = Json::object();
            for (size_t i = 0; i < kFrameStageCount; ++i) {
                const StringView name = EnumUtils<FrameStage>::ToString(static_cast<FrameStage>(i));
                stages[ToStdString(name)] = stats.stageMs[i];
            }
            frame["stagesMs"] = std::move(stages);
            frame["drawCalls"] = stats.draws.drawCalls;
            frame["triangles"] = stats.draws.triangles;
            frame["textureSwitches"] = stats.draws.textureSwitches;
            frame["allocCount"] = stats.allocCount;
            frame["allocBytes"] = stats.allocBytes;
            frame["liveBytes"] = stats.liveBytes;
            frame["arenaUsedBytes"] = stats.arenaUsedBytes;
            frame["arenaPeakBytes"] = stats.arenaPeakBytes;
            frame["arenaOverflows"] = stats.arenaOverflows;
            Json counters = Json::object();
            for (const FrameCounter& counter : stats.counters) {
                const StringView name = counter.name.ToStringView();
                counters[ToStdString(name)] = counter.value;
            }
            frame["counters"] = std::move(counters);
            return frame;
        }

        /// A positive integer member of at most @p max, or @p fallback when absent
        std::expected<int64_t, ControlError> ReadCount(const Json& params, StringView key, int64_t fallback, int64_t max) {
            if (FindJsonMember(params, key) == nullptr) {
                return fallback;
            }
            const auto value = GetJsonInt(params, key);
            if (!value || *value < 1 || *value > max) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'{}' has to be an integer from 1 to {}", key, max)));
            }
            return *value;
        }

        Json DescribeArenas() {
            Json arenas = Json::array();
            for (const ArenaStatsEntry& entry : AllocTracker::GetArenaStats()) {
                Json described = Json::object();
                described["name"] = entry.name;
                described["capacity"] = entry.capacity;
                described["used"] = entry.used;
                described["peak"] = entry.peak;
                described["overflows"] = entry.overflows;
                described["chunks"] = entry.chunks;
                arenas.push_back(std::move(described));
            }
            return arenas;
        }

        void HandleFrameStats(ControlCall& call) {
            const auto frames = ReadCount(call.Params(), "frames", 1, static_cast<int64_t>(FrameStatsCollector::kFrameHistory));
            if (!frames) {
                call.Fail(frames.error());
                return;
            }
            Json list = Json::array();
            for (const FrameStats& stats : CoreManager::GetFrameStats().GetHistory(static_cast<size_t>(*frames))) {
                list.push_back(DescribeFrame(stats));
            }
            Json result = Json::object();
            result["frames"] = std::move(list);
            result["allocTracking"] = AllocTracker::kEnabled;
            result["arenas"] = DescribeArenas();
            call.Reply(std::move(result));
        }

        /// @p text as a trace path the game may write: relative and inside the working folder
        bool IsSafeRelativePath(StringView text) {
            if (text.empty()) {
                return false;
            }
            const std::filesystem::path path(ToStdString(text));
            if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
                return false;
            }
            for (const std::filesystem::path& part : path) {
                if (part == "..") {
                    return false;
                }
            }
            return true;
        }

        void HandleProfilerDump(ControlCall& call) {
            String path(kDefaultTracePath.data(), kDefaultTracePath.size());
            if (FindJsonMember(call.Params(), "path") != nullptr) {
                const auto given = GetJsonString(call.Params(), "path");
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
            Json result = Json::object();
            //-NoStd: a JSON string of nlohmann
            result["path"] = std::string(path.c_str());
            result["dropped"] = recorder.GetDroppedCount();
            call.Reply(std::move(result));
        }

        void HandleZoneAllocs(ControlCall& call) {
            const auto top = ReadCount(call.Params(), "top", kDefaultTopZones, kMaxTopZones);
            if (!top) {
                call.Fail(top.error());
                return;
            }
            bool reset = false;
            if (FindJsonMember(call.Params(), "reset") != nullptr) {
                const auto value = GetJsonBool(call.Params(), "reset");
                if (!value) {
                    call.Fail(ControlErrors::kInvalidParams, "'reset' has to be a boolean");
                    return;
                }
                reset = *value;
            }

            Json zones = Json::array();
            for (const ZoneAllocStats& stats : AllocTracker::GetZoneStats()) {
                if (zones.size() >= static_cast<size_t>(*top)) {
                    break;
                }
                Json zone = Json::object();
                zone["name"] = stats.site->name;
                zone["file"] = stats.site->file;
                zone["line"] = stats.site->line;
                zone["calls"] = stats.calls;
                zone["allocs"] = stats.count;
                zone["bytes"] = stats.bytes;
                zones.push_back(std::move(zone));
            }
            const AllocTotals totals = AllocTracker::GetTotals();
            Json described = Json::object();
            described["count"] = totals.count;
            described["bytes"] = totals.bytes;
            described["frees"] = totals.frees;
            described["liveBytes"] = totals.liveBytes;
            described["peakLiveBytes"] = totals.peakLiveBytes;
            described["largest"] = totals.largest;
            described["noAllocViolations"] = totals.noAllocViolations;

            Json result = Json::object();
            result["enabled"] = AllocTracker::kEnabled;
            result["zones"] = std::move(zones);
            result["names"] = DescribeNames(static_cast<size_t>(*top));
            result["totals"] = std::move(described);
            if (reset) {
                AllocTracker::ResetZoneStats();
            }
            call.Reply(std::move(result));
        }

    }  // namespace

    void RegisterProfilerControl(ControlService& service) {
        service.RegisterHandler("frame_stats", HandleFrameStats);
        service.RegisterHandler("profiler_dump", HandleProfilerDump);
        service.RegisterHandler("zone_allocs", HandleZoneAllocs);

        service.DescribeMethod("frame_stats", {"The last frames with stage times, draws, allocations, arena totals and counters, oldest first; arenas: the counters of every arena",
                                               {{"frames", "integer", false, Format("How many frames, 1 to {} (default 1)", FrameStatsCollector::kFrameHistory)}}});
        service.DescribeMethod("profiler_dump", {"Writes the Chrome trace of the run (the game has to record one)",
                                                 {{"path", "string", false, Format("A relative file name without '..' (default {})", kDefaultTracePath)}}});
        service.DescribeMethod("zone_allocs", {"Allocations per profiler zone, per zone and allocation name (names) and the totals",
                                               {{"top", "integer", false, Format("How many zones and how many names, 1 to {} (default {})", kMaxTopZones, kDefaultTopZones)},
                                                {"reset", "boolean", false, "Clear the per-zone and per-name counts after answering"}}});
    }

}  // namespace BECore
