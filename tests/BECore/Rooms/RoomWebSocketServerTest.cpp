#include <BECore/Control/Json.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/Backends/ChromeTraceBackend.h>
#include <BECore/Profiler/Profiler.h>
#include <BECore/Rooms/FakeSeatedGame.h>
#include <BECore/Rooms/ManualClock.h>
#include <BECore/Rooms/RoomTableDriver.h>
#include <BECore/Rooms/RoomTestTimings.h>
#include <BECore/Rooms/TestRandom.h>
#include <Common/LogRecorder.h>
#include <Common/Wait.h>
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
            settings.hub.timings = NoComputerDelayTimings();
            settings.pingInterval = 0ms;  // on the real clock a loaded machine would see pings; the liveness cases set their own
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

        /// Waits until the hub thread has read @p clock twice more. An event of a connection reads it once and a tick once, so at least
        /// one of the two is a tick that saw the time as it is now, however long the machine kept the hub thread waiting
        void LetTicksPass(const ManualClock& clock) {
            const uint64_t before = clock.Reads();
            EXPECT_TRUE(WaitUntil([&] { return clock.Reads() >= before + 2; })) << "the hub thread did not tick";
        }

        class RoomWebSocketServerTest : public ::testing::Test {
        protected:
            void SetUp() override {
                ASSERT_TRUE(registry.Register(SeatedGameInfo{"fake", FakeSeatedGame::kSeatCount}, [] { return MakeUnique<FakeSeatedGame>(); }));
            }

            void TearDown() override {
                if (server) {
                    server->Stop();
                }
            }

            uint16_t StartServer(RoomServerSettings settings = MakeSettings()) {
                server = MakeUnique<RoomWebSocketServer>(registry, std::move(settings));
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
            UniquePtr<RoomWebSocketServer> server;
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
        RoomWebSocketServer other(registry, std::move(settings));
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
        EXPECT_NE(joined->text.find(client->token), String::npos);
        EXPECT_EQ(lobby->text.find(client->token), String::npos) << "the token is for its owner only";
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

        const Vector<Pair<String, String>> script = {
            {"{\"v\":1,\"type\":\"ping\"}", "pong"}, {"not json", "error"}, {"{\"v\":1,\"type\":\"ping\"}", "pong"}, {"{\"v\":1,\"type\":\"no_such\"}", "error"},
            {"{\"v\":1,\"type\":\"ping\"}", "pong"},
        };
        Vector<uint8_t> batch;
        for (const auto& [text, reply] : script) {
            const auto frame = EncodeTextFrame(text, kClientMask);
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
        const uint16_t port = StartServer(std::move(settings));

        auto first = RoomSocketClient::Connect(port);
        auto second = RoomSocketClient::Connect(port);
        ASSERT_TRUE(first && second);
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
        Vector<uint8_t> header = {0x81, 0xFE, static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(length)};
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

        client->Raw().Send(AsBytes(EncodeFrame(WebSocketOpcode::kBinary, AsBytes(String("\x01\x02")), kClientMask)));
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
        const uint16_t port = StartServer(std::move(settings));

        auto good = WebSocketTestClient::Open(port);
        ASSERT_TRUE(good.has_value());
        good->Send(HandshakeRequest(port, "Origin: https://good.example\r\n", "rooms.example.net"));
        EXPECT_TRUE(StringView(good->ReadHttpResponse()).starts_with("HTTP/1.1 101 "));

        auto evil = WebSocketTestClient::Open(port);
        ASSERT_TRUE(evil.has_value());
        evil->Send(HandshakeRequest(port, "Origin: https://evil.example\r\n", "rooms.example.net"));
        EXPECT_TRUE(StringView(evil->ReadHttpResponse()).starts_with("HTTP/1.1 403 Forbidden\r\n"));
    }

    TEST_F(RoomWebSocketServerTest, PerAddressLimitRefusesTheNextConnection) {
        RoomServerSettings settings = MakeSettings();
        settings.maxConnectionsPerAddress = 2;
        const uint16_t port = StartServer(std::move(settings));

        auto first = RoomSocketClient::Connect(port);
        auto second = RoomSocketClient::Connect(port);
        ASSERT_TRUE(first && second);

        auto third = WebSocketTestClient::Open(port);
        ASSERT_TRUE(third.has_value());
        third->Send(HandshakeRequest(port));
        EXPECT_TRUE(StringView(third->ReadHttpResponse()).starts_with("HTTP/1.1 503 Service Unavailable\r\n"));

        first->Send(PingMessage{});
        EXPECT_TRUE(first->WaitFor(RoomMessageTypes::kPong).has_value());
    }

    TEST_F(RoomWebSocketServerTest, StopWithOpenConnectionsAndMessagesInFlightReturnsPromptly) {
        const uint16_t port = StartServer();
        Vector<RoomSocketClient> clients;
        for (int i = 0; i < 6; ++i) {
            auto client = RoomSocketClient::Connect(port);
            ASSERT_TRUE(client.has_value());
            clients.push_back(std::move(*client));
        }
        auto silent = WebSocketTestClient::Open(port);
        ASSERT_TRUE(silent.has_value());

        // A flood from every client, the hub busy answering it, and Stop() in the middle
        const Vector<uint8_t> flood = RepeatText("{\"v\":1,\"type\":\"ping\"}", 2000);
        for (RoomSocketClient& client : clients) {
            (void)client.Raw().TrySend(AsBytes(flood));
        }

        server->Stop();
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
        const uint16_t port = StartServer(std::move(settings));

        auto slow = RoomSocketClient::Connect(port);
        auto fast = RoomSocketClient::Connect(port);
        ASSERT_TRUE(slow && fast);

        // The slow client asks and never reads the answers: the system's buffers fill, then its queue passes the cap and it is dropped
        const Vector<uint8_t> chunk = RepeatText("{\"v\":1,\"type\":\"ping\"}", 2000);
        bool dropped = false;
        for (int round = 0; round < 3000 && !dropped; ++round) {
            dropped = !slow->Raw().TrySend(AsBytes(chunk));
            fast->Send(PingMessage{});
            ASSERT_TRUE(fast->WaitFor(RoomMessageTypes::kPong).has_value()) << "the hub stalled in round " << round;
        }
        EXPECT_TRUE(dropped) << "the connection that never reads was not dropped";
    }

    namespace {

        constexpr std::chrono::milliseconds kShutdownGrace{600};
        /// Longer than any stall of a loaded machine, so a Stop() that ends before it did not wait for the grace
        constexpr std::chrono::milliseconds kGraceNoStopWaitsFor{120'000};

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
            // `join` is no message type: each is answered with a `bad_message` error, which is larger than it and spends no failed-join
            // budget. 13 MB of answers in all, far above what the send and receive buffers of a local socket hold together
            constexpr int kChunks = 80;
            const Vector<uint8_t> chunk = RepeatText("{\"v\":1,\"type\":\"join\",\"room\":\"ZZZZ\",\"name\":\"x\"}", 2000);
            for (int i = 0; i < kChunks; ++i) {
                ASSERT_TRUE(client.Raw().TrySend(AsBytes(chunk)));
            }
        }

        /// Settings for FloodWithoutReading(): no rate limit, an outbox that holds every answer
        RoomServerSettings MakeStuckClientSettings(std::chrono::milliseconds grace) {
            RoomServerSettings settings = MakeShutdownSettings(grace);
            settings.maxOutboxBytes = 512 * 1024 * 1024;
            settings.hub.rateBurst = 100'000'000;
            settings.hub.ratePerSecond = 100'000'000.0;
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
        const uint16_t port = StartServer(MakeShutdownSettings(kGraceNoStopWaitsFor));
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

        EXPECT_LT(elapsed, kGraceNoStopWaitsFor) << "Stop() waited for the grace although the client had answered";
        EXPECT_TRUE(host->Raw().WaitForEndOfStream());
    }

    TEST_F(RoomWebSocketServerTest, StopWithNoConnectionsDoesNotWaitForTheGrace) {
        StartServer(MakeShutdownSettings(kGraceNoStopWaitsFor));
        EXPECT_LT(TimeStop(*server), kGraceNoStopWaitsFor);
    }

    TEST_F(RoomWebSocketServerTest, StopWaitsForTheGraceForAClientThatDoesNotReadAndThenCutsIt) {
        const uint16_t port = StartServer(MakeStuckClientSettings(kShutdownGrace));
        auto stuck = RoomSocketClient::Connect(port);
        ASSERT_TRUE(stuck.has_value());
        FloodWithoutReading(*stuck);

        const auto elapsed = TimeStop(*server);

        // Not before the grace: the close frame is behind the answers that the client does not take
        EXPECT_GE(elapsed, kShutdownGrace - 50ms);
        EXPECT_FALSE(server->IsRunning());
        EXPECT_TRUE(stuck->Raw().WaitForEndOfStream());
    }

    TEST_F(RoomWebSocketServerTest, GraceZeroSendsNoCloseFrameAndReturnsAtOnce) {
        const uint16_t port = StartServer(MakeShutdownSettings(0ms));
        auto host = MakeHost(port);
        ASSERT_TRUE(host.has_value());

        server->Stop();

        EXPECT_FALSE(host->WaitForClose(kShortWait).has_value());
        EXPECT_TRUE(host->Raw().WaitForEndOfStream());
    }

    TEST_F(RoomWebSocketServerTest, AConnectionThatOpensDuringTheDrainIsClosedWithGoingAwayToo) {
        constexpr std::chrono::milliseconds kDrainGrace{2000};
        const uint16_t port = StartServer(MakeStuckClientSettings(kDrainGrace));
        auto stuck = RoomSocketClient::Connect(port);
        ASSERT_TRUE(stuck.has_value());
        FloodWithoutReading(*stuck);

        std::chrono::milliseconds elapsed{0};
        std::thread stopping([&] { elapsed = TimeStop(*server); });
        std::this_thread::sleep_for(150ms);

        auto late = RoomSocketClient::Connect(port);
        Optional<RawFrame> close;
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

        server.reset();
        const auto close = next->WaitForClose();
        ASSERT_TRUE(close.has_value());
        EXPECT_EQ(CloseCodeOf(*close), WebSocketCloseCode::kGoingAway);
    }

    TEST_F(RoomWebSocketServerTest, StopRepeatedWithClientsComingAndGoingNeverHangs) {
        for (int round = 0; round < 5; ++round) {
            const uint16_t port = StartServer(MakeShutdownSettings(300ms));
            Vector<RoomSocketClient> clients;
            for (int i = 0; i < 4; ++i) {
                auto client = RoomSocketClient::Connect(port);
                ASSERT_TRUE(client.has_value());
                clients.push_back(std::move(*client));
            }
            clients[0].Raw().Drop();  // leaves at about the moment the server stops
            server->Stop();
            for (size_t i = 1; i < clients.size(); ++i) {
                EXPECT_TRUE(clients[i].Raw().WaitForEndOfStream());
            }
        }
    }

    namespace {

        constexpr std::chrono::milliseconds kPingInterval{1000};
        constexpr std::chrono::milliseconds kClockStep{250};
        /// How long a step waits for the ping of its tick before the clock moves on; a ping that comes later counts for a later step
        constexpr std::chrono::milliseconds kStepPingWait{200};

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

        /// Reads frames until a ping arrives (text frames before it are skipped); false when none does or the connection ends first
        bool WaitForPing(RoomSocketClient& client, std::chrono::milliseconds limit = kWaitLimit) {
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

        /// Moves the clock on by kClockStep at a time, each step seen by a tick of the hub, until a ping reaches @p client, and tells how far
        /// it moved; nullopt when no ping came for @p limit of clock time and kWaitLimit after it. A ping that is slow to arrive is counted
        /// at a later step, so the answer is never shorter than the silence the hub waited for
        Optional<std::chrono::milliseconds> AdvanceUntilPing(ManualClock& clock, RoomSocketClient& client, std::chrono::milliseconds limit = 3 * kPingInterval) {
            std::chrono::milliseconds advanced{0};
            while (advanced < limit) {
                clock.Advance(kClockStep);
                advanced += kClockStep;
                LetTicksPass(clock);
                if (WaitForPing(client, kStepPingWait)) {
                    return advanced;
                }
            }
            if (WaitForPing(client)) {
                return advanced;
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
        bool ReadUntil(RoomSocketClient& client, Predicate done, std::chrono::milliseconds limit = kWaitLimit) {
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
            LetTicksPass(clock);  // the hub thread has to look at every clock value, or the guest would be dropped without the ping before it
        }
        // Three intervals have passed by now, so the lobby of the drop is only a matter of delivery
        EXPECT_TRUE(ReadUntil(*host, [&] { return SawAwayHuman(*host); })) << "the host never saw the silent guest go away";

        EXPECT_TRUE(WaitForPing(*guest));
        EXPECT_TRUE(guest->Raw().WaitForEndOfStream());
        RoundTrip(*host);
        EXPECT_FALSE(host->GetCloseFrame().has_value());
    }

    TEST_F(RoomWebSocketServerTest, ClientThatSendsRegularlyIsNeverPinged) {
        ManualClock clock;
        const uint16_t port = StartServer(MakeLivenessSettings(clock));
        auto client = RoomSocketClient::Connect(port);
        ASSERT_TRUE(client.has_value());
        RoundTrip(*client);
        LetTicksPass(clock);

        for (int step = 0; step < 16; ++step) {
            clock.Advance(kClockStep);  // four intervals in all
            RoundTrip(*client);
        }
        LetTicksPass(clock);
        EXPECT_FALSE(client->GetCloseFrame().has_value());
        EXPECT_EQ(client->GetPingCount(), 0u);
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
        LetTicksPass(clock);
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

        /// The stats lines (`Room server: <n> connections, <m> rooms`) that LogEvent::Flush() delivers while it lives
        class StatsLines {
        public:
            /// Delivers the pending log entries, then the lines so far
            [[nodiscard]] const Vector<String>& Lines() {
                LogEvent::Flush();
                _lines = _log.Messages(LogLevel::Info, {"Room server: ", " connections, ", " rooms"});
                return _lines;
            }

            bool WaitForCount(size_t count) {
                return WaitUntil([&] { return Lines().size() >= count; });
            }

        private:
            LogRecorder _log;
            Vector<String> _lines;
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
        LetTicksPass(clock);
        EXPECT_TRUE(lines.Lines().empty()) << "the interval has not passed";

        clock.Advance(kStatsInterval + 100ms);
        ASSERT_TRUE(lines.WaitForCount(1));
        EXPECT_NE(lines.Lines()[0].find("1 connections, 1 rooms"), String::npos) << lines.Lines()[0];

        clock.Advance(5 * kStatsInterval);
        LetTicksPass(clock);
        EXPECT_EQ(lines.Lines().size(), 1u) << "nothing changed: no second line";

        auto second = RoomSocketClient::Connect(port);
        ASSERT_TRUE(second.has_value());
        RoundTrip(*second);
        ASSERT_TRUE(lines.WaitForCount(2));
        EXPECT_NE(lines.Lines()[1].find("2 connections, 1 rooms"), String::npos) << lines.Lines()[1];

        auto third = RoomSocketClient::Connect(port);
        ASSERT_TRUE(third.has_value());
        RoundTrip(*third);
        LetTicksPass(clock);
        EXPECT_EQ(lines.Lines().size(), 2u) << "a change inside the interval waits for it";
        clock.Advance(kStatsInterval + 100ms);
        ASSERT_TRUE(lines.WaitForCount(3));
        EXPECT_NE(lines.Lines()[2].find("3 connections, 1 rooms"), String::npos) << lines.Lines()[2];

        for (const String& line : lines.Lines()) {
            EXPECT_EQ(line.find("Host"), String::npos) << "no name in a log line: " << line;
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
        LetTicksPass(clock);
        EXPECT_TRUE(lines.Lines().empty());
    }

    // ---- Per-address rules: rooms, the lobby timeout, an address behind a proxy -----------------------------------------------------

    TEST_F(RoomWebSocketServerTest, FifthRoomFromOneAddressIsRefusedAndTheFourStayIntact) {
        RoomServerSettings settings = MakeSettings();
        settings.hub.maxRoomsPerAddress = 4;
        const uint16_t port = StartServer(std::move(settings));

        Vector<RoomSocketClient> hosts;
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
        EXPECT_NE(refused->GetString("message").find("your address"), String::npos);

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
        const uint16_t port = StartServer(std::move(settings));
        Vector<RoomSocketClient> hosts;
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
        settings.hub.lobbyTimeout = 1min;
        const uint16_t port = StartServer(std::move(settings));
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
        const uint16_t port = StartServer(std::move(settings));

        Vector<RoomSocketClient> hosts;
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
        auto clock = MakeUnique<ManualClock>();
        const RoomTime before = clock->Now();
        const MoveOnlyFunction<RoomTime()> now = clock->AsFunction();
        clock->Advance(5s);
        clock.reset();

        const RoomTime first = now();
        EXPECT_GE(first - before, 5s);
        EXPECT_GE(now(), first);
    }

#if BE_PROFILER

    namespace {

        /// Records into a recorder of its own while the server runs; the server is stopped (its threads joined) before the backend goes, since a zone ends on the backend it began on
        class RoomServerZonesTest : public RoomWebSocketServerTest {
        protected:
            void SetUp() override {
                RoomWebSocketServerTest::SetUp();
                _previous = Profiler::GetPrimaryBackend();
                AllocTracker::ResetZoneStats();
                Profiler::SetBackend(&recorder);
            }

            void TearDown() override {
                RoomWebSocketServerTest::TearDown();
                Profiler::SetBackend(_previous);
            }

            /// The zone names that began on a thread of each name (a connection has a `Room reader` and a `Room writer` thread of its own)
            using ZoneThreads = Map<String, Set<String>>;

            /// Checks that every thread's B and E events pair up, and returns where each zone ran
            ZoneThreads ReadTrace() const {
                const auto parsed = ParseJson(recorder.WriteJson());
                const Json* events = parsed ? FindJsonMember(*parsed, "traceEvents") : nullptr;
                EXPECT_NE(events, nullptr);
                ZoneThreads result;
                if (events == nullptr) {
                    return result;
                }
                Map<int64_t, String> threadNames;
                for (const Json& event : *events) {
                    const Json* args = FindJsonMember(event, "args");
                    if (GetJsonString(event, "name") == "thread_name" && args != nullptr) {
                        threadNames[GetJsonInt(event, "tid").value_or(-1)] = GetJsonString(*args, "name").value_or("");
                    }
                }
                Map<int64_t, int> depth;
                for (const Json& event : *events) {
                    const String phase = GetJsonString(event, "ph").value_or("");
                    const int64_t tid = GetJsonInt(event, "tid").value_or(-1);
                    if (phase == "B") {
                        ++depth[tid];
                        result[GetJsonString(event, "name").value_or("")].insert(threadNames[tid]);
                    } else if (phase == "E") {
                        EXPECT_GT(depth[tid]--, 0) << "an E without its B on thread " << tid;
                    }
                }
                for (const auto& [tid, open] : depth) {
                    EXPECT_EQ(open, 0) << "zones left open on thread " << tid;
                }
                return result;
            }

            ChromeTraceBackend recorder;

        private:
            IProfilerBackend* _previous = nullptr;
        };

        /// The threads a zone began on, comma-separated in name order; empty when the zone is not in the trace
        String ThreadsOf(const Map<String, Set<String>>& zones, const char* zone) {
            String joined;
            if (const auto found = zones.find(String(zone)); found != zones.end()) {
                for (const String& thread : found->second) {
                    joined += joined.empty() ? "" : ",";
                    joined += thread;
                }
            }
            return joined;
        }

    }  // namespace

    TEST_F(RoomServerZonesTest, AHandPlayedOverTheWireIsInTheZonesOfTheThreadsThatDidTheWork) {
        const uint16_t port = StartServer();
        auto host = MakeHost(port);
        ASSERT_TRUE(host.has_value());
        {
            auto guest = RoomSocketClient::Connect(port);
            ASSERT_TRUE(guest.has_value());
            guest->JoinByName(host->roomCode, "Guest");
            ASSERT_TRUE(guest->WaitFor(RoomMessageTypes::kLobby).has_value());
            guest->Send(PingMessage{});
            ASSERT_TRUE(guest->WaitFor(RoomMessageTypes::kPong).has_value());
        }
        // The guest's socket is gone: the host hears of it from the lobby that lists one human, which the hub sends after its Closed event
        bool guestGone = false;
        for (int lobbies = 0; lobbies < 8 && !guestGone; ++lobbies) {
            const auto lobby = host->WaitFor(RoomMessageTypes::kLobby);
            ASSERT_TRUE(lobby.has_value());
            guestGone = CountHumans(*lobby) == 1 && lobbies > 0;
        }
        EXPECT_TRUE(guestGone);
        server->Stop();

        const auto zones = ReadTrace();
        for (const char* hubZone : {"Room open", "Room message", "Room close", "Room tick", "Room send"}) {
            EXPECT_EQ(ThreadsOf(zones, hubZone), "Room hub") << hubZone;
        }
        EXPECT_EQ(ThreadsOf(zones, "WS read"), "Room reader");
        EXPECT_EQ(ThreadsOf(zones, "WS dispatch"), "Room reader");
        EXPECT_EQ(ThreadsOf(zones, "WS write"), "Room writer");
        // The hub encodes what it sends; a reader may encode a pong or a close frame too
        EXPECT_NE(ThreadsOf(zones, "WS encode").find("Room hub"), String::npos);

#if BE_PROFILER_ALLOC
        // What the exchange allocated is charged to named zones: the hub's handling, the reader's parse and dispatch, the encode
        Map<String, uint64_t> allocationsOf;
        for (const AllocSiteStats& row : AllocTracker::GetNameStats()) {
            allocationsOf[row.ZoneLabel()] += row.count;
        }
        for (const char* zone : {"Room message", "WS read", "WS dispatch", "WS encode"}) {
            EXPECT_GT(allocationsOf[zone], 0u) << zone;
        }
#endif
    }

#endif

}  // namespace BECore::Tests
