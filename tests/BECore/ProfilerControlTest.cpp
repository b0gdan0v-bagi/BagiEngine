#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/JsonViewOps.h>
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
#include <Common/TestFiles.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    class ProfilerControlTest : public ::testing::Test {
    protected:
        void SetUp() override {
            RegisterProfilerControl(service);
        }

        /// Sends one request and runs the main-thread side; the response
        ControlReply Call(StringView method, StringView paramsText = R"({})") {
            return CallControlText(service, 1, method, paramsText);
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
        String Ask(ControlService& service, StringView method, StringView paramsText = R"({})") {
            String text;
            service.Post(MakeRequestText(1, method, paramsText), [&text](const String& reply) { text = reply; });
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

        /// A reply is written with the keys of every object in byte order and each number in the writer's form: its text is what the same document
        /// written again in sorted order gives
        void ExpectCanonical(const String& text) {
            const JsonDoc doc(StringView(text.c_str(), text.size()));
            ASSERT_TRUE(doc.IsParsed());
            String again;
            JsonWriter writer(again);
            WriteJsonSorted(writer, doc.Root());
            EXPECT_EQ(again, text);
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
            R"json("frames":[{"allocBytes":N,"allocCount":N,"arenaOverflows":N,"arenaPeakBytes":N,"arenaUsedBytes":N,"counters":{"Mid":0.25,"alpha":2.0,"zeta":1.5},"cpuMs":2.25,"drawCalls":N,"frame":N,"frameMs":2.25,"liveBytes":N,"paused":false,"poolLiveBlocks":N,"poolPageBytes":N,"primitives":N,"stagesMs":{"Present":0.0,"Render":0.0,"SceneDraw":0.25,"SceneUpdate":0.0,"Update":1.0,"Wait":0.0},"tagAllocCount":{"Audio":N,"Core":N,"Game":N,"Network":N,"Resource":N,"Scene":N,"Text":N,"UI":N,"Untagged":N},"tagLiveBytes":{"Audio":N,"Core":N,"Game":N,"Network":N,"Resource":N,"Scene":N,"Text":N,"UI":N,"Untagged":N},"textureSwitches":N,"triangles":N}])json");
    }

    TEST_F(ProfilerControlGoldenTest, FrameStatsOfSeveralFramesKeepsTheirBytes) {
        AddFrame(1);
        AddFrame(2);
        AddFrame(3);
        const StringView params = R"({"frames":3})";

        const String text = Ask(service, "frame_stats", params);

        ExpectCanonical(text);
        const size_t from = text.find("\"frames\":");
        const size_t to = text.find(",\"pools\":");
        ASSERT_NE(from, String::npos);
        ASSERT_NE(to, String::npos);
        EXPECT_EQ(
            MaskIntegers(String(text.substr(from, to - from))),
            R"json("frames":[{"allocBytes":N,"allocCount":N,"arenaOverflows":N,"arenaPeakBytes":N,"arenaUsedBytes":N,"counters":{"Mid":0.25,"alpha":2.0,"zeta":1.5},"cpuMs":2.25,"drawCalls":N,"frame":N,"frameMs":2.25,"liveBytes":N,"paused":false,"poolLiveBlocks":N,"poolPageBytes":N,"primitives":N,"stagesMs":{"Present":0.0,"Render":0.0,"SceneDraw":0.25,"SceneUpdate":0.0,"Update":1.0,"Wait":0.0},"tagAllocCount":{"Audio":N,"Core":N,"Game":N,"Network":N,"Resource":N,"Scene":N,"Text":N,"UI":N,"Untagged":N},"tagLiveBytes":{"Audio":N,"Core":N,"Game":N,"Network":N,"Resource":N,"Scene":N,"Text":N,"UI":N,"Untagged":N},"textureSwitches":N,"triangles":N},{"allocBytes":N,"allocCount":N,"arenaOverflows":N,"arenaPeakBytes":N,"arenaUsedBytes":N,"counters":{"Mid":0.25,"alpha":2.0,"zeta":1.5},"cpuMs":3.25,"drawCalls":N,"frame":N,"frameMs":3.25,"liveBytes":N,"paused":true,"poolLiveBlocks":N,"poolPageBytes":N,"primitives":N,"stagesMs":{"Present":0.0,"Render":0.0,"SceneDraw":0.25,"SceneUpdate":0.0,"Update":1.0,"Wait":0.0},"tagAllocCount":{"Audio":N,"Core":N,"Game":N,"Network":N,"Resource":N,"Scene":N,"Text":N,"UI":N,"Untagged":N},"tagLiveBytes":{"Audio":N,"Core":N,"Game":N,"Network":N,"Resource":N,"Scene":N,"Text":N,"UI":N,"Untagged":N},"textureSwitches":N,"triangles":N},{"allocBytes":N,"allocCount":N,"arenaOverflows":N,"arenaPeakBytes":N,"arenaUsedBytes":N,"counters":{"Mid":0.25,"alpha":2.0,"zeta":1.5},"cpuMs":4.25,"drawCalls":N,"frame":N,"frameMs":4.25,"liveBytes":N,"paused":false,"poolLiveBlocks":N,"poolPageBytes":N,"primitives":N,"stagesMs":{"Present":0.0,"Render":0.0,"SceneDraw":0.25,"SceneUpdate":0.0,"Update":1.0,"Wait":0.0},"tagAllocCount":{"Audio":N,"Core":N,"Game":N,"Network":N,"Resource":N,"Scene":N,"Text":N,"UI":N,"Untagged":N},"tagLiveBytes":{"Audio":N,"Core":N,"Game":N,"Network":N,"Resource":N,"Scene":N,"Text":N,"UI":N,"Untagged":N},"textureSwitches":N,"triangles":N}])json");
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
        const StringView params = R"({"reset":true})";

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
        const StringView params = R"({"top":1})";

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
        const StringView params = R"({"reset":true,"top":1})";

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
        const Path file = TestTempPath("ProfilerDump caf\xC3\xA9").Filename();
        String params;
        {
            JsonWriter writer(params);
            writer.BeginObject();
            writer.Member("path", file.View());
            writer.EndObject();
        }

        const String text = Ask(service, "profiler_dump", StringView(params.c_str(), params.size()));

        Profiler::SetBackend(previous);
        EXPECT_TRUE(PathExists(file));
        RemoveFileIfPresent(file);
        ExpectCanonical(text);
        String shown = text;
        const size_t pathAt = shown.find(file.CStr());
        ASSERT_NE(pathAt, String::npos);
        shown.replace(pathAt, file.View().size(), "FILE");
        EXPECT_EQ(MaskIntegers(shown), R"json({"id":N,"ok":true,"result":{"dropped":N,"path":"FILE"}})json");
    }

    TEST_F(ProfilerControlGoldenTest, ProfilerDumpRefusesAPathOutsideTheWorkingFolderBeforeItLooksAtTheBackend) {
        if (Profiler::GetPrimaryBackend() == &Profiler::GetChromeTrace()) {
            GTEST_SKIP() << "an accepted path would write a file";
        }
        static constexpr const char* kRefused = "has to be a relative file name";
        const auto refused = [this](const String& path) {
            String params;
            JsonWriter writer(params);
            writer.BeginObject();
            writer.Member("path", StringView(path.c_str(), path.size()));
            writer.EndObject();
            return Ask(service, "profiler_dump", StringView(params.c_str(), params.size())).find(kRefused) != String::npos;
        };

        // An accepted path goes on to the backend check, which answers that nothing is recording
        for (const char* accepted : {"trace.json", "sub/trace.json", "caf\xC3\xA9.json", "with space.json", "a/..b", "a../b", "a/.b/c"}) {
            EXPECT_FALSE(refused(accepted)) << accepted;
        }
        for (const char* unsafe : {"", "/abs/trace.json", "..", "../trace.json", "a/../b", "a/..", "a//..", "./../x"}) {
            EXPECT_TRUE(refused(unsafe)) << unsafe;
        }
#ifdef _WIN32
        for (const char* unsafe : {"C:x", "C:\\x", "\\\\server\\share\\x", "\\x", "a\\..\\b"}) {
            EXPECT_TRUE(refused(unsafe)) << unsafe;
        }
#endif
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
            service.Post(MakeRequestText(1, method), [](const String&) {});
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
            collector.EndFrame(RenderCounters{.drawCalls = 4, .primitives = 9, .triangles = 8}, AllocTracker::GetTotals());
        }
        const StringView params = R"({"frames":2})";
        const ControlReply response = Call("frame_stats", params);
        const JsonView result = response.Result();
        ASSERT_TRUE(result.IsObject());
        const JsonView frames = MemberOf(result, "frames");
        ASSERT_TRUE(frames.IsArray());
        ASSERT_EQ(frames.Size(), 2u);
        const JsonView last = ItemAt(frames, 1);
        EXPECT_EQ(IntOf(last, "drawCalls"), 4);
        EXPECT_EQ(IntOf(last, "primitives"), 9);
        EXPECT_EQ(IntOf(last, "triangles"), 8);
        for (const char* key : {"frame", "paused", "frameMs", "cpuMs", "stagesMs", "textureSwitches", "allocCount", "allocBytes", "liveBytes", "arenaUsedBytes", "arenaPeakBytes", "arenaOverflows",
                                "poolLiveBlocks", "poolPageBytes", "primitives", "counters", "tagAllocCount", "tagLiveBytes"}) {
            EXPECT_TRUE(last.Find(key).has_value()) << key;
        }
        for (size_t i = 0; i < kMemTagCount; ++i) {
            const StringView name = EnumUtils<MemTag>::ToString(static_cast<MemTag>(i));
            EXPECT_TRUE(MemberOf(last, "tagAllocCount").Find(name).has_value()) << ToStdString(name);
            EXPECT_TRUE(MemberOf(last, "tagLiveBytes").Find(name).has_value()) << ToStdString(name);
        }
        EXPECT_TRUE(MemberOf(last, "stagesMs").Find("SceneDraw").has_value());
        EXPECT_EQ(BoolOf(result, "allocTracking"), AllocTracker::kEnabled);
    }

    TEST_F(ProfilerControlTest, FrameStatsListTheArenasWithTheirCounters) {
        constexpr size_t kBytes = 48;
        constexpr const char* kName = "ProfilerControlArena";
        LinearArena arena(kName);
        ASSERT_NE(arena.Allocate(kBytes, 1), nullptr);
        FrameStatsCollector& collector = CoreManager::GetFrameStats();
        collector.BeginFrame();
        collector.EndFrame({}, AllocTracker::GetTotals());

        const ControlReply response = Call("frame_stats");
        const JsonView arenas = MemberOf(response.Result(), "arenas");
        ASSERT_TRUE(arenas.IsArray());
        JsonView mine;
        for (const JsonView described : arenas.Items()) {
            if (StringOf(described, "name") == kName) {
                mine = described;
            }
        }
        ASSERT_TRUE(mine.IsObject());
        EXPECT_EQ(IntOf(mine, "used"), static_cast<int64_t>(kBytes));
        EXPECT_EQ(IntOf(mine, "peak"), static_cast<int64_t>(kBytes));
        EXPECT_EQ(IntOf(mine, "chunks"), 1);
        EXPECT_EQ(IntOf(mine, "overflows"), 0);
        EXPECT_GE(IntOf(mine, "capacity"), static_cast<int64_t>(kBytes));
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

        const ControlReply response = Call("frame_stats");
        const JsonView tags = MemberOf(response.Result(), "tags");
        ASSERT_TRUE(tags.IsArray());
        ASSERT_EQ(tags.Size(), kMemTagCount);
        for (size_t i = 0; i < kMemTagCount; ++i) {
            const JsonView described = ItemAt(tags, i);
            EXPECT_EQ(StringOf(described, "tag"), EnumUtils<MemTag>::ToString(static_cast<MemTag>(i)));
            for (const char* key : {"liveBytes", "peakLiveBytes", "count", "frees"}) {
                EXPECT_TRUE(described.Find(key).has_value()) << key;
            }
        }
        const JsonView scene = ItemAt(tags, +MemTag::Scene);
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_GE(IntOf(scene, "liveBytes").value_or(0), static_cast<int64_t>(kBlockBytes));
            EXPECT_GE(IntOf(scene, "count").value_or(0), 1);
        } else {
            EXPECT_EQ(IntOf(scene, "liveBytes"), 0);
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

        const ControlReply response = Call("frame_stats");
        const JsonView pools = MemberOf(response.Result(), "pools");
        ASSERT_TRUE(pools.IsArray());
        JsonView mine;
        for (const JsonView described : pools.Items()) {
            if (StringOf(described, "name") == kName) {
                mine = described;
            }
        }
        ASSERT_TRUE(mine.IsObject());
        EXPECT_EQ(IntOf(mine, "blockSize"), static_cast<int64_t>(pool.GetBlockSize()));
        EXPECT_EQ(IntOf(mine, "live"), 1);
        EXPECT_EQ(IntOf(mine, "peak"), 2);
        EXPECT_EQ(IntOf(mine, "pages"), 1);
        EXPECT_EQ(IntOf(mine, "allocations"), 2);
        pool.Deallocate(first);
    }

    TEST_F(ProfilerControlTest, FrameStatsCarryThePausedFlagOfEveryFrame) {
        FrameStatsCollector& collector = CoreManager::GetFrameStats();
        collector.BeginFrame(false);
        collector.EndFrame({}, AllocTracker::GetTotals());
        collector.BeginFrame(true);
        collector.EndFrame({}, AllocTracker::GetTotals());

        const StringView params = R"({"frames":2})";
        const ControlReply response = Call("frame_stats", params);
        const JsonView frames = MemberOf(response.Result(), "frames");
        ASSERT_TRUE(frames.IsArray());
        ASSERT_EQ(frames.Size(), 2u);
        EXPECT_EQ(BoolOf(ItemAt(frames, 0), "paused"), false);
        EXPECT_EQ(BoolOf(ItemAt(frames, 1), "paused"), true);
    }

    TEST_F(ProfilerControlTest, InvalidParamsAreRejected) {
        EXPECT_EQ(Call("frame_stats", R"({"frames":0})").ErrorCode(), "invalid_params");
        EXPECT_EQ(Call("frame_stats", R"({"frames":"3"})").ErrorCode(), "invalid_params");

        for (const char* path : {"../trace.json", "/tmp/trace.json", ""}) {
            String params;
            JsonWriter writer(params);
            writer.BeginObject();
            writer.Member("path", path);
            writer.EndObject();
            EXPECT_EQ(Call("profiler_dump", StringView(params.c_str(), params.size())).ErrorCode(), "invalid_params") << path;
        }
        EXPECT_EQ(Call("zone_allocs", R"({"top":0})").ErrorCode(), "invalid_params");
        EXPECT_EQ(Call("zone_allocs", R"({"reset":1})").ErrorCode(), "invalid_params");
    }

    TEST_F(ProfilerControlTest, DumpNeedsTheChromeTraceRecording) {
        Profiler::SetBackend(nullptr);
        EXPECT_EQ(Call("profiler_dump").ErrorCode(), "not_recording");
    }

    TEST_F(ProfilerControlTest, ZoneAllocsAnswersTheTotalsAndTheZones) {
        const ControlReply response = Call("zone_allocs");
        const JsonView result = response.Result();
        ASSERT_TRUE(result.IsObject());
        EXPECT_EQ(BoolOf(result, "enabled"), AllocTracker::kEnabled);
        EXPECT_TRUE(result.Find("zones").has_value());
        EXPECT_TRUE(MemberOf(result, "names").IsArray());
        EXPECT_TRUE(MemberOf(result, "sizes").IsArray());
        const JsonView totals = MemberOf(result, "totals");
        ASSERT_TRUE(totals.IsObject());
        for (const char* key : {"count", "bytes", "frees", "liveBytes", "peakLiveBytes", "largest", "noAllocViolations"}) {
            EXPECT_TRUE(totals.Find(key).has_value()) << key;
        }
    }

