#include "ManualClock.h"
#include "RoomTableDriver.h"
#include "TestRandom.h"

#include <RoomServer/DemoCardGame.h>
#include <RoomServer/RoomWebSocketServer.h>
#include <Rooms/RoomErrors.h>
#include <gtest/gtest.h>
#include <memory>

namespace BECore::Tests {

    /// A room server over real sockets against clients that write the bytes themselves (WebSocketTestClient). The table of the `cards` game that plays
    /// beside the attacks is the thing to protect: it must finish its hand with no error and no leak, and every text it received goes through the wire judge.

    namespace {

        using namespace std::chrono_literals;

        constexpr size_t kLimit = RoomLimits::kMaxMessageBytes;
        constexpr size_t kFragmentBytes = 6000;
        constexpr int kSilentSockets = 40;
        constexpr int kFloodSockets = 20;
        constexpr int kFloodRounds = 15;
        constexpr int kGuessers = 6;
        constexpr int kGuessesPerSocket = 5;
        constexpr int kAddressBudget = 8;

        RoomServerSettings MakeSettings() {
            RoomServerSettings settings;
            settings.tickInterval = 10ms;
            settings.hub.timings = RoomTimings{30s, 10min, 0ms};
            // A client that only writes bytes would be pinged and dropped; the liveness rule has its own tests
            settings.pingInterval = 0ms;
            settings.random = MakeSequence(7);
            return settings;
        }

        std::string PingText() {
            return R"({"v":1,"type":"ping"})";
        }

        /// A ping padded with blanks inside the braces to @p bytes: still one valid message, as long as the pieces say so
        std::string PaddedPing(size_t bytes) {
            std::string text = R"({"v":1,"type":"ping")";
            text.append(bytes - text.size() - 1, ' ');
            text += '}';
            return text;
        }

        eastl::vector<uint8_t> Fragment(uint8_t opcode, const std::string& payload, bool fin) {
            return EncodeFrame(opcode, eastl::span<const uint8_t>(reinterpret_cast<const uint8_t*>(payload.data()), payload.size()), kClientMask, fin);
        }

        class RoomServerHostileTest : public ::testing::Test {
        protected:
            void SetUp() override {
                ASSERT_TRUE(registry.Register(SeatedGameInfo{"cards", DemoCardGame::kSeatCount}, [] { return eastl::make_unique<DemoCardGame>(); }));
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

            /// The pump of the waits of an attack: the good table plays from them, so its hand is under way the whole time
            WaitPump PumpTheTable(Clients& table) {
                players.clear();
                for (const auto& client : table) {
                    players.emplace_back(*client);
                }
                return [this] {
                    for (HandPlayer& player : players) {
                        player.Pump();
                    }
                };
            }

            /// The table finishes its hand untouched, nobody was told of an error and no card is on a wire it must not be on
            void ExpectTheTableFinishes(Clients& table) {
                ASSERT_TRUE(PlayToTheEnd(table)) << "the hand of the good table did not finish";
                for (const auto& client : table) {
                    ExpectAllTwelveCardsPlayed(*client);
                    EXPECT_EQ(CountOf(*client, RoomMessageTypes::kError), 0u);
                }
                ExpectCleanWire(table);
            }

            SeatedGameRegistry registry;
            std::unique_ptr<RoomWebSocketServer> server;
            std::vector<HandPlayer> players;
        };

    }  // namespace

    // ---- frames ------------------------------------------------------------------------------------------------------------------

    TEST_F(RoomServerHostileTest, FragmentsWhoseTotalPassesTheLimitAreClosedWith1009BeforeTheLastIsBuffered) {
        const uint16_t port = StartServer();
        auto client = WebSocketTestClient::Open(port);
        ASSERT_TRUE(client.has_value());
        ASSERT_TRUE(client->Handshake(port));

        // Two whole fragments are fine so far; the third announces 6000 more and only its header arrives
        const std::string piece(kFragmentBytes, ' ');
        ASSERT_GT(3 * kFragmentBytes, kLimit);
        ASSERT_LE(2 * kFragmentBytes, kLimit);
        client->Send(AsBytes(Fragment(WebSocketOpcode::kText, piece, false)));
        client->Send(AsBytes(Fragment(WebSocketOpcode::kContinuation, piece, false)));
        const auto third = Fragment(WebSocketOpcode::kContinuation, piece, true);
        const size_t headerBytes = third.size() - kFragmentBytes;
        client->Send(eastl::span<const uint8_t>(third.data(), headerBytes));

        const auto close = ReadUntilClose(*client);
        ASSERT_TRUE(close.has_value()) << "the message was held until its last byte";
        EXPECT_EQ(CloseCodeOf(*close), WebSocketCloseCode::kMessageTooBig);
        EXPECT_TRUE(client->WaitForEndOfStream());
    }

