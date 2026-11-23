#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Memory/BlockPool.h>
#include <BECore/Memory/LinearArena.h>
#include <BECore/Memory/MemTag.h>
#include <BECore/Memory/ScratchArena.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/Backends/ChromeTraceBackend.h>
#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Profiler/Profiler.h>
#include <BECore/Profiler/ProfilerAnswers.h>
#include <BECore/Profiler/ProfilerControl.h>
#include <Common/AllocCheck.h>
#include <Common/ControlTestSupport.h>
#include <Common/TempPath.h>
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

    namespace {

        uint64_t g_fakeNowNs = 0;

        uint64_t FakeNowNs() {
            return g_fakeNowNs;
        }

        constexpr uint64_t kNsPerMs = 1'000'000;

        /// The raw text of the answer: the tests below pin its bytes
        String Ask(ControlService& service, StringView method, const Json& params = Json::object()) {
            String text;
            service.Post(MakeRequest(1, method, params), [&text](const String& reply) { text = reply; });
            service.Drain();
            return text;
        }

        /// @p text with every integer replaced by N: a counter that depends on the process (live bytes, arenas) keeps its place but not its value
        String MaskIntegers(const String& text) {
            String masked;
            size_t i = 0;
            while (i < text.size()) {
                const bool digit = text[i] >= '0' && text[i] <= '9';
                if (!digit) {
                    masked.push_back(text[i]);
                    ++i;
                    continue;
                }
                size_t end = i;
                while (end < text.size() && text[end] >= '0' && text[end] <= '9') {
                    ++end;
                }
                const bool fraction = end < text.size() && (text[end] == '.' || text[end] == 'e' || text[end] == 'E');
                const bool afterPoint = i > 0 && text[i - 1] == '.';
                if (fraction || afterPoint) {
                    masked.append(text.data() + i, end - i);
                } else {
                    masked.push_back('N');
                }
                i = end;
            }
            return masked;
        }

        /// The `{...}` row of @p text that holds @p needle (rows here have no nested object)
        String RowAround(const String& text, StringView needle) {
            const size_t at = text.find(needle.data(), 0, needle.size());
            if (at == String::npos) {
                return String();
            }
            const size_t open = text.rfind('{', at);
            const size_t close = text.find('}', at);
            return open == String::npos || close == String::npos ? String() : String(text.substr(open, close - open + 1));
        }

        /// A reply is written in the order the `Json` object keeps: its text is the canonical dump of its own parse
        void ExpectCanonical(const String& text) {
            const auto parsed = ParseJson(text);
            ASSERT_TRUE(parsed.has_value());
            EXPECT_EQ(DumpJson(*parsed), text);
        }

    }  // namespace

    /// Fills the process's collector with frames of a fake clock; the first frame only moves the clock out of the real one's reach
    class ProfilerControlGoldenTest : public ProfilerControlTest {
    protected:
        void SetUp() override {
            ProfilerControlTest::SetUp();
            CoreManager::GetFrameStats().SetClock(FakeNowNs);
            g_fakeNowNs = kNsPerMs;
            AddFrame(0);
        }

        void TearDown() override {
            CoreManager::GetFrameStats().SetClock(nullptr);
            ProfilerControlTest::TearDown();
        }

        /// A frame of @p index * 2 ms with a stage of 1 ms and counters set in an order that differs from the order they sort in
        void AddFrame(uint32_t index) {
            FrameStatsCollector& collector = CoreManager::GetFrameStats();
            collector.BeginFrame(index == 2);
            collector.BeginStage(FrameStage::Update);
            g_fakeNowNs += kNsPerMs;
            collector.EndStage(FrameStage::Update);
            collector.BeginStage(FrameStage::SceneDraw);
            g_fakeNowNs += kNsPerMs / 4;
            collector.EndStage(FrameStage::SceneDraw);
            collector.SetCounter("zeta"_intern, 1.5);
            collector.SetCounter("alpha"_intern, 2.0);
            collector.SetCounter("Mid"_intern, 0.25);
            g_fakeNowNs += kNsPerMs * index;
            collector.EndFrame(RenderCounters{.drawCalls = 4, .triangles = 8, .textureSwitches = 1}, AllocTotals{});
        }
    };

    TEST_F(ProfilerControlGoldenTest, FrameStatsOfOneFrameKeepsItsBytes) {
        AddFrame(1);

        const String text = Ask(service, "frame_stats");

        ExpectCanonical(text);
        const size_t from = text.find("\"frames\":");
        const size_t to = text.find(",\"pools\":");
        ASSERT_NE(from, String::npos);
        ASSERT_NE(to, String::npos);
        EXPECT_EQ(
            MaskIntegers(String(text.substr(from, to - from))),
            R"json("frames":[{"allocBytes":N,"allocCount":N,"arenaOverflows":N,"arenaPeakBytes":N,"arenaUsedBytes":N,"counters":{"Mid":0.25,"alpha":2.0,"zeta":1.5},"cpuMs":2.25,"drawCalls":N,"frame":N,"frameMs":2.25,"liveBytes":N,"paused":false,"poolLiveBlocks":N,"poolPageBytes":N,"stagesMs":{"Present":0.0,"Render":0.0,"SceneDraw":0.25,"SceneUpdate":0.0,"Update":1.0,"Wait":0.0},"tagAllocCount":{"Audio":N,"Core":N,"Game":N,"Network":N,"Resource":N,"Scene":N,"Text":N,"UI":N,"Untagged":N},"tagLiveBytes":{"Audio":N,"Core":N,"Game":N,"Network":N,"Resource":N,"Scene":N,"Text":N,"UI":N,"Untagged":N},"textureSwitches":N,"triangles":N}])json");
    }

    TEST_F(ProfilerControlGoldenTest, FrameStatsOfSeveralFramesKeepsTheirBytes) {
        AddFrame(1);
        AddFrame(2);
        AddFrame(3);
        Json params = Json::object();
        params["frames"] = 3;

        const String text = Ask(service, "frame_stats", params);

        ExpectCanonical(text);
        const size_t from = text.find("\"frames\":");
        const size_t to = text.find(",\"pools\":");
        ASSERT_NE(from, String::npos);
        ASSERT_NE(to, String::npos);
        EXPECT_EQ(
            MaskIntegers(String(text.substr(from, to - from))),
            R"json("frames":[{"allocBytes":N,"allocCount":N,"arenaOverflows":N,"arenaPeakBytes":N,"arenaUsedBytes":N,"counters":{"Mid":0.25,"alpha":2.0,"zeta":1.5},"cpuMs":2.25,"drawCalls":N,"frame":N,"frameMs":2.25,"liveBytes":N,"paused":false,"poolLiveBlocks":N,"poolPageBytes":N,"stagesMs":{"Present":0.0,"Render":0.0,"SceneDraw":0.25,"SceneUpdate":0.0,"Update":1.0,"Wait":0.0},"tagAllocCount":{"Audio":N,"Core":N,"Game":N,"Network":N,"Resource":N,"Scene":N,"Text":N,"UI":N,"Untagged":N},"tagLiveBytes":{"Audio":N,"Core":N,"Game":N,"Network":N,"Resource":N,"Scene":N,"Text":N,"UI":N,"Untagged":N},"textureSwitches":N,"triangles":N},{"allocBytes":N,"allocCount":N,"arenaOverflows":N,"arenaPeakBytes":N,"arenaUsedBytes":N,"counters":{"Mid":0.25,"alpha":2.0,"zeta":1.5},"cpuMs":3.25,"drawCalls":N,"frame":N,"frameMs":3.25,"liveBytes":N,"paused":true,"poolLiveBlocks":N,"poolPageBytes":N,"stagesMs":{"Present":0.0,"Render":0.0,"SceneDraw":0.25,"SceneUpdate":0.0,"Update":1.0,"Wait":0.0},"tagAllocCount":{"Audio":N,"Core":N,"Game":N,"Network":N,"Resource":N,"Scene":N,"Text":N,"UI":N,"Untagged":N},"tagLiveBytes":{"Audio":N,"Core":N,"Game":N,"Network":N,"Resource":N,"Scene":N,"Text":N,"UI":N,"Untagged":N},"textureSwitches":N,"triangles":N},{"allocBytes":N,"allocCount":N,"arenaOverflows":N,"arenaPeakBytes":N,"arenaUsedBytes":N,"counters":{"Mid":0.25,"alpha":2.0,"zeta":1.5},"cpuMs":4.25,"drawCalls":N,"frame":N,"frameMs":4.25,"liveBytes":N,"paused":false,"poolLiveBlocks":N,"poolPageBytes":N,"stagesMs":{"Present":0.0,"Render":0.0,"SceneDraw":0.25,"SceneUpdate":0.0,"Update":1.0,"Wait":0.0},"tagAllocCount":{"Audio":N,"Core":N,"Game":N,"Network":N,"Resource":N,"Scene":N,"Text":N,"UI":N,"Untagged":N},"tagLiveBytes":{"Audio":N,"Core":N,"Game":N,"Network":N,"Resource":N,"Scene":N,"Text":N,"UI":N,"Untagged":N},"textureSwitches":N,"triangles":N}])json");
    }

    TEST_F(ProfilerControlGoldenTest, FrameStatsListsArenasPoolsAndTagsInTheirOwnKeyOrder) {
        LinearArena arena("GoldenArena");
        ASSERT_NE(arena.Allocate(48, 1), nullptr);
        BlockPool pool("GoldenPool", 40, alignof(void*));
        void* block = pool.Allocate();
        AddFrame(1);

        const String text = Ask(service, "frame_stats");

        EXPECT_EQ(MaskIntegers(RowAround(text, "\"name\":\"GoldenArena\"")), R"json({"capacity":N,"chunks":N,"name":"GoldenArena","overflows":N,"peak":N,"used":N})json");
        EXPECT_EQ(MaskIntegers(RowAround(text, "\"name\":\"GoldenPool\"")), R"json({"allocations":N,"blockSize":N,"live":N,"name":"GoldenPool","pages":N,"peak":N})json");
        EXPECT_EQ(MaskIntegers(RowAround(text, "\"tag\":\"Scene\"")), R"json({"count":N,"frees":N,"liveBytes":N,"peakLiveBytes":N,"tag":"Scene"})json");
        pool.Deallocate(block);
    }

    TEST_F(ProfilerControlGoldenTest, ZoneAllocsKeepsItsBytesAndResetsAfterTheText) {
        static constexpr ProfileSite kBig{"GoldenBig", "GoldenFunction", "Golden \"quoted\".cpp", 42, 0};
        static constexpr ProfileSite kSmall{"GoldenSmall", "GoldenFunction", "Golden.cpp", 7, 0};
        AllocTracker::ResetZoneStats();
        AllocTracker::AddZone(kSmall, 3, 30);
        AllocTracker::AddZone(kBig, 5, 500);
        Json params = Json::object();
        params["reset"] = true;

        const String text = Ask(service, "zone_allocs", params);

        ExpectCanonical(text);
        EXPECT_EQ(RowAround(text, "\"name\":\"GoldenBig\""), R"json({"allocs":5,"bytes":500,"calls":1,"file":"Golden \"quoted\".cpp","line":42,"name":"GoldenBig"})json");
        EXPECT_EQ(RowAround(text, "\"name\":\"GoldenSmall\""), R"json({"allocs":3,"bytes":30,"calls":1,"file":"Golden.cpp","line":7,"name":"GoldenSmall"})json");
        EXPECT_EQ(MaskIntegers(RowAround(text, "\"largest\":")), R"json({"bytes":N,"count":N,"frees":N,"largest":N,"liveBytes":N,"noAllocViolations":N,"peakLiveBytes":N})json");
        const String after = Ask(service, "zone_allocs");
        EXPECT_EQ(RowAround(after, "\"name\":\"GoldenBig\""), "");
    }

    TEST_F(ProfilerControlGoldenTest, ZoneAllocsTopCutsTheZones) {
        static constexpr ProfileSite kBig{"GoldenTopBig", "f", "a.cpp", 1, 0};
        static constexpr ProfileSite kSmall{"GoldenTopSmall", "f", "a.cpp", 2, 0};
        AllocTracker::ResetZoneStats();
        AllocTracker::AddZone(kSmall, 1, 10);
        AllocTracker::AddZone(kBig, 1, 1000);
        Json params = Json::object();
        params["top"] = 1;

        const String text = Ask(service, "zone_allocs", params);

        EXPECT_EQ(RowAround(text, "\"name\":\"GoldenTopBig\""), R"json({"allocs":1,"bytes":1000,"calls":1,"file":"a.cpp","line":1,"name":"GoldenTopBig"})json");
        EXPECT_EQ(RowAround(text, "\"name\":\"GoldenTopSmall\""), "");
        AllocTracker::ResetZoneStats();
    }

    TEST_F(ProfilerControlGoldenTest, ZoneAllocsTopAndResetTogetherCutTheTextAndThenClear) {
        static constexpr ProfileSite kBig{"GoldenBothBig", "f", "a.cpp", 1, 0};
        static constexpr ProfileSite kSmall{"GoldenBothSmall", "f", "a.cpp", 2, 0};
        AllocTracker::ResetZoneStats();
        AllocTracker::AddZone(kSmall, 1, 10);
        AllocTracker::AddZone(kBig, 1, 1000);
        Json params = Json::object();
        params["top"] = 1;
        params["reset"] = true;

        const String text = Ask(service, "zone_allocs", params);

        ExpectCanonical(text);
        EXPECT_EQ(RowAround(text, "\"name\":\"GoldenBothBig\""), R"json({"allocs":1,"bytes":1000,"calls":1,"file":"a.cpp","line":1,"name":"GoldenBothBig"})json");
        EXPECT_EQ(RowAround(text, "\"name\":\"GoldenBothSmall\""), "");
        const String after = Ask(service, "zone_allocs");
        EXPECT_EQ(RowAround(after, "\"name\":\"GoldenBothBig\""), "");
        EXPECT_EQ(RowAround(after, "\"name\":\"GoldenBothSmall\""), "");
    }

    TEST_F(ProfilerControlGoldenTest, ProfilerDumpAnswersThePathAndTheDroppedCount) {
        IProfilerBackend* const previous = Profiler::GetPrimaryBackend();
        Profiler::SetBackend(&Profiler::GetChromeTrace());
        const std::filesystem::path file = TestTempPath("ProfilerDump caf\xC3\xA9").filename();
        Json params = Json::object();
        params["path"] = file.string();

        const String text = Ask(service, "profiler_dump", params);

        Profiler::SetBackend(previous);
        std::error_code ignored;
        const bool written = std::filesystem::remove(file, ignored);
        EXPECT_TRUE(written);
        ExpectCanonical(text);
        String shown = text;
        const size_t pathAt = shown.find(file.string().c_str());
        ASSERT_NE(pathAt, String::npos);
        shown.replace(pathAt, file.string().size(), "FILE");
        EXPECT_EQ(MaskIntegers(shown), R"json({"id":N,"ok":true,"result":{"dropped":N,"path":"FILE"}})json");
    }

    TEST_F(ProfilerControlGoldenTest, FrameStatsAllocatesNothingBeyondTheStatsItReads) {
        // The stats are returned as Vectors, which allocate whoever asks: a method that reads the same three and answers `{}` is the measure of the rest
        service.RegisterHandler("reads_only", [](ControlCall& call) {
            const Vector<FrameStats> history = CoreManager::GetFrameStats().GetHistory(1);
            const Vector<ArenaStatsEntry> arenas = AllocTracker::GetArenaStats();
            const Vector<PoolStatsEntry> pools = AllocTracker::GetPoolStats();
            call.Reply([](JsonWriter& writer) {
                writer.BeginObject();
                writer.EndObject();
            });
        });
        AddFrame(1);
        const auto drained = [this](StringView method) {
            service.Post(MakeRequest(1, method), [](const String&) {});
            return CountAllocations([this] { service.Drain(); });
        };
        drained("frame_stats");
        drained("reads_only");

        const AllocCount stats = drained("frame_stats");
        const AllocCount reads = drained("reads_only");

        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        EXPECT_EQ(stats.count, reads.count);
        EXPECT_EQ(stats.bytes, reads.bytes);
    }

    TEST_F(ProfilerControlGoldenTest, ZoneAllocsAnswerBuiltFromStatsAllocatesNothingTheSecondTime) {
        static constexpr ProfileSite kSite{"AnswerZone", "AnswerFunction", "Answer.cpp", 12, 0};
        static constexpr const char* kName = "AnswerName";
        ZoneAllocStats zone;
        zone.site = &kSite;
        zone.calls = 2;
        zone.count = 9;
        zone.bytes = 900;
        AllocSiteStats name;
        name.zone = &kSite;
        name.name = kName;
        name.count = 4;
        name.bytes = 40;
        AllocSizeStats size;
        size.zone = &kSite;
        size.allocs = 5;
        size.buckets[3] = 2;
        size.buckets[7] = 3;
        String out;
        out.reserve(1024);
        const auto writeOnce = [&] {
            const ScratchScope scratch;
            ZoneAllocsAnswer answer;
            answer.enabled = true;
            answer.names.reserve(1);
            answer.names.push_back(ProfilerAllocName::Read(name));
            answer.sizes.reserve(1);
            answer.sizes.push_back(ProfilerAllocSizes::Read(size));
            answer.zones.reserve(1);
            answer.zones.push_back(ProfilerAllocZone::Read(zone));
            out.clear();
            JsonWriter writer(out, JsonWriter::kCompact);
            answer.WriteJson(writer);
        };
        writeOnce();
        const String first = out;

        ExpectNoAllocations(writeOnce);

        EXPECT_EQ(out, first);
        EXPECT_NE(out.find("\"name\":\"AnswerName\""), String::npos);
        EXPECT_NE(out.find("\"file\":\"Answer.cpp\""), String::npos);
    }

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
                                "poolLiveBlocks", "poolPageBytes", "counters", "tagAllocCount", "tagLiveBytes"}) {
            EXPECT_NE(FindJsonMember(last, key), nullptr) << key;
        }
        for (size_t i = 0; i < kMemTagCount; ++i) {
            const StringView name = EnumUtils<MemTag>::ToString(static_cast<MemTag>(i));
            EXPECT_NE(FindJsonMember(Require(FindJsonMember(last, "tagAllocCount")), name), nullptr) << ToStdString(name);
            EXPECT_NE(FindJsonMember(Require(FindJsonMember(last, "tagLiveBytes")), name), nullptr) << ToStdString(name);
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

    TEST_F(ProfilerControlTest, FrameStatsListEveryTagInTheOrderOfTheEnum) {
        constexpr size_t kBlockBytes = 4096;
        void* block = nullptr;
        {
            BE_MEM_TAG(Scene);
            block = ::operator new(kBlockBytes);
        }
        FrameStatsCollector& collector = CoreManager::GetFrameStats();
        collector.BeginFrame();
        collector.EndFrame({}, AllocTracker::GetTotals());

        const Json response = Call("frame_stats");
        const Json* tags = FindJsonMember(Require(FindJsonMember(response, "result")), "tags");
        ASSERT_NE(tags, nullptr);
        ASSERT_EQ(tags->size(), kMemTagCount);
        for (size_t i = 0; i < kMemTagCount; ++i) {
            const Json& described = (*tags)[i];
            EXPECT_EQ(GetJsonString(described, "tag").value_or(String()), EnumUtils<MemTag>::ToString(static_cast<MemTag>(i)));
            for (const char* key : {"liveBytes", "peakLiveBytes", "count", "frees"}) {
                EXPECT_NE(FindJsonMember(described, key), nullptr) << key;
            }
        }
        const Json& scene = (*tags)[+MemTag::Scene];
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_GE(GetJsonInt(scene, "liveBytes").value_or(0), static_cast<int64_t>(kBlockBytes));
            EXPECT_GE(GetJsonInt(scene, "count").value_or(0), 1);
        } else {
            EXPECT_EQ(GetJsonInt(scene, "liveBytes"), 0);
        }
        ::operator delete(block);
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
        const Json* names = FindJsonMember(*result, "names");
        ASSERT_NE(names, nullptr);
        EXPECT_TRUE(names->is_array());
        const Json* sizes = FindJsonMember(*result, "sizes");
        ASSERT_NE(sizes, nullptr);
        EXPECT_TRUE(sizes->is_array());
        const Json* totals = FindJsonMember(*result, "totals");
        ASSERT_NE(totals, nullptr);
        for (const char* key : {"count", "bytes", "frees", "liveBytes", "peakLiveBytes", "largest", "noAllocViolations"}) {
            EXPECT_NE(FindJsonMember(*totals, key), nullptr) << key;
        }
    }

