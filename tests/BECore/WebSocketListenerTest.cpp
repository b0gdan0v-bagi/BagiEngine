#include "WebSocketTestClient.h"

#include <RemoteControl/WebSocketFrame.h>
#include <RemoteControl/WebSocketListener.h>
#include <gtest/gtest.h>
#include <map>
#include <thread>

namespace BECore::Tests {

    namespace {

        using namespace std::chrono_literals;

        constexpr std::chrono::milliseconds kShortWait{300};

        /// Everything the handler of the test saw, in the order it happened, safe to read from the test thread
        class Recorder {
        public:
            void Add(std::string event) {
                std::lock_guard<std::mutex> lock(_mutex);
                _events.push_back(std::move(event));
            }

            void AddConnection(const WebSocketHandler::ConnectionPtr& connection) {
                std::lock_guard<std::mutex> lock(_mutex);
                _connections.push_back(connection);
            }

            [[nodiscard]] std::vector<std::string> Events() const {
                std::lock_guard<std::mutex> lock(_mutex);
                return _events;
            }

            [[nodiscard]] size_t CountPrefix(const std::string& prefix) const {
                std::lock_guard<std::mutex> lock(_mutex);
                size_t count = 0;
                for (const std::string& event : _events) {
                    count += event.starts_with(prefix) ? 1 : 0;
                }
                return count;
            }

            [[nodiscard]] WebSocketHandler::ConnectionPtr ConnectionAt(size_t index) const {
                std::lock_guard<std::mutex> lock(_mutex);
                return index < _connections.size() ? _connections[index] : nullptr;
            }

            /// The open order of two clients is not the order they connected in: look a connection up by what it is
            [[nodiscard]] WebSocketHandler::ConnectionPtr ConnectionWithClientAddress(const char* address) const {
                std::lock_guard<std::mutex> lock(_mutex);
                for (const WebSocketHandler::ConnectionPtr& connection : _connections) {
                    if (connection && connection->GetClientAddress() == address) {
                        return connection;
                    }
                }
                return nullptr;
            }

        private:
            mutable std::mutex _mutex;
            std::vector<std::string> _events;
            std::vector<WebSocketHandler::ConnectionPtr> _connections;
        };

        class WebSocketListenerTest : public ::testing::Test {
        protected:
            void TearDown() override {
                if (listener) {
                    listener->Stop();
                }
            }

            /// A listener whose handler records every call and answers a text with the same text; "quit" is refused (the connection ends)
            uint16_t Start(WebSocketListenerSettings settings = {}) {
                WebSocketHandler handler;
                handler.onOpen = [this](const WebSocketHandler::ConnectionPtr& connection) {
                    recorder.AddConnection(connection);
                    recorder.Add("open:" + std::to_string(connection->GetId()));
                };
                handler.onMessage = [this](const WebSocketHandler::ConnectionPtr& connection, WebSocketMessage message) {
                    const std::string text(message.GetText().data(), message.GetText().size());
                    recorder.Add("text:" + std::to_string(connection->GetId()) + ":" + text);
                    if (text == "quit") {
                        return false;
                    }
                    connection->QueueText(message.GetText());
                    return true;
                };
                handler.onClosed = [this](const WebSocketHandler::ConnectionPtr& connection) { recorder.Add("closed:" + std::to_string(connection->GetId())); };

                listener = std::make_unique<WebSocketListener>(std::move(settings), std::move(handler));
                const auto port = listener->Start(0);
                EXPECT_TRUE(port.has_value()) << (port.has_value() ? "" : port.error().message.c_str());
                return port.value_or(0);
            }

            /// A client that finished the handshake
            Optional<WebSocketTestClient> Connect(uint16_t port, const std::string& extraHeaders = "") {
                auto client = WebSocketTestClient::Open(port);
                if (client && !client->Handshake(port, extraHeaders)) {
                    ADD_FAILURE() << "the handshake was not accepted";
                    return std::nullopt;
                }
                return client;
            }

            /// The answer to a handshake request of a new client ("" when none came)
            std::string TryHandshake(uint16_t port, const std::string& extraHeaders = "", const std::string& host = "") {
                auto client = WebSocketTestClient::Open(port);
                if (!client) {
                    return {};
                }
                client->Send(HandshakeRequest(port, extraHeaders, host));
                return client->ReadHttpResponse();
            }

