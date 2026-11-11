#include "ProfilerControl.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/Backends/ChromeTraceBackend.h>
#include <BECore/Profiler/Profiler.h>

namespace BECore {

    namespace {

        constexpr eastl::string_view kNotRecording = "not_recording";
        constexpr eastl::string_view kDefaultTracePath = "trace.json";
        constexpr int64_t kDefaultTopZones = 20;
        constexpr int64_t kMaxTopZones = 1000;

        Json DescribeFrame(const FrameStats& stats) {
            Json frame = Json::object();
            frame["frame"] = stats.frame;
            frame["frameMs"] = stats.frameMs;
            frame["cpuMs"] = stats.cpuMs;
            Json stages = Json::object();
            for (size_t i = 0; i < kFrameStageCount; ++i) {
                const eastl::string_view name = EnumUtils<FrameStage>::ToString(static_cast<FrameStage>(i));
                stages[std::string(name.data(), name.size())] = stats.stageMs[i];
            }
            frame["stagesMs"] = std::move(stages);
            frame["drawCalls"] = stats.draws.drawCalls;
            frame["triangles"] = stats.draws.triangles;
            frame["textureSwitches"] = stats.draws.textureSwitches;
            frame["allocCount"] = stats.allocCount;
            frame["allocBytes"] = stats.allocBytes;
            frame["liveBytes"] = stats.liveBytes;
            Json counters = Json::object();
            for (const FrameCounter& counter : stats.counters) {
                const eastl::string_view name = counter.name.ToStringView();
                counters[std::string(name.data(), name.size())] = counter.value;
            }
            frame["counters"] = std::move(counters);
            return frame;
        }

        /// A positive integer member of at most @p max, or @p fallback when absent
        std::expected<int64_t, ControlError> ReadCount(const Json& params, eastl::string_view key, int64_t fallback, int64_t max) {
            if (FindJsonMember(params, key) == nullptr) {
                return fallback;
            }
            const auto value = GetJsonInt(params, key);
            if (!value || *value < 1 || *value > max) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'{}' has to be an integer from 1 to {}", key, max)));
            }
            return *value;
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
            call.Reply(std::move(result));
        }

        /// @p text as a trace path the game may write: relative and inside the working folder
        bool IsSafeRelativePath(eastl::string_view text) {
            if (text.empty()) {
                return false;
            }
            const std::filesystem::path path(std::string(text.data(), text.size()));
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
            eastl::string path(kDefaultTracePath.data(), kDefaultTracePath.size());
            if (FindJsonMember(call.Params(), "path") != nullptr) {
                const auto given = GetJsonString(call.Params(), "path");
                if (!given || !IsSafeRelativePath(*given)) {
                    call.Fail(ControlErrors::kInvalidParams, "'path' has to be a relative file name without '..'");
                    return;
                }
                path = *given;
            }
            ChromeTraceBackend& recorder = Profiler::GetChromeTrace();
            if (Profiler::GetBackend() != &recorder) {
                call.Fail(kNotRecording, "the Chrome trace is not recording: start the game with --trace=<file> or --profiler=chrome (a build with ENGINE_PROFILER)");
                return;
            }
            const auto written = recorder.WriteFile(path);
            if (!written) {
                call.Fail(ControlErrors::kInternalError, written.error());
                return;
            }
            Json result = Json::object();
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
    }

}  // namespace BECore
