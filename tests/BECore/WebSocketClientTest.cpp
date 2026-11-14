#include "Tls/FakeTlsStream.h"
#include "WebSocketTestClient.h"

#include <RemoteControl/WebSocketClient.h>
#include <RemoteControl/WebSocketHandshake.h>
#include <RemoteControl/WebSocketListener.h>
#include <gtest/gtest.h>
#include <set>
#include <thread>

namespace BECore::Tests {

    namespace {

        using namespace std::chrono_literals;

        constexpr std::chrono::milliseconds kShortWait{100};
        constexpr std::chrono::milliseconds kShortTimeout{200};
        constexpr std::chrono::seconds kNeverTimeout{60};  // a timeout the test must not wait for: the thing under test has to end earlier

        [[nodiscard]] std::string ToStd(const String& text) {
            return std::string(text.c_str(), text.size());
        }

        /// A client and the events it produced, polled the way a game does it
        struct Session {
            WebSocketClient client;
            std::vector<WebSocketClientEvent> events;

            void Pump() {
                WebSocketClientEvent event;
                while (client.PollEvent(event)) {
                    events.push_back(std::move(event));
                }
            }

            [[nodiscard]] size_t Count(WebSocketClientEventKind kind) const {
                return static_cast<size_t>(std::count_if(events.begin(), events.end(), [kind](const WebSocketClientEvent& event) { return event.kind == kind; }));
            }

            bool WaitForCount(WebSocketClientEventKind kind, size_t count, std::chrono::milliseconds limit = kClientWaitLimit) {
                return WaitUntil([this] { Pump(); }, [&] { return Count(kind) >= count; }, limit);
            }

            bool WaitForOpened() {
                return WaitForCount(WebSocketClientEventKind::Opened, 1);
            }

            bool WaitForClosed() {
                return WaitForCount(WebSocketClientEventKind::Closed, 1);
            }

            [[nodiscard]] const WebSocketClientEvent& Closed() const {
                for (const WebSocketClientEvent& event : events) {
                    if (event.kind == WebSocketClientEventKind::Closed) {
                        return event;
                    }
                }
                ADD_FAILURE() << "no Closed event";
                static const WebSocketClientEvent none;
                return none;
            }

            [[nodiscard]] std::vector<std::string> Texts() const {
                std::vector<std::string> texts;
                for (const WebSocketClientEvent& event : events) {
                    if (event.kind == WebSocketClientEventKind::Text) {
                        texts.push_back(ToStd(event.text));
                    }
                }
                return texts;
            }

            /// Waits for Closed and then checks that nothing more happened: it is the only one and the last
            void ExpectSingleClosed() {
                ASSERT_TRUE(WaitForClosed());
                const auto until = std::chrono::steady_clock::now() + kShortWait;
                while (std::chrono::steady_clock::now() < until) {
                    Pump();
                    std::this_thread::sleep_for(1ms);
                }
                EXPECT_EQ(Count(WebSocketClientEventKind::Closed), 1u);
                EXPECT_EQ(events.back().kind, WebSocketClientEventKind::Closed) << "Closed has to be the last event";
            }
        };

        /// A frame the client sent, as the bytes said (masked, with its key)
        struct ClientFrame {
            uint8_t opcode = 0;
            std::string payload;  // unmasked
            bool masked = false;
            WebSocketMask key{};
        };

        /// A hand-written server: a listening socket that accepts one connection and speaks the bytes the test tells it to
        class ScriptedServer {
        public:
            ScriptedServer() {
                auto listener = Socket::Listen(0);
                EXPECT_TRUE(listener.has_value());
                if (listener) {
                    _listener = std::move(*listener);
                }
            }

            [[nodiscard]] uint16_t GetPort() const {
                return _listener.GetLocalPort();
            }

            [[nodiscard]] std::string Url(const std::string& path = "/") const {
                return "ws://127.0.0.1:" + std::to_string(GetPort()) + path;
            }

            /// A `wss://` address of this server; @p host is the name in it (localhost reaches the same loopback)
            [[nodiscard]] std::string TlsUrl(const std::string& host = "127.0.0.1", const std::string& path = "/") const {
                return "wss://" + host + ":" + std::to_string(GetPort()) + path;
            }

            /// Takes the connection of the client (no byte is read)
            bool AcceptConnection() {
                auto accepted = _listener.Accept(kClientWaitLimit);
                if (!accepted) {
                    ADD_FAILURE() << "no client connected";
                    return false;
                }
                _peer = std::move(*accepted);
                _peer.SetReceiveTimeout(5ms);
                return true;
            }

            /// The server's end of the fake TLS handshake (FakeTlsStream.h): from here on every byte is masked both ways when the
            /// client was accepted; @p rejectReason refuses it. What the hello said is in TlsHello()
            bool FakeTlsHandshake(const std::string& rejectReason = {}) {
                _fakeTls = _tlsHello.Handshake(_peer, rejectReason);
                _peer.SetReceiveTimeout(5ms);  // the handshake changed it
                return _fakeTls;
            }

            [[nodiscard]] const FakeTlsServerSide& TlsHello() const {
                return _tlsHello;
            }

            /// Takes the connection of the client and reads its opening request; the head is in Request()
            bool AcceptAndReadRequest() {
                return AcceptConnection() && ReadRequest();
            }

            /// Reads the opening request (after the fake TLS handshake, when there is one)
            bool ReadRequest() {
                const bool read = WaitUntil({}, [&] {
                    Fill();
                    return _buffer.find("\r\n\r\n") != std::string::npos;
                });
                if (!read) {
                    ADD_FAILURE() << "no request head";
                    return false;
                }
                const size_t end = _buffer.find("\r\n\r\n") + 4;
                _request = _buffer.substr(0, end);
                _buffer.erase(0, end);
                return true;
            }

            [[nodiscard]] const std::string& Request() const {
                return _request;
            }

            /// The value of the request header @p name ("" when there is none)
            [[nodiscard]] std::string RequestHeader(const std::string& name) const {
                const std::string needle = "\r\n" + name + ": ";
                const size_t at = _request.find(needle);
                if (at == std::string::npos) {
                    return {};
                }
                const size_t start = at + needle.size();
                return _request.substr(start, _request.find("\r\n", start) - start);
            }

            [[nodiscard]] std::string AcceptResponse(const std::string& acceptValue) const {
                return "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + acceptValue + "\r\n\r\n";
            }

            /// The right answer to the request that was read; @p extraBytes follow it in the same send
            void Accept(const std::string& extraBytes = "") {
                const std::string key = RequestHeader("Sec-WebSocket-Key");
                const String accept = ComputeAcceptKey(String(key.c_str(), key.size()));
                SendRaw(AcceptResponse(ToStd(accept)) + extraBytes);
            }

            void SendRaw(const std::string& bytes) {
                Send(Vector<uint8_t>(bytes.data(), bytes.data() + bytes.size()));
            }

            void SendFrame(uint8_t opcode, const std::string& payload, Optional<WebSocketMask> mask = std::nullopt) {
                Send(EncodeFrame(opcode, Span<const uint8_t>(reinterpret_cast<const uint8_t*>(payload.data()), payload.size()), mask));
            }

            void SendText(const std::string& text) {
                SendFrame(WebSocketOpcode::kText, text);
            }

            void SendClose(uint16_t code, const std::string& reason) {
                Send(EncodeCloseFrame(code, FromStd(reason)));
            }

