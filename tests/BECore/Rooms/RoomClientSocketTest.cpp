#include <BECore/Rooms/ManualClock.h>
#include <BECore/Rooms/RoomClientPlayer.h>
#include <BECore/Rooms/RoomTableDriver.h>
#include <BECore/Rooms/RoomTestTimings.h>
#include <BECore/Rooms/TestRandom.h>
#include <BECore/WebSocketTestClient.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/SharedPtr.h>
#include <RemoteControl/Tls/TlsStream.h>
#include <RemoteControl/WebSocketClient.h>
#include <RoomClient/RoomClient.h>
#include <RoomServer/DemoCardGame.h>
#include <RoomServer/RoomWebSocketServer.h>
#include <Rooms/RoomErrors.h>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

namespace BECore::Tests {

    namespace {

        using namespace std::chrono_literals;
        using Kind = RoomClientEventKind;

        constexpr std::chrono::milliseconds kShortLimit{30'000};
        constexpr int kCardsPlayedInAHand = DemoCardGame::kTotalPlays;

        /// What the test wants to know of the transports a client made, and a hand on the connection to cut it
        struct Tap {
            class TappedTransport* current = nullptr;
            /// How many connections the client made
            size_t connections = 0;
            /// The `type` of every text the client received, in order
            Vector<String> types;
            /// While set, every new connection of the client is refused like a server that is down, so a test decides when the client can come back
            bool refuseConnections = false;

            [[nodiscard]] size_t CountType(StringView type) const {
                return static_cast<size_t>(std::count(types.begin(), types.end(), type));
            }

            /// Cuts the connection like a cable pulled: no close frame, the client sees the end by itself
            void Sever();
        };

        /// Records what a client's transport receives and lets the test abort the live connection
        class TappedTransport final : public IRoomTransport {
        public:
            TappedTransport(UniquePtr<IRoomTransport> inner, Tap& tap) : _inner(std::move(inner)), _tap(tap) {
                _tap.current = this;
                ++_tap.connections;
            }

            ~TappedTransport() override {
                if (_tap.current == this) {
                    _tap.current = nullptr;
                }
            }

            std::expected<void, String> Open(StringView url) override {
                // An error of Open would end the retries for good: a refused connect opens and then reports Closed
                _refused = _tap.refuseConnections;
                return _refused ? std::expected<void, String>{} : _inner->Open(url);
            }

            bool SendText(StringView text) override {
                return _inner->SendText(text);
            }

            void Close(uint16_t code, StringView reason) override {
                _inner->Close(code, reason);
            }

            void Abort() override {
                _inner->Abort();
            }

            bool PollEvent(RoomTransportEvent& event) override {
                if (_refused) {
                    if (_refusalReported) {
                        return false;
                    }
                    _refusalReported = true;
                    event = RoomTransportEvent{};
                    event.kind = RoomTransportEventKind::Closed;
                    event.closeCode = kRoomTransportAbnormalClosure;
                    return true;
                }
                if (!_inner->PollEvent(event)) {
                    return false;
                }
                if (event.kind == RoomTransportEventKind::Text) {
                    const auto parsed = ParseJson(event.text);
                    _tap.types.push_back(parsed ? GetJsonString(*parsed, "type").value_or(String()) : String("?"));
                }
                return true;
            }

        private:
            UniquePtr<IRoomTransport> _inner;
            Tap& _tap;
            bool _refused = false;
            bool _refusalReported = false;
        };

        void Tap::Sever() {
            if (current) {
                current->Abort();
            }
        }

        /// A RoomClientPlayer on the native transport, tapped; it takes its events and plays on every Pump() unless autoplay is off
        struct Player : RoomClientPlayer {
            explicit Player(RoomClientSettings settings = {}) {
                settings.transportFactory = [this]() -> UniquePtr<IRoomTransport> {
                    auto native = MakeDefaultRoomTransport();
                    if (!native) {
                        return nullptr;
                    }
                    return MakeUnique<TappedTransport>(MakeUnique<JudgedTransport>(std::move(native), judge), tap);
                };
                client = MakeUnique<RoomClient>(std::move(settings));
            }

            ~Player() {
                // The transports of the client use the tap
                client.reset();
            }

            void Pump() {
                client->Poll();
                TakeEvents();
                if (autoplay) {
                    // A refused send (the connection just dropped) is tried again on the next pump
                    ActIfItsTurn();
                }
            }

            Tap tap;
            bool autoplay = true;
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
            settings.hub.timings = NoComputerDelayTimings();
            settings.pingInterval = 0ms;
            settings.random = MakeSequence(5);
            return settings;
        }

        class RoomClientSocketTest : public ::testing::Test {
        protected:
            void SetUp() override {
                ASSERT_TRUE(registry.Register(SeatedGameInfo{"cards", DemoCardGame::kSeatCount}, [] { return MakeUnique<DemoCardGame>(); }));
                ASSERT_TRUE(registry.Register(SeatedGameInfo{"cards-leaky", DemoCardGame::kSeatCount}, [] { return MakeUnique<LeakyDemoCardGame>(); }));
            }

            void TearDown() override {
                // The clients go first: they hold connections to the server
                players.clear();
                if (server) {
                    server->Stop();
                }
            }

            void StartServer(RoomServerSettings settings = MakeServerSettings()) {
                server = MakeUnique<RoomWebSocketServer>(registry, std::move(settings));
                const auto started = server->Start();
                ASSERT_TRUE(started.has_value()) << (started.has_value() ? "" : started.error().message.c_str());
                port = *started;
                url = Format("ws://127.0.0.1:{}", port);
            }

            Player& AddPlayer(RoomClientSettings settings = MakeClientSettings()) {
                players.push_back(MakeUnique<Player>(std::move(settings)));
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

            /// The first player hosts a room of @p game, the others join it (each with fresh settings that @p tuneGuest may change);
            /// everybody is in the room afterwards, nobody has started
            void MakeTable(StringView game, size_t humans, const Json& options = Json::object(), FunctionRef<void(RoomClientSettings&)> tuneGuest = [](RoomClientSettings&) {}) {
                Player& host = AddPlayer();
                ASSERT_TRUE(host.client->Host(url, game, "P0", options).has_value());
                ASSERT_TRUE(WaitFor([&] { return host.client->IsInRoom(); }, kShortLimit));
                for (size_t i = 1; i < humans; ++i) {
                    RoomClientSettings guestSettings = MakeClientSettings();
                    tuneGuest(guestSettings);
                    Player& guest = AddPlayer(std::move(guestSettings));
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
                    EXPECT_EQ(player.TableSize(), kCardsPlayedInAHand) << "player " << i;
                    EXPECT_GT(player.judge->texts, 3u) << "player " << i;
                    EXPECT_TRUE(player.judge->leaks.empty()) << "player " << i << " was sent " << (player.judge->leaks.empty() ? "" : player.judge->leaks.front());
                }
            }

            SeatedGameRegistry registry;
            UniquePtr<RoomWebSocketServer> server;
            uint16_t port = 0;
            String url;
            Vector<UniquePtr<Player>> players;
        };

    }  // namespace

    // ---- Whole hands over real sockets ---------------------------------------------------------------------------------------------

    TEST_F(RoomClientSocketTest, TwoHumansAndTwoComputersPlayAHandAndNobodyPeeks) {
        ASSERT_NO_FATAL_FAILURE(StartServer());
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
        ASSERT_NO_FATAL_FAILURE(StartServer());
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
        ASSERT_NO_FATAL_FAILURE(StartServer());
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
        ASSERT_NO_FATAL_FAILURE(StartServer(std::move(settings)));
        ASSERT_NO_FATAL_FAILURE(MakeTable("cards", 2));
        Player& cut = *players[1];
        const String token = cut.client->GetSession().token;
        const int seat = cut.client->GetSeat();
        ASSERT_TRUE(players[0]->client->Start().has_value());
        ASSERT_TRUE(WaitFor([&] { return cut.TableSize() >= 1; }));
        ASSERT_FALSE(cut.client->IsFinished());

        cut.tap.Sever();
        ASSERT_TRUE(WaitFor([&] { return cut.Count(Kind::Resumed) == 1; }, kShortLimit)) << "the client did not come back";
        EXPECT_EQ(cut.Count(Kind::ConnectionLost), 1u);
        EXPECT_TRUE(Require(cut.Find(Kind::ConnectionLost)).willRetry);
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
        ASSERT_NO_FATAL_FAILURE(StartServer(std::move(settings)));

        // Seat 0 moves first, and seat 1 does not play until the test lets it: the hand stalls at seat 1 when the client is cut
        ASSERT_NO_FATAL_FAILURE(MakeTable("cards", 2, Json::object({{"firstSeat", 0}})));
        Player& host = *players[0];
        Player& away = *players[1];
        const String token = away.client->GetSession().token;
        away.autoplay = false;
        ASSERT_TRUE(host.client->Start().has_value());
        ASSERT_TRUE(WaitFor([&] { return away.client->HasView(); }));
        away.tap.refuseConnections = true;
        away.tap.Sever();

        // The host is told the guest is away
        ASSERT_TRUE(WaitFor([&] { return host.client->GetLobby().seats.size() > 1 && !host.client->GetLobby().seats[1].connected; }, kShortLimit));
        EXPECT_EQ(away.client->GetState(), RoomClientState::Reconnecting);

        // Inside the grace nobody plays the seat
        clock.Advance(20s);
        WaitFor([] { return false; }, 200ms);
        EXPECT_EQ(host.PlaysOf(1), 0);

        // Past it a computer does
        clock.Advance(15s);
        ASSERT_TRUE(WaitFor([&] { return host.PlaysOf(1) > 0; }, kShortLimit)) << "no computer took the seat after the grace";
        EXPECT_EQ(away.Count(Kind::Resumed), 0u);

        // The client returns with the same token, finds the hand moved on, and plays the rest; the view comes after `joined`
        away.tap.refuseConnections = false;
        ASSERT_TRUE(WaitFor([&] { return away.Count(Kind::Resumed) == 1; }, kShortLimit));
        ASSERT_TRUE(WaitFor([&] { return away.PlaysOf(1) >= 1; }, kShortLimit)) << "the view after the return does not show the computer's play";
        away.autoplay = true;
        EXPECT_EQ(away.client->GetSeat(), 1);
        EXPECT_EQ(away.client->GetSession().token, token);
        ASSERT_TRUE(WaitFor([&] { return EveryoneSeesTheEnd(); }));
        ExpectACleanFinishedHand();
    }

    TEST_F(RoomClientSocketTest, ANewClientObjectResumesTheSavedSessionAfterTheAppRestarted) {
        RoomServerSettings settings = MakeServerSettings();
        settings.hub.timings.computerDelay = 100ms;
        ASSERT_NO_FATAL_FAILURE(StartServer(std::move(settings)));
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
        ASSERT_NO_FATAL_FAILURE(StartServer());
        RoomClientSettings impatient = MakeClientSettings();
        impatient.reconnectGiveUp = 700ms;
        impatient.backoffStart = 50ms;
        impatient.backoffMax = 100ms;
        Player& host = AddPlayer(std::move(impatient));
        ASSERT_TRUE(host.client->Host(url, "cards", "P0").has_value());
        ASSERT_TRUE(WaitFor([&] { return host.client->IsInRoom(); }, kShortLimit));

        server->Stop();
        ASSERT_TRUE(WaitFor([&] { return host.client->GetState() == RoomClientState::Disconnected; }, kShortLimit)) << "the client never gave up";
        EXPECT_EQ(host.Count(Kind::ConnectionLost), 1u);
        EXPECT_TRUE(Require(host.Find(Kind::ConnectionLost)).willRetry);
        EXPECT_EQ(host.Count(Kind::Resumed), 0u);
        ASSERT_EQ(host.Count(Kind::Closed), 1u);
        EXPECT_EQ(Require(host.Find(Kind::Closed)).code, RoomClientCloseReasons::kGaveUp);
        // The first connection and at least one retry after the stop. How many more fit into the give-up time is the platform's: Windows
        // answers a connect to a closed loopback port after about 2 s of its own retries, where macOS and Linux refuse at once
        EXPECT_GE(host.tap.connections, 2u) << "it should have tried again after the server stopped";
        EXPECT_FALSE(host.client->GetSession().IsValid());
    }

    // ---- Addresses, policies and the heartbeat ---------------------------------------------------------------------------------

    TEST_F(RoomClientSocketTest, WssIsRefusedAtOnceWithoutATlsBackendAndTheMessageNamesTheBrowserBuild) {
        if (IsTlsSupported()) {
            GTEST_SKIP() << "this build has a TLS backend: WssToAPlainServerEndsWithATlsFailureNotAHang covers wss://";
        }
        Player& player = AddPlayer();
        const auto hosted = player.client->Host("wss://127.0.0.1:9", "cards", "P0");
        ASSERT_FALSE(hosted.has_value());
        EXPECT_EQ(hosted.error().code, RoomClientErrors::kConnectFailed);
        EXPECT_EQ(hosted.error().message.rfind(String(kWebSocketTlsUnsupported.data(), kWebSocketTlsUnsupported.size()), 0), 0u) << hosted.error().message.c_str();
        EXPECT_NE(hosted.error().message.find("browser"), String::npos);
        EXPECT_EQ(player.client->GetState(), RoomClientState::Disconnected);
        EXPECT_EQ(player.tap.connections, 1u);
    }

    TEST_F(RoomClientSocketTest, WssToAPlainServerEndsWithATlsFailureNotAHang) {
        if (!IsTlsSupported()) {
            GTEST_SKIP() << "no TLS backend in this build: WssIsRefusedAtOnceWithoutATlsBackendAndTheMessageNamesTheBrowserBuild covers wss://";
        }
        ASSERT_NO_FATAL_FAILURE(StartServer());
        Player& player = AddPlayer();
        // The address is accepted (there is a backend); the plain server does not speak TLS, so the handshake fails
        ASSERT_TRUE(player.client->Host(Format("wss://127.0.0.1:{}", port), "cards", "P0").has_value());
        ASSERT_TRUE(WaitFor([&] { return player.client->GetState() == RoomClientState::Disconnected; }, kShortLimit)) << "the TLS handshake never ended";
        ASSERT_EQ(player.Count(Kind::Closed), 1u);
        EXPECT_EQ(Require(player.Find(Kind::Closed)).closeCode, kRoomTransportAbnormalClosure);
        EXPECT_NE(Require(player.Find(Kind::Closed)).message.find("TLS"), String::npos) << Require(player.Find(Kind::Closed)).message.c_str();
    }

    TEST_F(RoomClientSocketTest, AWrongPortGivesAClosedEventAndNoHang) {
        // A port that was in use a moment ago and now has nobody on it: another test that binds the freed port before the connect would make this pass for the wrong reason; that clash is rare enough
        // to accept
        ASSERT_NO_FATAL_FAILURE(StartServer());
        const String deadUrl = url;
        server->Stop();
        server.reset();

        Player& player = AddPlayer();
        ASSERT_TRUE(player.client->Join(deadUrl, "ABCDE", "P0").has_value());
        ASSERT_TRUE(WaitFor([&] { return player.client->GetState() == RoomClientState::Disconnected; }, kShortLimit)) << "the connect never ended";
        ASSERT_EQ(player.Count(Kind::Closed), 1u);
        EXPECT_EQ(Require(player.Find(Kind::Closed)).closeCode, kRoomTransportAbnormalClosure);
        EXPECT_FALSE(Require(player.Find(Kind::Closed)).message.empty());
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
        ASSERT_NO_FATAL_FAILURE(StartServer(std::move(settings)));
        ASSERT_NO_FATAL_FAILURE(MakeTable("cards", 2));
        EXPECT_TRUE(players[0]->client->IsInRoom());
        EXPECT_TRUE(players[1]->client->IsInRoom());
    }

    TEST_F(RoomClientSocketTest, TheServerAnswersTheHeartbeatPingWithPong) {
        ASSERT_NO_FATAL_FAILURE(StartServer());
        RoomClientSettings quick = MakeClientSettings();
        quick.pingInterval = 40ms;
        Player& player = AddPlayer(std::move(quick));
        ASSERT_TRUE(player.client->Host(url, "cards", "P0").has_value());
        ASSERT_TRUE(WaitFor([&] { return player.tap.CountType(RoomMessageTypes::kPong.data()) >= 3; }, kShortLimit)) << "no pong came back";
        EXPECT_EQ(player.client->GetState(), RoomClientState::InRoom);
        EXPECT_EQ(player.Count(Kind::Error), 0u);
        EXPECT_EQ(player.Count(Kind::ConnectionLost), 0u);
    }

}  // namespace BECore::Tests
