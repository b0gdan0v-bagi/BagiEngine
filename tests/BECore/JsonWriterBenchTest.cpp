#include <BECore/Control/ControlCall.h>
#include <BECore/Control/Json.h>
#include <BECore/Control/JsonSerializer.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/JsonWriteReflectTypes.h>
#include <BECore/Rooms/RoomMessageReference.h>
#include <Common/AllocCheck.h>
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
            const Json id = 7;
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
            lobby.options = Json{{"fast", true}, {"rounds", 3}};
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
                listing.options = Json{{"fast", true}, {"rounds", 3}};
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
        const LobbySnapshot lobby = MakeBenchLobby();
        const RoomListMessage list = MakeBenchRoomList();

        Print(kPrefix, "view-dom", MeasureRoomDom([&] { return RoomMessageReference::MakeViewMessage(kRoom, 1, kVersion, false, view, actions); }));
        Print(kPrefix, "view-writer", MeasureRoomWriter([&](String& out) { AppendViewMessage(out, kRoom, 1, kVersion, false, view, actions); }));
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

}  // namespace BECore::Tests