            /// The next frame of the client (masked frames are expected; the key is kept), nullopt when none came in @p limit
            Optional<ClientFrame> ReadFrame(std::chrono::milliseconds limit = kClientWaitLimit) {
                Optional<ClientFrame> frame;
                WaitUntil(
                    {},
                    [&] {
                        Fill();
                        frame = ParseBuffered();
                        return frame.has_value();
                    },
                    limit);
                return frame;
            }

            /// True when the client ended the connection (everything it sent before was read)
            bool WaitForEndOfStream() {
                return WaitUntil({}, [&] {
                    Fill();
                    return _ended;
                });
            }

            void Drop() {
                _peer.ShutdownBoth();
                _peer.Close();
            }

        private:
            void Send(Vector<uint8_t> bytes) {
                if (_fakeTls) {
                    FakeTlsMask(bytes.data(), bytes.size());
                }
                EXPECT_TRUE(_peer.SendAll(Span<const uint8_t>(bytes.data(), bytes.size())));
            }

            void Fill() {
                std::array<uint8_t, 4096> chunk;
                for (int i = 0; i < 1024; ++i) {
                    const auto received = _peer.Receive(Span<uint8_t>(chunk.data(), chunk.size()));
                    if (!received) {
                        if (received.error() != SocketError::TimedOut) {
                            _ended = true;
                        }
                        return;
                    }
                    if (_fakeTls) {
                        FakeTlsMask(chunk.data(), *received);
                    }
                    _buffer.append(reinterpret_cast<const char*>(chunk.data()), *received);
                }
            }

            Optional<ClientFrame> ParseBuffered() {
                if (_buffer.size() < 2) {
                    return std::nullopt;
                }
                const auto byteAt = [this](size_t index) { return static_cast<uint8_t>(_buffer[index]); };
                ClientFrame frame;
                frame.opcode = byteAt(0) & 0x0F;
                frame.masked = (byteAt(1) & 0x80) != 0;
                size_t length = byteAt(1) & 0x7F;
                size_t offset = 2;
                if (length == 126 || length == 127) {
                    const size_t lengthBytes = length == 126 ? 2 : 8;
                    if (_buffer.size() < 2 + lengthBytes) {
                        return std::nullopt;
                    }
                    length = 0;
                    for (size_t i = 0; i < lengthBytes; ++i) {
                        length = (length << 8) | byteAt(2 + i);
                    }
                    offset += lengthBytes;
                }
                if (frame.masked) {
                    if (_buffer.size() < offset + frame.key.size()) {
                        return std::nullopt;
                    }
                    for (size_t i = 0; i < frame.key.size(); ++i) {
                        frame.key[i] = byteAt(offset + i);
                    }
                    offset += frame.key.size();
                }
                if (_buffer.size() < offset + length) {
                    return std::nullopt;
                }
                frame.payload = _buffer.substr(offset, length);
                if (frame.masked) {
                    for (size_t i = 0; i < frame.payload.size(); ++i) {
                        frame.payload[i] = static_cast<char>(static_cast<uint8_t>(frame.payload[i]) ^ frame.key[i % frame.key.size()]);
                    }
                }
                _buffer.erase(0, offset + length);
                return frame;
            }

            Socket _listener;
            Socket _peer;
            std::string _buffer;
            std::string _request;
            bool _ended = false;
            bool _fakeTls = false;  // the fake handshake was accepted: the bytes are masked both ways
            FakeTlsServerSide _tlsHello;
        };

        [[nodiscard]] uint16_t CloseCodeOfFrame(const ClientFrame& frame) {
            return static_cast<uint16_t>((static_cast<uint8_t>(frame.payload[0]) << 8) | static_cast<uint8_t>(frame.payload[1]));
        }

        /// A real WebSocketListener that echoes every text and counts what happened
        class EchoServer {
        public:
            explicit EchoServer(WebSocketListenerSettings settings = {}) {
                WebSocketHandler handler;
                handler.onOpen = [this](const WebSocketHandler::ConnectionPtr& connection) {
                    std::lock_guard<std::mutex> lock(_mutex);
                    _connections.push_back(connection);
                    ++_opened;
                };
                handler.onMessage = [this](const WebSocketHandler::ConnectionPtr& connection, WebSocketMessage message) {
                    connection->QueueText(message.GetText());
                    return true;
                };
                handler.onClosed = [this](const WebSocketHandler::ConnectionPtr&) {
                    std::lock_guard<std::mutex> lock(_mutex);
                    ++_closed;
                };
                _listener = std::make_unique<WebSocketListener>(std::move(settings), std::move(handler));
                const auto port = _listener->Start(0);
                EXPECT_TRUE(port.has_value());
                _port = port.value_or(0);
            }

            ~EchoServer() {
                _listener->Stop();
            }

            [[nodiscard]] std::string Url() const {
                return "ws://127.0.0.1:" + std::to_string(_port) + "/rooms";
            }

            [[nodiscard]] uint16_t GetPort() const {
                return _port;
            }

            [[nodiscard]] int Opened() const {
                std::lock_guard<std::mutex> lock(_mutex);
                return _opened;
            }

            [[nodiscard]] int Closed() const {
                std::lock_guard<std::mutex> lock(_mutex);
                return _closed;
            }

            [[nodiscard]] WebSocketHandler::ConnectionPtr Connection(size_t index) const {
                std::lock_guard<std::mutex> lock(_mutex);
                return index < _connections.size() ? _connections[index] : nullptr;
            }

            void Stop() {
                _listener->Stop();
            }

        private:
            std::unique_ptr<WebSocketListener> _listener;
            uint16_t _port = 0;
            mutable std::mutex _mutex;
            int _opened = 0;
            int _closed = 0;
            std::vector<WebSocketHandler::ConnectionPtr> _connections;
        };

        [[nodiscard]] WebSocketClientSettings QuickSettings() {
            WebSocketClientSettings settings;
            settings.handshakeTimeout = kShortTimeout;
            settings.closeTimeout = kShortTimeout;
            return settings;
        }

        /// Settings that reach a `wss://` server through the fake backend of FakeTlsStream.h
        [[nodiscard]] WebSocketClientSettings FakeTlsSettings() {
            WebSocketClientSettings settings = QuickSettings();
            settings.tls.factory = FakeTlsFactory();
            return settings;
        }

        /// Walks a server through the fake TLS handshake and the opening request of the client that was opened on it
        [[nodiscard]] bool AcceptFakeTlsAndRequest(ScriptedServer& server) {
            return server.AcceptConnection() && server.FakeTlsHandshake() && server.ReadRequest();
        }

    }  // namespace

    TEST(WebSocketClientTest, ExchangesTextWithARealServerInOrder) {
        EchoServer server;
        Session session;
        ASSERT_TRUE(session.client.Open(server.Url().c_str()).has_value());
        ASSERT_TRUE(session.WaitForOpened());
        EXPECT_TRUE(session.client.IsOpen());
        EXPECT_EQ(session.events.front().kind, WebSocketClientEventKind::Opened);

        constexpr int kCount = 100;
        for (int i = 0; i < kCount; ++i) {
            const std::string text = "message " + std::to_string(i);
            ASSERT_TRUE(session.client.SendText(FromStd(text)));
        }
        ASSERT_TRUE(session.WaitForCount(WebSocketClientEventKind::Text, kCount));

        const std::vector<std::string> texts = session.Texts();
        ASSERT_EQ(texts.size(), static_cast<size_t>(kCount));
        for (int i = 0; i < kCount; ++i) {
            EXPECT_EQ(texts[static_cast<size_t>(i)], "message " + std::to_string(i));
        }
        EXPECT_GT(session.client.GetBytesReceived(), 0u);
    }