            bool WaitForEvents(const std::string& prefix, size_t count) {
                return WaitUntil({}, [&] { return recorder.CountPrefix(prefix) >= count; });
            }

            Recorder recorder;
            std::unique_ptr<WebSocketListener> listener;
        };

    }  // namespace

    TEST_F(WebSocketListenerTest, StartReportsThePortAndStopIsHarmless) {
        WebSocketListener idle({}, {});
        idle.Stop();
        EXPECT_FALSE(idle.IsRunning());

        const uint16_t port = Start();
        EXPECT_NE(port, 0);
        EXPECT_TRUE(listener->IsRunning());
        EXPECT_EQ(listener->GetPort(), port);

        listener->Stop();
        listener->Stop();
        EXPECT_FALSE(listener->IsRunning());
        EXPECT_EQ(listener->GetPort(), 0);
    }

    TEST_F(WebSocketListenerTest, StartingTwiceOrOnATakenPortIsAnError) {
        const uint16_t port = Start();
        const auto again = listener->Start(0);
        ASSERT_FALSE(again.has_value());
        EXPECT_EQ(again.error().code, RemoteControlErrors::kAlreadyRunning);

        WebSocketListener other({}, {});
        const auto taken = other.Start(port);
        ASSERT_FALSE(taken.has_value());
        EXPECT_EQ(taken.error().code, RemoteControlErrors::kListenFailed);
        EXPECT_FALSE(other.IsRunning());
    }

    TEST_F(WebSocketListenerTest, HandlerSeesOpenTextsAndClosedInOrder) {
        const uint16_t port = Start();
        auto client = Connect(port);
        ASSERT_TRUE(client.has_value());

        client->SendText("one");
        client->SendText("two");
        for (const char* expected : {"one", "two"}) {
            const auto frame = client->WaitForFrame();
            ASSERT_TRUE(frame.has_value());
            EXPECT_EQ(frame->opcode, WebSocketOpcode::kText);
            EXPECT_EQ(frame->payload, expected);
        }

        client->Send(AsBytes(EncodeCloseFrame(WebSocketCloseCode::kNormal, "bye", kClientMask)));
        ASSERT_TRUE(client->WaitForEndOfStream());
        ASSERT_TRUE(WaitForEvents("closed:", 1));
        EXPECT_EQ(recorder.Events(), (std::vector<std::string>{"open:1", "text:1:one", "text:1:two", "closed:1"}));
    }

    TEST_F(WebSocketListenerTest, ClosedRunsOncePerConnectionHoweverItEnds) {
        const uint16_t port = Start();
        auto byCloseFrame = Connect(port);
        auto dropped = Connect(port);
        auto brokeProtocol = Connect(port);
        auto refused = Connect(port);
        auto leftToStop = Connect(port);
        ASSERT_TRUE(byCloseFrame && dropped && brokeProtocol && refused && leftToStop);
        ASSERT_TRUE(WaitForEvents("open:", 5));

        byCloseFrame->Send(AsBytes(EncodeCloseFrame(WebSocketCloseCode::kNormal, {}, kClientMask)));
        dropped->Drop();
        brokeProtocol->Send(AsBytes(EncodeTextFrame("unmasked")));
        refused->SendText("quit");

        ASSERT_TRUE(WaitForEvents("closed:", 4));
        const auto protocolFrame = brokeProtocol->WaitForFrame();
        ASSERT_TRUE(protocolFrame.has_value());
        EXPECT_EQ(CloseCodeOf(*protocolFrame), WebSocketCloseCode::kProtocolError);
        EXPECT_EQ(recorder.CountPrefix("closed:"), 4u);

        listener->Stop();
        EXPECT_EQ(recorder.CountPrefix("closed:"), 5u);
        std::this_thread::sleep_for(kShortWait);
        EXPECT_EQ(recorder.CountPrefix("closed:"), 5u) << "a callback ran after Stop() returned";

        std::map<std::string, int> perConnection;
        for (const std::string& event : recorder.Events()) {
            if (event.starts_with("closed:")) {
                ++perConnection[event];
            }
        }
        EXPECT_EQ(perConnection.size(), 5u);
        for (const auto& [event, count] : perConnection) {
            EXPECT_EQ(count, 1) << event;
        }
    }

