#include "FakeSeatedGame.h"
#include "ManualClock.h"
#include "RoomSocketClient.h"
#include "TestRandom.h"

#include <BECore/Logger/LogEvent.h>
#include <Events/SubscriptionHolder.h>
#include <RoomServer/RoomWebSocketServer.h>
#include <Rooms/RoomErrors.h>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    namespace {

        using namespace std::chrono_literals;

        constexpr std::chrono::milliseconds kShortWait{300};

        RoomServerSettings MakeSettings() {
            RoomServerSettings settings;
            settings.tickInterval = 10ms;
            settings.hub.timings = RoomTimings{30s, 10min, 0ms};
            settings.random = MakeSequence(7);
            return settings;
        }

        /// The humans a `lobby` message lists
        int CountHumans(const ReceivedMessage& lobby) {
            int humans = 0;
            const Json* seats = FindJsonMember(lobby.json, "seats");
            if (seats && seats->is_array()) {
                for (const Json& seat : *seats) {
                    humans += GetJsonString(seat, "kind").value_or("") == "human" ? 1 : 0;
                }
            }
            return humans;
        }

        /// Frames of @p count identical text messages in one buffer
        eastl::vector<uint8_t> RepeatText(const std::string& text, int count) {
            const auto frame = EncodeTextFrame(FromStd(text), kClientMask);
            eastl::vector<uint8_t> batch;
            batch.reserve(frame.size() * static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) {
                batch.insert(batch.end(), frame.data(), frame.data() + frame.size());
            }
            return batch;
        }

        /// Sleeps a few ticks of the hub thread (MakeSettings() ticks every 10 ms) so that it has looked at the clock and the sockets
        void LetTicksPass() {
            std::this_thread::sleep_for(60ms);
        }

        class RoomWebSocketServerTest : public ::testing::Test {
        protected:
            void SetUp() override {
                ASSERT_TRUE(registry.Register(SeatedGameInfo{"fake", FakeSeatedGame::kSeatCount}, [] { return eastl::make_unique<FakeSeatedGame>(); }));
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

            /// A host that created a room: the client with `joined` and `lobby` read
            Optional<RoomSocketClient> MakeHost(uint16_t port, StringView name = "Host") {
                auto host = RoomSocketClient::Connect(port);
                if (!host) {
                    return std::nullopt;
                }
                host->Create(name);
                EXPECT_TRUE(host->WaitFor(RoomMessageTypes::kJoined).has_value());
                EXPECT_TRUE(host->WaitFor(RoomMessageTypes::kLobby).has_value());
                return host;
            }

            SeatedGameRegistry registry;
            std::unique_ptr<RoomWebSocketServer> server;
        };

    }  // namespace

    TEST_F(RoomWebSocketServerTest, StartReportsThePortAndStopIsHarmless) {
        RoomWebSocketServer idle(registry, MakeSettings());
        idle.Stop();
        EXPECT_FALSE(idle.IsRunning());

        const uint16_t port = StartServer();
        EXPECT_NE(port, 0);
        EXPECT_TRUE(server->IsRunning());
        EXPECT_EQ(server->GetPort(), port);

        server->Stop();
        server->Stop();
        EXPECT_FALSE(server->IsRunning());
        EXPECT_EQ(server->GetPort(), 0);
    }

    TEST_F(RoomWebSocketServerTest, StartingTwiceOrOnATakenPortIsAnError) {
        const uint16_t port = StartServer();
        const auto again = server->Start();
        ASSERT_FALSE(again.has_value());
        EXPECT_EQ(again.error().code, RemoteControlErrors::kAlreadyRunning);

        RoomServerSettings settings = MakeSettings();
        settings.port = port;
        RoomWebSocketServer other(registry, settings);
        const auto taken = other.Start();
        ASSERT_FALSE(taken.has_value());
        EXPECT_EQ(taken.error().code, RemoteControlErrors::kListenFailed);
    }

    TEST_F(RoomWebSocketServerTest, CanBeStartedAgainAfterStop) {
        StartServer();
        server->Stop();
        const auto port = server->Start();
        ASSERT_TRUE(port.has_value());
        auto host = MakeHost(*port);
        EXPECT_TRUE(host.has_value());
    }

    TEST_F(RoomWebSocketServerTest, CreateRoomGetsJoinedThenLobbyAndOnlyJoinedHasTheToken) {
        const uint16_t port = StartServer();
        auto client = RoomSocketClient::Connect(port);
        ASSERT_TRUE(client.has_value());

        client->Create("Alice");
        const auto joined = client->Next();
        const auto lobby = client->Next();
        ASSERT_TRUE(joined && lobby);
        EXPECT_EQ(joined->type, RoomMessageTypes::kJoined);
        EXPECT_EQ(lobby->type, RoomMessageTypes::kLobby);
        EXPECT_EQ(client->token.size(), RoomLimits::kTokenLength);
        EXPECT_EQ(client->roomCode.size(), RoomLimits::kRoomCodeLength);
        EXPECT_NE(joined->text.find(client->token), eastl::string::npos);
        EXPECT_EQ(lobby->text.find(client->token), eastl::string::npos) << "the token is for its owner only";
    }

    TEST_F(RoomWebSocketServerTest, SecondClientJoinsByCodeAndBothSeeTheLobby) {
        const uint16_t port = StartServer();
        auto host = MakeHost(port);
        ASSERT_TRUE(host.has_value());

        auto guest = RoomSocketClient::Connect(port);
        ASSERT_TRUE(guest.has_value());
        guest->JoinByName(host->roomCode, "Guest");
        ASSERT_TRUE(guest->WaitFor(RoomMessageTypes::kJoined).has_value());
        const auto guestLobby = guest->WaitFor(RoomMessageTypes::kLobby);
        ASSERT_TRUE(guestLobby.has_value());
        EXPECT_EQ(CountHumans(*guestLobby), 2);

        const auto hostLobby = host->WaitFor(RoomMessageTypes::kLobby);
        ASSERT_TRUE(hostLobby.has_value());
        EXPECT_EQ(CountHumans(*hostLobby), 2);
        EXPECT_EQ(guest->roomCode, host->roomCode);
        EXPECT_NE(guest->token, host->token);
    }

    TEST_F(RoomWebSocketServerTest, ReplyOrderOfOneConnectionIsTheOrderOfItsMessages) {
        const uint16_t port = StartServer();
        auto client = RoomSocketClient::Connect(port);
        ASSERT_TRUE(client.has_value());

        // A ping answers pong, garbage answers error: sent in a row, answered in a row
        const std::vector<std::pair<std::string, std::string>> script = {
            {"{\"v\":1,\"type\":\"ping\"}", "pong"}, {"not json", "error"}, {"{\"v\":1,\"type\":\"ping\"}", "pong"}, {"{\"v\":1,\"type\":\"no_such\"}", "error"},
            {"{\"v\":1,\"type\":\"ping\"}", "pong"},
        };
        eastl::vector<uint8_t> batch;
        for (const auto& [text, reply] : script) {
            const auto frame = EncodeTextFrame(FromStd(text), kClientMask);
            batch.insert(batch.end(), frame.data(), frame.data() + frame.size());
        }
        client->Raw().Send(AsBytes(batch));

        for (const auto& [text, reply] : script) {
            const auto message = client->Next();
            ASSERT_TRUE(message.has_value()) << text;
            EXPECT_EQ(message->type.c_str(), reply) << text;
        }
    }

    TEST_F(RoomWebSocketServerTest, BadJsonIsAnErrorAndTheConnectionStays) {
        const uint16_t port = StartServer();
        auto client = RoomSocketClient::Connect(port);
        ASSERT_TRUE(client.has_value());

        client->SendText("{ this is not json");
        const auto error = client->Next();
        ASSERT_TRUE(error.has_value());
        EXPECT_EQ(error->type, RoomMessageTypes::kError);
        EXPECT_EQ(error->GetString("code"), RoomErrors::kBadMessage);

        client->Send(PingMessage{});
        const auto pong = client->Next();
        ASSERT_TRUE(pong.has_value());
        EXPECT_EQ(pong->type, RoomMessageTypes::kPong);
    }

    TEST_F(RoomWebSocketServerTest, HubCloseArrivesAsCloseFrameWithTheCodeAndTheReason) {
        const uint16_t port = StartServer();
        auto client = RoomSocketClient::Connect(port);
        ASSERT_TRUE(client.has_value());

        // The burst of the rate limiter is 20 messages: the next five are rate_limited, and the fifth in a row ends the connection
        client->Raw().Send(AsBytes(RepeatText("{\"v\":1,\"type\":\"ping\"}", 25)));
        const auto close = client->WaitForClose();
        ASSERT_TRUE(close.has_value());
        EXPECT_EQ(close->opcode, WebSocketOpcode::kClose);
        EXPECT_EQ(CloseCodeOf(*close), WebSocketCloseCode::kPolicyViolation);
        EXPECT_EQ(CloseReasonOf(*close), "rate_limited");
        EXPECT_TRUE(client->Raw().WaitForEndOfStream());
    }

    TEST_F(RoomWebSocketServerTest, ConnectionOverTheHubLimitIsClosedWithTryAgainLater) {
        RoomServerSettings settings = MakeSettings();
        settings.hub.maxConnections = 2;
        const uint16_t port = StartServer(settings);

        auto first = RoomSocketClient::Connect(port);
        auto second = RoomSocketClient::Connect(port);
        ASSERT_TRUE(first && second);
        // Both are known to the hub once it answers a ping
        first->Send(PingMessage{});
        second->Send(PingMessage{});
        ASSERT_TRUE(first->WaitFor(RoomMessageTypes::kPong).has_value());
        ASSERT_TRUE(second->WaitFor(RoomMessageTypes::kPong).has_value());

        auto third = RoomSocketClient::Connect(port);
        ASSERT_TRUE(third.has_value());
        const auto close = third->WaitForClose();
        ASSERT_TRUE(close.has_value());
        EXPECT_EQ(CloseCodeOf(*close), WebSocketCloseCode::kTryAgainLater);
        EXPECT_EQ(CloseReasonOf(*close), "server_full");
    }

    TEST_F(RoomWebSocketServerTest, MessageOverTheLimitClosesWithTooBigBeforeItIsBuffered) {
        const uint16_t port = StartServer();
        auto client = RoomSocketClient::Connect(port);
        ASSERT_TRUE(client.has_value());

        const uint64_t length = RoomLimits::kMaxMessageBytes + 1;
        eastl::vector<uint8_t> header = {0x81, 0xFE, static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(length)};
        header.insert(header.end(), kClientMask.data(), kClientMask.data() + kClientMask.size());
        client->Raw().Send(AsBytes(header));

        const auto close = client->WaitForClose();
        ASSERT_TRUE(close.has_value());
        EXPECT_EQ(CloseCodeOf(*close), WebSocketCloseCode::kMessageTooBig);
    }

    TEST_F(RoomWebSocketServerTest, BinaryMessageClosesWithUnsupportedData) {
        const uint16_t port = StartServer();
        auto client = RoomSocketClient::Connect(port);
        ASSERT_TRUE(client.has_value());

        client->Raw().Send(AsBytes(EncodeFrame(WebSocketOpcode::kBinary, AsBytes(std::string("\x01\x02")), kClientMask)));
        const auto close = client->WaitForClose();
        ASSERT_TRUE(close.has_value());
        EXPECT_EQ(CloseCodeOf(*close), WebSocketCloseCode::kUnsupportedData);
    }

    TEST_F(RoomWebSocketServerTest, AbruptDropInALobbyRemovesTheMember) {
        const uint16_t port = StartServer();
        auto host = MakeHost(port);
        ASSERT_TRUE(host.has_value());

        auto guest = RoomSocketClient::Connect(port);
        ASSERT_TRUE(guest.has_value());
        guest->JoinByName(host->roomCode, "Guest");
        const auto twoHumans = host->WaitFor(RoomMessageTypes::kLobby);
        ASSERT_TRUE(twoHumans.has_value());
        EXPECT_EQ(CountHumans(*twoHumans), 2);

        guest->Raw().Drop();
        const auto oneHuman = host->WaitFor(RoomMessageTypes::kLobby);
        ASSERT_TRUE(oneHuman.has_value());
        EXPECT_EQ(CountHumans(*oneHuman), 1);
    }

    TEST_F(RoomWebSocketServerTest, ListedOriginPassesAndForeignOriginIsRefusedInPublicMode) {
        RoomServerSettings settings = MakeSettings();
        settings.bind = SocketBind::AnyAddress;
        settings.policy.loopbackOnly = false;
        settings.policy.allowedOrigins = {"https://good.example"};
        const uint16_t port = StartServer(settings);

        auto good = WebSocketTestClient::Open(port);
        ASSERT_TRUE(good.has_value());
        good->Send(HandshakeRequest(port, "Origin: https://good.example\r\n", "rooms.example.net"));
        EXPECT_TRUE(good->ReadHttpResponse().starts_with("HTTP/1.1 101 "));

        auto evil = WebSocketTestClient::Open(port);
        ASSERT_TRUE(evil.has_value());
        evil->Send(HandshakeRequest(port, "Origin: https://evil.example\r\n", "rooms.example.net"));
        EXPECT_TRUE(evil->ReadHttpResponse().starts_with("HTTP/1.1 403 Forbidden\r\n"));
    }

    TEST_F(RoomWebSocketServerTest, PerAddressLimitRefusesTheNextConnection) {
        RoomServerSettings settings = MakeSettings();
        settings.maxConnectionsPerAddress = 2;
        const uint16_t port = StartServer(settings);

        auto first = RoomSocketClient::Connect(port);
        auto second = RoomSocketClient::Connect(port);
        ASSERT_TRUE(first && second);

        auto third = WebSocketTestClient::Open(port);
        ASSERT_TRUE(third.has_value());
        third->Send(HandshakeRequest(port));
        EXPECT_TRUE(third->ReadHttpResponse().starts_with("HTTP/1.1 503 Service Unavailable\r\n"));

        first->Send(PingMessage{});
        EXPECT_TRUE(first->WaitFor(RoomMessageTypes::kPong).has_value());
    }

    TEST_F(RoomWebSocketServerTest, StopWithOpenConnectionsAndMessagesInFlightReturnsPromptly) {
        const uint16_t port = StartServer();
        eastl::vector<RoomSocketClient> clients;
        for (int i = 0; i < 6; ++i) {
            auto client = RoomSocketClient::Connect(port);
            ASSERT_TRUE(client.has_value());
            clients.push_back(std::move(*client));
        }
        auto silent = WebSocketTestClient::Open(port);
        ASSERT_TRUE(silent.has_value());

        // A flood from every client, the hub busy answering it, and Stop() in the middle
        const eastl::vector<uint8_t> flood = RepeatText("{\"v\":1,\"type\":\"ping\"}", 2000);
        for (RoomSocketClient& client : clients) {
            (void)client.Raw().TrySend(AsBytes(flood));
        }

        const auto before = std::chrono::steady_clock::now();
        server->Stop();
        EXPECT_LT(std::chrono::steady_clock::now() - before, 4s);
        server->Stop();
        EXPECT_FALSE(server->IsRunning());
        for (RoomSocketClient& client : clients) {
            EXPECT_TRUE(client.Raw().WaitForEndOfStream());
        }
        EXPECT_TRUE(silent->WaitForEndOfStream());
    }

    TEST_F(RoomWebSocketServerTest, ClientThatNeverReadsIsDroppedAndOthersAreNotSlowed) {
        RoomServerSettings settings = MakeSettings();
        settings.maxOutboxBytes = 4 * 1024;
        settings.hub.rateBurst = 10'000'000;  // the flood below must reach the hub
        settings.hub.ratePerSecond = 10'000'000.0;
        const uint16_t port = StartServer(settings);

        auto slow = RoomSocketClient::Connect(port);
        auto fast = RoomSocketClient::Connect(port);
        ASSERT_TRUE(slow && fast);

        // The slow client asks and never reads the answers: the system's buffers fill, then its queue passes the cap and it is dropped
        const eastl::vector<uint8_t> chunk = RepeatText("{\"v\":1,\"type\":\"ping\"}", 2000);
        bool dropped = false;
        for (int round = 0; round < 3000 && !dropped; ++round) {
            dropped = !slow->Raw().TrySend(AsBytes(chunk));
            fast->Send(PingMessage{});
            const auto start = std::chrono::steady_clock::now();
            ASSERT_TRUE(fast->WaitFor(RoomMessageTypes::kPong, 10s).has_value()) << "the hub stalled in round " << round;
            EXPECT_LT(std::chrono::steady_clock::now() - start, 5s);
        }
        EXPECT_TRUE(dropped) << "the connection that never reads was not dropped";
    }

    namespace {

        constexpr std::chrono::milliseconds kShutdownGrace{600};

        /// Settings of a server that tells its clients on Stop() and waits @p grace for them
        RoomServerSettings MakeShutdownSettings(std::chrono::milliseconds grace = kShutdownGrace) {
            RoomServerSettings settings = MakeSettings();
            settings.shutdownGrace = grace;
            return settings;
        }

        /// How long Stop() takes
        std::chrono::milliseconds TimeStop(RoomWebSocketServer& server) {
            const auto before = std::chrono::steady_clock::now();
            server.Stop();
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - before);
        }

        /// Asks the server for so many answers without ever reading one: its send buffer and the client's receive buffer fill, the writer of the
        /// connection blocks in the socket and the rest of the answers wait in the outbox (`maxOutboxBytes` of the settings has to be above them)
        void FloodWithoutReading(RoomSocketClient& client) {
            // About 80 bytes of answer for 50 sent, 13 MB in all: above the 4 MB the system lets a send buffer and a receive buffer grow to, together
            constexpr int kChunks = 80;
            const eastl::vector<uint8_t> chunk = RepeatText("{\"v\":1,\"type\":\"join\",\"room\":\"ZZZZ\",\"name\":\"x\"}", 2000);
            for (int i = 0; i < kChunks; ++i) {
                ASSERT_TRUE(client.Raw().TrySend(AsBytes(chunk)));
            }
        }

        /// Settings for FloodWithoutReading(): no rate limit, no end after failed joins, an outbox that holds every answer
        RoomServerSettings MakeStuckClientSettings(std::chrono::milliseconds grace) {
            RoomServerSettings settings = MakeShutdownSettings(grace);
            settings.maxOutboxBytes = 512 * 1024 * 1024;
            settings.hub.rateBurst = 100'000'000;
            settings.hub.ratePerSecond = 100'000'000.0;
            settings.hub.maxFailedJoinsInRow = std::numeric_limits<int>::max();
            return settings;
        }

    }  // namespace

    TEST_F(RoomWebSocketServerTest, StopTellsEveryClientWithCloseGoingAwayBeforeTheEndOfTheStream) {
        const uint16_t port = StartServer(MakeShutdownSettings());
        auto host = MakeHost(port);
        auto guest = RoomSocketClient::Connect(port);
        ASSERT_TRUE(host && guest);
        guest->Send(PingMessage{});
        ASSERT_TRUE(guest->WaitFor(RoomMessageTypes::kPong).has_value());

        server->Stop();

        for (RoomSocketClient* client : {&*host, &*guest}) {
            const auto close = client->WaitForClose();
            ASSERT_TRUE(close.has_value());
            EXPECT_EQ(close->opcode, WebSocketOpcode::kClose);
            EXPECT_EQ(CloseCodeOf(*close), WebSocketCloseCode::kGoingAway);
            EXPECT_EQ(CloseReasonOf(*close), "server_stopping");
            EXPECT_TRUE(client->Raw().WaitForEndOfStream());
        }
    }

    TEST_F(RoomWebSocketServerTest, StopReturnsWellBeforeTheGraceWhenTheClientsAnswer) {
        constexpr std::chrono::milliseconds kLongGrace{10'000};
        const uint16_t port = StartServer(MakeShutdownSettings(kLongGrace));
        auto host = MakeHost(port);
        ASSERT_TRUE(host.has_value());

        // The client reads the close frame and answers it, as a browser does
        std::thread answering([&] {
            if (const auto close = host->WaitForClose(); close.has_value()) {
                (void)host->Raw().TrySend(AsBytes(EncodeCloseFrame(CloseCodeOf(*close), "", kClientMask)));
            }
        });
        const auto elapsed = TimeStop(*server);
        answering.join();

        EXPECT_LT(elapsed, 2s) << "Stop() waited for the grace although the client had answered";
        EXPECT_TRUE(host->Raw().WaitForEndOfStream());
    }

    TEST_F(RoomWebSocketServerTest, StopWithNoConnectionsDoesNotWaitForTheGrace) {
        StartServer(MakeShutdownSettings(10'000ms));
        EXPECT_LT(TimeStop(*server), 2s);
    }

    TEST_F(RoomWebSocketServerTest, StopWaitsForTheGraceForAClientThatDoesNotReadAndThenCutsIt) {
        const uint16_t port = StartServer(MakeStuckClientSettings(kShutdownGrace));
        auto stuck = RoomSocketClient::Connect(port);
        ASSERT_TRUE(stuck.has_value());
        FloodWithoutReading(*stuck);

        const auto elapsed = TimeStop(*server);

        // Not before the grace: the close frame is behind the answers that the client does not take
        EXPECT_GE(elapsed, kShutdownGrace - 50ms);
        EXPECT_LT(elapsed, kShutdownGrace + 3s);
        EXPECT_FALSE(server->IsRunning());
        EXPECT_TRUE(stuck->Raw().WaitForEndOfStream());
    }

    TEST_F(RoomWebSocketServerTest, GraceZeroSendsNoCloseFrameAndReturnsAtOnce) {
        const uint16_t port = StartServer(MakeShutdownSettings(0ms));
        auto host = MakeHost(port);
        ASSERT_TRUE(host.has_value());

        EXPECT_LT(TimeStop(*server), 2s);

        EXPECT_FALSE(host->WaitForClose(kShortWait).has_value());
        EXPECT_TRUE(host->Raw().WaitForEndOfStream());
    }

    TEST_F(RoomWebSocketServerTest, AConnectionThatOpensDuringTheDrainIsClosedWithGoingAwayToo) {
        constexpr std::chrono::milliseconds kDrainGrace{2000};
        const uint16_t port = StartServer(MakeStuckClientSettings(kDrainGrace));
        auto stuck = RoomSocketClient::Connect(port);
        ASSERT_TRUE(stuck.has_value());
        FloodWithoutReading(*stuck);

        // The stuck client holds the drain open for the whole grace
        std::chrono::milliseconds elapsed{0};
        std::thread stopping([&] { elapsed = TimeStop(*server); });
        std::this_thread::sleep_for(150ms);

        auto late = RoomSocketClient::Connect(port);
        Optional<ServerFrame> close;
        if (late) {
            close = late->WaitForClose();
        }
        stopping.join();

        ASSERT_TRUE(late.has_value());
        ASSERT_TRUE(close.has_value());
        EXPECT_EQ(CloseCodeOf(*close), WebSocketCloseCode::kGoingAway);
        EXPECT_EQ(CloseReasonOf(*close), "server_stopping");
        EXPECT_GE(elapsed, kDrainGrace - 50ms);
    }

    TEST_F(RoomWebSocketServerTest, StopTwiceAfterADrainAndTheDestructorAreSafeAndTheServerStartsAgain) {
        const uint16_t port = StartServer(MakeShutdownSettings());
        auto host = MakeHost(port);
        ASSERT_TRUE(host.has_value());

        server->Stop();
        server->Stop();
        EXPECT_FALSE(server->IsRunning());

        // A new run is not draining: the client stays connected and is answered
        const auto again = server->Start();
        ASSERT_TRUE(again.has_value());
        auto next = MakeHost(*again);
        ASSERT_TRUE(next.has_value());
        next->Send(PingMessage{});
        ASSERT_TRUE(next->WaitFor(RoomMessageTypes::kPong).has_value());
        EXPECT_FALSE(next->WaitForClose(kShortWait).has_value());

        // The destructor stops a running server, with a drain
        server.reset();
        const auto close = next->WaitForClose();
        ASSERT_TRUE(close.has_value());
        EXPECT_EQ(CloseCodeOf(*close), WebSocketCloseCode::kGoingAway);
    }

    TEST_F(RoomWebSocketServerTest, StopRepeatedWithClientsComingAndGoingNeverHangs) {
        for (int round = 0; round < 5; ++round) {
            const uint16_t port = StartServer(MakeShutdownSettings(300ms));
            eastl::vector<RoomSocketClient> clients;
            for (int i = 0; i < 4; ++i) {
                auto client = RoomSocketClient::Connect(port);
                ASSERT_TRUE(client.has_value());
                clients.push_back(std::move(*client));
            }
            clients[0].Raw().Drop();  // leaves at about the moment the server stops
            EXPECT_LT(TimeStop(*server), 3s);
            for (size_t i = 1; i < clients.size(); ++i) {
                EXPECT_TRUE(clients[i].Raw().WaitForEndOfStream());
            }
        }
    }

    namespace {

        constexpr std::chrono::milliseconds kPingInterval{1000};
        constexpr std::chrono::milliseconds kClockStep{250};

        /// Settings with a short liveness interval on a manual clock
        RoomServerSettings MakeLivenessSettings(ManualClock& clock, std::chrono::milliseconds pingInterval = kPingInterval) {
            RoomServerSettings settings = MakeSettings();
            settings.pingInterval = pingInterval;
            settings.clock = clock.AsFunction();
            return settings;
        }

        /// A message of the protocol the server answers at once: proves the hub got what the client sent before it
        void RoundTrip(RoomSocketClient& client) {
            client.Send(PingMessage{});
            ASSERT_TRUE(client.WaitFor(RoomMessageTypes::kPong).has_value());
        }

        /// True when @p lobby shows a human seat that is not connected
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

        /// Reads frames until a ping arrives (text frames before it are skipped); false when none does or the connection ends first
        bool WaitForPing(RoomSocketClient& client, std::chrono::milliseconds limit = 5s) {
            const auto deadline = std::chrono::steady_clock::now() + limit;
            while (std::chrono::steady_clock::now() < deadline) {
                const auto frame = client.Raw().WaitForFrame(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()));
                if (!frame) {
                    return false;
                }
                if (frame->opcode == WebSocketOpcode::kPing) {
                    return true;
                }
            }
            return false;
        }

        void SendPong(RoomSocketClient& client) {
            client.Raw().Send(AsBytes(EncodeFrame(WebSocketOpcode::kPong, {}, kClientMask)));
        }

        /// Moves the clock on by kClockStep at a time until a ping reaches @p client, and tells how far it moved; nullopt when no ping came
        /// within @p limit of clock time. The hub thread looks at the clock on its own schedule, so a test cannot know which step it
        /// notices a silence at: it asks how long the silence had to be
        Optional<std::chrono::milliseconds> AdvanceUntilPing(ManualClock& clock, RoomSocketClient& client, std::chrono::milliseconds limit = 3 * kPingInterval) {
            std::chrono::milliseconds advanced{0};
            while (advanced < limit) {
                clock.Advance(kClockStep);
                advanced += kClockStep;
                if (WaitForPing(client, 200ms)) {
                    return advanced;
                }
            }
            return std::nullopt;
        }

        /// True once @p client has received a lobby that shows a human seat away (all the messages it ever got, whatever was read from them)
        bool SawAwayHuman(const RoomSocketClient& client) {
            for (const ReceivedMessage& message : client.Received()) {
                if (message.type == RoomMessageTypes::kLobby && HasAwayHuman(message)) {
                    return true;
                }
            }
            return false;
        }

        /// Reads what the server sends until @p done holds for @p client (it looks at Received(), so the cursor does not matter) or @p limit has passed
        template <typename Predicate>
        bool ReadUntil(RoomSocketClient& client, Predicate done, std::chrono::milliseconds limit = 10s) {
            const auto deadline = std::chrono::steady_clock::now() + limit;
            while (!done()) {
                const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
                if (left <= 0ms || client.GetCloseFrame().has_value()) {
                    return false;
                }
                client.Next(std::min(left, 20ms));
            }
            return true;
        }

    }  // namespace

    TEST_F(RoomWebSocketServerTest, SilentClientGetsAPingAnswersWithAPongAndStays) {
        ManualClock clock;
        const uint16_t port = StartServer(MakeLivenessSettings(clock));
        auto client = RoomSocketClient::Connect(port);
        ASSERT_TRUE(client.has_value());
        RoundTrip(*client);  // the hub knows the connection

        // Stepped like the second ping: the hub may take its liveness baseline at any tick after the RoundTrip, so one jump of the
        // clock could land before that tick and the ping would never come
        const auto first = AdvanceUntilPing(clock, *client);
        ASSERT_TRUE(first.has_value()) << "a connection that was silent for pingInterval must be pinged";
        EXPECT_GE(*first, kPingInterval) << "no ping inside the interval";

        // The pong is life: the next ping comes only after another interval of silence, counted from the pong
        SendPong(*client);
        RoundTrip(*client);
        const auto silence = AdvanceUntilPing(clock, *client);
        ASSERT_TRUE(silence.has_value()) << "the connection that answered must be pinged again after another silence";
        EXPECT_GE(*silence, kPingInterval) << "no second ping inside the interval";
        SendPong(*client);
        RoundTrip(*client);
        EXPECT_FALSE(client->GetCloseFrame().has_value());
    }

    TEST_F(RoomWebSocketServerTest, ClientThatNeverAnswersIsDroppedAfterTwoIntervalsAndItsSeatGoesAway) {
        ManualClock clock;
        const uint16_t port = StartServer(MakeLivenessSettings(clock));
        auto host = MakeHost(port);
        ASSERT_TRUE(host.has_value());
        auto guest = RoomSocketClient::Connect(port);
        ASSERT_TRUE(guest.has_value());
        guest->JoinByName(host->roomCode, "Guest");
        ASSERT_TRUE(guest->WaitFor(RoomMessageTypes::kJoined).has_value());
        ASSERT_TRUE(guest->WaitFor(RoomMessageTypes::kLobby).has_value());
        host->Start();
        ASSERT_TRUE(host->WaitFor(RoomMessageTypes::kView).has_value());
        RoundTrip(*host);
        RoundTrip(*guest);

        // The host is alive all the time (it sends something every step), the guest says nothing and never pongs. The drop shows as a
        // lobby in Received() of the host; RoundTrip() reads past it, so it is looked for there, not read from the cursor
        for (int step = 0; step < 12 && !SawAwayHuman(*host); ++step) {
            clock.Advance(kClockStep);
            RoundTrip(*host);
            LetTicksPass();  // pacing: the hub thread has to look at every clock value, or the guest would be dropped without the ping before it
        }
        // Three intervals have passed by now, so the lobby of the drop is only a matter of delivery
        EXPECT_TRUE(ReadUntil(*host, [&] { return SawAwayHuman(*host); })) << "the host never saw the silent guest go away";

        // The guest got the ping first, then the end of the connection
        EXPECT_TRUE(WaitForPing(*guest));
        EXPECT_TRUE(guest->Raw().WaitForEndOfStream());
        // The host stayed
        RoundTrip(*host);
        EXPECT_FALSE(host->GetCloseFrame().has_value());
    }

    TEST_F(RoomWebSocketServerTest, ClientThatSendsRegularlyIsNeverPinged) {
        ManualClock clock;
        const uint16_t port = StartServer(MakeLivenessSettings(clock));
        auto client = RoomSocketClient::Connect(port);
        ASSERT_TRUE(client.has_value());
        RoundTrip(*client);
        LetTicksPass();

        for (int step = 0; step < 16; ++step) {
            clock.Advance(kClockStep);  // four intervals in all
            RoundTrip(*client);
        }
        LetTicksPass();
        EXPECT_FALSE(client->GetCloseFrame().has_value());
        for (const ReceivedMessage& message : client->Received()) {
            EXPECT_EQ(message.type, RoomMessageTypes::kPong);
        }
        EXPECT_FALSE(client->Raw().WaitForFrame(100ms).has_value()) << "no ping, no close frame";
    }

    TEST_F(RoomWebSocketServerTest, PingIntervalZeroSendsNoPingAndDropsNobody) {
        ManualClock clock;
        const uint16_t port = StartServer(MakeLivenessSettings(clock, 0ms));
        auto client = RoomSocketClient::Connect(port);
        ASSERT_TRUE(client.has_value());
        RoundTrip(*client);

        clock.Advance(25s);  // far over any interval, under the 30 s the hub lets a connection that is in no room stay silent
        LetTicksPass();
        EXPECT_FALSE(client->Raw().WaitForFrame(200ms).has_value());
        RoundTrip(*client);
    }

    TEST_F(RoomWebSocketServerTest, TwoServersOnTwoPortsDoNotInterfere) {
        const uint16_t firstPort = StartServer();
        RoomWebSocketServer second(registry, MakeSettings());
        const auto secondPort = second.Start();
        ASSERT_TRUE(secondPort.has_value());
        EXPECT_NE(firstPort, *secondPort);

        auto host = MakeHost(firstPort);
        ASSERT_TRUE(host.has_value());
        auto stranger = RoomSocketClient::Connect(*secondPort);
        ASSERT_TRUE(stranger.has_value());
        stranger->JoinByName(host->roomCode, "Stranger");
        const auto error = stranger->Next();
        ASSERT_TRUE(error.has_value());
        EXPECT_EQ(error->GetString("code"), RoomErrors::kUnknownRoom);

        auto other = RoomSocketClient::Connect(*secondPort);
        ASSERT_TRUE(other.has_value());
        other->Create("Other");
        EXPECT_TRUE(other->WaitFor(RoomMessageTypes::kJoined).has_value());
        second.Stop();
        host->Send(PingMessage{});
        EXPECT_TRUE(host->WaitFor(RoomMessageTypes::kPong).has_value()) << "stopping one server must not touch the other";
    }

    namespace {

        constexpr std::chrono::milliseconds kStatsInterval{1000};

        /// Collects the stats lines (`Room server: <n> connections, <m> rooms`) that LogEvent::Flush() delivers while it lives
        class StatsLines : public SubscriptionHolder {
        public:
            StatsLines() {
                LogEvent::Flush();  // what earlier tests left in the ring is not ours
                Subscribe<LogEvent, &StatsLines::OnLog>(this);
            }

            /// Delivers the pending log entries, then the lines so far
            [[nodiscard]] const std::vector<std::string>& Lines() {
                LogEvent::Flush();
                return _lines;
            }

            /// Waits (real time, up to @p limit) until @p count lines have come
            bool WaitForCount(size_t count, std::chrono::milliseconds limit = 3s) {
                const auto deadline = std::chrono::steady_clock::now() + limit;
                while (Lines().size() < count && std::chrono::steady_clock::now() < deadline) {
                    std::this_thread::sleep_for(10ms);
                }
                return _lines.size() >= count;
            }

        private:
            void OnLog(const LogEvent& event) {
                const std::string message(event.message.data(), event.message.size());
                if (message.find("Room server: ") != std::string::npos && message.find(" connections, ") != std::string::npos && message.find(" rooms") != std::string::npos) {
                    _lines.push_back(message);
                }
            }

            std::vector<std::string> _lines;
        };

        RoomServerSettings MakeStatsSettings(ManualClock& clock, std::chrono::milliseconds interval = kStatsInterval) {
            RoomServerSettings settings = MakeSettings();
            settings.statsLogInterval = interval;
            settings.clock = clock.AsFunction();
            return settings;
        }

    }  // namespace

    TEST(RoomServerStatsTest, LogsOnlyAChangeAfterTheInterval) {
        using Stats = RoomServerStats;
        const RoomTime start = std::chrono::steady_clock::now();
        EXPECT_FALSE(ShouldLogRoomServerStats(Stats{1, 0}, Stats{}, start + 999ms, start, 1000ms)) << "the interval has not passed";
        EXPECT_TRUE(ShouldLogRoomServerStats(Stats{1, 0}, Stats{}, start + 1000ms, start, 1000ms));
        EXPECT_FALSE(ShouldLogRoomServerStats(Stats{1, 0}, Stats{1, 0}, start + 1h, start, 1000ms)) << "nothing changed";
        EXPECT_TRUE(ShouldLogRoomServerStats(Stats{1, 1}, Stats{1, 0}, start + 1h, start, 1000ms)) << "the rooms alone count";
        EXPECT_TRUE(ShouldLogRoomServerStats(Stats{}, Stats{3, 1}, start + 1h, start, 1000ms)) << "the way back to nothing is a change too";
        EXPECT_FALSE(ShouldLogRoomServerStats(Stats{1, 0}, Stats{}, start + 1h, start, 0ms)) << "0 is off";
    }

    TEST_F(RoomWebSocketServerTest, StatsLineComesAfterTheIntervalOnlyWhenTheNumbersChanged) {
        ManualClock clock;
        StatsLines lines;
        const uint16_t port = StartServer(MakeStatsSettings(clock));
        auto host = MakeHost(port);
        ASSERT_TRUE(host.has_value());
        RoundTrip(*host);
        LetTicksPass();
        EXPECT_TRUE(lines.Lines().empty()) << "the interval has not passed";

        clock.Advance(kStatsInterval + 100ms);
        ASSERT_TRUE(lines.WaitForCount(1));
        EXPECT_NE(lines.Lines()[0].find("1 connections, 1 rooms"), std::string::npos) << lines.Lines()[0];

        clock.Advance(5 * kStatsInterval);
        LetTicksPass();
        EXPECT_EQ(lines.Lines().size(), 1u) << "nothing changed: no second line";

        // The interval has long passed, so a change is logged at once
        auto second = RoomSocketClient::Connect(port);
        ASSERT_TRUE(second.has_value());
        RoundTrip(*second);
        ASSERT_TRUE(lines.WaitForCount(2));
        EXPECT_NE(lines.Lines()[1].find("2 connections, 1 rooms"), std::string::npos) << lines.Lines()[1];

        auto third = RoomSocketClient::Connect(port);
        ASSERT_TRUE(third.has_value());
        RoundTrip(*third);
        LetTicksPass();
        EXPECT_EQ(lines.Lines().size(), 2u) << "a change inside the interval waits for it";
        clock.Advance(kStatsInterval + 100ms);
        ASSERT_TRUE(lines.WaitForCount(3));
        EXPECT_NE(lines.Lines()[2].find("3 connections, 1 rooms"), std::string::npos) << lines.Lines()[2];

        for (const std::string& line : lines.Lines()) {
            EXPECT_EQ(line.find("Host"), std::string::npos) << "no name in a log line: " << line;
        }
    }

    TEST_F(RoomWebSocketServerTest, StatsIntervalZeroLogsNoStats) {
        ManualClock clock;
        StatsLines lines;
        const uint16_t port = StartServer(MakeStatsSettings(clock, 0ms));
        auto host = MakeHost(port);
        ASSERT_TRUE(host.has_value());
        RoundTrip(*host);
        clock.Advance(1h);
        LetTicksPass();
        EXPECT_TRUE(lines.Lines().empty());
    }

    // ---- Per-address rules: rooms, the lobby timeout, an address behind a proxy -----------------------------------------------------

    TEST_F(RoomWebSocketServerTest, FifthRoomFromOneAddressIsRefusedAndTheFourStayIntact) {
        RoomServerSettings settings = MakeSettings();
        settings.hub.maxRoomsPerAddress = 4;
        const uint16_t port = StartServer(settings);

        std::vector<RoomSocketClient> hosts;
        for (int i = 0; i < 4; ++i) {
            auto host = MakeHost(port);
            ASSERT_TRUE(host.has_value());
            hosts.push_back(std::move(*host));
        }
        auto fifth = RoomSocketClient::Connect(port);
        ASSERT_TRUE(fifth.has_value());
        fifth->Create("Host");
        const auto refused = fifth->WaitFor(RoomMessageTypes::kError);
        ASSERT_TRUE(refused.has_value());
        EXPECT_EQ(refused->GetString("code"), RoomErrors::kTooManyRooms);
        EXPECT_NE(refused->GetString("message").find("your address"), eastl::string::npos);

        for (RoomSocketClient& host : hosts) {
            RoundTrip(host);
            EXPECT_FALSE(host.GetCloseFrame().has_value());
        }
        fifth->JoinByName(hosts[0].roomCode, "Guest");
        EXPECT_TRUE(fifth->WaitFor(RoomMessageTypes::kJoined).has_value()) << "the connection that was refused a room can still join one";
    }

    TEST_F(RoomWebSocketServerTest, WithNoPerAddressLimitAndNoProxyTheHubSeesNoAddress) {
        RoomServerSettings settings = MakeSettings();
        settings.maxConnectionsPerAddress = 0;
        settings.hub.maxRoomsPerAddress = 1;
        const uint16_t port = StartServer(settings);
        std::vector<RoomSocketClient> hosts;
        for (int i = 0; i < 3; ++i) {
            auto host = MakeHost(port);
            ASSERT_TRUE(host.has_value()) << "the image's --max-per-address 0 keeps every per-address rule off";
            hosts.push_back(std::move(*host));
        }
    }

    TEST_F(RoomWebSocketServerTest, ASilentLobbyIsClosedAfterTheTimeoutAndTheSocketsStayOpen) {
        ManualClock clock;
        RoomServerSettings settings = MakeSettings();
        settings.clock = clock.AsFunction();
        settings.pingInterval = 0ms;
        settings.hub.lobbyTimeout = 1min;
        const uint16_t port = StartServer(settings);
        auto host = MakeHost(port);
        ASSERT_TRUE(host.has_value());
        auto guest = RoomSocketClient::Connect(port);
        ASSERT_TRUE(guest.has_value());
        guest->JoinByName(host->roomCode, "Guest");
        ASSERT_TRUE(guest->WaitFor(RoomMessageTypes::kLobby).has_value());

        clock.Advance(59s);
        RoundTrip(*host);
        clock.Advance(2s);
        for (RoomSocketClient* client : {&*host, &*guest}) {
            const auto left = client->WaitFor(RoomMessageTypes::kLeft);
            ASSERT_TRUE(left.has_value());
            EXPECT_EQ(left->GetString("reason"), "lobby_timeout");
        }
        // Both sockets are open and in no room, so the idle rule (30 s) is theirs now
        RoundTrip(*host);
        RoundTrip(*guest);
        EXPECT_FALSE(host->GetCloseFrame().has_value());
        EXPECT_FALSE(guest->GetCloseFrame().has_value());
    }

    TEST_F(RoomWebSocketServerTest, BehindATrustedProxyEachForwardedAddressHasItsOwnRooms) {
        RoomServerSettings settings = MakeSettings();
        settings.policy.trustedProxyHops = 1;
        settings.hub.maxRoomsPerAddress = 4;
        const uint16_t port = StartServer(settings);

        std::vector<RoomSocketClient> hosts;
        for (const char* header : {"X-Forwarded-For: 203.0.113.1\r\n", "X-Forwarded-For: 203.0.113.2\r\n"}) {
            for (int i = 0; i < 4; ++i) {
                auto client = RoomSocketClient::Connect(port, header);
                ASSERT_TRUE(client.has_value());
                client->Create("Host");
                ASSERT_TRUE(client->WaitFor(RoomMessageTypes::kJoined).has_value()) << header;
                hosts.push_back(std::move(*client));
            }
        }
        // A fifth room of the first address is refused, also when the client puts another address to the left of the one the proxy appended
        auto forged = RoomSocketClient::Connect(port, "X-Forwarded-For: 1.1.1.1, 203.0.113.1\r\n");
        ASSERT_TRUE(forged.has_value());
        forged->Create("Host");
        const auto refused = forged->WaitFor(RoomMessageTypes::kError);
        ASSERT_TRUE(refused.has_value());
        EXPECT_EQ(refused->GetString("code"), RoomErrors::kTooManyRooms);
    }

    // ASan is the assertion: a function that held the clock itself would read freed memory
    TEST(ManualClockTest, AFunctionMadeFromTheClockOutlivesIt) {
        auto clock = std::make_unique<ManualClock>();
        const RoomTime before = clock->Now();
        const std::function<RoomTime()> now = clock->AsFunction();
        clock->Advance(5s);
        clock.reset();

        const RoomTime first = now();
        EXPECT_GE(first - before, 5s);
        EXPECT_GE(now(), first);
    }

}  // namespace BECore::Tests
