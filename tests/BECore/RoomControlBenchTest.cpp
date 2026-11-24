// What a room message and a control call cost: allocations, bytes and time per call, taken through the public entry points only (the parsers, the hub's
// OnMessage, ControlService::Post + Drain), so one file compiles on the tree before and after the room protocol moved off the DOM, and the two tables
// compare. Prints one line per case and asserts only that the message was understood; the allocations need macos-profile.
//
// Run by hand: `BagiEngineTests --gtest_also_run_disabled_tests --gtest_filter=RoomControlBenchTest.DISABLED_*`

#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Format/Format.h>
#include <BECore/Rooms/FakeSeatedGame.h>
#include <BECore/Rooms/RoomTestTimings.h>
#include <BECore/Rooms/TestRandom.h>
#include <Common/AllocCheck.h>
#include <Games/SeatedGameRegistry.h>
#include <Rooms/RoomHub.h>
#include <Rooms/RoomLimits.h>
#include <Rooms/RoomProtocol.h>
#include <algorithm>
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>

namespace BECore::Tests {

    namespace {

        constexpr int kBatches = 5;  // the time of a case is the best of these
        constexpr int kRoundsPerBatch = 2000;
        constexpr int kHubRoundsPerBatch = 400;
        constexpr int kAllocRounds = 50;  // the allocations of a case are the mean over these steady calls
        constexpr int kListedRooms = 20;

        struct Measure {
            double allocs = 0.0;
            double bytes = 0.0;
            double nanoseconds = 0.0;
        };

        /// Runs @p call warm, then times the best of kBatches batches and counts the allocations of a steady run
        template <typename TCall>
        Measure MeasureCall(int roundsPerBatch, TCall&& call) {
            for (int i = 0; i < kAllocRounds; ++i) {
                call();  // sizes every buffer and container the next calls reuse
            }
            Measure measure;
            measure.nanoseconds = 1.0e30;
            for (int batch = 0; batch < kBatches; ++batch) {
                const auto start = std::chrono::steady_clock::now();
                for (int i = 0; i < roundsPerBatch; ++i) {
                    call();
                }
                const auto stop = std::chrono::steady_clock::now();
                measure.nanoseconds = std::min(measure.nanoseconds, std::chrono::duration<double, std::nano>(stop - start).count() / roundsPerBatch);
            }
            const AllocCount counted = CountAllocations([&] {
                for (int i = 0; i < kAllocRounds; ++i) {
                    call();
                }
            });
            measure.allocs = static_cast<double>(counted.count) / kAllocRounds;
            measure.bytes = static_cast<double>(counted.bytes) / kAllocRounds;
            return measure;
        }

        void Print(const char* name, const Measure& measure) {
            std::cout << "RoomControlBench " << name << " allocs=" << measure.allocs << " bytes=" << measure.bytes << " ns=" << measure.nanoseconds << std::endl;
        }

        void MeasureClientParse(const char* name, StringView text) {
            const auto first = ParseClientMessage(text);
            ASSERT_TRUE(first.has_value()) << name << ": " << first.error().message.c_str();
            Print(name, MeasureCall(kRoundsPerBatch, [&] { const auto parsed = ParseClientMessage(text); }));
        }

        void MeasureServerParse(const char* name, StringView text) {
            const auto first = ParseServerMessage(text);
            ASSERT_TRUE(first.has_value()) << name << ": " << first.error().message.c_str();
            Print(name, MeasureCall(kRoundsPerBatch, [&] { const auto parsed = ParseServerMessage(text); }));
        }

        String MakeRoomsText() {
            String text = Format(R"({{"v":{},"type":"rooms","game":"fake-public","more":false,"rooms":[)", kRoomProtocolVersion);
            for (int i = 0; i < kListedRooms; ++i) {
                text += Format(R"({}{{"room":"ABCD{}","host":"Host name","seats":4,"taken":{},"started":{},"options":{{"count":3,"open":true}}}})", i == 0 ? "" : ",",
                               RoomLimits::kRoomCodeAlphabet[static_cast<size_t>(i) % 10], i % 4, i % 2 == 0 ? "true" : "false");
            }
            text += "]}";
            return text;
        }

        class BenchHub {
        public:
            BenchHub() : _hub(_registry, MakeSettings(), MakeSequence(42), MakeSink()) {
                EXPECT_TRUE(_registry.Register(SeatedGameInfo{"fake", FakeSeatedGame::kSeatCount}, [] { return MakeUnique<FakeSeatedGame>(); }));
            }

            /// A started room with the connection as its host, so an `action` reaches the room's game
            void StartRoom() {
                _hub.OnConnected(kConnection, _now);
                Send(Format(R"({{"v":{},"type":"create_room","game":"fake","name":"P0","options":{{"seed":7}}}})", kRoomProtocolVersion));
                Send(Format(R"({{"v":{},"type":"start"}})", kRoomProtocolVersion));
            }