    TEST_F(WebSocketListenerTest, ConnectionThatNeverFinishedTheHandshakeIsNotReportedAtAll) {
        const uint16_t port = Start();
        auto silent = WebSocketTestClient::Open(port);
        auto halfway = WebSocketTestClient::Open(port);
        ASSERT_TRUE(silent && halfway);
        halfway->Send(std::string("GET / HTT"));
        std::this_thread::sleep_for(50ms);

        listener->Stop();
        EXPECT_TRUE(silent->WaitForEndOfStream());
        EXPECT_TRUE(halfway->WaitForEndOfStream());
        EXPECT_TRUE(recorder.Events().empty());
    }

    TEST_F(WebSocketListenerTest, ClientThatDripsItsRequestIsCutAtTheHandshakeDeadline) {
        WebSocketListenerSettings settings;
        settings.handshakeTimeout = 600ms;
        const uint16_t port = Start(settings);
        auto dripper = WebSocketTestClient::Open(port);
        ASSERT_TRUE(dripper.has_value());

        // One byte now and then, each well inside the timeout, and never the blank line that ends the head: the timeout counts from
        // the connect, not from the last byte, so the server has to hang up although the client never goes quiet for long
        constexpr std::chrono::milliseconds kDripInterval{50};
        const auto start = std::chrono::steady_clock::now();
        const auto giveUp = start + 5 * settings.handshakeTimeout;
        dripper->Send(std::string("GET / HTTP/1.1\r\nX-Slow: "));
        bool hungUp = false;
        while (!hungUp && std::chrono::steady_clock::now() < giveUp) {
            std::this_thread::sleep_for(kDripInterval);
            hungUp = !dripper->TrySend(AsBytes(std::string("a")));
        }
        EXPECT_TRUE(hungUp) << "a client that sends a byte every " << kDripInterval.count() << " ms held its slot past the handshake timeout";
        EXPECT_TRUE(dripper->WaitForEndOfStream());
        EXPECT_TRUE(recorder.Events().empty()) << "the handler must never hear of it";
    }

    TEST_F(WebSocketListenerTest, HandlerThatRefusesAMessageEndsTheConnection) {
        const uint16_t port = Start();
        auto client = Connect(port);
        ASSERT_TRUE(client.has_value());

        client->SendText("quit");
        EXPECT_TRUE(client->WaitForEndOfStream());
        EXPECT_TRUE(WaitForEvents("closed:", 1));
    }

    TEST_F(WebSocketListenerTest, PerAddressLimitRefusesTheNextClientAndFreesTheSlot) {
        WebSocketListenerSettings settings;
        settings.maxConnectionsPerAddress = 2;
        settings.busyMessage = "too many from you";
        const uint16_t port = Start(settings);

        auto first = Connect(port);
        auto second = Connect(port);
        ASSERT_TRUE(first && second);

        const std::string refused = TryHandshake(port);
        EXPECT_TRUE(refused.starts_with("HTTP/1.1 503 Service Unavailable\r\n")) << refused;
        EXPECT_NE(refused.find("too many from you"), std::string::npos);

        // The two are not affected, and when one leaves the slot is free again
        second->Send(AsBytes(EncodeCloseFrame(WebSocketCloseCode::kNormal, {}, kClientMask)));
        ASSERT_TRUE(second->WaitForEndOfStream());
        bool connected = false;
        for (int attempt = 0; attempt < 200 && !connected; ++attempt) {
            connected = TryHandshake(port).starts_with("HTTP/1.1 101 ");
            if (!connected) {
                std::this_thread::sleep_for(10ms);  // the accept thread reaps finished connections every 50 ms
            }
        }
        EXPECT_TRUE(connected);
        first->SendText("still here");
        EXPECT_TRUE(first->WaitForFrame().has_value());
    }

    TEST_F(WebSocketListenerTest, PerAddressLimitOfZeroIsOff) {
        const uint16_t port = Start();
        eastl::vector<WebSocketTestClient> clients;
        for (int i = 0; i < 10; ++i) {
            auto client = Connect(port);
            ASSERT_TRUE(client.has_value()) << i;
            clients.push_back(std::move(*client));
        }
    }