    TEST(WebSocketClientTest, TheRequestHasTheRightFormAndNoOriginByDefault) {
        ScriptedServer server;
        Session session;
        ASSERT_TRUE(session.client.Open(server.Url("/rooms?lang=en").c_str()).has_value());
        ASSERT_TRUE(server.AcceptAndReadRequest());

        EXPECT_TRUE(server.Request().starts_with("GET /rooms?lang=en HTTP/1.1\r\n"));
        EXPECT_EQ(server.RequestHeader("Host"), "127.0.0.1:" + std::to_string(server.GetPort()));
        EXPECT_EQ(server.RequestHeader("Sec-WebSocket-Version"), "13");
        EXPECT_EQ(server.RequestHeader("Origin"), "");
        EXPECT_EQ(server.Request().find("Origin"), std::string::npos);
    }

    TEST(WebSocketClientTest, BytesThatCameWithTheHandshakeAreRead) {
        ScriptedServer server;
        Session session;
        ASSERT_TRUE(session.client.Open(server.Url().c_str()).has_value());
        ASSERT_TRUE(server.AcceptAndReadRequest());

        const Vector<uint8_t> frame = EncodeTextFrame("first");
        server.Accept(std::string(reinterpret_cast<const char*>(frame.data()), frame.size()));
        ASSERT_TRUE(session.WaitForCount(WebSocketClientEventKind::Text, 1));
        EXPECT_EQ(session.Texts().front(), "first");
    }

    TEST(WebSocketClientTest, EveryFrameIsMaskedWithAFreshKey) {
        ScriptedServer server;
        Session session;
        ASSERT_TRUE(session.client.Open(server.Url().c_str()).has_value());
        ASSERT_TRUE(server.AcceptAndReadRequest());
        server.Accept();
        ASSERT_TRUE(session.WaitForOpened());

        constexpr int kCount = 16;
        for (int i = 0; i < kCount; ++i) {
            ASSERT_TRUE(session.client.SendText("hello"));
        }
        Set<uint32_t> keys;
        for (int i = 0; i < kCount; ++i) {
            const auto frame = server.ReadFrame();
            ASSERT_TRUE(frame.has_value());
            EXPECT_EQ(frame->opcode, WebSocketOpcode::kText);
            EXPECT_TRUE(frame->masked);
            EXPECT_EQ(frame->payload, "hello");
            keys.insert(static_cast<uint32_t>(frame->key[0]) << 24 | static_cast<uint32_t>(frame->key[1]) << 16 | static_cast<uint32_t>(frame->key[2]) << 8 | frame->key[3]);
        }
        EXPECT_GT(keys.size(), 1u) << "16 frames with one key";
    }

    TEST(WebSocketClientTest, ThePingOfTheServerIsAnsweredWithAMaskedPong) {
        ScriptedServer server;
        Session session;
        ASSERT_TRUE(session.client.Open(server.Url().c_str()).has_value());
        ASSERT_TRUE(server.AcceptAndReadRequest());
        server.Accept();
        ASSERT_TRUE(session.WaitForOpened());

        server.SendFrame(WebSocketOpcode::kPing, "are you there");
        const auto pong = server.ReadFrame();
        ASSERT_TRUE(pong.has_value());
        EXPECT_EQ(pong->opcode, WebSocketOpcode::kPong);
        EXPECT_TRUE(pong->masked);
        EXPECT_EQ(pong->payload, "are you there");
        EXPECT_TRUE(session.client.IsOpen());
        EXPECT_EQ(session.Count(WebSocketClientEventKind::Text), 0u);
    }

    TEST(WebSocketClientTest, TheCloseOfTheServerIsEchoedAndReported) {
        ScriptedServer server;
        Session session;
        ASSERT_TRUE(session.client.Open(server.Url().c_str()).has_value());
        ASSERT_TRUE(server.AcceptAndReadRequest());
        server.Accept();
        ASSERT_TRUE(session.WaitForOpened());

        server.SendText("last words");
        server.SendClose(WebSocketCloseCode::kGoingAway, "restarting");
        const auto echo = server.ReadFrame();
        ASSERT_TRUE(echo.has_value());
        EXPECT_EQ(echo->opcode, WebSocketOpcode::kClose);
        EXPECT_EQ(CloseCodeOfFrame(*echo), WebSocketCloseCode::kGoingAway);

        session.ExpectSingleClosed();
        EXPECT_EQ(session.Closed().closeCode, WebSocketCloseCode::kGoingAway);
        EXPECT_EQ(ToStd(session.Closed().reason), "restarting");
        EXPECT_EQ(session.Texts(), std::vector<std::string>{"last words"}) << "a message that arrived before the close is delivered before Closed";
        EXPECT_FALSE(session.client.IsOpen());
        EXPECT_TRUE(server.WaitForEndOfStream());
    }

    TEST(WebSocketClientTest, CloseOfTheClientEndsCleanlyAgainstARealServer) {
        EchoServer server;
        Session session;
        ASSERT_TRUE(session.client.Open(server.Url().c_str()).has_value());
        ASSERT_TRUE(session.WaitForOpened());

        EXPECT_TRUE(session.client.SendText("one"));
        session.client.Close(WebSocketCloseCode::kNormal, "done");
        EXPECT_FALSE(session.client.IsOpen());
        EXPECT_FALSE(session.client.SendText("too late"));

        session.ExpectSingleClosed();
        EXPECT_EQ(session.Closed().closeCode, WebSocketCloseCode::kNormal);
        EXPECT_EQ(ToStd(session.Closed().reason), "done");
        EXPECT_TRUE(WaitUntil({}, [&] { return server.Closed() == 1; })) << "the server saw the connection end";
        EXPECT_EQ(session.Texts(), std::vector<std::string>{"one"}) << "what was sent before the close is delivered and answered";
    }

    TEST(WebSocketClientTest, CloseSendsTheCodeAndWaitsOnlyCloseTimeoutForAnAnswer) {
        ScriptedServer server;
        Session session;
        ASSERT_TRUE(session.client.Open(server.Url().c_str(), QuickSettings()).has_value());
        ASSERT_TRUE(server.AcceptAndReadRequest());
        server.Accept();
        ASSERT_TRUE(session.WaitForOpened());

        session.client.Close(4000, "bye");
        const auto frame = server.ReadFrame();
        ASSERT_TRUE(frame.has_value());
        EXPECT_EQ(frame->opcode, WebSocketOpcode::kClose);
        EXPECT_EQ(CloseCodeOfFrame(*frame), 4000);
        EXPECT_EQ(frame->payload.substr(2), "bye");

        // The server stays silent: the connection is aborted after closeTimeout
        session.ExpectSingleClosed();
        EXPECT_EQ(session.Closed().closeCode, 4000);
        EXPECT_TRUE(server.WaitForEndOfStream());
    }

