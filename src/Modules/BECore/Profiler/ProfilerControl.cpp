#include "ProfilerControl.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/Backends/ChromeTraceBackend.h>
#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Profiler/Profiler.h>
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

        /// The zone x name rows of EASTL's allocations (Alloc/CLAUDE.md, "Names"), the most bytes first; empty without ENGINE_PROFILER_ALLOC
        void WriteNames(JsonWriter& writer, const Vector<AllocSiteStats>& names, size_t top) {
            writer.BeginArray();
            for (size_t i = 0; i < names.size() && i < top; ++i) {
                const AllocSiteStats& stats = names[i];
                writer.BeginObject();
                writer.Member("allocs", stats.count);
                writer.Member("bytes", stats.bytes);
                writer.Member("name", stats.name != nullptr ? stats.name : "");
                writer.Member("zone", stats.ZoneLabel());
                writer.EndObject();
            }
            writer.EndArray();
        }

        /// The size histograms of the innermost zones (Alloc/CLAUDE.md, "Sizes"), the most allocations first: the non-empty buckets only, `upTo` the bucket's
        /// largest size in bytes and 0 for the open last bucket; empty without ENGINE_PROFILER_ALLOC
        void WriteSizes(JsonWriter& writer, const Vector<AllocSizeStats>& sizes, size_t top) {
            writer.BeginArray();
            for (size_t i = 0; i < sizes.size() && i < top; ++i) {
                const AllocSizeStats& stats = sizes[i];
                writer.BeginObject();
                writer.Member("allocs", stats.allocs);
                writer.Key("buckets");
                writer.BeginArray();
                for (uint32_t bucket = 0; bucket < kSizeBuckets; ++bucket) {
                    if (stats.buckets[bucket] == 0) {
                        continue;
                    }
                    writer.BeginObject();
                    writer.Member("allocs", stats.buckets[bucket]);
                    writer.Member("upTo", SizeBucketBound(bucket));
                    writer.EndObject();
                }
                writer.EndArray();
                writer.Member("zone", stats.ZoneLabel());
                writer.EndObject();
            }
            writer.EndArray();
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

        void WriteArenas(JsonWriter& writer, const Vector<ArenaStatsEntry>& arenas) {
            writer.BeginArray();
            for (const ArenaStatsEntry& entry : arenas) {
                writer.BeginObject();
                writer.Member("capacity", entry.capacity);
                writer.Member("chunks", entry.chunks);
                writer.Member("name", entry.name);
                writer.Member("overflows", entry.overflows);
                writer.Member("peak", entry.peak);
                writer.Member("used", entry.used);
                writer.EndObject();
            }
            writer.EndArray();
        }

        void WriteTags(JsonWriter& writer) {
            const Array<AllocTagTotals, kMemTagCount> totals = AllocTracker::GetTagTotals();
            writer.BeginArray();
            for (size_t i = 0; i < kMemTagCount; ++i) {
                writer.BeginObject();
                writer.Member("count", totals[i].count);
                writer.Member("frees", totals[i].frees);
                writer.Member("liveBytes", totals[i].liveBytes);
                writer.Member("peakLiveBytes", totals[i].peakLiveBytes);
                writer.Member("tag", EnumUtils<MemTag>::ToString(static_cast<MemTag>(i)));
                writer.EndObject();
            }
            writer.EndArray();
        }

        void WritePools(JsonWriter& writer, const Vector<PoolStatsEntry>& pools) {
            writer.BeginArray();
            for (const PoolStatsEntry& entry : pools) {
                writer.BeginObject();
                writer.Member("allocations", entry.allocations);
                writer.Member("blockSize", entry.blockSize);
                writer.Member("live", entry.live);
                writer.Member("name", entry.name);
                writer.Member("pages", entry.pages);
                writer.Member("peak", entry.peak);
                writer.EndObject();
            }
            writer.EndArray();
        }

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
            const uint64_t dropped = recorder.GetDroppedCount();
            call.Reply([&](JsonWriter& writer) {
                writer.BeginObject();
                writer.Member("dropped", dropped);
                writer.Member("path", StringView(path));
                writer.EndObject();
            });
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

            const Vector<ZoneAllocStats> zones = AllocTracker::GetZoneStats();
            const Vector<AllocSiteStats> names = AllocTracker::GetNameStats();
            const Vector<AllocSizeStats> sizes = AllocTracker::GetSizeStats();
            const AllocTotals totals = AllocTracker::GetTotals();
            call.Reply([&](JsonWriter& writer) {
                writer.BeginObject();
                writer.Member("enabled", AllocTracker::kEnabled);
                writer.Key("names");
                WriteNames(writer, names, static_cast<size_t>(*top));
                writer.Key("sizes");
                WriteSizes(writer, sizes, static_cast<size_t>(*top));
                writer.Key("totals");
                writer.BeginObject();
                writer.Member("bytes", totals.bytes);
                writer.Member("count", totals.count);
                writer.Member("frees", totals.frees);
                writer.Member("largest", totals.largest);
                writer.Member("liveBytes", totals.liveBytes);
                writer.Member("noAllocViolations", totals.noAllocViolations);
                writer.Member("peakLiveBytes", totals.peakLiveBytes);
                writer.EndObject();
                writer.Key("zones");
                writer.BeginArray();
                for (size_t i = 0; i < zones.size() && i < static_cast<size_t>(*top); ++i) {
                    const ZoneAllocStats& stats = zones[i];
                    writer.BeginObject();
                    writer.Member("allocs", stats.count);
                    writer.Member("bytes", stats.bytes);
                    writer.Member("calls", stats.calls);
                    writer.Member("file", stats.site->file);
                    writer.Member("line", stats.site->line);
                    writer.Member("name", stats.site->name);
                    writer.EndObject();
                }
                writer.EndArray();
                writer.EndObject();
            });
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