    TEST_F(WebSocketListenerTest, MaxConnectionsRefusesTheNextClient) {
        WebSocketListenerSettings settings;
        settings.maxConnections = 2;
        const uint16_t port = Start(settings);

        auto first = Connect(port);
        auto second = Connect(port);
        ASSERT_TRUE(first && second);
        const std::string refused = TryHandshake(port);
        EXPECT_TRUE(refused.starts_with("HTTP/1.1 503 Service Unavailable\r\n")) << refused;
        EXPECT_EQ(refused.find("control"), std::string::npos) << "the default text names no product";
    }

    TEST_F(WebSocketListenerTest, MessageOverTheLimitClosesWithTooBigAndOneAtTheLimitPasses) {
        WebSocketListenerSettings settings;
        settings.maxMessageBytes = 64;
        const uint16_t port = Start(settings);
        auto client = Connect(port);
        ASSERT_TRUE(client.has_value());

        client->SendText(std::string(64, 'x'));
        const auto echoed = client->WaitForFrame();
        ASSERT_TRUE(echoed.has_value());
        EXPECT_EQ(echoed->payload.size(), 64u);

        // Only the header of a frame that announces more than the limit: the server must not wait for the payload
        eastl::vector<uint8_t> header = {0x81, 0xFE, 0x00, 65};
        header.insert(header.end(), kClientMask.data(), kClientMask.data() + kClientMask.size());
        client->Send(AsBytes(header));
        const auto frame = client->WaitForFrame();
        ASSERT_TRUE(frame.has_value());
        EXPECT_EQ(frame->opcode, WebSocketOpcode::kClose);
        EXPECT_EQ(CloseCodeOf(*frame), WebSocketCloseCode::kMessageTooBig);
        EXPECT_TRUE(client->WaitForEndOfStream());
    }

    TEST_F(WebSocketListenerTest, PingIsAnsweredByTheListenerAndBinaryIsRefused) {
        const uint16_t port = Start();
        auto client = Connect(port);
        ASSERT_TRUE(client.has_value());

        const std::string payload = "are you there";
        client->Send(AsBytes(EncodeFrame(WebSocketOpcode::kPing, AsBytes(payload), kClientMask)));
        const auto pong = client->WaitForFrame();
        ASSERT_TRUE(pong.has_value());
        EXPECT_EQ(pong->opcode, WebSocketOpcode::kPong);
        EXPECT_EQ(pong->payload, payload);

        client->Send(AsBytes(EncodeFrame(WebSocketOpcode::kBinary, AsBytes(std::string("\x01")), kClientMask)));
        const auto close = client->WaitForFrame();
        ASSERT_TRUE(close.has_value());
        EXPECT_EQ(CloseCodeOf(*close), WebSocketCloseCode::kUnsupportedData);
        EXPECT_EQ(recorder.CountPrefix("text:"), 0u);
    }

    TEST_F(WebSocketListenerTest, PublicPolicyChecksTheOriginAndNotTheHost) {
        WebSocketListenerSettings settings;
        settings.bind = SocketBind::AnyAddress;
        settings.policy.loopbackOnly = false;
        settings.policy.allowedOrigins = {"https://good.example"};
        const uint16_t port = Start(settings);

        EXPECT_TRUE(TryHandshake(port, "Origin: https://good.example\r\n", "game.example.net").starts_with("HTTP/1.1 101 "));
        EXPECT_TRUE(TryHandshake(port, "Origin: https://evil.example\r\n", "game.example.net").starts_with("HTTP/1.1 403 Forbidden\r\n"));
        EXPECT_TRUE(TryHandshake(port, "", "game.example.net").starts_with("HTTP/1.1 101 ")) << "a program sends no Origin";
    }

    TEST_F(WebSocketListenerTest, LoopbackPolicyStillRefusesAForeignHost) {
        const uint16_t port = Start();
        EXPECT_TRUE(TryHandshake(port, "", "game.example.net").starts_with("HTTP/1.1 403 Forbidden\r\n"));
    }