    TEST(WebSocketClientTest, TheAnswerToOurCloseCompletesTheHandshake) {
        ScriptedServer server;
        Session session;
        WebSocketClientSettings settings;
        settings.closeTimeout = kNeverTimeout;  // the answer has to end it, not the timeout
        ASSERT_TRUE(session.client.Open(server.Url().c_str(), settings).has_value());
        ASSERT_TRUE(server.AcceptAndReadRequest());
        server.Accept();
        ASSERT_TRUE(session.WaitForOpened());

        session.client.Close();
        ASSERT_TRUE(server.ReadFrame().has_value());
        server.SendClose(WebSocketCloseCode::kNormal, "");
        session.ExpectSingleClosed();
        EXPECT_EQ(session.Closed().closeCode, WebSocketCloseCode::kNormal);
    }

    TEST(WebSocketClientTest, ADroppedConnectionIsClosed1006Once) {
        ScriptedServer server;
        Session session;
        ASSERT_TRUE(session.client.Open(server.Url().c_str()).has_value());
        ASSERT_TRUE(server.AcceptAndReadRequest());
        server.Accept();
        ASSERT_TRUE(session.WaitForOpened());

        server.Drop();
        session.ExpectSingleClosed();
        EXPECT_EQ(session.Closed().closeCode, WebSocketCloseCode::kAbnormalClosure);
        EXPECT_FALSE(session.Closed().reason.empty());
        EXPECT_FALSE(session.client.IsOpen());
        EXPECT_FALSE(session.client.SendText("anybody there"));
    }

    TEST(WebSocketClientTest, AServerThatIsStoppedEndsTheConnection) {
        EchoServer server;
        Session session;
        ASSERT_TRUE(session.client.Open(server.Url().c_str()).has_value());
        ASSERT_TRUE(session.WaitForOpened());

        server.Stop();
        session.ExpectSingleClosed();
        EXPECT_EQ(session.Closed().closeCode, WebSocketCloseCode::kAbnormalClosure);
    }

    TEST(WebSocketClientTest, AConnectionRefusedIsClosed) {
        uint16_t port = 0;
        {
            auto listener = Socket::Listen(0);
            ASSERT_TRUE(listener.has_value());
            port = listener->GetLocalPort();
        }  // the port is free again, nobody listens
        Session session;
        const std::string url = "ws://127.0.0.1:" + std::to_string(port);
        ASSERT_TRUE(session.client.Open(url.c_str()).has_value());
        session.ExpectSingleClosed();
        EXPECT_EQ(session.Closed().closeCode, WebSocketCloseCode::kAbnormalClosure);
        EXPECT_NE(ToStd(session.Closed().reason).find(std::to_string(port)), std::string::npos) << "the reason names the port";
        EXPECT_EQ(session.Count(WebSocketClientEventKind::Opened), 0u);
    }

    TEST(WebSocketClientTest, AnAnswerThatIsNotAHandshakeIsClosedWithAReason) {
        struct Case {
            const char* name;
            std::function<std::string(const ScriptedServer&)> response;
            const char* expectedInReason;
        };
        const std::vector<Case> cases = {
            {"wrong accept", [](const ScriptedServer& server) { return server.AcceptResponse("AAAAAAAAAAAAAAAAAAAAAAAAAAA="); }, "Accept"},
            {"not 101", [](const ScriptedServer&) { return std::string("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n"); }, "403"},
            {"not http", [](const ScriptedServer&) { return std::string("SSH-2.0-OpenSSH_9.0\r\n\r\n"); }, "HTTP"},
        };
        for (const Case& item : cases) {
            SCOPED_TRACE(item.name);
            ScriptedServer server;
            Session session;
            ASSERT_TRUE(session.client.Open(server.Url().c_str()).has_value());
            ASSERT_TRUE(server.AcceptAndReadRequest());
            server.SendRaw(item.response(server));
            session.ExpectSingleClosed();
            EXPECT_EQ(session.Closed().closeCode, WebSocketCloseCode::kAbnormalClosure);
            EXPECT_NE(ToStd(session.Closed().reason).find(item.expectedInReason), std::string::npos) << ToStd(session.Closed().reason);
            EXPECT_EQ(session.Count(WebSocketClientEventKind::Opened), 0u);
            EXPECT_TRUE(server.WaitForEndOfStream()) << "the client leaves a server that refused it";
        }
    }

    TEST(WebSocketClientTest, ATooLongAnswerHeadIsRefused) {
        ScriptedServer server;
        Session session;
        ASSERT_TRUE(session.client.Open(server.Url().c_str()).has_value());
        ASSERT_TRUE(server.AcceptAndReadRequest());
        server.SendRaw("HTTP/1.1 101 Switching Protocols\r\nX-Padding: " + std::string(kMaxHandshakeBytes + 100, 'a'));
        session.ExpectSingleClosed();
        EXPECT_NE(ToStd(session.Closed().reason).find("too long"), std::string::npos);
    }

    TEST(WebSocketClientTest, AServerThatNeverAnswersTheHandshakeTimesOut) {
        ScriptedServer server;
        Session session;
        WebSocketClientSettings settings;
        settings.handshakeTimeout = 150ms;
        ASSERT_TRUE(session.client.Open(server.Url().c_str(), settings).has_value());
        ASSERT_TRUE(server.AcceptAndReadRequest());  // and then it says nothing
        session.ExpectSingleClosed();
        EXPECT_EQ(session.Closed().closeCode, WebSocketCloseCode::kAbnormalClosure);
        EXPECT_NE(ToStd(session.Closed().reason).find("did not answer"), std::string::npos) << ToStd(session.Closed().reason);
    }

    TEST(WebSocketClientTest, AMaskedFrameOfTheServerIsAProtocolError) {
        ScriptedServer server;
        Session session;
        ASSERT_TRUE(session.client.Open(server.Url().c_str()).has_value());
        ASSERT_TRUE(server.AcceptAndReadRequest());
        server.Accept();
        ASSERT_TRUE(session.WaitForOpened());

        server.SendFrame(WebSocketOpcode::kText, "masked", kClientMask);
        const auto close = server.ReadFrame();
        ASSERT_TRUE(close.has_value());
        EXPECT_EQ(close->opcode, WebSocketOpcode::kClose);
        EXPECT_EQ(CloseCodeOfFrame(*close), WebSocketCloseCode::kProtocolError);
        session.ExpectSingleClosed();
        EXPECT_EQ(session.Closed().closeCode, WebSocketCloseCode::kProtocolError);
        EXPECT_EQ(session.Count(WebSocketClientEventKind::Text), 0u);
    }

    TEST(WebSocketClientTest, AMessageOverTheLimitIsCloseWith1009) {
        ScriptedServer server;
        Session session;
        WebSocketClientSettings settings = QuickSettings();
        settings.maxMessageBytes = 100;
        ASSERT_TRUE(session.client.Open(server.Url().c_str(), settings).has_value());
        ASSERT_TRUE(server.AcceptAndReadRequest());
        server.Accept();
        ASSERT_TRUE(session.WaitForOpened());

        server.SendText(std::string(101, 'x'));
        const auto close = server.ReadFrame();
        ASSERT_TRUE(close.has_value());
        EXPECT_EQ(CloseCodeOfFrame(*close), WebSocketCloseCode::kMessageTooBig);
        session.ExpectSingleClosed();
        EXPECT_EQ(session.Closed().closeCode, WebSocketCloseCode::kMessageTooBig);
        EXPECT_EQ(session.Count(WebSocketClientEventKind::Text), 0u);
    }