    TEST_F(RoomServerHostileTest, FragmentsWithTheSameTotalUnderTheLimitAreOneMessage) {
        const uint16_t port = StartServer();
        auto client = RoomSocketClient::Connect(port);
        ASSERT_TRUE(client.has_value());
        // Exactly the limit, in three pieces: the boundary is on the allowed side
        const std::string message = PaddedPing(kLimit);
        const size_t cut = message.size() / 3;
        client->Raw().Send(AsBytes(Fragment(WebSocketOpcode::kText, message.substr(0, cut), false)));
        client->Raw().Send(AsBytes(Fragment(WebSocketOpcode::kContinuation, message.substr(cut, cut), false)));
        client->Raw().Send(AsBytes(Fragment(WebSocketOpcode::kContinuation, message.substr(2 * cut), true)));
        ASSERT_TRUE(client->WaitFor(RoomMessageTypes::kPong).has_value());
        EXPECT_EQ(CountOf(*client, RoomMessageTypes::kPong), 1u);
        EXPECT_EQ(CountOf(*client, RoomMessageTypes::kError), 0u);
    }

    TEST_F(RoomServerHostileTest, AValidMessageSentOneByteAtATimeIsAccepted) {
        const uint16_t port = StartServer();
        auto client = RoomSocketClient::Connect(port);
        ASSERT_TRUE(client.has_value());
        const std::string text = PingText();
        const auto frame = EncodeTextFrame(FromStd(text), kClientMask);
        for (size_t i = 0; i < frame.size(); ++i) {
            client->Raw().Send(eastl::span<const uint8_t>(frame.data() + i, 1));
        }
        const auto pong = client->WaitFor(RoomMessageTypes::kPong);
        ASSERT_TRUE(pong.has_value());
        EXPECT_FALSE(client->GetCloseFrame().has_value());
    }

    TEST_F(RoomServerHostileTest, APingBetweenFragmentsIsAnsweredAndTheMessageStillArrivesWhole) {
        const uint16_t port = StartServer();
        auto client = WebSocketTestClient::Open(port);
        ASSERT_TRUE(client.has_value());
        ASSERT_TRUE(client->Handshake(port));
        const std::string text = PingText();
        const size_t cut = text.size() / 2;
        client->Send(AsBytes(Fragment(WebSocketOpcode::kText, text.substr(0, cut), false)));
        client->Send(AsBytes(Fragment(WebSocketOpcode::kPing, "are you there", true)));
        const auto pong = client->WaitForFrame();
        ASSERT_TRUE(pong.has_value());
        EXPECT_EQ(pong->opcode, WebSocketOpcode::kPong);
        EXPECT_EQ(pong->payload, "are you there");
        client->Send(AsBytes(Fragment(WebSocketOpcode::kContinuation, text.substr(cut), true)));
        const auto reply = client->WaitForFrame();
        ASSERT_TRUE(reply.has_value());
        EXPECT_EQ(reply->opcode, WebSocketOpcode::kText);
        EXPECT_NE(reply->payload.find("\"pong\""), std::string::npos) << reply->payload;
    }

    TEST_F(RoomServerHostileTest, AnUnmaskedClientFrameIsClosedWith1002) {
        const uint16_t port = StartServer();
        auto client = WebSocketTestClient::Open(port);
        ASSERT_TRUE(client.has_value());
        ASSERT_TRUE(client->Handshake(port));
        const std::string text = PingText();
        client->Send(AsBytes(EncodeTextFrame(FromStd(text))));
        const auto close = ReadUntilClose(*client);
        ASSERT_TRUE(close.has_value());
        EXPECT_EQ(CloseCodeOf(*close), WebSocketCloseCode::kProtocolError);
        EXPECT_TRUE(client->WaitForEndOfStream());
    }

    // ---- many clients ------------------------------------------------------------------------------------------------------------

    TEST_F(RoomServerHostileTest, SocketsThatNeverHandshakeAreAllCutWhileATablePlaysOn) {
        RoomServerSettings settings = MakeSettings();
        settings.maxConnectionsPerAddress = kSilentSockets + kHumansAtFullTable + 8;
        const uint16_t port = StartServer(settings);
        Clients table;
        ASSERT_NO_FATAL_FAILURE(OpenTable(port, "cards", kHumansAtFullTable, table));
        table[0]->Start();
        const WaitPump pumpTheHand = PumpTheTable(table);

        std::vector<WebSocketTestClient> silent;
        for (int i = 0; i < kSilentSockets; ++i) {
            auto client = WebSocketTestClient::Open(port, pumpTheHand);
            ASSERT_TRUE(client.has_value());
            silent.push_back(std::move(*client));
        }
        // The handshake deadline of the listener ends every one of them, each by itself
        for (WebSocketTestClient& client : silent) {
            EXPECT_TRUE(client.WaitForEndOfStream());
        }
        ASSERT_NO_FATAL_FAILURE(ExpectTheTableFinishes(table));
    }