    TEST_F(WebSocketListenerTest, PublicListenerAnswersTheHealthCheckAndFreesTheSlot) {
        WebSocketListenerSettings settings;
        settings.bind = SocketBind::AnyAddress;
        settings.policy.loopbackOnly = false;
        settings.policy.healthPath = "/healthz";
        settings.maxConnectionsPerAddress = 1;
        const uint16_t port = Start(settings);

        for (int probe = 0; probe < 3; ++probe) {
            auto client = WebSocketTestClient::Open(port);
            ASSERT_TRUE(client.has_value());
            client->Send(std::string("GET /healthz HTTP/1.1\r\nHost: game.example.net\r\n\r\n"));
            const std::string response = client->ReadHttpResponse();
            EXPECT_TRUE(response.starts_with("HTTP/1.1 200 OK\r\n")) << response;
            EXPECT_TRUE(response.ends_with("\r\n\r\nok")) << response;
            EXPECT_TRUE(client->WaitForEndOfStream());
        }
        EXPECT_TRUE(recorder.Events().empty()) << "the handler must never hear of a health check";

        // Each probe freed its slot (one connection per address is allowed): a client can still connect
        bool connected = false;
        for (int attempt = 0; attempt < 200 && !connected; ++attempt) {
            connected = TryHandshake(port, "", "game.example.net").starts_with("HTTP/1.1 101 ");
            if (!connected) {
                std::this_thread::sleep_for(10ms);  // the accept thread reaps finished connections every 50 ms
            }
        }
        EXPECT_TRUE(connected);
    }

    TEST_F(WebSocketListenerTest, OtherRequestsOfAPublicListenerWithAHealthPathAreJudgedAsBefore) {
        WebSocketListenerSettings settings;
        settings.bind = SocketBind::AnyAddress;
        settings.policy.loopbackOnly = false;
        settings.policy.healthPath = "/healthz";
        const uint16_t port = Start(settings);

        auto other = WebSocketTestClient::Open(port);
        ASSERT_TRUE(other.has_value());
        other->Send(std::string("GET /other HTTP/1.1\r\nHost: game.example.net\r\n\r\n"));
        const std::string refused = other->ReadHttpResponse();
        EXPECT_TRUE(refused.starts_with("HTTP/1.1 400 Bad Request\r\n")) << refused;
        EXPECT_NE(refused.find("expected 'Upgrade: websocket'"), std::string::npos);

        EXPECT_TRUE(TryHandshake(port, "", "game.example.net").starts_with("HTTP/1.1 101 ")) << "a handshake on any path still opens";
    }

    TEST_F(WebSocketListenerTest, LoopbackListenerDoesNotAnswerTheHealthCheck) {
        WebSocketListenerSettings settings;
        settings.policy.healthPath = "/healthz";  // ignored while loopbackOnly is true
        const uint16_t port = Start(settings);

        auto client = WebSocketTestClient::Open(port);
        ASSERT_TRUE(client.has_value());
        client->Send(std::string("GET /healthz HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"));
        const std::string response = client->ReadHttpResponse();
        EXPECT_TRUE(response.starts_with("HTTP/1.1 400 Bad Request\r\n")) << response;
    }

    TEST_F(WebSocketListenerTest, ForEachOpenConnectionVisitsOnlyConnectionsWithADoneHandshake) {
        const uint16_t port = Start();
        auto first = Connect(port);
        auto second = Connect(port);
        auto unfinished = WebSocketTestClient::Open(port);
        ASSERT_TRUE(first && second && unfinished);
        unfinished->Send(std::string("GET / HTT"));
        ASSERT_TRUE(WaitForEvents("open:", 2));

        int visited = 0;
        listener->ForEachOpenConnection([&visited](WebSocketConnection& connection) {
            ++visited;
            connection.QueueText("broadcast");
        });
        EXPECT_EQ(visited, 2);
        for (WebSocketTestClient* client : {&*first, &*second}) {
            const auto frame = client->WaitForFrame();
            ASSERT_TRUE(frame.has_value());
            EXPECT_EQ(frame->payload, "broadcast");
        }

        // A connection that ended is not visited
        first->Send(AsBytes(EncodeCloseFrame(WebSocketCloseCode::kNormal, {}, kClientMask)));
        ASSERT_TRUE(first->WaitForEndOfStream());
        visited = 0;
        listener->ForEachOpenConnection([&visited](WebSocketConnection&) { ++visited; });
        EXPECT_EQ(visited, 1);
    }