#if BE_PROFILER_ALLOC

    namespace {

        constexpr size_t kInts = 100;

        /// The `names` row of the zone @p zone and allocation name @p name in the answer of zone_allocs, or nullptr
        const Json* FindNameRow(const Json& result, const char* zone, const char* name) {
            const Json* names = FindJsonMember(result, "names");
            if (names == nullptr) {
                return nullptr;
            }
            for (const Json& row : *names) {
                if (GetJsonString(row, "zone") == String(zone) && GetJsonString(row, "name") == String(name)) {
                    return &row;
                }
            }
            return nullptr;
        }

        /// The `sizes` row of the zone @p zone in the answer of zone_allocs, or nullptr
        const Json* FindSizeRow(const Json& result, const char* zone) {
            const Json* sizes = FindJsonMember(result, "sizes");
            if (sizes == nullptr) {
                return nullptr;
            }
            for (const Json& row : *sizes) {
                if (GetJsonString(row, "zone") == String(zone)) {
                    return &row;
                }
            }
            return nullptr;
        }

        /// Allocates @p bytes in a container, so the optimizer keeps the call
        void AllocateBytes(size_t bytes) {
            Vector<char> block;
            block.reserve(bytes);
        }

        constexpr size_t kSmallBlock = 100;
        constexpr size_t kBigBlock = 1000;

    }  // namespace

    TEST_F(ProfilerControlTest, ZoneAllocsGivesTheSizeHistogramOfTheInnermostZone) {
        AllocTracker::ResetZoneStats();
        {
            BE_PROFILE_SCOPE("ControlSizesOuter");
            AllocateBytes(kSmallBlock);
            {
                BE_PROFILE_SCOPE("ControlSizesInner");
                AllocateBytes(kBigBlock);
                AllocateBytes(kBigBlock);
            }
        }
        const Json response = Call("zone_allocs");
        const Json* result = FindJsonMember(response, "result");
        ASSERT_NE(result, nullptr);

        const Json* outer = FindSizeRow(*result, "ControlSizesOuter");
        ASSERT_NE(outer, nullptr);
        EXPECT_EQ(GetJsonInt(*outer, "allocs"), 1);
        const Json* outerBuckets = FindJsonMember(*outer, "buckets");
        ASSERT_NE(outerBuckets, nullptr);
        ASSERT_EQ(outerBuckets->size(), 1u);
        EXPECT_EQ(GetJsonInt((*outerBuckets)[0], "upTo"), static_cast<int64_t>(SizeBucketBound(SizeBucket(kSmallBlock))));
        EXPECT_EQ(GetJsonInt((*outerBuckets)[0], "allocs"), 1);

        const Json* inner = FindSizeRow(*result, "ControlSizesInner");
        ASSERT_NE(inner, nullptr);
        EXPECT_EQ(GetJsonInt(*inner, "allocs"), 2);
        const Json* innerBuckets = FindJsonMember(*inner, "buckets");
        ASSERT_NE(innerBuckets, nullptr);
        ASSERT_EQ(innerBuckets->size(), 1u);
        EXPECT_EQ(GetJsonInt((*innerBuckets)[0], "upTo"), static_cast<int64_t>(SizeBucketBound(SizeBucket(kBigBlock))));
        EXPECT_EQ(GetJsonInt((*innerBuckets)[0], "allocs"), 2);
    }

    TEST_F(ProfilerControlTest, ZoneAllocsTopLimitsTheSizesAndResetEmptiesThem) {
        AllocTracker::ResetZoneStats();
        {
            BE_PROFILE_SCOPE("ControlSizesMany");
            AllocateBytes(kSmallBlock);
            AllocateBytes(kBigBlock);
        }
        {
            BE_PROFILE_SCOPE("ControlSizesFew");
            AllocateBytes(kSmallBlock);
        }
        Json limited = Json::object();
        limited["top"] = 1;
        const Json first = Call("zone_allocs", limited);
        const Json* limitedResult = FindJsonMember(first, "result");
        ASSERT_NE(limitedResult, nullptr);
        const Json* limitedSizes = FindJsonMember(*limitedResult, "sizes");
        ASSERT_NE(limitedSizes, nullptr);
        ASSERT_EQ(limitedSizes->size(), 1u);

        // The most allocations first (the test's own work outside every zone may lead, so the order is read between its two zones)
        const Json all = Call("zone_allocs");
        const Json* allResult = FindJsonMember(all, "result");
        ASSERT_NE(allResult, nullptr);
        const Json* allSizes = FindJsonMember(*allResult, "sizes");
        ASSERT_NE(allSizes, nullptr);
        size_t manyAt = allSizes->size();
        size_t fewAt = allSizes->size();
        for (size_t i = 0; i < allSizes->size(); ++i) {
            const Optional<String> zone = GetJsonString((*allSizes)[i], "zone");
            manyAt = zone == String("ControlSizesMany") ? i : manyAt;
            fewAt = zone == String("ControlSizesFew") ? i : fewAt;
        }
        ASSERT_LT(manyAt, allSizes->size());
        EXPECT_LT(manyAt, fewAt);

        Json clearing = Json::object();
        clearing["reset"] = true;
        const Json before = Call("zone_allocs", clearing);
        const Json* beforeResult = FindJsonMember(before, "result");
        ASSERT_NE(beforeResult, nullptr);
        EXPECT_NE(FindSizeRow(*beforeResult, "ControlSizesFew"), nullptr);

        const Json after = Call("zone_allocs");
        const Json* afterResult = FindJsonMember(after, "result");
        ASSERT_NE(afterResult, nullptr);
        EXPECT_EQ(FindSizeRow(*afterResult, "ControlSizesFew"), nullptr);
        EXPECT_EQ(FindSizeRow(*afterResult, "ControlSizesMany"), nullptr);
    }

    TEST_F(ProfilerControlTest, ZoneAllocsNamesTheContainerThatAllocated) {
        AllocTracker::ResetZoneStats();
        {
            BE_PROFILE_SCOPE("ControlNamesZone");
            Vector<int> values;
            values.get_allocator().set_name("ControlNamed");
            values.reserve(kInts);
        }
        const Json response = Call("zone_allocs");
        const Json* result = FindJsonMember(response, "result");
        ASSERT_NE(result, nullptr);
        const Json* row = FindNameRow(*result, "ControlNamesZone", "ControlNamed");
        ASSERT_NE(row, nullptr);
        EXPECT_EQ(GetJsonInt(*row, "allocs"), 1);
        EXPECT_EQ(GetJsonInt(*row, "bytes"), static_cast<int64_t>(kInts * sizeof(int)));
    }

    TEST_F(ProfilerControlTest, ZoneAllocsTopLimitsTheNamesAndResetEmptiesThem) {
        AllocTracker::ResetZoneStats();
        {
            BE_PROFILE_SCOPE("ControlTopZone");
            Vector<int> big;
            big.get_allocator().set_name("ControlBig");
            big.reserve(kInts * 4);
            Vector<int> small;
            small.get_allocator().set_name("ControlSmall");
            small.reserve(kInts);
        }
        Json limited = Json::object();
        limited["top"] = 1;
        const Json first = Call("zone_allocs", limited);
        const Json* limitedResult = FindJsonMember(first, "result");
        ASSERT_NE(limitedResult, nullptr);
        const Json* limitedNames = FindJsonMember(*limitedResult, "names");
        ASSERT_NE(limitedNames, nullptr);
        EXPECT_EQ(limitedNames->size(), 1u);

        Json clearing = Json::object();
        clearing["reset"] = true;
        const Json before = Call("zone_allocs", clearing);
        const Json* beforeResult = FindJsonMember(before, "result");
        ASSERT_NE(beforeResult, nullptr);
        EXPECT_NE(FindNameRow(*beforeResult, "ControlTopZone", "ControlBig"), nullptr);
        EXPECT_NE(FindNameRow(*beforeResult, "ControlTopZone", "ControlSmall"), nullptr);

        const Json after = Call("zone_allocs");
        const Json* afterResult = FindJsonMember(after, "result");
        ASSERT_NE(afterResult, nullptr);
        EXPECT_EQ(FindNameRow(*afterResult, "ControlTopZone", "ControlBig"), nullptr);
    }

#else

    TEST_F(ProfilerControlTest, ZoneAllocsHasNoNamesWithoutTheOption) {
        const Json response = Call("zone_allocs");
        const Json* result = FindJsonMember(response, "result");
        ASSERT_NE(result, nullptr);
        const Json* names = FindJsonMember(*result, "names");
        ASSERT_NE(names, nullptr);
        EXPECT_TRUE(names->empty());
        const Json* sizes = FindJsonMember(*result, "sizes");
        ASSERT_NE(sizes, nullptr);
        EXPECT_TRUE(sizes->empty());
    }

#endif

}  // namespace BECore::Tests
