#include "FakeSeatedGame.h"
#include "ManualClock.h"
#include "RoomSocketClient.h"
#include "TestRandom.h"

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
            const auto frame = EncodeTextFrame(eastl::string_view(text.data(), text.size()), kClientMask);
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
            std::optional<RoomSocketClient> MakeHost(uint16_t port, eastl::string_view name = "Host") {
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
            const auto frame = EncodeTextFrame(eastl::string_view(text.data(), text.size()), kClientMask);
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
        std::optional<std::chrono::milliseconds> AdvanceUntilPing(ManualClock& clock, RoomSocketClient& client, std::chrono::milliseconds limit = 3 * kPingInterval) {
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

}  // namespace BECore::Tests