    TEST_F(WebSocketListenerTest, StopEndsOpenAndUnfinishedConnectionsPromptly) {
        WebSocketListenerSettings settings;
        const uint16_t port = Start(settings);
        auto open = Connect(port);
        auto silent = WebSocketTestClient::Open(port);
        auto halfway = WebSocketTestClient::Open(port);
        ASSERT_TRUE(open && silent && halfway);
        halfway->Send(std::string("GET / HTT"));
        std::this_thread::sleep_for(50ms);

        const auto before = std::chrono::steady_clock::now();
        listener->Stop();
        EXPECT_LT(std::chrono::steady_clock::now() - before, settings.handshakeTimeout) << "Stop() waited for the handshake timeout";
        EXPECT_TRUE(open->WaitForEndOfStream());
        EXPECT_TRUE(silent->WaitForEndOfStream());
        EXPECT_TRUE(halfway->WaitForEndOfStream());
        EXPECT_EQ(recorder.CountPrefix("closed:"), 1u);
    }

    TEST_F(WebSocketListenerTest, IdsAreUniqueIncreasingAndNotReusedAfterARestart) {
        const uint16_t port = Start();
        for (int i = 0; i < 3; ++i) {
            auto client = Connect(port);
            ASSERT_TRUE(client.has_value());
            ASSERT_TRUE(WaitForEvents("open:", static_cast<size_t>(i) + 1));
        }
        listener->Stop();
        const auto again = listener->Start(0);
        ASSERT_TRUE(again.has_value());
        auto client = Connect(*again);
        ASSERT_TRUE(client.has_value());
        ASSERT_TRUE(WaitForEvents("open:", 4));

        std::vector<uint64_t> ids;
        for (const std::string& event : recorder.Events()) {
            if (event.starts_with("open:")) {
                ids.push_back(std::stoull(event.substr(5)));
            }
        }
        ASSERT_EQ(ids.size(), 4u);
        for (size_t i = 1; i < ids.size(); ++i) {
            EXPECT_GT(ids[i], ids[i - 1]);
        }
    }

    TEST_F(WebSocketListenerTest, ByteCounterGrowsWithEveryReceive) {
        const uint16_t port = Start();
        auto client = Connect(port);
        ASSERT_TRUE(client.has_value());
        ASSERT_TRUE(WaitForEvents("open:", 1));
        const auto connection = recorder.ConnectionAt(0);
        ASSERT_NE(connection, nullptr);

        const uint64_t afterHandshake = connection->GetBytesReceived();
        EXPECT_GE(afterHandshake, HandshakeRequest(port).size());

        const auto frame = EncodeTextFrame("count me", kClientMask);
        client->Send(AsBytes(frame));
        ASSERT_TRUE(WaitUntil({}, [&] { return connection->GetBytesReceived() == afterHandshake + frame.size(); }));
    }

    TEST_F(WebSocketListenerTest, PeerAddressIsTheClientsAddress) {
        const uint16_t port = Start();
        auto client = Connect(port);
        ASSERT_TRUE(client.has_value());
        ASSERT_TRUE(WaitForEvents("open:", 1));
        EXPECT_EQ(recorder.ConnectionAt(0)->GetPeerAddress(), "127.0.0.1");
    }

    TEST_F(WebSocketListenerTest, ClientAddressIsThePeerAddressWithoutTrustedProxies) {
        const uint16_t port = Start();
        auto client = Connect(port, "X-Forwarded-For: 203.0.113.9\r\n");
        ASSERT_TRUE(client.has_value());
        ASSERT_TRUE(WaitForEvents("open:", 1));
        EXPECT_EQ(recorder.ConnectionAt(0)->GetClientAddress(), "127.0.0.1") << "hops 0 never reads the header";
    }

