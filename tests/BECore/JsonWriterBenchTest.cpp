// Disabled benchmarks of the writer paths (`.claude/rules/perf-project.md`): allocations and time of a message written with `JsonWriter`. The numbers of the DOM
// path they replaced (a `Json` document, `dump`, a copy into the transport's `String`) are in the notes of the entry `json-deserializer` (the measurement of
// stage 4); nothing here builds a DOM any more.

#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlLog.h>
#include <BECore/Control/ControlRoots.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/GameControlTest.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/JsonWriteReflectTypes.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Profiler/ProfilerControl.h>
#include <BECore/Reflection/PropertyAccess.h>
#include <BECore/Reflection/TypedRef.h>
#include <Common/AllocCheck.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Games/GameControl.h>
#include <Games/GameManager.h>
#include <Games/GameText.h>
#include <Games/ReplayFile.h>
#include <Rooms/RoomProtocol.h>
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>

namespace BECore::Tests {

    namespace {

        constexpr int kIterations = 2000;
        constexpr int kLargeMembers = 40;
        constexpr int kLargeLevels = 3;

        void WriteStatusResult(JsonWriter& writer) {
            writer.BeginObject();
            writer.Member("deltaTime", 0.016666666666666666);
            writer.Member("fixedDeltaTime", 0.016666666666666666);
            writer.Member("frame", 120);
            writer.Key("log");
            writer.BeginObject();
            writer.Member("errors", 0);
            writer.Member("warnings", 3);
            writer.EndObject();
            writer.Member("manualClock", false);
            writer.Member("paused", false);
            writer.Member("totalTime", 2.0);
            writer.Key("window");
            writer.BeginObject();
            writer.Member("density", 2.0);
            writer.Member("height", 720);
            writer.Member("width", 1280);
            writer.EndObject();
            writer.EndObject();
        }

        /// Member names that sort the same as numbers, so the members are written in byte order
        String LargeKey(int index) {
            return Format("member_{:02}", index);
        }

        void WriteLargeResult(JsonWriter& writer, int level) {
            writer.BeginObject();
            for (int i = 0; i < kLargeMembers; ++i) {
                const String key = LargeKey(i);
                if (level + 1 < kLargeLevels && i % 10 == 0) {
                    writer.Key(key);
                    WriteLargeResult(writer, level + 1);
                } else if (i % 3 == 0) {
                    writer.Member(key, i * 1000 + level);
                } else if (i % 3 == 1) {
                    writer.Member(key, i * 0.37);
                } else {
                    writer.Member(key, StringView("text value of a member"));
                }
            }
            writer.EndObject();
        }

        struct Measure {
            uint64_t allocs = 0;
            uint64_t bytes = 0;
            double ns = 0.0;
        };

        template <typename TReply>
        Measure MeasureReply(TReply&& reply) {
            reply();  // the first message sizes every buffer the second one reuses
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < kIterations; ++i) {
                reply();
            }
            const auto stop = std::chrono::steady_clock::now();
            Measure measure;
            measure.ns = std::chrono::duration<double, std::nano>(stop - start).count() / kIterations;
            const AllocCount allocs = CountAllocations(reply);
            measure.allocs = allocs.count;
            measure.bytes = allocs.bytes;
            return measure;
        }

        void Print(const char* prefix, const char* name, const Measure& measure) {
            std::cout << prefix << ' ' << name << " allocs=" << measure.allocs << " bytes=" << measure.bytes << " ns=" << static_cast<uint64_t>(measure.ns) << std::endl;
        }

