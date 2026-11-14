#include "ManualClock.h"
#include "RoomTableDriver.h"
#include "TestRandom.h"

#include <RoomServer/DemoCardGame.h>
#include <RoomServer/RoomWebSocketServer.h>
#include <Rooms/RoomErrors.h>
#include <gtest/gtest.h>
#include <memory>
#include <set>
#include <thread>

namespace BECore::Tests {

    namespace {

        using namespace std::chrono_literals;

        constexpr std::chrono::milliseconds kPause{300};
        constexpr std::chrono::milliseconds kReconnectGrace{30'000};

        RoomServerSettings MakeSettings() {
            RoomServerSettings settings;
            settings.tickInterval = 10ms;
            settings.hub.timings = RoomTimings{kReconnectGrace, 10min, 0ms};
            // No liveness pings: a test that moves the clock by half a minute would see its idle clients pinged and dropped
            settings.pingInterval = 0ms;
            settings.random = MakeSequence(7);
            return settings;
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
        const String code = guest.roomCode;
        const String token = guest.token;
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

    TEST_F(RoomServerScenarioTest, AClientListsPublicRoomsJoinsFromTheListAndNeverSeesAPrivateCode) {
        const uint16_t port = StartServer();
        auto open = RoomSocketClient::Connect(port);
        auto closed = RoomSocketClient::Connect(port);
        ASSERT_TRUE(open.has_value() && closed.has_value());
        open->Send(CreateRoomMessage{"cards", "OpenHost", Json::object(), true});
        closed->Send(CreateRoomMessage{"cards", "ClosedHost", Json::object()});
        ASSERT_TRUE(open->WaitFor(RoomMessageTypes::kLobby).has_value());
        ASSERT_TRUE(closed->WaitFor(RoomMessageTypes::kLobby).has_value());

        auto browser = RoomSocketClient::Connect(port);
        ASSERT_TRUE(browser.has_value());
        browser->Send(ListRoomsMessage{"no-such-game"});
        const auto unknown = browser->WaitFor(RoomMessageTypes::kError);
        ASSERT_TRUE(unknown.has_value());
        EXPECT_EQ(unknown->GetString("code"), RoomErrors::kUnknownGame);
        EXPECT_EQ(unknown->GetString("ref"), RoomMessageTypes::kListRooms);

        browser->Send(ListRoomsMessage{"cards"});
        const auto listed = browser->WaitFor(RoomMessageTypes::kRooms);
        ASSERT_TRUE(listed.has_value());
        const auto list = ParseServerMessage(listed->text, RoomLimits::kMaxServerMessageBytes);
        ASSERT_TRUE(list.has_value());
        const auto* rooms = std::get_if<RoomListMessage>(&*list);
        ASSERT_NE(rooms, nullptr);
        ASSERT_EQ(rooms->rooms.size(), 1u);
        EXPECT_EQ(rooms->rooms[0].room, open->roomCode);
        EXPECT_EQ(rooms->rooms[0].host, "OpenHost");
        EXPECT_EQ(rooms->rooms[0].taken, 1);
        EXPECT_EQ(rooms->rooms[0].seats, DemoCardGame::kSeatCount);
        EXPECT_FALSE(rooms->rooms[0].started);
        EXPECT_FALSE(rooms->more);

        // Join from the list is the join by code; the hand is played to the end
        browser->JoinByName(rooms->rooms[0].room, "Browser");
        ASSERT_TRUE(browser->WaitFor(RoomMessageTypes::kLobby).has_value());
        open->Start();
        Clients playing;
        playing.push_back(std::make_unique<RoomSocketClient>(std::move(*open)));
        playing.push_back(std::make_unique<RoomSocketClient>(std::move(*browser)));
        ASSERT_TRUE(PlayToTheEnd(playing));
        for (const auto& client : playing) {
            ExpectAllTwelveCardsPlayed(*client);
            for (const ReceivedMessage& message : client->Received()) {
                EXPECT_EQ(message.text.find(closed->roomCode), String::npos) << "the code of a private room reached another room: " << message.text.c_str();
            }
        }
        ExpectCleanWire(playing);
    }

}  // namespace BECore::Tests