    TEST(WebSocketClientTest, AMessageAtTheLimitPasses) {
        ScriptedServer server;
        Session session;
        WebSocketClientSettings settings;
        settings.maxMessageBytes = 100;
        ASSERT_TRUE(session.client.Open(server.Url().c_str(), settings).has_value());
        ASSERT_TRUE(server.AcceptAndReadRequest());
        server.Accept();
        ASSERT_TRUE(session.WaitForOpened());
        server.SendText(std::string(100, 'x'));
        ASSERT_TRUE(session.WaitForCount(WebSocketClientEventKind::Text, 1));
        EXPECT_EQ(session.Texts().front().size(), 100u);
    }

    TEST(WebSocketClientTest, ABinaryMessageIsClosedWith1003) {
        ScriptedServer server;
        Session session;
        ASSERT_TRUE(session.client.Open(server.Url().c_str(), QuickSettings()).has_value());
        ASSERT_TRUE(server.AcceptAndReadRequest());
        server.Accept();
        ASSERT_TRUE(session.WaitForOpened());

        server.SendFrame(WebSocketOpcode::kBinary, "\x01\x02");
        const auto close = server.ReadFrame();
        ASSERT_TRUE(close.has_value());
        EXPECT_EQ(CloseCodeOfFrame(*close), WebSocketCloseCode::kUnsupportedData);
        session.ExpectSingleClosed();
        EXPECT_EQ(session.Closed().closeCode, WebSocketCloseCode::kUnsupportedData);
    }

    TEST(WebSocketClientTest, AFullInboxStopsTheReaderAndPollingResumesIt) {
        constexpr int kMessages = 100;
        constexpr size_t kMessageBytes = 128 * 1024;  // 12.5 MiB in all: more than the socket buffers hold, so a stopped reader shows
        WebSocketListenerSettings serverSettings;
        serverSettings.maxOutboxBytes = 64 * 1024 * 1024;
        serverSettings.maxMessageBytes = 1024 * 1024;
        EchoServer server(serverSettings);
        Session session;
        WebSocketClientSettings settings;
        settings.maxInboxEvents = 4;
        ASSERT_TRUE(session.client.Open(server.Url().c_str(), settings).has_value());
        ASSERT_TRUE(session.WaitForOpened());

        const auto connection = server.Connection(0);
        ASSERT_TRUE(connection != nullptr);
        for (int i = 0; i < kMessages; ++i) {
            const std::string text = std::to_string(i) + ":" + std::string(kMessageBytes, 'x');
            ASSERT_TRUE(connection->QueueText(FromStd(text)));
        }

        // Nothing is polled: the reader stops after a few messages and the bytes it took stay far below what was sent
        uint64_t before = 0;
        ASSERT_TRUE(WaitUntil({}, [&] {
            const uint64_t now = session.client.GetBytesReceived();
            const bool steady = now > 0 && now == before;
            before = now;
            std::this_thread::sleep_for(100ms);
            return steady;
        }));
        EXPECT_LT(session.client.GetBytesReceived(), static_cast<uint64_t>(kMessages) * kMessageBytes / 2);

        // Taking the events lets it go on: every message arrives, in order
        ASSERT_TRUE(session.WaitForCount(WebSocketClientEventKind::Text, kMessages));
        const std::vector<std::string> texts = session.Texts();
        for (int i = 0; i < kMessages; ++i) {
            EXPECT_TRUE(texts[static_cast<size_t>(i)].starts_with(std::to_string(i) + ":")) << "message " << i;
        }
    }

    TEST(WebSocketClientTest, AbortDuringTheHandshakeEndsAtOnce) {
        ScriptedServer server;
        Session session;
        WebSocketClientSettings settings;
        settings.handshakeTimeout = kNeverTimeout;
        ASSERT_TRUE(session.client.Open(server.Url().c_str(), settings).has_value());
        ASSERT_TRUE(server.AcceptAndReadRequest());  // the reader now waits for an answer that never comes

        const auto started = std::chrono::steady_clock::now();
        session.client.Abort();
        session.ExpectSingleClosed();
        EXPECT_LT(std::chrono::steady_clock::now() - started, 5s);
        EXPECT_EQ(session.Closed().closeCode, WebSocketCloseCode::kAbnormalClosure);
        EXPECT_TRUE(server.WaitForEndOfStream());
    }

    TEST(WebSocketClientTest, AbortBeforeTheConnectionIsMadeStillGivesOneClosed) {
        // The abort races with the connect: whichever side wins, one Closed comes
        for (int round = 0; round < 10; ++round) {
            ScriptedServer server;
            Session session;
            WebSocketClientSettings settings;
            settings.handshakeTimeout = kNeverTimeout;
            ASSERT_TRUE(session.client.Open(server.Url().c_str(), settings).has_value());
            session.client.Abort();
            session.ExpectSingleClosed();
        }
    }

    TEST(WebSocketClientTest, AbortWhileOpenWakesTheBlockedRead) {
        EchoServer server;
        Session session;
        WebSocketClientSettings settings;
        settings.closeTimeout = kNeverTimeout;
        ASSERT_TRUE(session.client.Open(server.Url().c_str(), settings).has_value());
        ASSERT_TRUE(session.WaitForOpened());

        session.client.Abort();
        session.ExpectSingleClosed();
        EXPECT_EQ(session.Closed().closeCode, WebSocketCloseCode::kAbnormalClosure);
        EXPECT_FALSE(session.client.IsOpen());
        EXPECT_TRUE(WaitUntil({}, [&] { return server.Closed() == 1; }));
    }

    TEST(WebSocketClientTest, AbortWakesAReaderWaitingForRoomInTheInbox) {
        ScriptedServer server;
        Session session;
        WebSocketClientSettings settings;
        settings.maxInboxEvents = 1;
        ASSERT_TRUE(session.client.Open(server.Url().c_str(), settings).has_value());
        ASSERT_TRUE(server.AcceptAndReadRequest());
        server.Accept();
        // The Opened event fills the inbox; the texts wait behind it
        server.SendText("a");
        server.SendText("b");
        std::this_thread::sleep_for(kShortWait);
        session.client.Abort();
        session.ExpectSingleClosed();
        EXPECT_TRUE(session.Texts().size() <= 1);
    }

    TEST(WebSocketClientTest, TheDestructorEndsAClientInEveryState) {
        {  // never opened
            WebSocketClient client;
        }
        {  // Open() refused (no backend) or a connect that fails
            WebSocketClient client;
            EXPECT_EQ(client.Open("wss://127.0.0.1:1/").has_value(), IsTlsSupported());
        }
        {  // connecting: the server accepts and says nothing
            ScriptedServer server;
            WebSocketClient client;
            WebSocketClientSettings settings;
            settings.handshakeTimeout = kNeverTimeout;
            ASSERT_TRUE(client.Open(server.Url().c_str(), settings).has_value());
            ASSERT_TRUE(server.AcceptAndReadRequest());
        }
        {  // open, a read is blocked
            EchoServer server;
            Session session;
            WebSocketClientSettings settings;
            settings.closeTimeout = kNeverTimeout;
            ASSERT_TRUE(session.client.Open(server.Url().c_str(), settings).has_value());
            ASSERT_TRUE(session.WaitForOpened());
        }
        {  // closing: our close frame is sent and the server never answers
            ScriptedServer server;
            Session session;
            WebSocketClientSettings settings;
            settings.closeTimeout = kNeverTimeout;
            ASSERT_TRUE(session.client.Open(server.Url().c_str(), settings).has_value());
            ASSERT_TRUE(server.AcceptAndReadRequest());
            server.Accept();
            ASSERT_TRUE(session.WaitForOpened());
            session.client.Close();
            ASSERT_TRUE(server.ReadFrame().has_value());
        }
        {  // ended
            ScriptedServer server;
            Session session;
            ASSERT_TRUE(session.client.Open(server.Url().c_str()).has_value());
            ASSERT_TRUE(server.AcceptAndReadRequest());
            server.Accept();
            ASSERT_TRUE(session.WaitForOpened());
            server.Drop();
            ASSERT_TRUE(session.WaitForClosed());
        }
        {  // the reader waits for room in the inbox
            ScriptedServer server;
            WebSocketClient client;
            WebSocketClientSettings settings;
            settings.maxInboxEvents = 1;
            ASSERT_TRUE(client.Open(server.Url().c_str(), settings).has_value());
            ASSERT_TRUE(server.AcceptAndReadRequest());
            server.Accept();
            server.SendText("a");
            server.SendText("b");
            std::this_thread::sleep_for(100ms);
        }
    }