    TEST_F(WebSocketListenerTest, TrustedProxyHopsAdmitsDifferentForwardedAddressesUnderALimitOfOne) {
        WebSocketListenerSettings settings;
        settings.maxConnectionsPerAddress = 1;
        settings.policy.trustedProxyHops = 1;
        settings.busyMessage = "too many from you";
        const uint16_t port = Start(settings);

        auto first = Connect(port, "X-Forwarded-For: 203.0.113.1\r\n");
        auto second = Connect(port, "X-Forwarded-For: 203.0.113.2\r\n");
        ASSERT_TRUE(first && second) << "the socket address is the proxy's: it is not what the limit counts";
        ASSERT_TRUE(WaitForEvents("open:", 2));
        const WebSocketHandler::ConnectionPtr firstOpened = recorder.ConnectionWithClientAddress("203.0.113.1");
        ASSERT_NE(firstOpened, nullptr);
        EXPECT_EQ(firstOpened->GetPeerAddress(), "127.0.0.1");
        EXPECT_NE(recorder.ConnectionWithClientAddress("203.0.113.2"), nullptr);

        // The same forwarded address again is refused after its handshake, before the handler hears of it
        const std::string refused = TryHandshake(port, "X-Forwarded-For: 203.0.113.1\r\n");
        EXPECT_TRUE(refused.starts_with("HTTP/1.1 503 Service Unavailable\r\n")) << refused;
        EXPECT_NE(refused.find("too many from you"), std::string::npos);
        EXPECT_EQ(recorder.CountPrefix("open:"), 2u);

        // A forged left entry changes nothing: the rightmost one is the client
        const std::string forged = TryHandshake(port, "X-Forwarded-For: 203.0.113.2, 203.0.113.1\r\n");
        EXPECT_TRUE(forged.starts_with("HTTP/1.1 503 ")) << forged;

        // The slot of a client that leaves is free again
        first->Send(AsBytes(EncodeCloseFrame(WebSocketCloseCode::kNormal, {}, kClientMask)));
        ASSERT_TRUE(first->WaitForEndOfStream());
        bool connected = false;
        for (int attempt = 0; attempt < 200 && !connected; ++attempt) {
            connected = TryHandshake(port, "X-Forwarded-For: 203.0.113.1\r\n").starts_with("HTTP/1.1 101 ");
            if (!connected) {
                std::this_thread::sleep_for(10ms);
            }
        }
        EXPECT_TRUE(connected);
    }

    TEST_F(WebSocketListenerTest, TrustedProxyHopsFallsBackToThePeerAddressWithoutAUsableEntry) {
        WebSocketListenerSettings settings;
        settings.maxConnectionsPerAddress = 1;
        settings.policy.trustedProxyHops = 2;
        const uint16_t port = Start(settings);

        auto first = Connect(port, "X-Forwarded-For: 203.0.113.9\r\n");  // one entry for two hops
        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(WaitForEvents("open:", 1));
        EXPECT_EQ(recorder.ConnectionAt(0)->GetClientAddress(), "127.0.0.1");
        EXPECT_TRUE(TryHandshake(port, "X-Forwarded-For: not an address, 203.0.113.9\r\n").starts_with("HTTP/1.1 503 ")) << "the peer address is now in use";
        EXPECT_TRUE(TryHandshake(port).starts_with("HTTP/1.1 503 "));
        EXPECT_TRUE(TryHandshake(port, "X-Forwarded-For: 203.0.113.5, 203.0.113.9\r\n").starts_with("HTTP/1.1 101 ")) << "a client with two entries is the first of them";
    }

    TEST_F(WebSocketListenerTest, TrustedProxyHopsWithoutALimitStillTellsTheClientAddress) {
        WebSocketListenerSettings settings;
        settings.policy.trustedProxyHops = 1;
        const uint16_t port = Start(settings);
        auto first = Connect(port, "X-Forwarded-For: 203.0.113.1\r\n");
        auto second = Connect(port, "X-Forwarded-For: 203.0.113.1\r\n");
        ASSERT_TRUE(first && second);
        ASSERT_TRUE(WaitForEvents("open:", 2));
        EXPECT_EQ(recorder.ConnectionAt(1)->GetClientAddress(), "203.0.113.1");
    }

    TEST_F(WebSocketListenerTest, ForwardedHeaderDoesNotSidestepThePerAddressLimitWithoutTrustedProxies) {
        WebSocketListenerSettings settings;
        settings.maxConnectionsPerAddress = 1;
        const uint16_t port = Start(settings);
        auto first = Connect(port, "X-Forwarded-For: 203.0.113.1\r\n");
        ASSERT_TRUE(first.has_value());
        const std::string refused = TryHandshake(port, "X-Forwarded-For: 203.0.113.2\r\n");
        EXPECT_TRUE(refused.starts_with("HTTP/1.1 503 ")) << refused;
    }

}  // namespace BECore::Tests
