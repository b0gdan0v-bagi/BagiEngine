#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <Common/Printers.h>
#include <RemoteControl/WebSocketFrame.h>
#include <RemoteControl/WebSocketHandshake.h>
#include <RemoteControl/WebSocketServer.h>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    namespace {

        using namespace std::chrono_literals;

        constexpr WebSocketMask kMask = {0x12, 0x34, 0x56, 0x78};
        constexpr const char* kKey = "dGhlIHNhbXBsZSBub25jZQ==";
        constexpr auto kWaitLimit = 30s;
        constexpr int kMaxChunksPerFill = 1024;

        /// The test thread plays the main thread: it drains the queue while it waits, so responses can come back
        template <typename Predicate>
        bool WaitUntil(ControlService& service, Predicate done, std::chrono::milliseconds limit = kWaitLimit) {
            const auto deadline = std::chrono::steady_clock::now() + limit;
            while (std::chrono::steady_clock::now() < deadline) {
                service.Drain();
                if (done()) {
                    return true;
                }
                std::this_thread::sleep_for(1ms);
            }
            return false;
        }

        eastl::span<const uint8_t> AsBytes(const std::string& text) {
            return eastl::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size());
        }

        eastl::span<const uint8_t> AsBytes(const eastl::vector<uint8_t>& bytes) {
            return eastl::span<const uint8_t>(bytes.data(), bytes.size());
        }

        std::string HandshakeRequest(uint16_t port, const std::string& extraHeaders = "", const std::string& host = "") {
            const std::string hostValue = host.empty() ? "127.0.0.1:" + std::to_string(port) : host;
            return "GET /control HTTP/1.1\r\nHost: " + hostValue + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + std::string(kKey) + "\r\nSec-WebSocket-Version: 13\r\n" +
                   extraHeaders + "\r\n";
        }

        struct ServerFrame {
            uint8_t opcode = 0;
            std::string payload;
        };

        /// A raw client: the handshake and the frames are written by hand, so the server is tested against the bytes of the RFC
        class TestClient {
        public:
            static std::optional<TestClient> Open(uint16_t port) {
                auto socket = Socket::Connect(port);
                if (!socket) {
                    ADD_FAILURE() << "connect failed: " << socket.error().c_str();
                    return std::nullopt;
                }
                TestClient client;
                client._socket = std::move(*socket);
                client._socket.SetReceiveTimeout(5ms);
                return client;
            }

            void Send(eastl::span<const uint8_t> bytes) {
                EXPECT_TRUE(_socket.SendAll(bytes));
            }

            void Send(const std::string& text) {
                Send(AsBytes(text));
            }

            void SendText(const std::string& text) {
                Send(AsBytes(EncodeTextFrame(eastl::string_view(text.data(), text.size()), kMask)));
            }

            /// The response head of the handshake, and the body when it says Content-Length. Empty when nothing came in time
            std::string ReadHttpResponse(ControlService& service) {
                constexpr std::string_view kHeadEnd = "\r\n\r\n";
                std::string response;
                size_t bodyExpected = 0;
                const bool complete = WaitUntil(service, [&] {
                    FillBuffer();
                    const std::string text(reinterpret_cast<const char*>(_buffer.data()), _buffer.size());
                    const size_t headEnd = text.find(kHeadEnd);
                    if (headEnd == std::string::npos) {
                        return false;
                    }
                    const size_t lengthAt = text.find("Content-Length: ");
                    bodyExpected = (lengthAt == std::string::npos || lengthAt > headEnd) ? 0 : static_cast<size_t>(std::stoul(text.substr(lengthAt + 16)));
                    if (text.size() < headEnd + kHeadEnd.size() + bodyExpected) {
                        return false;
                    }
                    response = text.substr(0, headEnd + kHeadEnd.size() + bodyExpected);
                    _buffer.erase(_buffer.begin(), _buffer.begin() + static_cast<ptrdiff_t>(response.size()));
                    return true;
                });
                return complete ? response : std::string();
            }

            /// Performs the handshake of the RFC and returns whether the server switched protocols
            bool Handshake(ControlService& service, uint16_t port) {
                Send(HandshakeRequest(port));
                return ReadHttpResponse(service).starts_with("HTTP/1.1 101 ");
            }

            /// The next complete frame of the server, nullopt when none came yet
            std::optional<ServerFrame> TryReadFrame() {
                if (const auto frame = ParseBuffered()) {
                    return frame;
                }
                FillBuffer();
                return ParseBuffered();
            }

            std::optional<ServerFrame> WaitForFrame(ControlService& service) {
                std::optional<ServerFrame> frame;
                WaitUntil(service, [&] {
                    frame = TryReadFrame();
                    return frame.has_value();
                });
                return frame;
            }

            /// True when the server ended the connection (and every frame it sent before has been read)
            bool WaitForEndOfStream(ControlService& service) {
                return WaitUntil(service, [&] {
                    FillBuffer();
                    return _ended;
                });
            }

        private:
            TestClient() = default;

            /// Reads everything that has arrived (up to kMaxChunksPerFill chunks); a loop, because WaitUntil sleeps 1 ms
            /// between its calls, which is 15 ms on Windows, and a megabyte in chunks of 4 KiB would take seconds
            void FillBuffer() {
                std::array<uint8_t, 4096> chunk;
                for (int i = 0; i < kMaxChunksPerFill; ++i) {
                    const auto received = _socket.Receive(eastl::span<uint8_t>(chunk.data(), chunk.size()));
                    if (!received) {
                        if (received.error() != SocketError::TimedOut) {
                            _ended = true;
                        }
                        return;
                    }
                    // Pointers, not std::array iterators: EASTL's insert does not take MSVC's class iterator
                    _buffer.insert(_buffer.end(), chunk.data(), chunk.data() + *received);
                }
            }

            /// The frames of a server are not masked
            std::optional<ServerFrame> ParseBuffered() {
                if (_buffer.size() < 2) {
                    return std::nullopt;
                }
                size_t length = _buffer[1] & 0x7F;
                size_t headerSize = 2;
                if (length == 126 || length == 127) {
                    const size_t lengthBytes = (length == 126) ? 2 : 8;
                    if (_buffer.size() < 2 + lengthBytes) {
                        return std::nullopt;
                    }
                    length = 0;
                    for (size_t i = 0; i < lengthBytes; ++i) {
                        length = (length << 8) | _buffer[2 + i];
                    }
                    headerSize += lengthBytes;
                }
                if (_buffer.size() < headerSize + length) {
                    return std::nullopt;
                }
                ServerFrame frame;
                frame.opcode = _buffer[0] & 0x0F;
                frame.payload.assign(reinterpret_cast<const char*>(_buffer.data()) + headerSize, length);
                _buffer.erase(_buffer.begin(), _buffer.begin() + static_cast<ptrdiff_t>(headerSize + length));
                return frame;
            }

            Socket _socket;
            eastl::vector<uint8_t> _buffer;
            bool _ended = false;
        };

        uint16_t CloseCodeOf(const ServerFrame& frame) {
            return static_cast<uint16_t>((static_cast<uint8_t>(frame.payload[0]) << 8) | static_cast<uint8_t>(frame.payload[1]));
        }

        std::string Request(int64_t id, std::string_view method, std::string_view paramsJson = "{}") {
            return "{\"id\":" + std::to_string(id) + ",\"method\":\"" + std::string(method) + "\",\"params\":" + std::string(paramsJson) + "}";
        }

        /// The members of a response, read through the checked helpers
        struct Response {
            bool ok = false;
            int64_t id = -1;
            eastl::string errorCode;
            Json json;
        };

        Response ParseResponse(const std::string& text) {
            Response response;
            auto parsed = ParseJson(eastl::string_view(text.data(), text.size()));
            if (!parsed) {
                ADD_FAILURE() << "the response is not JSON: " << text;
                return response;
            }
            response.json = std::move(*parsed);
            response.ok = GetJsonBool(response.json, "ok").value_or(false);
            response.id = GetJsonInt(response.json, "id").value_or(-1);
            if (const Json* error = FindJsonMember(response.json, "error")) {
                response.errorCode = GetJsonString(*error, "code").value_or("");
            }
            return response;
        }

        class WebSocketServerTest : public ::testing::Test {
        protected:
            void SetUp() override {
                service.RegisterHandler("ping", [](ControlCall& call) {
                    Json result = Json::object();
                    result["pong"] = true;
                    call.Reply(std::move(result));
                });
                service.RegisterHandler("echo", [this](ControlCall& call) {
                    ++echoCalls;
                    call.Reply(call.Params());
                });
            }

            void TearDown() override {
                server.Stop();
                service.Shutdown();
            }

            uint16_t StartServer() {
                const auto port = server.Start(0);
                EXPECT_TRUE(port.has_value()) << (port.has_value() ? "" : port.error().message.c_str());
                return port.value_or(0);
            }

            /// A connected client that finished the handshake
            std::optional<TestClient> Connect(uint16_t port) {
                auto client = TestClient::Open(port);
                if (client && !client->Handshake(service, port)) {
                    ADD_FAILURE() << "the handshake was not accepted";
                    return std::nullopt;
                }
                return client;
            }

            Response Call(TestClient& client, const std::string& request) {
                client.SendText(request);
                const auto frame = client.WaitForFrame(service);
                if (!frame) {
                    ADD_FAILURE() << "no response to " << request;
                    return {};
                }
                EXPECT_EQ(frame->opcode, WebSocketOpcode::kText);
                return ParseResponse(frame->payload);
            }

            ControlService service;
            WebSocketServer server{service};
            std::atomic<int> echoCalls{0};
        };

    }  // namespace

    TEST_F(WebSocketServerTest, StartReportsTheChosenPort) {
        EXPECT_FALSE(server.IsRunning());
        EXPECT_EQ(server.GetPort(), 0);

        const uint16_t port = StartServer();
        EXPECT_NE(port, 0);
        EXPECT_TRUE(server.IsRunning());
        EXPECT_EQ(server.GetPort(), port);
    }

    TEST_F(WebSocketServerTest, StopIsHarmlessWithoutStartAndTwice) {
        server.Stop();
        StartServer();
        server.Stop();
        server.Stop();
        EXPECT_FALSE(server.IsRunning());
        EXPECT_EQ(server.GetPort(), 0);
    }

    TEST_F(WebSocketServerTest, CanBeStartedAgainAfterStop) {
        StartServer();
        server.Stop();
        const uint16_t port = StartServer();
        auto client = Connect(port);
        ASSERT_TRUE(client.has_value());
        EXPECT_TRUE(Call(*client, Request(1, "ping")).ok);
    }

    TEST_F(WebSocketServerTest, SecondServerOnATakenPortFailsToStart) {
        const uint16_t port = StartServer();

        ControlService otherService;
        WebSocketServer other(otherService);
        const auto result = other.Start(port);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, RemoteControlErrors::kListenFailed);
        EXPECT_NE(result.error().message.find("127.0.0.1"), eastl::string::npos);
        EXPECT_FALSE(other.IsRunning());
        otherService.Shutdown();
    }

    TEST_F(WebSocketServerTest, StartingARunningServerIsAnError) {
        StartServer();
        const auto again = server.Start(0);
        ASSERT_FALSE(again.has_value());
        EXPECT_EQ(again.error().code, RemoteControlErrors::kAlreadyRunning);
    }

    TEST_F(WebSocketServerTest, HandshakeSwitchesProtocols) {
        const uint16_t port = StartServer();
        auto client = TestClient::Open(port);
        ASSERT_TRUE(client.has_value());

        client->Send(HandshakeRequest(port));
        const std::string response = client->ReadHttpResponse(service);
        EXPECT_TRUE(response.starts_with("HTTP/1.1 101 Switching Protocols\r\n")) << response;
        EXPECT_NE(response.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"), std::string::npos) << response;
    }

    TEST_F(WebSocketServerTest, HandshakeMayArriveInPieces) {
        const uint16_t port = StartServer();
        auto client = TestClient::Open(port);
        ASSERT_TRUE(client.has_value());

        const std::string request = HandshakeRequest(port);
        client->Send(request.substr(0, 20));
        std::this_thread::sleep_for(20ms);
        client->Send(request.substr(20));
        EXPECT_TRUE(client->ReadHttpResponse(service).starts_with("HTTP/1.1 101 "));
    }

    TEST_F(WebSocketServerTest, RequestGetsItsResponse) {
        const uint16_t port = StartServer();
        auto client = Connect(port);
        ASSERT_TRUE(client.has_value());

        const Response response = Call(*client, Request(7, "ping"));
        EXPECT_TRUE(response.ok);
        EXPECT_EQ(response.id, 7);
        const Json* result = FindJsonMember(response.json, "result");
        ASSERT_NE(result, nullptr);
        EXPECT_TRUE(GetJsonBool(*result, "pong").value_or(false));
    }

    TEST_F(WebSocketServerTest, UnknownMethodIsAnErrorResponseAndTheConnectionLives) {
        const uint16_t port = StartServer();
        auto client = Connect(port);
        ASSERT_TRUE(client.has_value());

        const Response failed = Call(*client, Request(1, "no_such_method"));
        EXPECT_FALSE(failed.ok);
        EXPECT_EQ(failed.errorCode, ControlErrors::kUnknownMethod);
        EXPECT_TRUE(Call(*client, Request(2, "ping")).ok);
    }

    TEST_F(WebSocketServerTest, PipelinedRequestsAreAnsweredInOrder) {
        const uint16_t port = StartServer();
        auto client = Connect(port);
        ASSERT_TRUE(client.has_value());

        constexpr int kRequests = 50;
        eastl::vector<uint8_t> batch;
        for (int id = 1; id <= kRequests; ++id) {
            const std::string text = Request(id, "echo", "{\"n\":" + std::to_string(id) + "}");
            const auto frame = EncodeTextFrame(eastl::string_view(text.data(), text.size()), kMask);
            batch.insert(batch.end(), frame.begin(), frame.end());
        }
        client->Send(AsBytes(batch));

        for (int id = 1; id <= kRequests; ++id) {
            const auto frame = client->WaitForFrame(service);
            ASSERT_TRUE(frame.has_value()) << "response " << id << " is missing";
            const Response response = ParseResponse(frame->payload);
            EXPECT_TRUE(response.ok);
            EXPECT_EQ(response.id, id);
        }
        EXPECT_EQ(echoCalls.load(), kRequests);
    }

    TEST_F(WebSocketServerTest, RequestInTwoFragmentsIsPutTogether) {
        const uint16_t port = StartServer();
        auto client = Connect(port);
        ASSERT_TRUE(client.has_value());

        const std::string text = Request(3, "ping");
        const std::string first = text.substr(0, 10);
        const std::string second = text.substr(10);
        client->Send(AsBytes(EncodeFrame(WebSocketOpcode::kText, AsBytes(first), kMask, /*fin=*/false)));
        client->Send(AsBytes(EncodeFrame(WebSocketOpcode::kContinuation, AsBytes(second), kMask, /*fin=*/true)));

        const auto frame = client->WaitForFrame(service);
        ASSERT_TRUE(frame.has_value());
        const Response response = ParseResponse(frame->payload);
        EXPECT_TRUE(response.ok);
        EXPECT_EQ(response.id, 3);
    }

    TEST_F(WebSocketServerTest, GarbageGetsAParseErrorAndTheConnectionLives) {
        const uint16_t port = StartServer();
        auto client = Connect(port);
        ASSERT_TRUE(client.has_value());

        for (const char* garbage : {"not json", "{", "[1,2", "", "{\"id\":1}", "\xff\xfe"}) {
            const Response failed = Call(*client, garbage);
            EXPECT_FALSE(failed.ok) << garbage;
            EXPECT_FALSE(failed.errorCode.empty()) << garbage;
        }
        EXPECT_TRUE(Call(*client, Request(9, "ping")).ok);
    }

    TEST_F(WebSocketServerTest, PingFrameIsAnsweredWithAPongOfTheSamePayload) {
        const uint16_t port = StartServer();
        auto client = Connect(port);
        ASSERT_TRUE(client.has_value());

        const std::string payload = "are you there";
        client->Send(AsBytes(EncodeFrame(WebSocketOpcode::kPing, AsBytes(payload), kMask)));
        const auto frame = client->WaitForFrame(service);
        ASSERT_TRUE(frame.has_value());
        EXPECT_EQ(frame->opcode, WebSocketOpcode::kPong);
        EXPECT_EQ(frame->payload, payload);
    }

    TEST_F(WebSocketServerTest, CloseFrameIsEchoedAndTheConnectionEnds) {
        const uint16_t port = StartServer();
        auto client = Connect(port);
        ASSERT_TRUE(client.has_value());

        client->Send(AsBytes(EncodeCloseFrame(WebSocketCloseCode::kNormal, "bye", kMask)));
        const auto frame = client->WaitForFrame(service);
        ASSERT_TRUE(frame.has_value());
        EXPECT_EQ(frame->opcode, WebSocketOpcode::kClose);
        EXPECT_EQ(CloseCodeOf(*frame), WebSocketCloseCode::kNormal);
        EXPECT_TRUE(client->WaitForEndOfStream(service));
    }

    TEST_F(WebSocketServerTest, BinaryMessageEndsTheConnectionWithUnsupportedData) {
        const uint16_t port = StartServer();
        auto client = Connect(port);
        ASSERT_TRUE(client.has_value());

        client->Send(AsBytes(EncodeFrame(WebSocketOpcode::kBinary, AsBytes(std::string("\x01\x02")), kMask)));
        const auto frame = client->WaitForFrame(service);
        ASSERT_TRUE(frame.has_value());
        EXPECT_EQ(frame->opcode, WebSocketOpcode::kClose);
        EXPECT_EQ(CloseCodeOf(*frame), WebSocketCloseCode::kUnsupportedData);
        EXPECT_TRUE(client->WaitForEndOfStream(service));
    }

    TEST_F(WebSocketServerTest, UnmaskedFrameEndsTheConnectionWithAProtocolError) {
        const uint16_t port = StartServer();
        auto client = Connect(port);
        ASSERT_TRUE(client.has_value());

        client->Send(AsBytes(EncodeTextFrame(Request(1, "ping").c_str())));
        const auto frame = client->WaitForFrame(service);
        ASSERT_TRUE(frame.has_value());
        EXPECT_EQ(frame->opcode, WebSocketOpcode::kClose);
        EXPECT_EQ(CloseCodeOf(*frame), WebSocketCloseCode::kProtocolError);
        EXPECT_TRUE(client->WaitForEndOfStream(service));
    }

    TEST_F(WebSocketServerTest, MessageOverTheLimitEndsTheConnectionWithTooBig) {
        const uint16_t port = StartServer();
        auto client = Connect(port);
        ASSERT_TRUE(client.has_value());

        // Only the header of a frame that announces more than the limit: the server must not wait for the payload
        const uint64_t length = ControlService::kMaxRequestBytes + 1;
        eastl::vector<uint8_t> header = {0x81, 0xFF};
        for (int shift = 56; shift >= 0; shift -= 8) {
            header.push_back(static_cast<uint8_t>(length >> shift));
        }
        header.insert(header.end(), kMask.data(), kMask.data() + kMask.size());
        client->Send(AsBytes(header));

        const auto frame = client->WaitForFrame(service);
        ASSERT_TRUE(frame.has_value());
        EXPECT_EQ(frame->opcode, WebSocketOpcode::kClose);
        EXPECT_EQ(CloseCodeOf(*frame), WebSocketCloseCode::kMessageTooBig);
    }

    TEST_F(WebSocketServerTest, RequestOfTheLimitSizeIsAnswered) {
        const uint16_t port = StartServer();
        auto client = Connect(port);
        ASSERT_TRUE(client.has_value());

        // A request that is exactly at the limit is passed on; the answer to it is not a transport matter
        const std::string prefix = "{\"id\":1,\"method\":\"echo\",\"params\":{\"pad\":\"";
        const std::string suffix = "\"}}";
        const std::string request = prefix + std::string(ControlService::kMaxRequestBytes - prefix.size() - suffix.size(), 'x') + suffix;
        ASSERT_EQ(request.size(), ControlService::kMaxRequestBytes);

        const Response response = Call(*client, request);
        EXPECT_TRUE(response.ok);
        EXPECT_EQ(echoCalls.load(), 1);
    }

    TEST_F(WebSocketServerTest, ForeignOriginIsRefusedAndNothingReachesTheQueue) {
        const uint16_t port = StartServer();
        auto client = TestClient::Open(port);
        ASSERT_TRUE(client.has_value());

        client->Send(HandshakeRequest(port, "Origin: http://evil.example\r\n"));
        const std::string response = client->ReadHttpResponse(service);
        EXPECT_TRUE(response.starts_with("HTTP/1.1 403 Forbidden\r\n")) << response;

        // A request in the same breath is not read: the connection is over
        client->SendText(Request(1, "echo"));
        EXPECT_TRUE(client->WaitForEndOfStream(service));
        EXPECT_EQ(echoCalls.load(), 0);
    }

    TEST_F(WebSocketServerTest, LoopbackOriginIsAccepted) {
        const uint16_t port = StartServer();
        auto client = TestClient::Open(port);
        ASSERT_TRUE(client.has_value());

        client->Send(HandshakeRequest(port, "Origin: http://localhost:3000\r\n"));
        EXPECT_TRUE(client->ReadHttpResponse(service).starts_with("HTTP/1.1 101 "));
    }

    TEST_F(WebSocketServerTest, ForeignHostIsRefused) {
        const uint16_t port = StartServer();
        auto client = TestClient::Open(port);
        ASSERT_TRUE(client.has_value());

        client->Send(HandshakeRequest(port, "", "rebound.example.com:" + std::to_string(port)));
        EXPECT_TRUE(client->ReadHttpResponse(service).starts_with("HTTP/1.1 403 Forbidden\r\n"));
    }

    TEST_F(WebSocketServerTest, PlainHttpRequestIsAnsweredWithAnError) {
        const uint16_t port = StartServer();
        auto client = TestClient::Open(port);
        ASSERT_TRUE(client.has_value());

        client->Send(std::string("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"));
        EXPECT_TRUE(client->ReadHttpResponse(service).starts_with("HTTP/1.1 400 Bad Request\r\n"));
    }

    TEST_F(WebSocketServerTest, WrongVersionIsAskedToUseThirteen) {
        const uint16_t port = StartServer();
        auto client = TestClient::Open(port);
        ASSERT_TRUE(client.has_value());

        std::string request = HandshakeRequest(port);
        request.replace(request.find("Version: 13"), 11, "Version: 8");
        client->Send(request);
        const std::string response = client->ReadHttpResponse(service);
        EXPECT_TRUE(response.starts_with("HTTP/1.1 426 Upgrade Required\r\n")) << response;
        EXPECT_NE(response.find("Sec-WebSocket-Version: 13\r\n"), std::string::npos);
    }

    TEST_F(WebSocketServerTest, FifthConnectionIsRefusedAsBusy) {
        const uint16_t port = StartServer();

        eastl::vector<TestClient> clients;
        for (size_t i = 0; i < WebSocketServer::kMaxConnections; ++i) {
            auto client = Connect(port);
            ASSERT_TRUE(client.has_value());
            clients.push_back(std::move(*client));
        }

        auto fifth = TestClient::Open(port);
        ASSERT_TRUE(fifth.has_value());
        fifth->Send(HandshakeRequest(port));
        const std::string response = fifth->ReadHttpResponse(service);
        EXPECT_TRUE(response.starts_with("HTTP/1.1 503 Service Unavailable\r\n")) << response;

        // The four are not affected
        for (TestClient& client : clients) {
            EXPECT_TRUE(Call(client, Request(1, "ping")).ok);
        }
    }

    TEST_F(WebSocketServerTest, SlotOfAClosedConnectionIsFreedForTheNextClient) {
        const uint16_t port = StartServer();

        // More sequential clients than there are slots: every one has to find its slot free again
        for (size_t round = 0; round < WebSocketServer::kMaxConnections * 3; ++round) {
            bool connected = false;
            for (int attempt = 0; attempt < 200 && !connected; ++attempt) {
                auto client = TestClient::Open(port);
                ASSERT_TRUE(client.has_value());
                client->Send(HandshakeRequest(port));
                const std::string response = client->ReadHttpResponse(service);
                if (response.starts_with("HTTP/1.1 101 ")) {
                    connected = true;
                } else {
                    std::this_thread::sleep_for(10ms);  // the accept thread reaps finished connections every 50 ms
                }
            }
            ASSERT_TRUE(connected) << "round " << round;
        }
    }

    TEST_F(WebSocketServerTest, PublishedEventReachesEveryOpenClient) {
        const uint16_t port = StartServer();
        auto first = Connect(port);
        auto second = Connect(port);
        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());

        Json data = Json::object();
        data["version"] = 5;
        service.Publish("state_changed", data);

        for (TestClient* client : {&*first, &*second}) {
            const auto frame = client->WaitForFrame(service);
            ASSERT_TRUE(frame.has_value());
            EXPECT_EQ(frame->opcode, WebSocketOpcode::kText);
            const auto event = ParseJson(eastl::string_view(frame->payload.data(), frame->payload.size()));
            ASSERT_TRUE(event.has_value());
            EXPECT_EQ(GetJsonString(*event, "event").value_or(""), "state_changed");
            const Json* eventData = FindJsonMember(*event, "data");
            ASSERT_NE(eventData, nullptr);
            EXPECT_EQ(GetJsonInt(*eventData, "version").value_or(-1), 5);
        }
    }

    TEST_F(WebSocketServerTest, NoEventsAreSentAfterStop) {
        const uint16_t port = StartServer();
        auto client = Connect(port);
        ASSERT_TRUE(client.has_value());

        server.Stop();
        service.Publish("state_changed", Json::object());  // the sink is gone: nothing to send to, nothing to crash
        EXPECT_TRUE(client->WaitForEndOfStream(service));
    }

    TEST_F(WebSocketServerTest, StopEndsOpenAndUnfinishedConnections) {
        const uint16_t port = StartServer();
        auto open = Connect(port);
        ASSERT_TRUE(open.has_value());

        // A client that connected but never finishes its handshake holds a reader thread in the 5 s wait
        auto silent = TestClient::Open(port);
        ASSERT_TRUE(silent.has_value());
        auto halfway = TestClient::Open(port);
        ASSERT_TRUE(halfway.has_value());
        halfway->Send(std::string("GET / HTT"));
        std::this_thread::sleep_for(50ms);

        const auto before = std::chrono::steady_clock::now();
        server.Stop();
        EXPECT_LT(std::chrono::steady_clock::now() - before, WebSocketServer::kHandshakeTimeout) << "Stop() waited for the handshake timeout";

        EXPECT_TRUE(open->WaitForEndOfStream(service));
        EXPECT_TRUE(silent->WaitForEndOfStream(service));
        EXPECT_TRUE(halfway->WaitForEndOfStream(service));
    }

    TEST_F(WebSocketServerTest, RequestsAfterServiceShutdownAreAnsweredShuttingDown) {
        const uint16_t port = StartServer();
        auto client = Connect(port);
        ASSERT_TRUE(client.has_value());

        service.Shutdown();
        const Response response = Call(*client, Request(4, "ping"));
        EXPECT_FALSE(response.ok);
        EXPECT_EQ(response.id, 4);
        EXPECT_EQ(response.errorCode, ControlErrors::kShuttingDown);
    }

    TEST_F(WebSocketServerTest, ClientThatLeavesWhileItsRequestIsPendingDoesNotHarmTheServer) {
        const uint16_t port = StartServer();
        {
            auto client = Connect(port);
            ASSERT_TRUE(client.has_value());
            client->SendText(Request(1, "echo"));
            // The client is destroyed here, before the main thread has drained the request
        }

        // The request runs, its response has nowhere to go, and the server still works
        ASSERT_TRUE(WaitUntil(service, [&] { return echoCalls.load() == 1; }));
        auto other = Connect(port);
        ASSERT_TRUE(other.has_value());
        EXPECT_TRUE(Call(*other, Request(2, "ping")).ok);
    }

    TEST_F(WebSocketServerTest, TwoClientsGetTheirOwnResponses) {
        const uint16_t port = StartServer();
        auto first = Connect(port);
        auto second = Connect(port);
        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());

        first->SendText(Request(100, "echo"));
        second->SendText(Request(200, "echo"));

        const auto firstFrame = first->WaitForFrame(service);
        const auto secondFrame = second->WaitForFrame(service);
        ASSERT_TRUE(firstFrame.has_value());
        ASSERT_TRUE(secondFrame.has_value());
        EXPECT_EQ(ParseResponse(firstFrame->payload).id, 100);
        EXPECT_EQ(ParseResponse(secondFrame->payload).id, 200);
    }

}  // namespace BECore::Tests