    TEST(WebSocketClientTest, OpenAgainAfterClosed) {
        EchoServer first;
        EchoServer second;
        Session session;
        ASSERT_TRUE(session.client.Open(first.Url().c_str()).has_value());
        ASSERT_TRUE(session.WaitForOpened());
        session.client.Close();
        session.ExpectSingleClosed();
        session.events.clear();

        ASSERT_TRUE(session.client.Open(second.Url().c_str()).has_value());
        ASSERT_TRUE(session.WaitForOpened());
        EXPECT_TRUE(session.client.IsOpen());
        ASSERT_TRUE(session.client.SendText("again"));
        ASSERT_TRUE(session.WaitForCount(WebSocketClientEventKind::Text, 1));
        EXPECT_EQ(session.Texts().front(), "again");
        EXPECT_EQ(second.Opened(), 1);

        session.client.Close();
        session.ExpectSingleClosed();
        EXPECT_EQ(session.Count(WebSocketClientEventKind::Opened), 1u);
    }

    TEST(WebSocketClientTest, OpenAgainRightAfterAClosedWasTakenNeverFailsForTiming) {
        EchoServer server;
        Session session;
        for (int round = 0; round < 10; ++round) {
            session.events.clear();
            ASSERT_TRUE(session.client.Open(server.Url().c_str()).has_value()) << "round " << round;
            ASSERT_TRUE(session.WaitForOpened());
            session.client.Close();
            ASSERT_TRUE(session.WaitForClosed());
        }
    }

    TEST(WebSocketClientTest, OpenWhileAConnectionRunsIsRefused) {
        EchoServer server;
        Session session;
        ASSERT_TRUE(session.client.Open(server.Url().c_str()).has_value());
        const auto again = session.client.Open(server.Url().c_str());
        ASSERT_FALSE(again.has_value());
        ASSERT_TRUE(session.WaitForOpened());
        EXPECT_FALSE(session.client.Open(server.Url().c_str()).has_value());
        EXPECT_TRUE(session.client.IsOpen());
    }

    TEST(WebSocketClientTest, WssAndBadUrlsAreRefusedAtOnceWithoutEvents) {
        Session session;
        if (!IsTlsSupported()) {  // with a backend `wss://` is accepted and fails later (the TLS tests)
            const auto tls = session.client.Open("wss://example.com/rooms");
            ASSERT_FALSE(tls.has_value());
            EXPECT_TRUE(ToStd(tls.error()).starts_with("tls_unsupported"));
        }

        for (const char* url : {"http://example.com", "ws://", "ws://host:99999", "ws://[::1]:80/", "ws://a b/"}) {
            SCOPED_TRACE(url);
            EXPECT_FALSE(session.client.Open(url).has_value());
        }
        std::this_thread::sleep_for(50ms);
        session.Pump();
        EXPECT_TRUE(session.events.empty());
        EXPECT_FALSE(session.client.IsOpen());
    }

    TEST(WebSocketClientTest, SendTextNeedsAnOpenConnectionAndRespectsTheCap) {
        ScriptedServer server;
        Session session;
        WebSocketClientSettings settings;
        settings.maxOutboxBytes = 1024;
        EXPECT_FALSE(session.client.SendText("before open"));
        ASSERT_TRUE(session.client.Open(server.Url().c_str(), settings).has_value());
        EXPECT_FALSE(session.client.SendText("while connecting"));
        ASSERT_TRUE(server.AcceptAndReadRequest());
        server.Accept();
        ASSERT_TRUE(session.WaitForOpened());

        EXPECT_TRUE(session.client.SendText("fits"));
        EXPECT_FALSE(session.client.SendText(std::string(2000, 'x').c_str())) << "one message over the cap of an empty queue is refused";
        EXPECT_TRUE(session.client.IsOpen()) << "and the connection stays";
        const auto frame = server.ReadFrame();
        ASSERT_TRUE(frame.has_value());
        EXPECT_EQ(frame->payload, "fits");
    }

    TEST(WebSocketClientTest, AServerThatDoesNotReadGetsTheConnectionDropped) {
        ScriptedServer server;
        Session session;
        WebSocketClientSettings settings;
        settings.maxOutboxBytes = 512 * 1024;
        ASSERT_TRUE(session.client.Open(server.Url().c_str(), settings).has_value());
        ASSERT_TRUE(server.AcceptAndReadRequest());
        server.Accept();  // and from now on it reads nothing
        ASSERT_TRUE(session.WaitForOpened());

        // The socket buffers take some megabytes; then the writer blocks and the queue fills up to its cap
        const std::string text(100 * 1024, 'x');
        bool refused = false;
        for (int i = 0; i < 2000 && !refused; ++i) {
            refused = !session.client.SendText(FromStd(text));
            if (!refused && i % 16 == 15) {
                std::this_thread::sleep_for(1ms);  // let the writer run, a queue that is merely not drained yet is no proof
            }
        }
        EXPECT_TRUE(refused);
        session.ExpectSingleClosed();
        EXPECT_EQ(session.Closed().closeCode, WebSocketCloseCode::kAbnormalClosure);
        EXPECT_NE(ToStd(session.Closed().reason).find("does not read"), std::string::npos) << ToStd(session.Closed().reason);
    }

    TEST(WebSocketClientTest, TheOriginSettingReachesTheServersPolicy) {
        WebSocketListenerSettings serverSettings;
        serverSettings.policy.loopbackOnly = false;
        serverSettings.policy.allowedOrigins = {"https://game.example"};
        EchoServer server(serverSettings);

        {  // a page of the list
            Session session;
            WebSocketClientSettings settings;
            settings.origin = "https://game.example";
            ASSERT_TRUE(session.client.Open(server.Url().c_str(), settings).has_value());
            EXPECT_TRUE(session.WaitForOpened());
        }
        {  // a program sends none and gets in
            Session session;
            ASSERT_TRUE(session.client.Open(server.Url().c_str()).has_value());
            EXPECT_TRUE(session.WaitForOpened());
        }
        {  // another page does not
            Session session;
            WebSocketClientSettings settings;
            settings.origin = "https://evil.example";
            ASSERT_TRUE(session.client.Open(server.Url().c_str(), settings).has_value());
            session.ExpectSingleClosed();
            EXPECT_NE(ToStd(session.Closed().reason).find("403"), std::string::npos) << ToStd(session.Closed().reason);
            EXPECT_EQ(session.Count(WebSocketClientEventKind::Opened), 0u);
        }
    }