#if BE_PROFILER_ALLOC

    namespace {

        constexpr size_t kInts = 100;

        /// The row of @p array whose `zone` is @p zone and, when @p name is not null, whose `name` is @p name; a view that is not valid when none
        JsonView FindRow(JsonView array, const char* zone, const char* name) {
            for (const JsonView row : array.Items()) {
                if (StringOf(row, "zone") == zone && (name == nullptr || StringOf(row, "name") == name)) {
                    return row;
                }
            }
            return JsonView();
        }

        /// The `names` row of the zone @p zone and allocation name @p name in the answer of zone_allocs
        JsonView FindNameRow(JsonView result, const char* zone, const char* name) {
            return FindRow(MemberOf(result, "names"), zone, name);
        }

        /// The `sizes` row of the zone @p zone in the answer of zone_allocs
        JsonView FindSizeRow(JsonView result, const char* zone) {
            return FindRow(MemberOf(result, "sizes"), zone, nullptr);
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
        const ControlReply response = Call("zone_allocs");
        const JsonView result = response.Result();
        ASSERT_TRUE(result.IsObject());

        const JsonView outer = FindSizeRow(result, "ControlSizesOuter");
        ASSERT_TRUE(outer.IsObject());
        EXPECT_EQ(IntOf(outer, "allocs"), 1);
        const JsonView outerBuckets = MemberOf(outer, "buckets");
        ASSERT_TRUE(outerBuckets.IsArray());
        ASSERT_EQ(outerBuckets.Size(), 1u);
        EXPECT_EQ(IntOf(ItemAt(outerBuckets, 0), "upTo"), static_cast<int64_t>(SizeBucketBound(SizeBucket(kSmallBlock))));
        EXPECT_EQ(IntOf(ItemAt(outerBuckets, 0), "allocs"), 1);

        const JsonView inner = FindSizeRow(result, "ControlSizesInner");
        ASSERT_TRUE(inner.IsObject());
        EXPECT_EQ(IntOf(inner, "allocs"), 2);
        const JsonView innerBuckets = MemberOf(inner, "buckets");
        ASSERT_TRUE(innerBuckets.IsArray());
        ASSERT_EQ(innerBuckets.Size(), 1u);
        EXPECT_EQ(IntOf(ItemAt(innerBuckets, 0), "upTo"), static_cast<int64_t>(SizeBucketBound(SizeBucket(kBigBlock))));
        EXPECT_EQ(IntOf(ItemAt(innerBuckets, 0), "allocs"), 2);
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
        const ControlReply first = Call("zone_allocs", R"({"top":1})");
        const JsonView limitedSizes = MemberOf(first.Result(), "sizes");
        ASSERT_TRUE(limitedSizes.IsArray());
        ASSERT_EQ(limitedSizes.Size(), 1u);

        // The most allocations first (the test's own work outside every zone may lead, so the order is read between its two zones)
        const ControlReply all = Call("zone_allocs");
        const JsonView allSizes = MemberOf(all.Result(), "sizes");
        ASSERT_TRUE(allSizes.IsArray());
        size_t manyAt = allSizes.Size();
        size_t fewAt = allSizes.Size();
        size_t position = 0;
        for (const JsonView row : allSizes.Items()) {
            const String zone = StringOf(row, "zone");
            manyAt = zone == "ControlSizesMany" ? position : manyAt;
            fewAt = zone == "ControlSizesFew" ? position : fewAt;
            ++position;
        }
        ASSERT_LT(manyAt, allSizes.Size());
        EXPECT_LT(manyAt, fewAt);

        const ControlReply before = Call("zone_allocs", R"({"reset":true})");
        ASSERT_TRUE(before.Result().IsObject());
        EXPECT_TRUE(FindSizeRow(before.Result(), "ControlSizesFew").IsObject());

        const ControlReply after = Call("zone_allocs");
        ASSERT_TRUE(after.Result().IsObject());
        EXPECT_FALSE(FindSizeRow(after.Result(), "ControlSizesFew").IsValid());
        EXPECT_FALSE(FindSizeRow(after.Result(), "ControlSizesMany").IsValid());
    }

    TEST_F(ProfilerControlTest, ZoneAllocsNamesTheContainerThatAllocated) {
        AllocTracker::ResetZoneStats();
        {
            BE_PROFILE_SCOPE("ControlNamesZone");
            Vector<int> values;
            values.get_allocator().set_name("ControlNamed");
            values.reserve(kInts);
        }
        const ControlReply response = Call("zone_allocs");
        const JsonView result = response.Result();
        ASSERT_TRUE(result.IsObject());
        const JsonView row = FindNameRow(result, "ControlNamesZone", "ControlNamed");
        ASSERT_TRUE(row.IsObject());
        EXPECT_EQ(IntOf(row, "allocs"), 1);
        EXPECT_EQ(IntOf(row, "bytes"), static_cast<int64_t>(kInts * sizeof(int)));
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
        const ControlReply first = Call("zone_allocs", R"({"top":1})");
        const JsonView limitedNames = MemberOf(first.Result(), "names");
        ASSERT_TRUE(limitedNames.IsArray());
        EXPECT_EQ(limitedNames.Size(), 1u);

        const ControlReply before = Call("zone_allocs", R"({"reset":true})");
        ASSERT_TRUE(before.Result().IsObject());
        EXPECT_TRUE(FindNameRow(before.Result(), "ControlTopZone", "ControlBig").IsObject());
        EXPECT_TRUE(FindNameRow(before.Result(), "ControlTopZone", "ControlSmall").IsObject());

        const ControlReply after = Call("zone_allocs");
        ASSERT_TRUE(after.Result().IsObject());
        EXPECT_FALSE(FindNameRow(after.Result(), "ControlTopZone", "ControlBig").IsValid());
    }

#else

    TEST_F(ProfilerControlTest, ZoneAllocsHasNoNamesWithoutTheOption) {
        const ControlReply response = Call("zone_allocs");
        const JsonView result = response.Result();
        ASSERT_TRUE(result.IsObject());
        ASSERT_TRUE(MemberOf(result, "names").IsArray());
        EXPECT_EQ(MemberOf(result, "names").Size(), 0u);
        ASSERT_TRUE(MemberOf(result, "sizes").IsArray());
        EXPECT_EQ(MemberOf(result, "sizes").Size(), 0u);
    }

#endif

}  // namespace BECore::Tests
