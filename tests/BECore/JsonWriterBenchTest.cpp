#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlLog.h>
#include <BECore/Control/ControlRoots.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/Control/JsonSerializer.h>
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
#include <BECore/Rooms/RoomMessageReference.h>
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

        Json MakeStatusResult() {
            Json result = Json::object();
            result["frame"] = 120;
            result["totalTime"] = 2.0;
            result["deltaTime"] = 0.016666666666666666;
            result["fixedDeltaTime"] = 0.016666666666666666;
            result["manualClock"] = false;
            result["paused"] = false;
            result["log"] = Json{{"errors", 0}, {"warnings", 3}};
            result["window"] = Json{{"width", 1280}, {"height", 720}, {"density", 2.0}};
            return result;
        }

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

        /// Member names that sort the same as numbers, so the writer path can write them in the DOM's order
        String LargeKey(int index) {
            return Format("member_{:02}", index);
        }

        Json MakeLargeResult(int level) {
            Json result = Json::object();
            for (int i = 0; i < kLargeMembers; ++i) {
                const String key = LargeKey(i);
                if (level + 1 < kLargeLevels && i % 10 == 0) {
                    result[key.c_str()] = MakeLargeResult(level + 1);
                } else if (i % 3 == 0) {
                    result[key.c_str()] = i * 1000 + level;
                } else if (i % 3 == 1) {
                    result[key.c_str()] = i * 0.37;
                } else {
                    result[key.c_str()] = "text value of a member";
                }
            }
            return result;
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

        /// The reference: the DOM path of before the writer (a `Json` envelope, `dump`, a copy into the `String` the transport gets)
        template <typename TMakeResult>
        Measure MeasureDomPath(TMakeResult&& makeResult) {
            String sent;
            const Measure measure = MeasureReply([&] {
                Json response = Json::object();
                response["id"] = 7;
                response["ok"] = true;
                response["result"] = makeResult();
                const std::string text = response.dump();  //-NoStd: the reference measures nlohmann's own dump
                sent.assign(text.data(), text.size());
            });
            EXPECT_FALSE(sent.empty());
            return measure;
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
        Print(kPrefix, "status-dom", MeasureDomPath([] { return MakeStatusResult(); }));
        Print(kPrefix, "status-writer", MeasureWriterPath([](JsonWriter& writer) { WriteStatusResult(writer); }));
        Print(kPrefix, "large-dom", MeasureDomPath([] { return MakeLargeResult(0); }));
        Print(kPrefix, "large-writer", MeasureWriterPath([](JsonWriter& writer) { WriteLargeResult(writer, 0); }));
    }

    namespace {

        constexpr int kHandSize = 7;
        constexpr int kTableCards = 5;
        constexpr int kListedRooms = 20;
        constexpr int kLobbySeats = 4;

        /// The shape of the demo card game's seat view (about 40 members: the hand, the table, the scores). `ISeatedGame` still returns a `Json`,
        /// so both paths get the same one, built once outside the measured part: the figure is the message around the game's own JSON
        Json MakeSeatView() {
            Json hand = Json::array();
            for (int i = 0; i < kHandSize; ++i) {
                hand.push_back(Json{{"rank", i + 2}, {"suit", i % 4}});
            }
            Json table = Json::array();
            for (int i = 0; i < kTableCards; ++i) {
                table.push_back(Json{{"rank", i + 5}, {"suit", (i + 1) % 4}, {"seat", i % kLobbySeats}});
            }
            Json scores = Json::array();
            for (int i = 0; i < kLobbySeats; ++i) {
                scores.push_back(Json{{"seat", i}, {"points", i * 11}, {"cards", kHandSize - i}, {"name", "player"}});
            }
            return Json{{"hand", hand}, {"table", table}, {"scores", scores}, {"turn", 2}, {"round", 3}, {"trump", 1}, {"deck", 21}};
        }

        Json MakeSeatActions() {
            Json actions = Json::array();
            for (int i = 0; i < kHandSize; ++i) {
                actions.push_back(Json{{"type", "play"}, {"card", i}});
            }
            return actions;
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

        /// The message of before the writer: a `Json` DOM, dumped, then copied into the `String` the transport gets
        template <typename TMakeReference>
        Measure MeasureRoomDom(TMakeReference&& makeReference) {
            String sent;
            const Measure measure = MeasureReply([&] {
                const String text = makeReference();
                sent.assign(text.data(), text.size());
            });
            EXPECT_FALSE(sent.empty());
            return measure;
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
        const Json view = MakeSeatView();
        const Json actions = MakeSeatActions();
        const String viewText = DumpJson(view);
        const String actionsText = DumpJson(actions);
        const LobbySnapshot lobby = MakeBenchLobby();
        const RoomListMessage list = MakeBenchRoomList();

        Print(kPrefix, "view-dom", MeasureRoomDom([&] { return RoomMessageReference::MakeViewMessage(kRoom, 1, kVersion, false, view, actions); }));
        Print(kPrefix, "view-writer", MeasureRoomWriter([&](String& out) { AppendViewMessage(out, kRoom, 1, kVersion, false, viewText, actionsText); }));
        Print(kPrefix, "lobby-dom", MeasureRoomDom([&] { return RoomMessageReference::MakeLobbyMessage(lobby); }));
        Print(kPrefix, "lobby-writer", MeasureRoomWriter([&](String& out) { AppendLobbyMessage(out, lobby); }));
        Print(kPrefix, "rooms-dom", MeasureRoomDom([&] { return RoomMessageReference::MakeRoomListMessage(list); }));
        Print(kPrefix, "rooms-writer", MeasureRoomWriter([&](String& out) { AppendRoomListMessage(out, list); }));
        Print(kPrefix, "error-dom", MeasureRoomDom([] { return RoomMessageReference::MakeErrorMessage("seat_taken", "that seat is taken", "choose_seat"); }));
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

        Json MakeStateResult() {
            Json result = Json::object();
            result["game"] = "FakeGame";
            result["state"] = MakeLargeResult(0);
            result["version"] = kBenchVersion;
            return result;
        }

        /// The old push: a `Json` event dumped, then copied into the `String` the sink gets
        String MakePushDom() {
            Json event = Json::object();
            event["data"] = MakeStateResult();
            event["event"] = "state_changed";
            return DumpJson(event);
        }

        /// The new push: the state is written into the watcher's kept scratch, its text enters the event with `Raw`
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
        String DomText(const T& value) {
            JsonSerializer serializer;
            value.Serialize(serializer);
            return DumpJson(serializer.Take());
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
        Print(kPrefix, "get-state-dom", MeasureDomPath([] { return MakeStateResult(); }));
        Print(kPrefix, "get-state-writer", MeasureWriterPath([](JsonWriter& writer) { WriteStateResult(writer, [](JsonWriter& state) { WriteLargeResult(state, 0); }); }));
        Print(kPrefix, "push-dom", MeasureRoomDom([] { return MakePushDom(); }));
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
        Print(kPrefix, "scalars-dom", MeasureRoomDom([&] { return DomText(scalars); }));
        Print(kPrefix, "scalars-writer", MeasureGenerated(scalars));

        JsonWriteTestData::JsonWriteInnerList list;
        for (int i = 0; i < kListedItems; ++i) {
            JsonWriteTestData::JsonWriteInner item;
            item.name = Format("item {}", i);
            item.count = i;
            list.items.push_back(item);
        }
        Print(kPrefix, "list-dom", MeasureRoomDom([&] { return DomText(list); }));
        Print(kPrefix, "list-writer", MeasureGenerated(list));

        JsonWriteTestData::JsonWriteBridged bridged;
        for (int i = 0; i < kTableEntries; ++i) {
            bridged.table[Format("key{}", i)] = i;
        }
        Print(kPrefix, "bridged-dom", MeasureRoomDom([&] { return DomText(bridged); }));
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
        /// outside), with a kept sink, so the same path is measured for a writing handler and for its DOM twin
        Measure MeasureHandler(ControlService& service, StringView method, const Json& params) {
            const String request = MakeRequest(kBenchRequestId, method, params);
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

        /// frame_stats of the old code: a `Json` per frame, per arena, per pool and per tag, replied as a document
        Json DescribeFrameDom(const FrameStats& stats) {
            Json frame = Json::object();
            frame["frame"] = stats.frame;
            frame["paused"] = stats.paused;
            frame["frameMs"] = stats.frameMs;
            frame["cpuMs"] = stats.cpuMs;
            Json stages = Json::object();
            for (size_t i = 0; i < kFrameStageCount; ++i) {
                stages[ToStdString(EnumUtils<FrameStage>::ToString(static_cast<FrameStage>(i)))] = stats.stageMs[i];
            }
            frame["stagesMs"] = std::move(stages);
            frame["drawCalls"] = stats.draws.drawCalls;
            frame["triangles"] = stats.draws.triangles;
            frame["textureSwitches"] = stats.draws.textureSwitches;
            frame["allocCount"] = stats.allocCount;
            frame["allocBytes"] = stats.allocBytes;
            frame["liveBytes"] = stats.liveBytes;
            Json tagAllocCount = Json::object();
            Json tagLiveBytes = Json::object();
            for (size_t i = 0; i < kMemTagCount; ++i) {
                const auto name = ToStdString(EnumUtils<MemTag>::ToString(static_cast<MemTag>(i)));
                tagAllocCount[name] = stats.tagAllocCount[i];
                tagLiveBytes[name] = stats.tagLiveBytes[i];
            }
            frame["tagAllocCount"] = std::move(tagAllocCount);
            frame["tagLiveBytes"] = std::move(tagLiveBytes);
            frame["arenaUsedBytes"] = stats.arenaUsedBytes;
            frame["arenaPeakBytes"] = stats.arenaPeakBytes;
            frame["arenaOverflows"] = stats.arenaOverflows;
            frame["poolLiveBlocks"] = stats.poolLiveBlocks;
            frame["poolPageBytes"] = stats.poolPageBytes;
            Json counters = Json::object();
            for (const FrameCounter& counter : stats.counters) {
                counters[ToStdString(counter.name.ToStringView())] = counter.value;
            }
            frame["counters"] = std::move(counters);
            return frame;
        }

        Json DescribeFrameStatsDom() {
            Json list = Json::array();
            for (const FrameStats& stats : CoreManager::GetFrameStats().GetHistory(kBenchFrames)) {
                list.push_back(DescribeFrameDom(stats));
            }
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
            Json tags = Json::array();
            const Array<AllocTagTotals, kMemTagCount> totals = AllocTracker::GetTagTotals();
            for (size_t i = 0; i < kMemTagCount; ++i) {
                Json described = Json::object();
                described["tag"] = ToStdString(EnumUtils<MemTag>::ToString(static_cast<MemTag>(i)));
                described["liveBytes"] = totals[i].liveBytes;
                described["peakLiveBytes"] = totals[i].peakLiveBytes;
                described["count"] = totals[i].count;
                described["frees"] = totals[i].frees;
                tags.push_back(std::move(described));
            }
            Json pools = Json::array();
            for (const PoolStatsEntry& entry : AllocTracker::GetPoolStats()) {
                Json described = Json::object();
                described["name"] = entry.name;
                described["blockSize"] = entry.blockSize;
                described["live"] = entry.live;
                described["peak"] = entry.peak;
                described["pages"] = entry.pages;
                described["allocations"] = entry.allocations;
                pools.push_back(std::move(described));
            }
            Json result = Json::object();
            result["frames"] = std::move(list);
            result["allocTracking"] = AllocTracker::kEnabled;
            result["arenas"] = std::move(arenas);
            result["tags"] = std::move(tags);
            result["pools"] = std::move(pools);
            return result;
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

        /// get_property of the old code: the value of the path as a `Json` in a document
        void ReplyPropertyDom(ControlCall& call, const ControlService& service) {
            const auto root = GetJsonString(call.ParamsJson(), "root");
            const auto path = GetJsonString(call.ParamsJson(), "path");
            if (!root || !path) {
                call.Fail(ControlErrors::kInvalidParams, "root and path");
                return;
            }
            const auto value = GetProperty(service.Roots().Resolve(*root), *path);
            if (!value) {
                call.Fail(ControlErrors::kInternalError, "unreadable");
                return;
            }
            Json result = Json::object();
            result["root"] = MakeJsonString(*root);
            result["path"] = MakeJsonString(*path);
            result["value"] = std::visit(
                [](const auto& held) -> Json {
                    using TValue = std::decay_t<decltype(held)>;
                    if constexpr (std::is_same_v<TValue, String>) {
                        return MakeJsonString(StringView(held.data(), held.size()));
                    } else {
                        return held;
                    }
                },
                *value);
            call.Reply(std::move(result));
        }

        /// log_since of the old code: a `Json` line per record
        void ReplyLogSinceDom(ControlCall& call, const ControlService& service) {
            const auto mark = GetJsonInt(call.ParamsJson(), "mark");
            if (!mark) {
                call.Fail(ControlErrors::kInvalidParams, "mark");
                return;
            }
            LogEvent::Flush();
            const ControlLog& log = service.Log();
            const ControlLog::Since since = log.ReadSince(static_cast<uint64_t>(*mark), LogLevel::Warning, ControlLog::kMaxLogSinceLines);
            Json lines = Json::array();
            for (const LogRecord* record : since.lines) {
                Json line = Json::object();
                line["sequence"] = record->sequence;
                line["level"] = "warning";
                line["time"] = MakeJsonString(StringView(record->time));
                line["message"] = MakeJsonString(StringView(record->message.data(), record->message.size()));
                if (record->category != kGeneralLogCategory) {
                    line["category"] = MakeJsonString(log.History().GetCategoryName(record->category));
                }
                lines.push_back(std::move(line));
            }
            Json result = Json::object();
            result["lines"] = std::move(lines);
            result["warnings"] = since.warnings;
            result["errors"] = since.errors;
            result["dropped"] = since.dropped;
            result["next"] = since.next;
            if (since.truncated) {
                result["truncated"] = true;
            }
            call.Reply(std::move(result));
        }

        FakeControllableGame* FindFakeGame() {
            for (const IntrusivePtr<IGame>& game : GameManager::GetInstance().GetAll()) {
                if (auto* fake = game->Cast<FakeControllableGame>()) {
                    return fake;
                }
            }
            return nullptr;
        }

        /// The old `{game, outcome, version}` of perform_action as a `Json` document: the params and the outcome go through a tree
        void ReplyActionDom(ControlCall& call, FakeControllableGame& game) {
            const auto outcome = PerformActionText(game, DumpJson(call.ParamsJson()));
            if (!outcome) {
                call.Fail(outcome.error());
                return;
            }
            auto parsed = ParseJson(StringView(*outcome));
            Json result = Json::object();
            result["game"] = "FakeControllable";
            result["version"] = game.GetStateVersion();
            result["outcome"] = parsed ? std::move(*parsed) : Json();
            call.Reply(std::move(result));
        }

        /// A replay entry of the old recorder: the pieces dumped from `Json` and joined with `AppendFormat`
        String FormatReplayEntryDom(const ReplayEntry& entry) {
            String line;
            AppendFormat(line, "{{\"op\":\"action\",\"params\":{}", DumpJson(*ParseJson(entry.params)));
            if (entry.outcome) {
                AppendFormat(line, ",\"outcome\":{}", DumpJson(*ParseJson(*entry.outcome)));
            }
            AppendFormat(line, ",\"gap\":{{\"frames\":{},\"seconds\":{}}}}}", entry.gap.frames, DumpJson(Json(entry.gap.seconds)));
            return line;
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
        service.RegisterHandler("frame_stats_dom", [](ControlCall& call) { call.Reply(DescribeFrameStatsDom()); });
        service.RegisterHandler("get_property_dom", [&service](ControlCall& call) { ReplyPropertyDom(call, service); });
        service.RegisterHandler("log_since_dom", [&service](ControlCall& call) { ReplyLogSinceDom(call, service); });
        Print(kPrefix, "noop", MeasureHandler(service, "noop", Json::object()));

        CoreManager::GetFrameStats().SetClock(BenchNowNs);
        g_benchNowNs = kNsPerMs;
        AddBenchFrame(0);
        for (uint32_t i = 1; i <= kBenchFrames; ++i) {
            AddBenchFrame(i);
        }
        Json frameParams = Json::object();
        frameParams["frames"] = kBenchFrames;
        Print(kPrefix, "frame_stats-dom", MeasureHandler(service, "frame_stats_dom", frameParams));
        Print(kPrefix, "frame_stats-writer", MeasureHandler(service, "frame_stats", frameParams));
        CoreManager::GetFrameStats().SetClock(nullptr);

        JsonWriteTestData::JsonWriteScalars scalars;
        scalars.name = "a name of the object";
        service.Roots().Register("bench", ControlRoot{[&scalars] { return TypedRef::Of(scalars); }});
        Json propertyParams = Json::object();
        propertyParams["root"] = "bench";
        propertyParams["path"] = "name";
        Print(kPrefix, "get_property-dom", MeasureHandler(service, "get_property_dom", propertyParams));
        Print(kPrefix, "get_property-writer", MeasureHandler(service, "get_property", propertyParams));

        LogEvent::Flush();
        const uint64_t mark = service.Log().GetNextSequence();
        for (int i = 0; i < kLoggedLines; ++i) {
            LogEvent::Emit(LogLevel::Warning, Format("JsonWriterBench log line {} of a message of an ordinary length", i));
        }
        LogEvent::Flush();
        Json logParams = Json::object();
        logParams["mark"] = mark;
        Print(kPrefix, "log_since-dom", MeasureHandler(service, "log_since_dom", logParams));
        Print(kPrefix, "log_since-writer", MeasureHandler(service, "log_since", logParams));

        GameManager::GetInstance().Initialize();
        GameManager::GetInstance().SwitchTo("FakeControllable"_intern);
        FakeControllableGame* fake = FindFakeGame();
        ASSERT_NE(fake, nullptr);
        service.RegisterHandler("perform_action_dom", [fake](ControlCall& call) { ReplyActionDom(call, *fake); });
        Json actionParams = Json::object();
        actionParams["type"] = "inc";
        Print(kPrefix, "perform_action-dom", MeasureHandler(service, "perform_action_dom", actionParams));
        Print(kPrefix, "perform_action-writer", MeasureHandler(service, "perform_action", actionParams));
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
        EXPECT_EQ(FormatReplayEntryDom(entry), FormatReplayEntry(entry));
        Print(kPrefix, "entry-dom", MeasureRoomDom([&] { return FormatReplayEntryDom(entry); }));
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
        Print(kPrefix, "derived-dom", MeasureRoomDom([&] { return DomText(derived); }));
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
        Print(kPrefix, "math-dom", MeasureRoomDom([&] { return DomText(math); }));
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
        Print(kPrefix, "map-dom", MeasureRoomDom([&] { return DomText(maps); }));
        Print(kPrefix, "map-writer", MeasureGenerated(maps));
    }

}  // namespace BECore::Tests
