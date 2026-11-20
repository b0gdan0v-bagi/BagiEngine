#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Memory/BlockPool.h>
#include <BECore/Memory/LinearArena.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Profiler/Profiler.h>
#include <BECore/Profiler/ProfilerControl.h>
#include <Common/ControlTestSupport.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    class ProfilerControlTest : public ::testing::Test {
    protected:
        void SetUp() override {
            RegisterProfilerControl(service);
        }

        /// Sends one request and runs the main-thread side; the response
        Json Call(StringView method, const Json& params = Json::object()) {
            return CallControl(service, 1, method, params);
        }

        ControlService service;
    };

    TEST_F(ProfilerControlTest, FrameStatsAnswersTheLastFrames) {
        FrameStatsCollector& collector = CoreManager::GetFrameStats();
        for (int i = 0; i < 3; ++i) {
            collector.BeginFrame();
            collector.EndFrame(RenderCounters{.drawCalls = 4, .triangles = 8}, AllocTracker::GetTotals());
        }
        Json params = Json::object();
        params["frames"] = 2;
        const Json response = Call("frame_stats", params);
        const Json* result = FindJsonMember(response, "result");
        ASSERT_NE(result, nullptr);
        const Json* frames = FindJsonMember(*result, "frames");
        ASSERT_NE(frames, nullptr);
        ASSERT_EQ(frames->size(), 2u);
        const Json& last = (*frames)[1];
        EXPECT_EQ(GetJsonInt(last, "drawCalls"), 4);
        EXPECT_EQ(GetJsonInt(last, "triangles"), 8);
        for (const char* key : {"frame", "paused", "frameMs", "cpuMs", "stagesMs", "textureSwitches", "allocCount", "allocBytes", "liveBytes", "arenaUsedBytes", "arenaPeakBytes", "arenaOverflows",
                                "poolLiveBlocks", "poolPageBytes", "counters"}) {
            EXPECT_NE(FindJsonMember(last, key), nullptr) << key;
        }
        EXPECT_NE(FindJsonMember(Require(FindJsonMember(last, "stagesMs")), "SceneDraw"), nullptr);
        EXPECT_EQ(GetJsonBool(*result, "allocTracking"), AllocTracker::kEnabled);
    }

    TEST_F(ProfilerControlTest, FrameStatsListTheArenasWithTheirCounters) {
        constexpr size_t kBytes = 48;
        constexpr const char* kName = "ProfilerControlArena";
        LinearArena arena(kName);
        ASSERT_NE(arena.Allocate(kBytes, 1), nullptr);
        FrameStatsCollector& collector = CoreManager::GetFrameStats();
        collector.BeginFrame();
        collector.EndFrame({}, AllocTracker::GetTotals());

        const Json response = Call("frame_stats");
        const Json* arenas = FindJsonMember(Require(FindJsonMember(response, "result")), "arenas");
        ASSERT_NE(arenas, nullptr);
        const Json* mine = nullptr;
        for (const Json& described : *arenas) {
            if (GetJsonString(described, "name").value_or(String()) == kName) {
                mine = &described;
            }
        }
        ASSERT_NE(mine, nullptr);
        EXPECT_EQ(GetJsonInt(*mine, "used"), static_cast<int64_t>(kBytes));
        EXPECT_EQ(GetJsonInt(*mine, "peak"), static_cast<int64_t>(kBytes));
        EXPECT_EQ(GetJsonInt(*mine, "chunks"), 1);
        EXPECT_EQ(GetJsonInt(*mine, "overflows"), 0);
        EXPECT_GE(GetJsonInt(*mine, "capacity"), static_cast<int64_t>(kBytes));
    }

    TEST_F(ProfilerControlTest, FrameStatsListThePoolsWithTheirCounters) {
        constexpr size_t kBlockSize = 40;
        constexpr const char* kName = "ProfilerControlPool";
        BlockPool pool(kName, kBlockSize, alignof(void*));
        void* first = pool.Allocate();
        void* second = pool.Allocate();
        pool.Deallocate(second);
        FrameStatsCollector& collector = CoreManager::GetFrameStats();
        collector.BeginFrame();
        collector.EndFrame({}, AllocTracker::GetTotals());

        const Json response = Call("frame_stats");
        const Json* pools = FindJsonMember(Require(FindJsonMember(response, "result")), "pools");
        ASSERT_NE(pools, nullptr);
        const Json* mine = nullptr;
        for (const Json& described : *pools) {
            if (GetJsonString(described, "name").value_or(String()) == kName) {
                mine = &described;
            }
        }
        ASSERT_NE(mine, nullptr);
        EXPECT_EQ(GetJsonInt(*mine, "blockSize"), static_cast<int64_t>(pool.GetBlockSize()));
        EXPECT_EQ(GetJsonInt(*mine, "live"), 1);
        EXPECT_EQ(GetJsonInt(*mine, "peak"), 2);
        EXPECT_EQ(GetJsonInt(*mine, "pages"), 1);
        EXPECT_EQ(GetJsonInt(*mine, "allocations"), 2);
        pool.Deallocate(first);
    }

    TEST_F(ProfilerControlTest, FrameStatsCarryThePausedFlagOfEveryFrame) {
        FrameStatsCollector& collector = CoreManager::GetFrameStats();
        collector.BeginFrame(false);
        collector.EndFrame({}, AllocTracker::GetTotals());
        collector.BeginFrame(true);
        collector.EndFrame({}, AllocTracker::GetTotals());

        Json params = Json::object();
        params["frames"] = 2;
        const Json response = Call("frame_stats", params);
        const Json* frames = FindJsonMember(Require(FindJsonMember(response, "result")), "frames");
        ASSERT_NE(frames, nullptr);
        ASSERT_EQ(frames->size(), 2u);
        EXPECT_EQ(GetJsonBool((*frames)[0], "paused"), false);
        EXPECT_EQ(GetJsonBool((*frames)[1], "paused"), true);
    }

    TEST_F(ProfilerControlTest, InvalidParamsAreRejected) {
        Json zero = Json::object();
        zero["frames"] = 0;
        EXPECT_EQ(ErrorCode(Call("frame_stats", zero)), "invalid_params");
        Json text = Json::object();
        text["frames"] = "3";
        EXPECT_EQ(ErrorCode(Call("frame_stats", text)), "invalid_params");

        for (const char* path : {"../trace.json", "/tmp/trace.json", ""}) {
            Json params = Json::object();
            params["path"] = path;
            EXPECT_EQ(ErrorCode(Call("profiler_dump", params)), "invalid_params") << path;
        }
        Json top = Json::object();
        top["top"] = 0;
        EXPECT_EQ(ErrorCode(Call("zone_allocs", top)), "invalid_params");
        Json reset = Json::object();
        reset["reset"] = 1;
        EXPECT_EQ(ErrorCode(Call("zone_allocs", reset)), "invalid_params");
    }

    TEST_F(ProfilerControlTest, DumpNeedsTheChromeTraceRecording) {
        Profiler::SetBackend(nullptr);
        EXPECT_EQ(ErrorCode(Call("profiler_dump")), "not_recording");
    }

    TEST_F(ProfilerControlTest, ZoneAllocsAnswersTheTotalsAndTheZones) {
        const Json response = Call("zone_allocs");
        const Json* result = FindJsonMember(response, "result");
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(GetJsonBool(*result, "enabled"), AllocTracker::kEnabled);
        ASSERT_NE(FindJsonMember(*result, "zones"), nullptr);
        const Json* totals = FindJsonMember(*result, "totals");
        ASSERT_NE(totals, nullptr);
        for (const char* key : {"count", "bytes", "frees", "liveBytes", "peakLiveBytes", "largest", "noAllocViolations"}) {
            EXPECT_NE(FindJsonMember(*totals, key), nullptr) << key;
        }
    }

}  // namespace BECore::Tests
