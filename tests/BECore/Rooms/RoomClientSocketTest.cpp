#include "../WebSocketTestClient.h"
#include "JudgedTransport.h"
#include "ManualClock.h"
#include "TestRandom.h"

#include <Common/Printers.h>
#include <RemoteControl/WebSocketClient.h>
#include <RoomClient/RoomClient.h>
#include <RoomServer/DemoCardGame.h>
#include <RoomServer/RoomWebSocketServer.h>
#include <Rooms/RoomErrors.h>
#include <algorithm>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

namespace BECore::Tests {

    namespace {

        using namespace std::chrono_literals;
        using Kind = RoomClientEventKind;

        constexpr std::chrono::milliseconds kPlayLimit{30'000};
        constexpr std::chrono::milliseconds kShortLimit{10'000};
        constexpr int kCardsInAHand = DemoCardGame::kTotalPlays;

        /// What the test wants to know of the transports a client made, and a hand on the connection to cut it
        struct Tap {
            class TappedTransport* current = nullptr;
            /// How many connections the client made
            size_t connections = 0;
            /// The `type` of every text the client received, in order
            std::vector<std::string> types;

            [[nodiscard]] size_t CountType(std::string_view type) const {
                return static_cast<size_t>(std::count(types.begin(), types.end(), type));
            }

            /// Cuts the connection like a cable pulled: no close frame, the client sees the end by itself
            void Sever();
        };

        /// Records what a client's transport receives and lets the test abort the live connection
        class TappedTransport final : public IRoomTransport {
        public:
            TappedTransport(eastl::unique_ptr<IRoomTransport> inner, Tap& tap) : _inner(std::move(inner)), _tap(tap) {
                _tap.current = this;
                ++_tap.connections;
            }

            ~TappedTransport() override {
                if (_tap.current == this) {
                    _tap.current = nullptr;
                }
            }

            std::expected<void, eastl::string> Open(eastl::string_view url) override {
                return _inner->Open(url);
            }

            bool SendText(eastl::string_view text) override {
                return _inner->SendText(text);
            }

            void Close(uint16_t code, eastl::string_view reason) override {
                _inner->Close(code, reason);
            }

            void Abort() override {
                _inner->Abort();
            }

            bool PollEvent(RoomTransportEvent& event) override {
                if (!_inner->PollEvent(event)) {
                    return false;
                }
                if (event.kind == RoomTransportEventKind::Text) {
                    const auto parsed = ParseJson(event.text);
                    _tap.types.push_back(parsed ? std::string(GetJsonString(*parsed, "type").value_or(eastl::string()).c_str()) : std::string("?"));
                }
                return true;
            }

        private:
            eastl::unique_ptr<IRoomTransport> _inner;
            Tap& _tap;
        };

        void Tap::Sever() {
            if (current) {
                current->Abort();
            }
        }

        /// A human at the table: a RoomClient on the native transport, judged on the wire, that plays the first action it is offered
        struct Player {
            explicit Player(RoomClientSettings settings = {}) : judge(std::make_shared<WireJudgeLog>()) {
                settings.transportFactory = [this]() -> eastl::unique_ptr<IRoomTransport> {
                    auto native = MakeDefaultRoomTransport();
                    if (!native) {
                        return nullptr;
                    }
                    return eastl::make_unique<TappedTransport>(eastl::make_unique<JudgedTransport>(std::move(native), judge), tap);
                };
                client = std::make_unique<RoomClient>(std::move(settings));
            }

            [[nodiscard]] size_t Count(Kind kind) const {
                return static_cast<size_t>(std::count_if(events.begin(), events.end(), [kind](const RoomClientEvent& event) { return event.kind == kind; }));
            }

            [[nodiscard]] const RoomClientEvent* Find(Kind kind) const {
                const auto found = std::find_if(events.begin(), events.end(), [kind](const RoomClientEvent& event) { return event.kind == kind; });
                return found == events.end() ? nullptr : &*found;
            }

            [[nodiscard]] int TableSize() const {
                const Json* table = FindJsonMember(client->GetView(), "table");
                return table && table->is_array() ? static_cast<int>(table->size()) : -1;
            }

            /// How many plays of @p seat the last view shows
            [[nodiscard]] int PlaysOf(int seat) const {
                int plays = 0;
                const Json* table = FindJsonMember(client->GetView(), "table");
                for (size_t i = 0; table && table->is_array() && i < table->size(); ++i) {
                    plays += GetJsonInt((*table)[i], "seat").value_or(-1) == seat ? 1 : 0;
                }
                return plays;
            }

            /// Polls, takes the events and plays the first action of the last view, once per version of it
            void Pump() {
                client->Poll();
                const size_t before = events.size();
                client->DrainEvents(events);
                for (size_t i = before; i < events.size(); ++i) {
                    if (events[i].kind == Kind::Resumed) {
                        // What was sent on the old connection may never have arrived
                        acted = false;
                    }
                }
                if (!autoplay || !client->IsInRoom() || !client->HasView() || client->IsFinished() || !client->GetActions().is_array() || client->GetActions().empty()) {
                    return;
                }
                if (acted && actedVersion == client->GetViewVersion()) {
                    return;
                }
                if (client->SendAction(client->GetActions()[0]).has_value()) {
                    acted = true;
                    actedVersion = client->GetViewVersion();
                }
            }

            // The tap and the judge are used by the transports the client makes: they outlive the client (declared first)
            Tap tap;
            std::shared_ptr<WireJudgeLog> judge;
            std::unique_ptr<RoomClient> client;
            eastl::vector<RoomClientEvent> events;
            bool autoplay = true;
            bool acted = false;
            uint64_t actedVersion = 0;
        };

        /// A client that reconnects quickly: real time passes in these tests
        RoomClientSettings MakeClientSettings() {
            RoomClientSettings settings;
            settings.backoffStart = 20ms;
            settings.backoffMax = 100ms;
            return settings;
        }

        RoomServerSettings MakeServerSettings() {
            RoomServerSettings settings;
            settings.tickInterval = 10ms;
            // The computers move at once (a test that needs a slower hand says so); no liveness pings, which a moved clock would turn into drops
            settings.hub.timings = RoomTimings{30s, 10min, 0ms};
            settings.pingInterval = 0ms;
            settings.random = MakeSequence(5);
            return settings;
        }

        class RoomClientSocketTest : public ::testing::Test {
        protected:
            void SetUp() override {
                ASSERT_TRUE(registry.Register(SeatedGameInfo{"cards", DemoCardGame::kSeatCount}, [] { return eastl::make_unique<DemoCardGame>(); }));
                ASSERT_TRUE(registry.Register(SeatedGameInfo{"cards-leaky", DemoCardGame::kSeatCount}, [] { return eastl::make_unique<LeakyDemoCardGame>(); }));
            }

            void TearDown() override {
                // The clients go first: they hold connections to the server
                players.clear();
                if (server) {
                    server->Stop();
                }
            }

            void StartServer(RoomServerSettings settings = MakeServerSettings()) {
                server = std::make_unique<RoomWebSocketServer>(registry, std::move(settings));
                const auto started = server->Start();
                ASSERT_TRUE(started.has_value()) << (started.has_value() ? "" : started.error().message.c_str());
                port = *started;
                url = Format("ws://127.0.0.1:{}", port);
            }

            Player& AddPlayer(RoomClientSettings settings = MakeClientSettings()) {
                players.push_back(std::make_unique<Player>(std::move(settings)));
                return *players.back();
            }

            void Pump() {
                for (const auto& player : players) {
                    player->Pump();
                }
            }

            template <typename Condition>
            bool WaitFor(Condition&& condition, std::chrono::milliseconds limit = kPlayLimit) {
                return WaitUntil([this] { Pump(); }, std::forward<Condition>(condition), limit);
            }

            /// The first player hosts a room of @p game, the others join it; everybody is in the room afterwards, nobody has started
            void MakeTable(eastl::string_view game, size_t humans, const Json& options = Json::object(), RoomClientSettings guestSettings = MakeClientSettings()) {
                Player& host = AddPlayer();
                ASSERT_TRUE(host.client->Host(url, game, "P0", options).has_value());
                ASSERT_TRUE(WaitFor([&] { return host.client->IsInRoom(); }, kShortLimit));
                for (size_t i = 1; i < humans; ++i) {
                    Player& guest = AddPlayer(guestSettings);
                    ASSERT_TRUE(guest.client->Join(url, host.client->GetRoom(), Format("P{}", i).c_str()).has_value());
                    ASSERT_TRUE(WaitFor([&] { return guest.client->IsInRoom(); }, kShortLimit));
                }
            }

            [[nodiscard]] bool EveryoneSeesTheEnd() const {
                return std::all_of(players.begin(), players.end(), [](const auto& player) { return player->client->IsFinished(); });
            }

            /// The hand is over for everybody, every card was played, and no text named a card its receiver may not know
            void ExpectACleanFinishedHand() {
                for (size_t i = 0; i < players.size(); ++i) {
                    const Player& player = *players[i];
                    EXPECT_EQ(player.TableSize(), kCardsInAHand) << "player " << i;
                    EXPECT_GT(player.judge->texts, 3u) << "player " << i;
                    EXPECT_TRUE(player.judge->leaks.empty()) << "player " << i << " was sent " << (player.judge->leaks.empty() ? "" : player.judge->leaks.front());
                }
            }

            SeatedGameRegistry registry;
            std::unique_ptr<RoomWebSocketServer> server;
            uint16_t port = 0;
            eastl::string url;
            std::vector<std::unique_ptr<Player>> players;
        };

    }  // namespace

    // ---- Whole hands over real sockets ---------------------------------------------------------------------------------------------

    TEST_F(RoomClientSocketTest, TwoHumansAndTwoComputersPlayAHandAndNobodyPeeks) {
        StartServer();
        ASSERT_NO_FATAL_FAILURE(MakeTable("cards", 2));
        ASSERT_TRUE(players[0]->client->Start().has_value());
        ASSERT_TRUE(WaitFor([&] { return EveryoneSeesTheEnd(); })) << "the hand did not reach its end";
        ExpectACleanFinishedHand();
        for (const auto& player : players) {
            EXPECT_EQ(player->Count(Kind::Error), 0u);
            EXPECT_EQ(player->Count(Kind::ConnectionLost), 0u);
        }
        EXPECT_EQ(players[0]->client->GetSeat(), 0);
        EXPECT_EQ(players[1]->client->GetSeat(), 1);
    }

    TEST_F(RoomClientSocketTest, FourHumansPlayAWholeHand) {
        StartServer();
        ASSERT_NO_FATAL_FAILURE(MakeTable("cards", 4));
        ASSERT_TRUE(players[0]->client->Start().has_value());
        ASSERT_TRUE(WaitFor([&] { return EveryoneSeesTheEnd(); }));
        ExpectACleanFinishedHand();
        for (const auto& player : players) {
            EXPECT_EQ(player->Count(Kind::Error), 0u);
        }
    }

    TEST_F(RoomClientSocketTest, TheWireJudgeCatchesAGameThatLeaks) {
        // The negative control: the same hand on a game whose view carries every hand. If the judge could not fail, the tests above would prove nothing
        StartServer();
        ASSERT_NO_FATAL_FAILURE(MakeTable("cards-leaky", 2));
        ASSERT_TRUE(players[0]->client->Start().has_value());
        ASSERT_TRUE(WaitFor([&] { return EveryoneSeesTheEnd(); }));
        for (const auto& player : players) {
            EXPECT_FALSE(player->judge->leaks.empty()) << "the judge did not notice that every hand was on the wire";
        }
    }

    // ---- A connection that breaks ----------------------------------------------------------------------------------------------

    TEST_F(RoomClientSocketTest, ASeveredConnectionIsBroughtBackInsideTheGraceAndTheHandFinishes) {
        // Slow computers keep the hand going long enough to cut it in the middle; the clock is the real one and the grace (30 s) is far away
        RoomServerSettings settings = MakeServerSettings();
        settings.hub.timings.computerDelay = 100ms;
        StartServer(std::move(settings));
        ASSERT_NO_FATAL_FAILURE(MakeTable("cards", 2));
        Player& cut = *players[1];
        const eastl::string token = cut.client->GetSession().token;
        const int seat = cut.client->GetSeat();
        ASSERT_TRUE(players[0]->client->Start().has_value());
        ASSERT_TRUE(WaitFor([&] { return cut.TableSize() >= 1; }));
        ASSERT_FALSE(cut.client->IsFinished());

        cut.tap.Sever();
        ASSERT_TRUE(WaitFor([&] { return cut.Count(Kind::Resumed) == 1; }, kShortLimit)) << "the client did not come back";
        EXPECT_EQ(cut.Count(Kind::ConnectionLost), 1u);
        EXPECT_TRUE(cut.Find(Kind::ConnectionLost)->willRetry);
        EXPECT_EQ(cut.client->GetSeat(), seat);
        EXPECT_EQ(cut.client->GetSession().token, token);
        EXPECT_EQ(cut.tap.connections, 2u);

        ASSERT_TRUE(WaitFor([&] { return EveryoneSeesTheEnd(); }));
        ExpectACleanFinishedHand();
    }

    TEST_F(RoomClientSocketTest, AClientKeptAwayPastTheGraceFindsAComputerPlayedItsSeatAndTakesItBack) {
        ManualClock clock;
        RoomServerSettings settings = MakeServerSettings();
        settings.clock = clock.AsFunction();
        StartServer(std::move(settings));

        // Seat 0 moves first, and seat 1 does not play until the test lets it: the hand stalls at seat 1 when the client is cut
        RoomClientSettings slowReturn = MakeClientSettings();
        slowReturn.backoffStart = 1500ms;
        slowReturn.backoffMax = 1500ms;
        ASSERT_NO_FATAL_FAILURE(MakeTable("cards", 2, Json::object({{"firstSeat", 0}}), slowReturn));
        Player& host = *players[0];
        Player& away = *players[1];
        const eastl::string token = away.client->GetSession().token;
        away.autoplay = false;
        ASSERT_TRUE(host.client->Start().has_value());
        ASSERT_TRUE(WaitFor([&] { return away.client->HasView(); }));
        away.tap.Sever();

        // The host is told the guest is away
        ASSERT_TRUE(WaitFor([&] { return host.client->GetLobby().seats.size() > 1 && !host.client->GetLobby().seats[1].connected; }, kShortLimit));
        EXPECT_EQ(away.client->GetState(), RoomClientState::Reconnecting);

        // Inside the grace nobody plays the seat
        clock.Advance(20s);
        std::this_thread::sleep_for(100ms);
        WaitFor([] { return false; }, 100ms);
        EXPECT_EQ(host.PlaysOf(1), 0);

        // Past it a computer does
        clock.Advance(15s);
        ASSERT_TRUE(WaitFor([&] { return host.PlaysOf(1) > 0; }, kShortLimit)) << "no computer took the seat after the grace";
        EXPECT_EQ(away.Count(Kind::Resumed), 0u);

        // The client returns after its backoff with the same token, finds the hand moved on, and plays the rest
        ASSERT_TRUE(WaitFor([&] { return away.Count(Kind::Resumed) == 1; }, kShortLimit));
        away.autoplay = true;
        EXPECT_EQ(away.client->GetSeat(), 1);
        EXPECT_EQ(away.client->GetSession().token, token);
        EXPECT_GE(away.PlaysOf(1), 1);
        ASSERT_TRUE(WaitFor([&] { return EveryoneSeesTheEnd(); }));
        ExpectACleanFinishedHand();
    }

    TEST_F(RoomClientSocketTest, ANewClientObjectResumesTheSavedSessionAfterTheAppRestarted) {
        RoomServerSettings settings = MakeServerSettings();
        settings.hub.timings.computerDelay = 100ms;
        StartServer(std::move(settings));
        ASSERT_NO_FATAL_FAILURE(MakeTable("cards", 2));
        const RoomSession saved = players[1]->client->GetSession();
        const int seat = players[1]->client->GetSeat();
        ASSERT_TRUE(saved.IsValid());
        ASSERT_TRUE(players[0]->client->Start().has_value());
        ASSERT_TRUE(WaitFor([&] { return players[1]->TableSize() >= 1; }));

        // The app is gone (its connection is dropped with it), and a new process starts with the saved session
        players.erase(players.begin() + 1);
        Player& restarted = AddPlayer();
        ASSERT_TRUE(restarted.client->Resume(saved).has_value());
        EXPECT_EQ(restarted.client->GetState(), RoomClientState::Reconnecting);
        ASSERT_TRUE(WaitFor([&] { return restarted.client->IsInRoom(); }, kShortLimit));
        EXPECT_EQ(restarted.Count(Kind::Resumed), 1u);
        EXPECT_EQ(restarted.client->GetSeat(), seat);

        ASSERT_TRUE(WaitFor([&] { return EveryoneSeesTheEnd(); }));
        ExpectACleanFinishedHand();
    }

    TEST_F(RoomClientSocketTest, AServerThatStopsIsRetriedUntilTheGiveUpTime) {
        StartServer();
        RoomClientSettings impatient = MakeClientSettings();
        impatient.reconnectGiveUp = 700ms;
        impatient.backoffStart = 50ms;
        impatient.backoffMax = 100ms;
        Player& host = AddPlayer(impatient);
        ASSERT_TRUE(host.client->Host(url, "cards", "P0").has_value());
        ASSERT_TRUE(WaitFor([&] { return host.client->IsInRoom(); }, kShortLimit));

        server->Stop();
        ASSERT_TRUE(WaitFor([&] { return host.client->GetState() == RoomClientState::Disconnected; }, kShortLimit)) << "the client never gave up";
        EXPECT_EQ(host.Count(Kind::ConnectionLost), 1u);
        EXPECT_TRUE(host.Find(Kind::ConnectionLost)->willRetry);
        EXPECT_EQ(host.Count(Kind::Resumed), 0u);
        ASSERT_EQ(host.Count(Kind::Closed), 1u);
        EXPECT_EQ(host.Find(Kind::Closed)->code, RoomClientCloseReasons::kGaveUp);
        EXPECT_GE(host.tap.connections, 3u) << "it should have tried again, not only once";
        EXPECT_FALSE(host.client->GetSession().IsValid());
    }

    // ---- Addresses, policies and the heartbeat ---------------------------------------------------------------------------------

    TEST_F(RoomClientSocketTest, WssIsRefusedAndTheMessageNamesTheBrowserBuild) {
        Player& player = AddPlayer();
        const auto hosted = player.client->Host("wss://127.0.0.1:9", "cards", "P0");
        ASSERT_FALSE(hosted.has_value());
        EXPECT_EQ(hosted.error().code, RoomClientErrors::kConnectFailed);
        EXPECT_EQ(std::string(hosted.error().message.c_str()).rfind(std::string(kWebSocketTlsUnsupported.data(), kWebSocketTlsUnsupported.size()), 0), 0u) << hosted.error().message.c_str();
        EXPECT_NE(std::string(hosted.error().message.c_str()).find("browser"), std::string::npos);
        EXPECT_EQ(player.client->GetState(), RoomClientState::Disconnected);
        EXPECT_EQ(player.tap.connections, 1u);
    }

    TEST_F(RoomClientSocketTest, AWrongPortGivesAClosedEventAndNoHang) {
        // A port that was in use a moment ago and now has nobody on it
        StartServer();
        const eastl::string deadUrl = url;
        server->Stop();
        server.reset();

        Player& player = AddPlayer();
        ASSERT_TRUE(player.client->Join(deadUrl, "ABCDE", "P0").has_value());
        ASSERT_TRUE(WaitFor([&] { return player.client->GetState() == RoomClientState::Disconnected; }, kShortLimit)) << "the connect never ended";
        ASSERT_EQ(player.Count(Kind::Closed), 1u);
        EXPECT_EQ(player.Find(Kind::Closed)->closeCode, kRoomTransportAbnormalClosure);
        EXPECT_FALSE(player.Find(Kind::Closed)->message.empty());
        // Without a room there is nothing to bring back
        std::this_thread::sleep_for(200ms);
        player.Pump();
        EXPECT_EQ(player.tap.connections, 1u);
    }

    TEST_F(RoomClientSocketTest, AServerThatAdmitsOnlyAWebPageStillAdmitsThisClient) {
        // A program sends no Origin, and the allow-list is for pages (the policy's own words: it keeps foreign pages out, not programs)
        RoomServerSettings settings = MakeServerSettings();
        settings.policy.loopbackOnly = false;
        settings.policy.allowedOrigins = {"https://page.example"};
        StartServer(std::move(settings));
        ASSERT_NO_FATAL_FAILURE(MakeTable("cards", 2));
        EXPECT_TRUE(players[0]->client->IsInRoom());
        EXPECT_TRUE(players[1]->client->IsInRoom());
    }

    TEST_F(RoomClientSocketTest, TheServerAnswersTheHeartbeatPingWithPong) {
        StartServer();
        RoomClientSettings quick = MakeClientSettings();
        quick.pingInterval = 40ms;
        Player& player = AddPlayer(quick);
        ASSERT_TRUE(player.client->Host(url, "cards", "P0").has_value());
        ASSERT_TRUE(WaitFor([&] { return player.tap.CountType(RoomMessageTypes::kPong.data()) >= 3; }, kShortLimit)) << "no pong came back";
        EXPECT_EQ(player.client->GetState(), RoomClientState::InRoom);
        EXPECT_EQ(player.Count(Kind::Error), 0u);
        EXPECT_EQ(player.Count(Kind::ConnectionLost), 0u);
    }

}  // namespace BECore::Tests