        template <typename TWriteResult>
        Measure MeasureWriterPath(TWriteResult&& writeResult) {
            String sent;
            const StringView id = "7";
            const Measure measure = MeasureReply([&] {
                sent.clear();
                AppendOkResponse(sent, id, writeResult);
            });
            EXPECT_FALSE(sent.empty());
            return measure;
        }

    }  // namespace

    /// Run by hand: `BagiEngineTests --gtest_also_run_disabled_tests --gtest_filter=JsonWriterBenchTest.DISABLED_*`; the counts need macos-profile
    TEST(JsonWriterBenchTest, DISABLED_ControlReply) {
        constexpr const char* kPrefix = "ControlReply";
        Print(kPrefix, "status-writer", MeasureWriterPath([](JsonWriter& writer) { WriteStatusResult(writer); }));
        Print(kPrefix, "large-writer", MeasureWriterPath([](JsonWriter& writer) { WriteLargeResult(writer, 0); }));
    }

    namespace {

        constexpr int kHandSize = 7;
        constexpr int kTableCards = 5;
        constexpr int kListedRooms = 20;
        constexpr int kLobbySeats = 4;

        /// The shape of the demo card game's seat view (about 40 members: the hand, the table, the scores). Both texts are made once outside the measured part: the
        /// figure is the message around the game's own JSON
        String MakeSeatViewText() {
            String text;
            JsonWriter writer(text);
            writer.BeginObject();
            writer.Key("hand");
            writer.BeginArray();
            for (int i = 0; i < kHandSize; ++i) {
                writer.BeginObject();
                writer.Member("rank", i + 2);
                writer.Member("suit", i % 4);
                writer.EndObject();
            }
            writer.EndArray();
            writer.Key("table");
            writer.BeginArray();
            for (int i = 0; i < kTableCards; ++i) {
                writer.BeginObject();
                writer.Member("rank", i + 5);
                writer.Member("seat", i % kLobbySeats);
                writer.Member("suit", (i + 1) % 4);
                writer.EndObject();
            }
            writer.EndArray();
            writer.Key("scores");
            writer.BeginArray();
            for (int i = 0; i < kLobbySeats; ++i) {
                writer.BeginObject();
                writer.Member("cards", kHandSize - i);
                writer.Member("name", "player");
                writer.Member("points", i * 11);
                writer.Member("seat", i);
                writer.EndObject();
            }
            writer.EndArray();
            writer.Member("deck", 21);
            writer.Member("round", 3);
            writer.Member("trump", 1);
            writer.Member("turn", 2);
            writer.EndObject();
            return text;
        }

        String MakeSeatActionsText() {
            String text;
            JsonWriter writer(text);
            writer.BeginArray();
            for (int i = 0; i < kHandSize; ++i) {
                writer.BeginObject();
                writer.Member("card", i);
                writer.Member("type", "play");
                writer.EndObject();
            }
            writer.EndArray();
            return text;
        }

        LobbySnapshot MakeBenchLobby() {
            LobbySnapshot lobby;
            lobby.room = "ABCDE";
            lobby.game = "cards";
            lobby.phase = RoomPhase::Lobby;
            lobby.hostSeat = 0;
            lobby.seats = {{0, SeatKind::Human, "Ann", true}, {1, SeatKind::Human, "Bob", false}, {2, SeatKind::Computer, {}, false}, {3, SeatKind::Empty, {}, false}};
            lobby.options = R"({"fast":true,"rounds":3})";
            return lobby;
        }

        RoomListMessage MakeBenchRoomList() {
            RoomListMessage list;
            list.game = "cards";
            for (int i = 0; i < kListedRooms; ++i) {
                RoomListing listing;
                listing.room = Format("ROOM{}", i % 10);
                listing.host = "Host name";
                listing.taken = i % kLobbySeats;
                listing.seats = kLobbySeats;
                listing.started = i % 2 == 0;
                listing.options = R"({"fast":true,"rounds":3})";
                list.rooms.push_back(listing);
            }
            return list;
        }

        /// The writer appends into a `String` that kept its capacity
        template <typename TAppend>
        Measure MeasureRoomWriter(TAppend&& append) {
            String sent;
            const Measure measure = MeasureReply([&] {
                sent.clear();
                append(sent);
            });
            EXPECT_FALSE(sent.empty());
            return measure;
        }

    }  // namespace

    TEST(JsonWriterBenchTest, DISABLED_RoomMessage) {
        constexpr const char* kPrefix = "RoomMessage";
        constexpr StringView kRoom = "ABCDE";
        constexpr uint64_t kVersion = 42;
        const String viewText = MakeSeatViewText();
        const String actionsText = MakeSeatActionsText();
        const LobbySnapshot lobby = MakeBenchLobby();
        const RoomListMessage list = MakeBenchRoomList();

        Print(kPrefix, "view-writer", MeasureRoomWriter([&](String& out) { AppendViewMessage(out, kRoom, 1, kVersion, false, viewText, actionsText); }));
        Print(kPrefix, "lobby-writer", MeasureRoomWriter([&](String& out) { AppendLobbyMessage(out, lobby); }));
        Print(kPrefix, "rooms-writer", MeasureRoomWriter([&](String& out) { AppendRoomListMessage(out, list); }));
        Print(kPrefix, "error-writer", MeasureRoomWriter([](String& out) { AppendErrorMessage(out, "seat_taken", "that seat is taken", "choose_seat"); }));
    }

    namespace {

        constexpr int64_t kBenchVersion = 42;
        constexpr StringView kBenchGame = "FakeGame";
        constexpr int kListedItems = 20;
        constexpr int kTableEntries = 10;

        /// The envelope of get_state: the result `{game, state, version}` with the state written by @p writeState
        template <typename TWriteState>
        void WriteStateResult(JsonWriter& writer, TWriteState&& writeState) {
            writer.BeginObject();
            writer.Member("game", kBenchGame);
            writer.Key("state");
            writeState(writer);
            writer.Member("version", kBenchVersion);
            writer.EndObject();
        }

        /// The push: the state is written into the watcher's kept scratch, its text enters the event with `Raw`
        void AppendPushWriter(String& out, String& scratch) {
            scratch.clear();
            JsonWriter state(scratch);
            WriteLargeResult(state, 0);

            JsonWriter writer(out);
            writer.BeginObject();
            writer.Key("data");
            writer.BeginObject();
            writer.Member("game", kBenchGame);
            writer.Key("state");
            writer.Raw(StringView(scratch.data(), scratch.size()));
            writer.Member("version", kBenchVersion);
            writer.EndObject();
            writer.Member("event", StringView("state_changed"));
            writer.EndObject();
        }

        template <typename T>
        Measure MeasureGenerated(const T& value) {
            return MeasureRoomWriter([&](String& out) {
                JsonWriter writer(out);
                value.WriteJson(writer);
            });
        }

    }  // namespace

    TEST(JsonWriterBenchTest, DISABLED_GameState) {
        constexpr const char* kPrefix = "GameState";
        Print(kPrefix, "get-state-writer", MeasureWriterPath([](JsonWriter& writer) { WriteStateResult(writer, [](JsonWriter& state) { WriteLargeResult(state, 0); }); }));
        String scratch;
        Print(kPrefix, "push-writer", MeasureRoomWriter([&](String& out) { AppendPushWriter(out, scratch); }));
    }

    TEST(JsonWriterBenchTest, DISABLED_ReflectedWrite) {
        constexpr const char* kPrefix = "ReflectedWrite";

        JsonWriteTestData::JsonWriteScalars scalars;
        scalars.count = 12;
        scalars.ratio = 0.25f;
        scalars.scale = 3.14159;
        scalars.name = "a name of the object";
        scalars.fixedName = "fixed";
        scalars.mode = JsonWriteTestData::JsonWriteMode::On;
        Print(kPrefix, "scalars-writer", MeasureGenerated(scalars));

        JsonWriteTestData::JsonWriteInnerList list;
        for (int i = 0; i < kListedItems; ++i) {
            JsonWriteTestData::JsonWriteInner item;
            item.name = Format("item {}", i);
            item.count = i;
            list.items.push_back(item);
        }
        Print(kPrefix, "list-writer", MeasureGenerated(list));

        JsonWriteTestData::JsonWriteBridged bridged;
        for (int i = 0; i < kTableEntries; ++i) {
            bridged.table[Format("key{}", i)] = i;
        }
        Print(kPrefix, "bridged-writer", MeasureGenerated(bridged));
    }

    namespace {

        constexpr int kLoggedLines = 20;
        constexpr int kBenchFrames = 3;
        constexpr int64_t kBenchRequestId = 9;
        constexpr uint64_t kNsPerMs = 1'000'000;

        uint64_t g_benchNowNs = 0;

        uint64_t BenchNowNs() {
            return g_benchNowNs;
        }

        /// What a handler of the control API costs from the posted request to the reply text: the `Drain()` alone is timed and counted (the request is posted
        /// outside), with a kept sink,
        Measure MeasureHandler(ControlService& service, StringView method, StringView paramsText = {}) {
            const String request = MakeRequestText(kBenchRequestId, method, paramsText);
            size_t replyBytes = 0;
            const auto post = [&] { service.Post(request, [&replyBytes](const String& text) { replyBytes = text.size(); }); };
            post();
            service.Drain();  // sizes every buffer the second call reuses
            double totalNs = 0.0;
            for (int i = 0; i < kIterations; ++i) {
                post();
                const auto start = std::chrono::steady_clock::now();
                service.Drain();
                totalNs += std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
            }
            post();
            const AllocCount allocs = CountAllocations([&] { service.Drain(); });
            EXPECT_GT(replyBytes, 0U);
            Measure measure;
            measure.ns = totalNs / kIterations;
            measure.allocs = allocs.count;
            measure.bytes = allocs.bytes;
            return measure;
        }

        void AddBenchFrame(uint32_t index) {
            FrameStatsCollector& collector = CoreManager::GetFrameStats();
            collector.BeginFrame(false);
            collector.BeginStage(FrameStage::Update);
            g_benchNowNs += kNsPerMs;
            collector.EndStage(FrameStage::Update);
            collector.BeginStage(FrameStage::SceneDraw);
            g_benchNowNs += kNsPerMs / 4;
            collector.EndStage(FrameStage::SceneDraw);
            collector.SetCounter("zeta"_intern, 1.5);
            collector.SetCounter("alpha"_intern, 2.0);
            collector.SetCounter("Mid"_intern, 0.25);
            g_benchNowNs += kNsPerMs * index;
            collector.EndFrame(RenderCounters{.drawCalls = 4, .triangles = 8, .textureSwitches = 1}, AllocTotals{});
        }

        FakeControllableGame* FindFakeGame() {
            for (const IntrusivePtr<IGame>& game : GameManager::GetInstance().GetAll()) {
                if (auto* fake = game->Cast<FakeControllableGame>()) {
                    return fake;
                }
            }
            return nullptr;
        }

    }  // namespace

    TEST(JsonWriterBenchTest, DISABLED_ControlHandlers) {
        constexpr const char* kPrefix = "ControlHandler";
        EngineEnvironment::EnsureCoreServices();
        ControlService service;
        RegisterControlBuiltins(service);
        RegisterProfilerControl(service);
        RegisterGameControl(service);
        service.RegisterHandler("noop", [](ControlCall& call) {
            call.Reply([](JsonWriter& writer) {
                writer.BeginObject();
                writer.EndObject();
            });
        });
        Print(kPrefix, "noop", MeasureHandler(service, "noop"));

        CoreManager::GetFrameStats().SetClock(BenchNowNs);
        g_benchNowNs = kNsPerMs;
        AddBenchFrame(0);
        for (uint32_t i = 1; i <= kBenchFrames; ++i) {
            AddBenchFrame(i);
        }
        const String frameParams = Format(R"({{"frames":{}}})", kBenchFrames);
        Print(kPrefix, "frame_stats-writer", MeasureHandler(service, "frame_stats", frameParams));
        CoreManager::GetFrameStats().SetClock(nullptr);

        JsonWriteTestData::JsonWriteScalars scalars;
        scalars.name = "a name of the object";
        service.Roots().Register("bench", ControlRoot{[&scalars] { return TypedRef::Of(scalars); }});
        constexpr StringView kPropertyParams = R"({"path":"name","root":"bench"})";
        Print(kPrefix, "get_property-writer", MeasureHandler(service, "get_property", kPropertyParams));

        LogEvent::Flush();
        const uint64_t mark = service.Log().GetNextSequence();
        for (int i = 0; i < kLoggedLines; ++i) {
            LogEvent::Emit(LogLevel::Warning, Format("JsonWriterBench log line {} of a message of an ordinary length", i));
        }
        LogEvent::Flush();
        const String logParams = Format(R"({{"mark":{}}})", mark);
        Print(kPrefix, "log_since-writer", MeasureHandler(service, "log_since", logParams));

        GameManager::GetInstance().Initialize();
        GameManager::GetInstance().SwitchTo("FakeControllable"_intern);
        FakeControllableGame* fake = FindFakeGame();
        ASSERT_NE(fake, nullptr);
        constexpr StringView kActionParams = R"({"type":"inc"})";
        Print(kPrefix, "perform_action-writer", MeasureHandler(service, "perform_action", kActionParams));
        service.Shutdown();
        GameManager::GetInstance().Shutdown();
    }

    TEST(JsonWriterBenchTest, DISABLED_ReplayLine) {
        constexpr const char* kPrefix = "ReplayLine";
        ReplayEntry entry;
        entry.op = ReplayOp::Action;
        entry.params = R"({"card":3,"seat":1,"type":"play"})";
        entry.outcome = String(R"({"legal":true,"points":4,"table":[1,2,3]})");
        entry.gap = ReplayGap{3, 0.05};
        EXPECT_EQ(FormatReplayEntry(entry), R"({"op":"action","params":{"card":3,"seat":1,"type":"play"},"outcome":{"legal":true,"points":4,"table":[1,2,3]},"gap":{"frames":3,"seconds":0.05}})");
        Print(kPrefix, "entry-writer", MeasureRoomWriter([&](String& out) { AppendReplayEntry(out, entry); }));
    }

    TEST(JsonWriterBenchTest, DISABLED_ReflectedShapes) {
        constexpr const char* kPrefix = "ReflectedWrite";
        constexpr int kPoints = 8;
        constexpr int kMapEntries = 8;

        JsonWriteTestData::JsonWriteChainLeaf derived;
        derived.same = "text";
        derived.e = "e value";
        derived.b = "b value";
        derived.maybe = 5;
        derived.m = {1, 2, 3};
        derived.c = 9;
        Print(kPrefix, "derived-writer", MeasureGenerated(derived));

        JsonWriteTestData::JsonWriteMathFields math;
        math.vec2 = Vec2(1.5f, 2.5f);
        math.vec3 = Vec3(1.0f, 2.0f, 3.0f);
        math.vec4 = Vec4(1.0f, 2.0f, 3.0f, 4.0f);
        math.ivec2 = IVec2(3, 4);
        math.quat = Quat(1.0f, 0.0f, 0.0f, 0.0f);
        math.color = Color(10, 20, 30, 255);
        math.rect = Rect(1.0f, 2.0f, 30.0f, 40.0f);
        for (int i = 0; i < kPoints; ++i) {
            math.points.push_back(Vec2(static_cast<float>(i), static_cast<float>(i) * 0.5f));
        }
        Print(kPrefix, "math-writer", MeasureGenerated(math));

        JsonWriteTestData::JsonWriteMaps maps;
        for (int i = 0; i < kMapEntries; ++i) {
            JsonWriteTestData::JsonWriteInner inner;
            inner.name = Format("inner {}", i);
            inner.count = i;
            maps.objects[Format("key{}", i)] = inner;
            maps.numbers[Format("number{}", i)] = i;
            maps.names[i] = Format("name {}", i);
        }
        Print(kPrefix, "map-writer", MeasureGenerated(maps));
    }

}  // namespace BECore::Tests