    TEST(WebSocketClientTest, WssOverTheFakeBackendOpensAndExchangesText) {
        ScriptedServer server;
        Session session;
        ASSERT_TRUE(session.client.Open(server.TlsUrl().c_str(), FakeTlsSettings()).has_value());
        ASSERT_TRUE(AcceptFakeTlsAndRequest(server));
        EXPECT_EQ(server.RequestHeader("Host"), "127.0.0.1:" + std::to_string(server.GetPort()));
        server.Accept();
        ASSERT_TRUE(session.WaitForOpened());
        EXPECT_TRUE(session.client.IsOpen());

        ASSERT_TRUE(session.client.SendText("to the server"));
        const auto frame = server.ReadFrame();
        ASSERT_TRUE(frame.has_value());
        EXPECT_EQ(frame->opcode, WebSocketOpcode::kText);
        EXPECT_TRUE(frame->masked);
        EXPECT_EQ(frame->payload, "to the server");

        server.SendText("to the client");
        server.SendText("and again");
        ASSERT_TRUE(session.WaitForCount(WebSocketClientEventKind::Text, 2));
        EXPECT_EQ(session.Texts(), (std::vector<std::string>{"to the client", "and again"}));
    }

    TEST(WebSocketClientTest, PingPongAndTheCloseHandshakeWorkOverTheFakeBackend) {
        ScriptedServer server;
        Session session;
        ASSERT_TRUE(session.client.Open(server.TlsUrl().c_str(), FakeTlsSettings()).has_value());
        ASSERT_TRUE(AcceptFakeTlsAndRequest(server));
        server.Accept();
        ASSERT_TRUE(session.WaitForOpened());

        server.SendFrame(WebSocketOpcode::kPing, "are you there");
        const auto pong = server.ReadFrame();
        ASSERT_TRUE(pong.has_value());
        EXPECT_EQ(pong->opcode, WebSocketOpcode::kPong);
        EXPECT_EQ(pong->payload, "are you there");

        session.client.Close(WebSocketCloseCode::kNormal, "done");
        const auto close = server.ReadFrame();
        ASSERT_TRUE(close.has_value());
        EXPECT_EQ(close->opcode, WebSocketOpcode::kClose);
        EXPECT_EQ(CloseCodeOfFrame(*close), WebSocketCloseCode::kNormal);
        server.SendClose(WebSocketCloseCode::kNormal, "");
        session.ExpectSingleClosed();
        EXPECT_EQ(session.Closed().closeCode, WebSocketCloseCode::kNormal);
    }

    TEST(WebSocketClientTest, TheCloseOfTheServerEndsAWssConnection) {
        ScriptedServer server;
        Session session;
        ASSERT_TRUE(session.client.Open(server.TlsUrl().c_str(), FakeTlsSettings()).has_value());
        ASSERT_TRUE(AcceptFakeTlsAndRequest(server));
        server.Accept();
        ASSERT_TRUE(session.WaitForOpened());

        server.SendClose(WebSocketCloseCode::kGoingAway, "restart");
        const auto echoed = server.ReadFrame();
        ASSERT_TRUE(echoed.has_value());
        EXPECT_EQ(echoed->opcode, WebSocketOpcode::kClose);
        session.ExpectSingleClosed();
        EXPECT_EQ(session.Closed().closeCode, WebSocketCloseCode::kGoingAway);
        EXPECT_EQ(ToStd(session.Closed().reason), "restart");
    }

    TEST(WebSocketClientTest, ARejectedTlsHandshakeIsClosed1006WithTheReason) {
        ScriptedServer server;
        Session session;
        ASSERT_TRUE(session.client.Open(server.TlsUrl().c_str(), FakeTlsSettings()).has_value());
        ASSERT_TRUE(server.AcceptConnection());
        EXPECT_FALSE(server.FakeTlsHandshake("certificate is not trusted for 127.0.0.1"));
        session.ExpectSingleClosed();
        EXPECT_EQ(session.Closed().closeCode, WebSocketCloseCode::kAbnormalClosure);
        EXPECT_NE(ToStd(session.Closed().reason).find("certificate is not trusted for 127.0.0.1"), std::string::npos) << ToStd(session.Closed().reason);
        EXPECT_EQ(session.Count(WebSocketClientEventKind::Opened), 0u);
        EXPECT_FALSE(session.client.IsOpen());
    }

    TEST(WebSocketClientTest, ATlsConnectionRefusedIsClosed1006) {
        uint16_t closedPort = 0;
        {
            ScriptedServer server;  // its port is free again when it is gone
            closedPort = server.GetPort();
        }
        Session session;
        ASSERT_TRUE(session.client.Open(("wss://127.0.0.1:" + std::to_string(closedPort) + "/").c_str(), FakeTlsSettings()).has_value());
        session.ExpectSingleClosed();
        EXPECT_EQ(session.Closed().closeCode, WebSocketCloseCode::kAbnormalClosure);
        EXPECT_NE(ToStd(session.Closed().reason).find("127.0.0.1:" + std::to_string(closedPort)), std::string::npos) << ToStd(session.Closed().reason);
    }

    TEST(WebSocketClientTest, TheStreamGetsTheHostOfTheUrlAsItsServerName) {
        ScriptedServer server;
        Session session;
        ASSERT_TRUE(session.client.Open(server.TlsUrl("localhost").c_str(), FakeTlsSettings()).has_value());
        ASSERT_TRUE(AcceptFakeTlsAndRequest(server));
        EXPECT_EQ(server.TlsHello().serverName, "localhost");
        EXPECT_EQ(server.TlsHello().anchorCount, 0u);
        server.Accept();
        ASSERT_TRUE(session.WaitForOpened());
    }

    TEST(WebSocketClientTest, TheAdditionalTrustAnchorsReachTheStream) {
        ScriptedServer server;
        Session session;
        WebSocketClientSettings settings = FakeTlsSettings();
        settings.tls.additionalTrustAnchorsDer = {{0x30, 0x01}, {0x30, 0x02}, {0x30, 0x03}};
        ASSERT_TRUE(session.client.Open(server.TlsUrl().c_str(), settings).has_value());
        ASSERT_TRUE(AcceptFakeTlsAndRequest(server));
        EXPECT_EQ(server.TlsHello().anchorCount, 3u);
        server.Accept();
        ASSERT_TRUE(session.WaitForOpened());
    }

    TEST(WebSocketClientTest, ATlsHandshakeThatNeverAnswersEndsAtTheHandshakeTimeout) {
        ScriptedServer server;
        Session session;
        WebSocketClientSettings settings = FakeTlsSettings();
        settings.handshakeTimeout = 150ms;
        settings.connectTimeout = kNeverTimeout;
        ASSERT_TRUE(session.client.Open(server.TlsUrl().c_str(), settings).has_value());
        ASSERT_TRUE(server.AcceptConnection());  // and then it says nothing to the hello
        session.ExpectSingleClosed();
        EXPECT_EQ(session.Closed().closeCode, WebSocketCloseCode::kAbnormalClosure);
        EXPECT_NE(ToStd(session.Closed().reason).find("timed out"), std::string::npos) << ToStd(session.Closed().reason);
    }

