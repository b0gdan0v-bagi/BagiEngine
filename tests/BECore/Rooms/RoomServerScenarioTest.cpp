#include "ManualClock.h"
#include "RoomSocketClient.h"
#include "TestRandom.h"
#include "WireJudge.h"

#include <RoomServer/DemoCardGame.h>
#include <RoomServer/RoomWebSocketServer.h>
#include <Rooms/RoomErrors.h>
#include <algorithm>
#include <gtest/gtest.h>
#include <memory>
#include <set>
#include <thread>

namespace BECore::Tests {

    namespace {

        using namespace std::chrono_literals;

        constexpr std::chrono::milliseconds kPlayLimit{30'000};
        constexpr std::chrono::milliseconds kPause{300};
        constexpr std::chrono::milliseconds kReconnectGrace{30'000};
        constexpr int kHumansAtFullTable = 4;
        constexpr int kHumansOfAPair = 2;

        using Clients = std::vector<std::unique_ptr<RoomSocketClient>>;

        RoomServerSettings MakeSettings() {
            RoomServerSettings settings;
            settings.tickInterval = 10ms;
            settings.hub.timings = RoomTimings{kReconnectGrace, 10min, 0ms};
            // No liveness pings: a test that moves the clock by half a minute would see its idle clients pinged and dropped
            settings.pingInterval = 0ms;
            settings.random = MakeSequence(7);
            return settings;
        }

        /// The last message of @p type the client received, or null
        const ReceivedMessage* LastOf(const RoomSocketClient& client, eastl::string_view type) {
            const auto& received = client.Received();
            for (size_t i = received.size(); i > 0; --i) {
                if (received[i - 1].type == type) {
                    return &received[i - 1];
                }
            }
            return nullptr;
        }

        size_t CountOf(const RoomSocketClient& client, eastl::string_view type) {
            return static_cast<size_t>(std::count_if(client.Received().begin(), client.Received().end(), [&](const ReceivedMessage& message) { return message.type == type; }));
        }

        /// The `view.table` of the last view of the client; null when it has none
        const Json* LastTable(const RoomSocketClient& client) {
            const ReceivedMessage* view = LastOf(client, RoomMessageTypes::kView);
            const Json* inner = view ? FindJsonMember(view->json, "view") : nullptr;
            const Json* table = inner ? FindJsonMember(*inner, "table") : nullptr;
            return table && table->is_array() ? table : nullptr;
        }

        int TableSize(const RoomSocketClient& client) {
            const Json* table = LastTable(client);
            return table ? static_cast<int>(table->size()) : -1;
        }

        bool HasActions(const RoomSocketClient& client) {
            const ReceivedMessage* view = LastOf(client, RoomMessageTypes::kView);
            const Json* actions = view ? FindJsonMember(view->json, "actions") : nullptr;
            return actions && actions->is_array() && !actions->empty();
        }

        bool SeesTheEnd(const RoomSocketClient& client) {
            const ReceivedMessage* view = LastOf(client, RoomMessageTypes::kView);
            return view && GetJsonBool(view->json, "finished").value_or(false);
        }

        /// Reads whatever has arrived
        void Drain(RoomSocketClient& client) {
            while (client.Next(1ms)) {
            }
        }

        /// A human at the table: reads what the server sends and, on its turn, plays the first action of its last view
        class HandPlayer {
        public:
            explicit HandPlayer(RoomSocketClient& client) : _client(client) {}

            void Pump() {
                Drain(_client);
                ActIfItsTurn();
            }

            void ActIfItsTurn() {
                const ReceivedMessage* view = LastOf(_client, RoomMessageTypes::kView);
                if (!view || !HasActions(_client)) {
                    return;
                }
                const auto version = static_cast<uint64_t>(view->GetInt("version", 0));
                if (_acted && _actedVersion == version) {
                    return;
                }
                ActionMessage action;
                action.version = version;
                action.action = (*FindJsonMember(view->json, "actions"))[0];
                _client.Send(action);
                _acted = true;
                _actedVersion = version;
            }

        private:
            RoomSocketClient& _client;
            bool _acted = false;
            uint64_t _actedVersion = 0;
        };

        /// Plays the hands of @p clients until each has seen the end of its game; @p alsoPump runs on every round (other sockets to serve)
        bool PlayToTheEnd(const Clients& clients, const WaitPump& alsoPump = {}) {
            std::vector<HandPlayer> players;
            for (const auto& client : clients) {
                players.emplace_back(*client);
            }
            return WaitUntil(
                [&] {
                    for (HandPlayer& player : players) {
                        player.Pump();
                    }
                    if (alsoPump) {
                        alsoPump();
                    }
                },
                [&] { return std::all_of(clients.begin(), clients.end(), [](const auto& client) { return SeesTheEnd(*client); }); }, kPlayLimit);
        }

        /// What the judge says of every text the client received: the ids of the cards it must not have seen
        std::vector<std::string> LeaksOf(const RoomSocketClient& client) {
            std::vector<std::string> leaks;
            for (const ReceivedMessage& message : client.Received()) {
                for (std::string& id : FindLeakedCardIds(std::string_view(message.text.c_str(), message.text.size()), client.seat)) {
                    leaks.push_back(std::move(id));
                }
            }
            return leaks;
        }

        /// The server sent only unmasked text frames (a binary frame, or a masked one, is a failure of its own), and the judge finds no leak
        void ExpectCleanWire(const RoomSocketClient& client, const char* who) {
            EXPECT_EQ(client.GetMaskedFrameCount(), 0u) << who;
            EXPECT_EQ(client.GetBinaryFrameCount(), 0u) << who;
            const auto leaks = LeaksOf(client);
            EXPECT_TRUE(leaks.empty()) << who << " was sent the hidden card " << (leaks.empty() ? "" : leaks.front());
        }

        void ExpectCleanWire(const Clients& clients) {
            for (size_t i = 0; i < clients.size(); ++i) {
                ExpectCleanWire(*clients[i], ("client " + std::to_string(i)).c_str());
            }
        }

        /// The end of a hand: the last view of the client is `finished` and shows every one of the twelve played cards once
        void ExpectAllTwelveCardsPlayed(const RoomSocketClient& client) {
            ASSERT_TRUE(SeesTheEnd(client));
            const Json* table = LastTable(client);
            ASSERT_NE(table, nullptr);
            const std::vector<std::string> ids = FindCardIds(table->dump());
            EXPECT_EQ(ids.size(), static_cast<size_t>(DemoCardGame::kTotalPlays));
            EXPECT_EQ(std::set<std::string>(ids.begin(), ids.end()).size(), static_cast<size_t>(DemoCardGame::kTotalPlays));
        }

        /// Every message of the client that names a room names its own
        void ExpectOwnRoomOnly(const RoomSocketClient& client) {
            for (const ReceivedMessage& message : client.Received()) {
                if (message.type == RoomMessageTypes::kView || message.type == RoomMessageTypes::kLobby || message.type == RoomMessageTypes::kJoined) {
                    EXPECT_EQ(message.GetString("room"), client.roomCode);
                }
            }
        }

        bool HasAwayHuman(const ReceivedMessage& lobby) {
            const Json* seats = FindJsonMember(lobby.json, "seats");
            if (!seats || !seats->is_array()) {
                return false;
            }
            for (const Json& seat : *seats) {
                const Json* connected = FindJsonMember(seat, "connected");
                if (GetJsonString(seat, "kind").value_or("") == "human" && connected && connected->is_boolean() && !connected->get<bool>()) {
                    return true;
                }
            }
            return false;
        }

        /// Frames of @p count identical text messages in one buffer
        eastl::vector<uint8_t> RepeatText(const std::string& text, int count) {
            const auto frame = EncodeTextFrame(eastl::string_view(text.data(), text.size()), kClientMask);
            eastl::vector<uint8_t> batch;
            for (int i = 0; i < count; ++i) {
                batch.insert(batch.end(), frame.data(), frame.data() + frame.size());
            }
            return batch;
        }

        /// Reads frames until the server's close frame; nullopt when none comes
        std::optional<ServerFrame> ReadUntilClose(WebSocketTestClient& client) {
            for (;;) {
                const auto frame = client.WaitForFrame();
                if (!frame) {
                    return std::nullopt;
                }
                if (frame->opcode == WebSocketOpcode::kClose) {
                    return frame;
                }
            }
        }

        class RoomServerScenarioTest : public ::testing::Test {
        protected:
            void SetUp() override {
                ASSERT_TRUE(registry.Register(SeatedGameInfo{"cards", DemoCardGame::kSeatCount}, [] { return eastl::make_unique<DemoCardGame>(); }));
                ASSERT_TRUE(registry.Register(SeatedGameInfo{"cards-leaky", DemoCardGame::kSeatCount}, [] { return eastl::make_unique<LeakyDemoCardGame>(); }));
            }

            void TearDown() override {
                if (server) {
                    server->Stop();
                }
            }

            uint16_t StartServer(RoomServerSettings settings = MakeSettings()) {
                server = std::make_unique<RoomWebSocketServer>(registry, std::move(settings));
                const auto port = server->Start();
                EXPECT_TRUE(port.has_value()) << (port.has_value() ? "" : port.error().message.c_str());
                return port.value_or(0);
            }

            /// A room of @p game with @p humans humans, each on its own socket and each with `joined` and `lobby` read; the first is the host.
            /// @p options go to the game in `create_room`. Not started
            void OpenTable(uint16_t port, eastl::string_view game, int humans, Clients& out, const Json& options = Json::object()) {
                for (int i = 0; i < humans; ++i) {
                    auto client = RoomSocketClient::Connect(port);
                    ASSERT_TRUE(client.has_value());
                    const eastl::string name = Format("Player{}", i);
                    if (i == 0) {
                        client->Send(CreateRoomMessage{eastl::string(game), name, options});
                    } else {
                        client->JoinByName(out.front()->roomCode, name);
                    }
                    ASSERT_TRUE(client->WaitFor(RoomMessageTypes::kJoined).has_value());
                    ASSERT_TRUE(client->WaitFor(RoomMessageTypes::kLobby).has_value());
                    out.push_back(std::make_unique<RoomSocketClient>(std::move(*client)));
                }
            }

            SeatedGameRegistry registry;
            std::unique_ptr<RoomWebSocketServer> server;
        };

    }  // namespace

    // ---- the judge itself --------------------------------------------------------------------------------------------------------

    TEST_F(RoomServerScenarioTest, TheJudgeFindsWholeCardIdsOnly) {
        EXPECT_EQ(FindCardIds("[\"c1_2\",\"c3_0\"]"), (std::vector<std::string>{"c1_2", "c3_0"}));
        EXPECT_TRUE(FindCardIds("c1_20 xc1_2 c1_2x c12 c_1 C1_2").empty());
        EXPECT_EQ(FindCardIds("c1_2"), (std::vector<std::string>{"c1_2"}));
        EXPECT_TRUE(FindCardIds("c1_").empty());
    }

    TEST_F(RoomServerScenarioTest, TheJudgeLetsASeatKnowItsHandAndTheTableAndNothingElse) {
        const std::string honest = R"({"type":"view","room":"ABCD","seat":0,"view":{"hand":["c0_1","c0_2"],"table":[{"seat":2,"card":"c2_0"}]},"actions":[{"type":"play","card":"c0_1"}]})";
        EXPECT_TRUE(FindLeakedCardIds(honest, 0).empty());
        // The same text for another seat: its hand is somebody else's
        EXPECT_EQ(FindLeakedCardIds(honest, 1), (std::vector<std::string>{"c0_1", "c0_2", "c0_1"}));

        const std::string leaky = R"({"type":"view","seat":0,"view":{"hand":["c0_1"],"table":[],"allHands":[["c0_1"],["c1_0"],["c2_2"]]}})";
        EXPECT_EQ(FindLeakedCardIds(leaky, 0), (std::vector<std::string>{"c1_0", "c2_2"}));

        // Not JSON, or a client with no seat: only what is on the table (nothing) is allowed
        EXPECT_EQ(FindLeakedCardIds("the card c0_1", 0), std::vector<std::string>{});
        EXPECT_EQ(FindLeakedCardIds("the card c0_1", -1), (std::vector<std::string>{"c0_1"}));
        // A card on the table of another message is not on the table of this one
        EXPECT_EQ(FindLeakedCardIds(R"({"type":"error","message":"c2_0"})", 0), (std::vector<std::string>{"c2_0"}));
    }

    // ---- the scenarios over real sockets -----------------------------------------------------------------------------------------

    TEST_F(RoomServerScenarioTest, TwoHumansAndTwoComputersPlayAHandAndNobodyPeeks) {
        const uint16_t port = StartServer();
        Clients clients;
        ASSERT_NO_FATAL_FAILURE(OpenTable(port, "cards", kHumansOfAPair, clients));
        clients[0]->Start();
        ASSERT_TRUE(PlayToTheEnd(clients)) << "the hand did not reach its end";

        for (const auto& client : clients) {
            ExpectAllTwelveCardsPlayed(*client);
            EXPECT_EQ(CountOf(*client, RoomMessageTypes::kError), 0u);
        }
        ExpectCleanWire(clients);
    }

    TEST_F(RoomServerScenarioTest, FourHumansPlayAWholeHand) {
        const uint16_t port = StartServer();
        Clients clients;
        ASSERT_NO_FATAL_FAILURE(OpenTable(port, "cards", kHumansAtFullTable, clients));
        std::set<int> seats;
        for (const auto& client : clients) {
            seats.insert(client->seat);
        }
        EXPECT_EQ(seats.size(), static_cast<size_t>(kHumansAtFullTable));

        clients[0]->Start();
        ASSERT_TRUE(PlayToTheEnd(clients));
        for (const auto& client : clients) {
            ExpectAllTwelveCardsPlayed(*client);
            EXPECT_EQ(CountOf(*client, RoomMessageTypes::kError), 0u);
        }
        ExpectCleanWire(clients);
    }

    TEST_F(RoomServerScenarioTest, TheJudgeOfTheWireCatchesALeakyGame) {
        // The negative control: the same hand on a game whose view carries every hand. If the judge did not fail here, a pass above would mean nothing
        const uint16_t port = StartServer();
        Clients clients;
        ASSERT_NO_FATAL_FAILURE(OpenTable(port, "cards-leaky", kHumansOfAPair, clients));
        clients[0]->Start();
        ASSERT_TRUE(PlayToTheEnd(clients));

        for (const auto& client : clients) {
            EXPECT_FALSE(LeaksOf(*client).empty()) << "the judge did not notice that every hand was on the wire";
            EXPECT_EQ(client->GetMaskedFrameCount(), 0u);
            EXPECT_EQ(client->GetBinaryFrameCount(), 0u);
        }
    }

    TEST_F(RoomServerScenarioTest, AHumanDropsMidHandAndReturnsWithTheToken) {
        ManualClock clock;
        RoomServerSettings settings = MakeSettings();
        settings.clock = clock.AsFunction();
        const uint16_t port = StartServer(std::move(settings));

        // Seat 1 starts, so the guest plays first, the others follow, and the host plays before the guest's second turn
        constexpr int kGuestSeat = 1;
        constexpr int kTableAtTheDrop = 4;
        Json options = Json::object();
        options["firstSeat"] = kGuestSeat;
        Clients clients;
        ASSERT_NO_FATAL_FAILURE(OpenTable(port, "cards", kHumansOfAPair, clients, options));
        RoomSocketClient& host = *clients[0];
        RoomSocketClient& guest = *clients[1];
        ASSERT_EQ(guest.seat, kGuestSeat);
        const eastl::string code = guest.roomCode;
        const eastl::string token = guest.token;
        host.Start();

        {
            HandPlayer hostPlayer(host);
            HandPlayer guestPlayer(guest);
            ASSERT_TRUE(WaitUntil(
                [&] {
                    hostPlayer.Pump();
                    Drain(guest);
                    if (TableSize(guest) < kTableAtTheDrop) {
                        guestPlayer.ActIfItsTurn();
                    }
                },
                [&] { return TableSize(guest) == kTableAtTheDrop && HasActions(guest); }, kPlayLimit));
        }

        // The guest's socket is cut on its turn
        guest.Raw().Drop();
        for (;;) {
            const auto lobby = host.WaitFor(RoomMessageTypes::kLobby);
            ASSERT_TRUE(lobby.has_value()) << "the host was never told that the guest is away";
            if (HasAwayHuman(*lobby)) {
                break;
            }
        }
        Drain(host);
        const size_t viewsBefore = CountOf(host, RoomMessageTypes::kView);

        // Inside the grace nobody plays for the seat
        for (int step = 0; step < 4; ++step) {
            clock.Advance(5s);
            std::this_thread::sleep_for(kPause / 3);
        }
        std::this_thread::sleep_for(kPause);
        Drain(host);
        EXPECT_EQ(CountOf(host, RoomMessageTypes::kView), viewsBefore) << "something was played while the seat was inside its grace";
        EXPECT_EQ(TableSize(host), kTableAtTheDrop);

        // Past it a computer plays the seat
        clock.Advance(15s);
        ASSERT_TRUE(WaitUntil([&] { Drain(host); }, [&] { return TableSize(host) > kTableAtTheDrop; })) << "no computer took the seat after the grace";

        // The human comes back on a new socket with its token: joined, lobby and the current view, in this order
        auto back = RoomSocketClient::Connect(port);
        ASSERT_TRUE(back.has_value());
        back->JoinByToken(code, token);
        ASSERT_TRUE(back->WaitFor(RoomMessageTypes::kView).has_value());
        ASSERT_GE(back->Received().size(), 3u);
        EXPECT_EQ(back->Received()[0].type, RoomMessageTypes::kJoined);
        EXPECT_EQ(back->Received()[1].type, RoomMessageTypes::kLobby);
        EXPECT_EQ(back->Received()[2].type, RoomMessageTypes::kView);
        EXPECT_EQ(back->seat, kGuestSeat);
        EXPECT_GT(TableSize(*back), kTableAtTheDrop);

        // A second socket with the token of a live one replaces it: the first is closed with `replaced`, the second takes the seat
        auto replacement = RoomSocketClient::Connect(port);
        ASSERT_TRUE(replacement.has_value());
        replacement->JoinByToken(code, token);
        const auto closed = back->WaitForClose();
        ASSERT_TRUE(closed.has_value());
        EXPECT_EQ(CloseCodeOf(*closed), WebSocketCloseCode::kNormal);
        EXPECT_EQ(CloseReasonOf(*closed), "replaced");
        ASSERT_TRUE(replacement->WaitFor(RoomMessageTypes::kView).has_value());
        EXPECT_EQ(replacement->seat, kGuestSeat);

        // Both humans play on to the end
        Clients playing;
        playing.push_back(std::move(clients[0]));
        playing.push_back(std::make_unique<RoomSocketClient>(std::move(*replacement)));
        ASSERT_TRUE(PlayToTheEnd(playing)) << "the hand did not end after the return";
        ExpectAllTwelveCardsPlayed(*playing[0]);
        ExpectAllTwelveCardsPlayed(*playing[1]);
        ExpectCleanWire(*back, "the replaced socket");
        ExpectCleanWire(guest, "the guest before the drop");
        ExpectCleanWire(playing);

        // After `leave` the token is no longer good
        playing[1]->Leave();
        ASSERT_TRUE(playing[1]->WaitFor(RoomMessageTypes::kLeft).has_value());
        auto late = RoomSocketClient::Connect(port);
        ASSERT_TRUE(late.has_value());
        late->JoinByToken(code, token);
        const auto refused = late->WaitFor(RoomMessageTypes::kError);
        ASSERT_TRUE(refused.has_value());
        EXPECT_EQ(refused->GetString("code"), RoomErrors::kBadToken);
    }

    TEST_F(RoomServerScenarioTest, TwoRoomsAtOnceNeverSeeEachOthersCards) {
        const uint16_t port = StartServer();
        Clients roomA;
        Clients roomB;
        ASSERT_NO_FATAL_FAILURE(OpenTable(port, "cards", kHumansOfAPair, roomA));
        ASSERT_NO_FATAL_FAILURE(OpenTable(port, "cards", kHumansOfAPair, roomB));
        ASSERT_NE(roomA[0]->roomCode, roomB[0]->roomCode);
        roomA[0]->Start();
        roomB[0]->Start();

        Clients all;
        for (auto* room : {&roomA, &roomB}) {
            for (auto& client : *room) {
                all.push_back(std::move(client));
            }
        }
        ASSERT_TRUE(PlayToTheEnd(all));
        for (const auto& client : all) {
            ExpectAllTwelveCardsPlayed(*client);
            ExpectOwnRoomOnly(*client);
        }
        ExpectCleanWire(all);
    }

    TEST_F(RoomServerScenarioTest, AHostileClientIsClosedAndAnotherRoomsHandFinishes) {
        const uint16_t port = StartServer();
        Clients table;
        ASSERT_NO_FATAL_FAILURE(OpenTable(port, "cards", kHumansAtFullTable, table));
        table[0]->Start();

        // The good hand is played from every wait of the hostile clients, so it is under way the whole time
        std::vector<HandPlayer> players;
        for (const auto& client : table) {
            players.emplace_back(*client);
        }
        const WaitPump pumpTheHand = [&] {
            for (HandPlayer& player : players) {
                player.Pump();
            }
        };

        // A handshake that never ends: held until the server's handshake timeout cuts it
        auto halfOpen = WebSocketTestClient::Open(port, pumpTheHand);
        ASSERT_TRUE(halfOpen.has_value());
        halfOpen->Send(std::string("GET /control HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\n"));

        // Bytes that are not a frame: a protocol error
        {
            auto garbage = WebSocketTestClient::Open(port, pumpTheHand);
            ASSERT_TRUE(garbage.has_value());
            ASSERT_TRUE(garbage->Handshake(port));
            garbage->Send(AsBytes(std::string("\xFF\xFF\xFF\xFF\xFF\xFF", 6)));
            const auto close = ReadUntilClose(*garbage);
            ASSERT_TRUE(close.has_value());
            EXPECT_EQ(CloseCodeOf(*close), WebSocketCloseCode::kProtocolError);
            EXPECT_TRUE(garbage->WaitForEndOfStream());
        }

        // Frames that are fine and text that is not: an error each, and the connection stays
        {
            auto junk = WebSocketTestClient::Open(port, pumpTheHand);
            ASSERT_TRUE(junk.has_value());
            ASSERT_TRUE(junk->Handshake(port));
            for (const char* text : {"garbage", "{\"v\":1}", "[1,2,3]"}) {
                junk->SendText(text);
                const auto reply = junk->WaitForFrame();
                ASSERT_TRUE(reply.has_value()) << text;
                EXPECT_EQ(reply->opcode, WebSocketOpcode::kText);
                EXPECT_NE(reply->payload.find("\"error\""), std::string::npos) << reply->payload;
            }
        }

        // A message over the limit: closed at its header
        {
            auto big = WebSocketTestClient::Open(port, pumpTheHand);
            ASSERT_TRUE(big.has_value());
            ASSERT_TRUE(big->Handshake(port));
            const uint64_t length = RoomLimits::kMaxMessageBytes + 1;
            eastl::vector<uint8_t> header = {0x81, 0xFE, static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(length)};
            header.insert(header.end(), kClientMask.data(), kClientMask.data() + kClientMask.size());
            big->Send(AsBytes(header));
            const auto close = ReadUntilClose(*big);
            ASSERT_TRUE(close.has_value());
            EXPECT_EQ(CloseCodeOf(*close), WebSocketCloseCode::kMessageTooBig);
        }

        // A flood: rate limited, then closed
        {
            auto flood = WebSocketTestClient::Open(port, pumpTheHand);
            ASSERT_TRUE(flood.has_value());
            ASSERT_TRUE(flood->Handshake(port));
            (void)flood->TrySend(AsBytes(RepeatText("{\"v\":1,\"type\":\"ping\"}", 300)));
            const auto close = ReadUntilClose(*flood);
            ASSERT_TRUE(close.has_value());
            EXPECT_EQ(CloseCodeOf(*close), WebSocketCloseCode::kPolicyViolation);
        }

        // The hand of the good table finishes, untouched
        ASSERT_TRUE(PlayToTheEnd(table)) << "the hand of the other room did not finish";
        for (const auto& client : table) {
            ExpectAllTwelveCardsPlayed(*client);
            EXPECT_EQ(CountOf(*client, RoomMessageTypes::kError), 0u);
        }
        ExpectCleanWire(table);

        // And the server ended the half-open handshake by itself
        EXPECT_TRUE(halfOpen->WaitForEndOfStream());
    }

}  // namespace BECore::Tests