            void Send(StringView text) {
                _now += std::chrono::milliseconds(100);
                _hub.OnMessage(kConnection, text, _now);
            }

            /// The last text the hub sent the connection
            [[nodiscard]] const String& GetLast() const {
                return _last;
            }

            [[nodiscard]] size_t GetSentBytes() const {
                return _sentBytes;
            }

        private:
            static constexpr ConnectionId kConnection = 1;

            static RoomHubSettings MakeSettings() {
                RoomHubSettings settings;
                settings.timings = NoComputerDelayTimings();
                settings.rateBurst = 1 << 30;  // a bench sends far more than a client may
                settings.ratePerSecond = 1.0e9;
                return settings;
            }

            HubSink MakeSink() {
                HubSink sink;
                sink.send = [this](ConnectionId, const String& text) {
                    _sentBytes += text.size();
                    _last = text;
                };
                sink.close = [](ConnectionId, String) {};
                return sink;
            }

            SeatedGameRegistry _registry;
            RoomHub _hub;
            RoomTime _now{std::chrono::seconds(1000)};
            String _last;
            size_t _sentBytes = 0;
        };

    }  // namespace

    TEST(RoomControlBenchTest, DISABLED_RoomMessages) {
        const String v = Format("{}", kRoomProtocolVersion);
        MeasureClientParse("client create_room", Format(R"({{"v":{},"type":"create_room","game":"fake","name":"Ann","options":{{"rounds":3,"fast":true,"tags":["a","b"]}},"public":true}})", v));
        MeasureClientParse("client action", Format(R"({{"v":{},"type":"action","version":12,"action":{{"type":"play","card":"c3_1"}}}})", v));
        MeasureClientParse("client join_room", Format(R"({{"v":{},"type":"join_room","code":"ABCDE","name":"Bob"}})", v));
        MeasureClientParse("client list_rooms", Format(R"({{"v":{},"type":"list_rooms","game":"fake"}})", v));

        MeasureServerParse("server lobby (4 seats)", Format(R"({{"v":{},"type":"lobby","room":"ABCDE","game":"fake-public","phase":"lobby","host":0,"seats":[)"
                                                            R"({{"seat":0,"kind":"human","name":"Ann","connected":true}},{{"seat":1,"kind":"human","name":"Bob","connected":false}},)"
                                                            R"({{"seat":2,"kind":"computer"}},{{"seat":3,"kind":"empty"}}],"options":{{"count":3,"open":true}}}})",
                                                            v));
        MeasureServerParse("server rooms (20 rooms)", MakeRoomsText());
        MeasureServerParse("server view", Format(R"({{"v":{},"type":"view","room":"ABCDE","seat":1,"version":9,"finished":false,)"
                                                 R"("view":{{"hand":["c1_1","c2_3","c4_2"],"turn":1,"table":[]}},"actions":[{{"type":"play","card":"c1_1"}},{{"type":"play","card":"c2_3"}}]}})",
                                                 v));
    }

    TEST(RoomControlBenchTest, DISABLED_HubRound) {
        BenchHub hub;
        hub.StartRoom();
        // A version no game has reached: the hub parses the message, finds the room and answers `stale_action` with the current view
        const String stale = Format(R"({{"v":{},"type":"action","version":0,"action":{{"type":"play","card":"c3_1"}}}})", kRoomProtocolVersion);
        hub.Send(stale);
        ASSERT_NE(hub.GetLast().find("view"), String::npos) << hub.GetLast().c_str();
        Print("hub action (stale, error + view)", MeasureCall(kHubRoundsPerBatch, [&] { hub.Send(stale); }));
        EXPECT_GT(hub.GetSentBytes(), 0u);
    }

    TEST(RoomControlBenchTest, DISABLED_ControlCalls) {
        ControlService service;
        RegisterControlBuiltins(service);
        size_t replyBytes = 0;
        const auto call = [&](StringView request) {
            return [&service, &replyBytes, request] {
                service.Post(request, [&replyBytes](const String& text) { replyBytes += text.size(); });
                service.Drain();
            };
        };
        Print("control ping", MeasureCall(kRoundsPerBatch, call(R"({"id":1,"method":"ping"})")));
        Print("control log_since", MeasureCall(kRoundsPerBatch, call(R"({"id":2,"method":"log_since","params":{"mark":0,"level":"error"}})")));
        Print("control batch (3 pings)", MeasureCall(kRoundsPerBatch, call(R"({"id":3,"method":"batch","params":{"calls":[{"method":"ping"},{"method":"ping"},{"method":"ping"}]}})")));
        EXPECT_GT(replyBytes, 0u);
    }

}  // namespace BECore::Tests