    TEST(WebSocketClientTest, TheTlsHandshakeAndTheOpeningRequestShareOneDeadline) {
        // The server finishes the TLS handshake after 1 s and then never answers the request. With the deadline of 1.2 s for both the
        // client gives up 0.2 s later; a second 1.2 s for the request would end it after 2.2 s
        constexpr auto kHandshakeTimeout = 1200ms;
        constexpr auto kTlsDelay = 1000ms;
        ScriptedServer server;
        Session session;
        WebSocketClientSettings settings = FakeTlsSettings();
        settings.handshakeTimeout = kHandshakeTimeout;
        const auto started = std::chrono::steady_clock::now();
        ASSERT_TRUE(session.client.Open(server.TlsUrl().c_str(), settings).has_value());
        ASSERT_TRUE(server.AcceptConnection());
        std::this_thread::sleep_for(kTlsDelay);
        ASSERT_TRUE(server.FakeTlsHandshake());
        ASSERT_TRUE(server.ReadRequest());
        session.ExpectSingleClosed();
        EXPECT_LT(std::chrono::steady_clock::now() - started, kHandshakeTimeout + 700ms);
        EXPECT_NE(ToStd(session.Closed().reason).find("did not answer"), std::string::npos) << ToStd(session.Closed().reason);
    }

    TEST(WebSocketClientTest, AbortEndsAWssConnectionInEveryState) {
        {  // in the TLS handshake: the server accepted the connection and says nothing
            ScriptedServer server;
            Session session;
            WebSocketClientSettings settings = FakeTlsSettings();
            settings.handshakeTimeout = kNeverTimeout;
            ASSERT_TRUE(session.client.Open(server.TlsUrl().c_str(), settings).has_value());
            ASSERT_TRUE(server.AcceptConnection());
            const auto started = std::chrono::steady_clock::now();
            session.client.Abort();
            session.ExpectSingleClosed();
            EXPECT_LT(std::chrono::steady_clock::now() - started, 5s);
            EXPECT_EQ(session.Closed().closeCode, WebSocketCloseCode::kAbnormalClosure);
            EXPECT_TRUE(server.WaitForEndOfStream());
        }
        {  // in the opening request: the TLS handshake is done
            ScriptedServer server;
            Session session;
            WebSocketClientSettings settings = FakeTlsSettings();
            settings.handshakeTimeout = kNeverTimeout;
            ASSERT_TRUE(session.client.Open(server.TlsUrl().c_str(), settings).has_value());
            ASSERT_TRUE(AcceptFakeTlsAndRequest(server));
            session.client.Abort();
            session.ExpectSingleClosed();
            EXPECT_TRUE(server.WaitForEndOfStream());
        }
        {  // open
            ScriptedServer server;
            Session session;
            WebSocketClientSettings settings = FakeTlsSettings();
            settings.closeTimeout = kNeverTimeout;
            ASSERT_TRUE(session.client.Open(server.TlsUrl().c_str(), settings).has_value());
            ASSERT_TRUE(AcceptFakeTlsAndRequest(server));
            server.Accept();
            ASSERT_TRUE(session.WaitForOpened());
            session.client.Abort();
            session.ExpectSingleClosed();
            EXPECT_EQ(session.Closed().closeCode, WebSocketCloseCode::kAbnormalClosure);
            EXPECT_TRUE(server.WaitForEndOfStream());
        }
        {  // before anything is accepted, racing with the connect
            for (int round = 0; round < 10; ++round) {
                ScriptedServer server;
                Session session;
                WebSocketClientSettings settings = FakeTlsSettings();
                settings.handshakeTimeout = kNeverTimeout;
                ASSERT_TRUE(session.client.Open(server.TlsUrl().c_str(), settings).has_value());
                session.client.Abort();
                session.ExpectSingleClosed();
            }
        }
    }

    TEST(WebSocketClientTest, TheDestructorEndsAWssClientInEveryState) {
        {  // in the TLS handshake
            ScriptedServer server;
            WebSocketClient client;
            WebSocketClientSettings settings = FakeTlsSettings();
            settings.handshakeTimeout = kNeverTimeout;
            ASSERT_TRUE(client.Open(server.TlsUrl().c_str(), settings).has_value());
            ASSERT_TRUE(server.AcceptConnection());
        }
        {  // in the opening request
            ScriptedServer server;
            WebSocketClient client;
            WebSocketClientSettings settings = FakeTlsSettings();
            settings.handshakeTimeout = kNeverTimeout;
            ASSERT_TRUE(client.Open(server.TlsUrl().c_str(), settings).has_value());
            ASSERT_TRUE(AcceptFakeTlsAndRequest(server));
        }
        {  // open, a read is blocked
            ScriptedServer server;
            Session session;
            WebSocketClientSettings settings = FakeTlsSettings();
            settings.closeTimeout = kNeverTimeout;
            ASSERT_TRUE(session.client.Open(server.TlsUrl().c_str(), settings).has_value());
            ASSERT_TRUE(AcceptFakeTlsAndRequest(server));
            server.Accept();
            ASSERT_TRUE(session.WaitForOpened());
        }
        {  // closing: our close frame is sent and the server never answers
            ScriptedServer server;
            Session session;
            WebSocketClientSettings settings = FakeTlsSettings();
            settings.closeTimeout = kNeverTimeout;
            ASSERT_TRUE(session.client.Open(server.TlsUrl().c_str(), settings).has_value());
            ASSERT_TRUE(AcceptFakeTlsAndRequest(server));
            server.Accept();
            ASSERT_TRUE(session.WaitForOpened());
            session.client.Close();
            ASSERT_TRUE(server.ReadFrame().has_value());
        }
        {  // ended
            ScriptedServer server;
            Session session;
            ASSERT_TRUE(session.client.Open(server.TlsUrl().c_str(), FakeTlsSettings()).has_value());
            ASSERT_TRUE(AcceptFakeTlsAndRequest(server));
            server.Accept();
            ASSERT_TRUE(session.WaitForOpened());
            server.Drop();
            ASSERT_TRUE(session.WaitForClosed());
        }
    }

    TEST(WebSocketClientTest, AFactoryThatGivesNothingEndsWithAReason) {
        ScriptedServer server;
        Session session;
        WebSocketClientSettings settings = QuickSettings();
        settings.tls.factory = [] { return UniquePtr<ITlsStream>(); };
        ASSERT_TRUE(session.client.Open(server.TlsUrl().c_str(), settings).has_value());
        session.ExpectSingleClosed();
        EXPECT_EQ(session.Closed().closeCode, WebSocketCloseCode::kAbnormalClosure);
    }

    TEST(WebSocketClientTest, PollingWithNothingToDoDoesNotAllocateOrBlock) {
        WebSocketClient client;
        WebSocketClientEvent event;
        event.text.reserve(64);
        const auto capacity = event.text.capacity();
        for (int i = 0; i < 1000; ++i) {
            EXPECT_FALSE(client.PollEvent(event));
        }
        EXPECT_EQ(event.text.capacity(), capacity);
    }

    TEST(WebSocketClientTest, ManyConnectionsInARowLeaveNothingBehind) {
        EchoServer server;
        for (int round = 0; round < 20; ++round) {
            Session session;
            ASSERT_TRUE(session.client.Open(server.Url().c_str()).has_value());
            ASSERT_TRUE(session.WaitForOpened());
            ASSERT_TRUE(session.client.SendText("x"));
            if (round % 3 == 0) {
                session.client.Close();
            } else if (round % 3 == 1) {
                session.client.Abort();
            }
            // round % 3 == 2: the destructor ends it
        }
        EXPECT_TRUE(WaitUntil({}, [&] { return server.Closed() == 20; }));
    }

}  // namespace BECore::Tests
