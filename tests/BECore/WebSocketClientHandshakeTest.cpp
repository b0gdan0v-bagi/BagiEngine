#include <Common/Printers.h>
#include <RemoteControl/WebSocketClientHandshake.h>
#include <RemoteControl/WebSocketHandshake.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kRfcKey = "dGhlIHNhbXBsZSBub25jZQ==";
        constexpr const char* kRfcAccept = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";

        /// A server answer that passes until a test changes it
        class Response {
        public:
            std::string statusLine = "HTTP/1.1 101 Switching Protocols";
            std::vector<std::string> headers = {"Upgrade: websocket", "Connection: Upgrade", std::string("Sec-WebSocket-Accept: ") + kRfcAccept};

            Response& Add(std::string_view line) {
                headers.emplace_back(line);
                return *this;
            }

            Response& Remove(std::string_view name) {
                std::erase_if(headers, [&](const std::string& line) { return line.starts_with(name); });
                return *this;
            }

            [[nodiscard]] std::string Text() const {
                std::string text = statusLine + "\r\n";
                for (const std::string& line : headers) {
                    text += line + "\r\n";
                }
                return text + "\r\n";
            }

            [[nodiscard]] std::expected<void, eastl::string> Check() const {
                const std::string text = Text();
                return CheckServerHandshakeResponse(eastl::string_view(text.data(), text.size()), kRfcKey);
            }
        };

        void ExpectRefused(const Response& response, std::string_view messagePart) {
            const auto result = response.Check();
            ASSERT_FALSE(result.has_value()) << response.Text();
            EXPECT_NE(std::string(result.error().c_str()).find(messagePart), std::string::npos) << result.error().c_str();
        }

        WebSocketUrl Url(eastl::string_view text) {
            auto url = ParseWebSocketUrl(text);
            EXPECT_TRUE(url.has_value());
            return url ? *url : WebSocketUrl{};
        }

    }  // namespace

    TEST(WebSocketClientHandshakeTest, RequestTextByteForByte) {
        const eastl::string request = BuildClientHandshakeRequest(Url("ws://example.com:8080/rooms?x=1"), kRfcKey);
        EXPECT_EQ(request, "GET /rooms?x=1 HTTP/1.1\r\n"
                           "Host: example.com:8080\r\n"
                           "Upgrade: websocket\r\n"
                           "Connection: Upgrade\r\n"
                           "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                           "Sec-WebSocket-Version: 13\r\n"
                           "\r\n");
    }

    TEST(WebSocketClientHandshakeTest, RequestHasAnOriginOnlyWhenOneIsGiven) {
        const WebSocketUrl url = Url("wss://example.com");
        EXPECT_EQ(BuildClientHandshakeRequest(url, kRfcKey).find("Origin"), eastl::string::npos);
        const eastl::string withOrigin = BuildClientHandshakeRequest(url, kRfcKey, "https://game.example.com");
        EXPECT_EQ(withOrigin, "GET / HTTP/1.1\r\n"
                              "Host: example.com\r\n"
                              "Upgrade: websocket\r\n"
                              "Connection: Upgrade\r\n"
                              "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                              "Sec-WebSocket-Version: 13\r\n"
                              "Origin: https://game.example.com\r\n"
                              "\r\n");
    }

    TEST(WebSocketClientHandshakeTest, TheServerParserAcceptsTheRequestOfTheClient) {
        HandshakePolicy policy;
        policy.loopbackOnly = false;
        const eastl::string request = BuildClientHandshakeRequest(Url("ws://example.com/x"), kRfcKey);
        const auto key = ParseHandshakeRequest(eastl::string_view(request.data(), request.size() - 2), policy);  // the head ends with the empty line, which the reader drops
        ASSERT_TRUE(key.has_value()) << key.error().message.c_str();
        EXPECT_EQ(*key, kRfcKey);
    }

    TEST(WebSocketClientHandshakeTest, ClientKeyIs16BytesInBase64AndDiffersBetweenDraws) {
        std::random_device random;
        const eastl::string first = GenerateClientKey(random);
        const eastl::string second = GenerateClientKey(random);
        EXPECT_EQ(first.size(), 24u);
        EXPECT_TRUE(first.substr(22) == "==");
        EXPECT_NE(first, second);
        // The server's own check of the shape accepts it
        const eastl::string request = BuildClientHandshakeRequest(Url("ws://a.com"), first);
        EXPECT_TRUE(ParseHandshakeRequest(eastl::string_view(request.data(), request.size() - 2), HandshakePolicy{.loopbackOnly = false}).has_value());
    }

    TEST(WebSocketClientHandshakeTest, TheAnswerOfRfc6455Passes) {
        const Response response;
        const auto result = response.Check();
        EXPECT_TRUE(result.has_value()) << result.error().c_str();
    }

    TEST(WebSocketClientHandshakeTest, TheServersOwnAnswerPasses) {
        const eastl::string answer = BuildAcceptResponse(kRfcKey);
        const auto result = CheckServerHandshakeResponse(eastl::string_view(answer.data(), answer.size()), kRfcKey);
        EXPECT_TRUE(result.has_value()) << result.error().c_str();
    }

    TEST(WebSocketClientHandshakeTest, HeaderNamesAndTheConnectionTokenListAreRead) {
        Response response;
        response.headers = {"UPGRADE: WebSocket", "connection: keep-alive, Upgrade", std::string("sec-websocket-accept:  ") + kRfcAccept + "  "};
        const auto result = response.Check();
        EXPECT_TRUE(result.has_value()) << result.error().c_str();
    }

    TEST(WebSocketClientHandshakeTest, AnswerWithoutTheTrailingEmptyLineIsRead) {
        const std::string text = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + std::string(kRfcAccept);
        EXPECT_TRUE(CheckServerHandshakeResponse(eastl::string_view(text.data(), text.size()), kRfcKey).has_value());
    }

    TEST(WebSocketClientHandshakeTest, EachCheckFailsOnItsOwn) {
        {
            Response response;
            response.statusLine = "HTTP/1.1 403 Forbidden";
            ExpectRefused(response, "403");
        }
        {
            Response response;
            response.statusLine = "HTTP/1.1 200 OK";
            ExpectRefused(response, "200");
        }
        {
            Response response;
            response.statusLine = "HTTP/1.0 101 Switching Protocols";
            ExpectRefused(response, "not an HTTP/1.1");
        }
        {
            Response response;
            response.statusLine = "garbage";
            ExpectRefused(response, "not an HTTP/1.1");
        }
        {
            Response response;
            response.statusLine = "HTTP/1.1 1011 x";
            ExpectRefused(response, "not an HTTP/1.1");
        }
        {
            Response response;
            response.Remove("Upgrade");
            ExpectRefused(response, "Upgrade");
        }
        {
            Response response;
            response.Remove("Upgrade").Add("Upgrade: h2c");
            ExpectRefused(response, "Upgrade");
        }
        {
            Response response;
            response.Remove("Connection");
            ExpectRefused(response, "Connection");
        }
        {
            Response response;
            response.Remove("Connection").Add("Connection: close");
            ExpectRefused(response, "Connection");
        }
        {
            Response response;
            response.Remove("Sec-WebSocket-Accept").Add("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOp=");
            ExpectRefused(response, "does not match");
        }
        {
            Response response;
            response.Remove("Sec-WebSocket-Accept").Add("Sec-WebSocket-Accept: S3PPLMBITXAQ9KYGZZHZRBK+XOO=");  // the right letters in the wrong case
            ExpectRefused(response, "does not match");
        }
        {
            Response response;
            response.Remove("Sec-WebSocket-Accept");
            ExpectRefused(response, "lacks 'Sec-WebSocket-Accept'");
        }
        {
            Response response;
            response.Add(std::string("Sec-WebSocket-Accept: ") + kRfcAccept);
            ExpectRefused(response, "repeats");
        }
    }

    TEST(WebSocketClientHandshakeTest, AnExtensionOrSubProtocolTheRequestDidNotAskForIsRefused) {
        {
            Response response;
            response.Add("Sec-WebSocket-Extensions: permessage-deflate");
            ExpectRefused(response, "sec-websocket-extensions");
        }
        {
            Response response;
            response.Add("Sec-WebSocket-Protocol: chat");
            ExpectRefused(response, "sec-websocket-protocol");
        }
    }

    TEST(WebSocketClientHandshakeTest, MalformedHeaderLineAndEmptyAnswerAreRefused) {
        Response response;
        response.Add("no colon here");
        ExpectRefused(response, "no name or no colon");
        const auto empty = CheckServerHandshakeResponse("", kRfcKey);
        ASSERT_FALSE(empty.has_value());
    }

    TEST(WebSocketClientHandshakeTest, AHeadOverTheLimitIsRefused) {
        Response response;
        response.Add("X-Padding: " + std::string(kMaxHandshakeBytes, 'a'));
        ExpectRefused(response, "too long");
    }

    TEST(WebSocketClientHandshakeTest, ARefusalRepeatsTheStatusLineCutAndAsPrintableText) {
        Response response;
        response.statusLine = "HTTP/1.1 503 " + std::string(500, 'x') + "\x1B[2J\x01";
        const auto result = response.Check();
        ASSERT_FALSE(result.has_value());
        EXPECT_NE(std::string(result.error().c_str()).find("HTTP/1.1 503"), std::string::npos);
        EXPECT_LT(result.error().size(), 200u) << "the line of a hostile server is not echoed whole";

        Response escapes;
        escapes.statusLine = "HTTP/1.1 404 \x1B[31mred\x07";
        const auto escaped = escapes.Check();
        ASSERT_FALSE(escaped.has_value());
        for (const char letter : escaped.error()) {
            EXPECT_TRUE(letter >= ' ' && letter <= '~') << "a control character of the server's line reached the log";
        }
    }

    TEST(WebSocketClientHandshakeTest, AFullHeadOfRepeatedTokensIsCheapToRead) {
        constexpr double kLimitMilliseconds = 250.0;  // a linear scan needs a few; the limit is for a Debug build on a loaded machine
        for (const std::string line : {std::string("Upgrade: a\r\n"), std::string("Connection: ,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,\r\n"), std::string("a:\r\n")}) {
            Response response;
            std::string head = response.Text();
            head.erase(head.size() - 2);
            while (head.size() + line.size() + 2 <= kMaxHandshakeBytes) {
                head += line;
            }
            head += "\r\n";
            const auto begin = std::chrono::steady_clock::now();
            (void)CheckServerHandshakeResponse(eastl::string_view(head.data(), head.size()), kRfcKey);
            const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
            EXPECT_LT(elapsed, kLimitMilliseconds) << line;
        }
    }

}  // namespace BECore::Tests
