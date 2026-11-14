#include "WebSocketTestClient.h"

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

        /// The test thread plays the main thread: it drains the queue while it waits, so responses can come back
        template <typename Predicate>
        bool WaitUntil(ControlService& service, Predicate done, std::chrono::milliseconds limit = kClientWaitLimit) {
            return WaitUntil(WaitPump([&service] { service.Drain(); }), std::move(done), limit);
        }

        using TestClient = WebSocketTestClient;
        constexpr WebSocketMask kMask = kClientMask;

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
            auto parsed = ParseJson(FromStd(text));
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

            Optional<TestClient> OpenClient(uint16_t port) {
                return TestClient::Open(port, [this] { service.Drain(); });
            }

            /// A connected client that finished the handshake
            Optional<TestClient> Connect(uint16_t port) {
                auto client = OpenClient(port);
                if (client && !client->Handshake(port)) {
                    ADD_FAILURE() << "the handshake was not accepted";
                    return std::nullopt;
                }
                return client;
            }

            Response Call(TestClient& client, const std::string& request) {
                client.SendText(request);
                const auto frame = client.WaitForFrame();
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
        auto client = OpenClient(port);
        ASSERT_TRUE(client.has_value());

        client->Send(HandshakeRequest(port));
        const std::string response = client->ReadHttpResponse();
        EXPECT_TRUE(response.starts_with("HTTP/1.1 101 Switching Protocols\r\n")) << response;
        EXPECT_NE(response.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"), std::string::npos) << response;
    }

    TEST_F(WebSocketServerTest, HandshakeMayArriveInPieces) {
        const uint16_t port = StartServer();
        auto client = OpenClient(port);
        ASSERT_TRUE(client.has_value());

        const std::string request = HandshakeRequest(port);
        client->Send(request.substr(0, 20));
        std::this_thread::sleep_for(20ms);
        client->Send(request.substr(20));
        EXPECT_TRUE(client->ReadHttpResponse().starts_with("HTTP/1.1 101 "));
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
            const auto frame = EncodeTextFrame(FromStd(text), kMask);
            batch.insert(batch.end(), frame.begin(), frame.end());
        }
        client->Send(AsBytes(batch));

        for (int id = 1; id <= kRequests; ++id) {
            const auto frame = client->WaitForFrame();
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

        const auto frame = client->WaitForFrame();
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
        const auto frame = client->WaitForFrame();
        ASSERT_TRUE(frame.has_value());
        EXPECT_EQ(frame->opcode, WebSocketOpcode::kPong);
        EXPECT_EQ(frame->payload, payload);
    }

    TEST_F(WebSocketServerTest, CloseFrameIsEchoedAndTheConnectionEnds) {
        const uint16_t port = StartServer();
        auto client = Connect(port);
        ASSERT_TRUE(client.has_value());

        client->Send(AsBytes(EncodeCloseFrame(WebSocketCloseCode::kNormal, "bye", kMask)));
        const auto frame = client->WaitForFrame();
        ASSERT_TRUE(frame.has_value());
        EXPECT_EQ(frame->opcode, WebSocketOpcode::kClose);
        EXPECT_EQ(CloseCodeOf(*frame), WebSocketCloseCode::kNormal);
        EXPECT_TRUE(client->WaitForEndOfStream());
    }

    TEST_F(WebSocketServerTest, BinaryMessageEndsTheConnectionWithUnsupportedData) {
        const uint16_t port = StartServer();
        auto client = Connect(port);
        ASSERT_TRUE(client.has_value());

        client->Send(AsBytes(EncodeFrame(WebSocketOpcode::kBinary, AsBytes(std::string("\x01\x02")), kMask)));
        const auto frame = client->WaitForFrame();
        ASSERT_TRUE(frame.has_value());
        EXPECT_EQ(frame->opcode, WebSocketOpcode::kClose);
        EXPECT_EQ(CloseCodeOf(*frame), WebSocketCloseCode::kUnsupportedData);
        EXPECT_TRUE(client->WaitForEndOfStream());
    }

    TEST_F(WebSocketServerTest, UnmaskedFrameEndsTheConnectionWithAProtocolError) {
        const uint16_t port = StartServer();
        auto client = Connect(port);
        ASSERT_TRUE(client.has_value());

        client->Send(AsBytes(EncodeTextFrame(Request(1, "ping").c_str())));
        const auto frame = client->WaitForFrame();
        ASSERT_TRUE(frame.has_value());
        EXPECT_EQ(frame->opcode, WebSocketOpcode::kClose);
        EXPECT_EQ(CloseCodeOf(*frame), WebSocketCloseCode::kProtocolError);
        EXPECT_TRUE(client->WaitForEndOfStream());
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

        const auto frame = client->WaitForFrame();
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
        auto client = OpenClient(port);
        ASSERT_TRUE(client.has_value());

        client->Send(HandshakeRequest(port, "Origin: http://evil.example\r\n"));
        const std::string response = client->ReadHttpResponse();
        EXPECT_TRUE(response.starts_with("HTTP/1.1 403 Forbidden\r\n")) << response;

        // A request in the same breath is not read: the connection is over
        client->SendText(Request(1, "echo"));
        EXPECT_TRUE(client->WaitForEndOfStream());
        EXPECT_EQ(echoCalls.load(), 0);
    }

    TEST_F(WebSocketServerTest, LoopbackOriginIsAccepted) {
        const uint16_t port = StartServer();
        auto client = OpenClient(port);
        ASSERT_TRUE(client.has_value());

        client->Send(HandshakeRequest(port, "Origin: http://localhost:3000\r\n"));
        EXPECT_TRUE(client->ReadHttpResponse().starts_with("HTTP/1.1 101 "));
    }

    TEST_F(WebSocketServerTest, ForeignHostIsRefused) {
        const uint16_t port = StartServer();
        auto client = OpenClient(port);
        ASSERT_TRUE(client.has_value());

        client->Send(HandshakeRequest(port, "", "rebound.example.com:" + std::to_string(port)));
        EXPECT_TRUE(client->ReadHttpResponse().starts_with("HTTP/1.1 403 Forbidden\r\n"));
    }

    TEST_F(WebSocketServerTest, PlainHttpRequestIsAnsweredWithAnError) {
        const uint16_t port = StartServer();
        auto client = OpenClient(port);
        ASSERT_TRUE(client.has_value());

        client->Send(std::string("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"));
        EXPECT_TRUE(client->ReadHttpResponse().starts_with("HTTP/1.1 400 Bad Request\r\n"));
    }

    TEST_F(WebSocketServerTest, WrongVersionIsAskedToUseThirteen) {
        const uint16_t port = StartServer();
        auto client = OpenClient(port);
        ASSERT_TRUE(client.has_value());

        std::string request = HandshakeRequest(port);
        request.replace(request.find("Version: 13"), 11, "Version: 8");
        client->Send(request);
        const std::string response = client->ReadHttpResponse();
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

        auto fifth = OpenClient(port);
        ASSERT_TRUE(fifth.has_value());
        fifth->Send(HandshakeRequest(port));
        const std::string response = fifth->ReadHttpResponse();
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
                auto client = OpenClient(port);
                ASSERT_TRUE(client.has_value());
                client->Send(HandshakeRequest(port));
                const std::string response = client->ReadHttpResponse();
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
            const auto frame = client->WaitForFrame();
            ASSERT_TRUE(frame.has_value());
            EXPECT_EQ(frame->opcode, WebSocketOpcode::kText);
            const auto event = ParseJson(FromStd(frame->payload));
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
        EXPECT_TRUE(client->WaitForEndOfStream());
    }

    TEST_F(WebSocketServerTest, StopEndsOpenAndUnfinishedConnections) {
        const uint16_t port = StartServer();
        auto open = Connect(port);
        ASSERT_TRUE(open.has_value());

        // A client that connected but never finishes its handshake holds a reader thread in the 5 s wait
        auto silent = OpenClient(port);
        ASSERT_TRUE(silent.has_value());
        auto halfway = OpenClient(port);
        ASSERT_TRUE(halfway.has_value());
        halfway->Send(std::string("GET / HTT"));
        std::this_thread::sleep_for(50ms);

        const auto before = std::chrono::steady_clock::now();
        server.Stop();
        EXPECT_LT(std::chrono::steady_clock::now() - before, WebSocketServer::kHandshakeTimeout) << "Stop() waited for the handshake timeout";

        EXPECT_TRUE(open->WaitForEndOfStream());
        EXPECT_TRUE(silent->WaitForEndOfStream());
        EXPECT_TRUE(halfway->WaitForEndOfStream());
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

    TEST_F(WebSocketServerTest, ClientCountFollowsTheOpenConnections) {
        EXPECT_EQ(server.GetClientCount(), 0u);
        const uint16_t port = StartServer();
        EXPECT_EQ(server.GetClientCount(), 0u);

        auto first = Connect(port);
        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(WaitUntil(service, [&] { return server.GetClientCount() == 1; }));

        auto second = Connect(port);
        ASSERT_TRUE(second.has_value());
        ASSERT_TRUE(WaitUntil(service, [&] { return server.GetClientCount() == 2; }));

        second->Drop();
        ASSERT_TRUE(WaitUntil(service, [&] { return server.GetClientCount() == 1; }));

        first->Drop();
        ASSERT_TRUE(WaitUntil(service, [&] { return server.GetClientCount() == 0; }));
    }

    TEST_F(WebSocketServerTest, ClientCountIsZeroAfterStop) {
        const uint16_t port = StartServer();
        auto first = Connect(port);
        auto second = Connect(port);
        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());
        ASSERT_TRUE(WaitUntil(service, [&] { return server.GetClientCount() == 2; }));

        server.Stop();
        EXPECT_EQ(server.GetClientCount(), 0u);
    }

    TEST_F(WebSocketServerTest, TwoClientsGetTheirOwnResponses) {
        const uint16_t port = StartServer();
        auto first = Connect(port);
        auto second = Connect(port);
        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());

        first->SendText(Request(100, "echo"));
        second->SendText(Request(200, "echo"));

        const auto firstFrame = first->WaitForFrame();
        const auto secondFrame = second->WaitForFrame();
        ASSERT_TRUE(firstFrame.has_value());
        ASSERT_TRUE(secondFrame.has_value());
        EXPECT_EQ(ParseResponse(firstFrame->payload).id, 100);
        EXPECT_EQ(ParseResponse(secondFrame->payload).id, 200);
    }

}  // namespace BECore::Tests