    TEST_F(RoomServerHostileTest, AFloodOfCreateAndJoinFromManySocketsLeavesTheGoodTablesHandAlone) {
        RoomServerSettings settings = MakeSettings();
        settings.maxConnectionsPerAddress = kFloodSockets + kHumansAtFullTable + 8;
        // The flood is the kind that would trip the per-connection limits one by one; the table must not notice any of it
        settings.hub.maxRoomsPerAddress = 6;
        const uint16_t port = StartServer(settings);
        Clients table;
        ASSERT_NO_FATAL_FAILURE(OpenTable(port, "cards", kHumansAtFullTable, table));
        table[0]->Start();
        const WaitPump pumpTheHand = PumpTheTable(table);

        Clients flooders;
        for (int i = 0; i < kFloodSockets; ++i) {
            auto client = RoomSocketClient::Connect(port);
            ASSERT_TRUE(client.has_value());
            flooders.push_back(std::make_unique<RoomSocketClient>(std::move(*client)));
        }
        for (int round = 0; round < kFloodRounds; ++round) {
            for (int i = 0; i < kFloodSockets; ++i) {
                RoomSocketClient& flooder = *flooders[static_cast<size_t>(i)];
                if (i % 2 == 0) {
                    flooder.Send(CreateRoomMessage{"cards", Format("Flood{}", i), Json::object()});
                } else {
                    flooder.JoinByName(table[0]->roomCode, Format("Flood{}", i));
                }
            }
            pumpTheHand();
        }

        ASSERT_NO_FATAL_FAILURE(ExpectTheTableFinishes(table));
        // The table's room was started: nobody joined it, and nobody learned its cards
        for (const auto& flooder : flooders) {
            Drain(*flooder);
            EXPECT_EQ(CountOf(*flooder, RoomMessageTypes::kView), 0u);
            ExpectCleanWire(*flooder, "a flooder");
        }
        // The cap of rooms per address held (the table's room and five more at most), and the server still answers
        size_t refusedCreates = 0;
        for (const auto& flooder : flooders) {
            for (const ReceivedMessage& message : flooder->Received()) {
                refusedCreates += message.type == RoomMessageTypes::kError && message.GetString("code") == RoomErrors::kTooManyRooms ? 1 : 0;
            }
        }
        EXPECT_GT(refusedCreates, 0u);
        auto probe = RoomSocketClient::Connect(port);
        ASSERT_TRUE(probe.has_value());
        probe->Send(PingMessage{});
        EXPECT_TRUE(probe->WaitFor(RoomMessageTypes::kPong).has_value());
    }

    TEST_F(RoomServerHostileTest, GuessingRoomCodesAcrossReconnectsIsStoppedByTheBudgetOfTheAddress) {
        RoomServerSettings settings = MakeSettings();
        settings.hub.addressJoinBurst = kAddressBudget;
        settings.hub.addressJoinRefill = std::chrono::milliseconds(0);
        const uint16_t port = StartServer(settings);
        Clients table;
        ASSERT_NO_FATAL_FAILURE(OpenTable(port, "cards", kHumansOfAPair, table));
        const String realCode = table[0]->roomCode;

        // Each socket has its own 5 guesses; the address has 8 in all
        int wrong = 0;
        int refused = 0;
        for (int socket = 0; socket < kGuessers; ++socket) {
            auto guesser = RoomSocketClient::Connect(port);
            ASSERT_TRUE(guesser.has_value());
            for (int guess = 0; guess < kGuessesPerSocket; ++guess) {
                const String code = Format("ZZZ{}{}", socket + 2, guess + 2);
                ASSERT_NE(code, realCode);
                guesser->JoinByName(code, "Mal");
                const auto answer = guesser->WaitFor(RoomMessageTypes::kError);
                if (!answer) {
                    ASSERT_TRUE(guesser->GetCloseFrame().has_value()) << "no answer and no close";
                    break;
                }
                wrong += answer->GetString("code") == RoomErrors::kUnknownRoom ? 1 : 0;
                refused += answer->GetString("code") == RoomErrors::kRateLimited ? 1 : 0;
            }
        }
        EXPECT_EQ(wrong, kAddressBudget);
        EXPECT_GT(refused, 0);

        // With the budget empty even the right code is answered the same way as a wrong one, and nobody gets in
        auto last = RoomSocketClient::Connect(port);
        ASSERT_TRUE(last.has_value());
        last->JoinByName(realCode, "Mal");
        const auto answer = last->WaitFor(RoomMessageTypes::kError);
        ASSERT_TRUE(answer.has_value());
        EXPECT_EQ(answer->GetString("code"), RoomErrors::kRateLimited);
        EXPECT_EQ(CountOf(*last, RoomMessageTypes::kJoined), 0u);

        // The table still plays
        table[0]->Start();
        ASSERT_TRUE(PlayToTheEnd(table));
        ExpectCleanWire(table);
    }

}  // namespace BECore::Tests
