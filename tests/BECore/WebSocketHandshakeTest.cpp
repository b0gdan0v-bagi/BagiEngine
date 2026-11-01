#include <Common/Printers.h>
#include <RemoteControl/WebSocketHandshake.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kRfcKey = "dGhlIHNhbXBsZSBub25jZQ==";

        eastl::string_view View(const std::string& text) {
            return eastl::string_view(text.data(), text.size());
        }

        bool NameIs(const std::string& line, std::string_view name) {
            if (line.size() <= name.size() || line[name.size()] != ':') {
                return false;
            }
            for (size_t i = 0; i < name.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(line[i])) != std::tolower(static_cast<unsigned char>(name[i]))) {
                    return false;
                }
            }
            return true;
        }

        /// A handshake request that is valid until a test changes it
        class Request {
        public:
            std::string requestLine = "GET /control HTTP/1.1";
            std::vector<std::string> headers = {
                "Host: 127.0.0.1:8765", "Upgrade: websocket", "Connection: Upgrade", std::string("Sec-WebSocket-Key: ") + kRfcKey, "Sec-WebSocket-Version: 13",
            };

            /// Replaces the header @p name (any case) or adds it
            Request& Set(std::string_view name, std::string_view value) {
                const std::string line = std::string(name) + ": " + std::string(value);
                for (std::string& existing : headers) {
                    if (NameIs(existing, name)) {
                        existing = line;
                        return *this;
                    }
                }
                headers.push_back(line);
                return *this;
            }

            Request& Remove(std::string_view name) {
                std::erase_if(headers, [&](const std::string& line) { return NameIs(line, name); });
                return *this;
            }

            Request& Add(std::string_view line) {
                headers.emplace_back(line);
                return *this;
            }

            [[nodiscard]] std::string Text() const {
                std::string text = requestLine + "\r\n";
                for (const std::string& line : headers) {
                    text += line + "\r\n";
                }
                return text + "\r\n";
            }
        };

        std::expected<eastl::string, HandshakeError> Parse(const Request& request) {
            const std::string text = request.Text();
            return ParseHandshakeRequest(View(text));
        }

        void ExpectRefused(const std::expected<eastl::string, HandshakeError>& result, int status) {
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().httpStatus, status) << result.error().message.c_str();
            EXPECT_FALSE(result.error().message.empty());
        }

    }  // namespace

    TEST(WebSocketHandshakeTest, AcceptKeyOfRfc6455) {
        EXPECT_EQ(ComputeAcceptKey(kRfcKey), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    }

    TEST(WebSocketHandshakeTest, ValidRequestGivesTheClientKey) {
        const auto key = Parse(Request());
        ASSERT_TRUE(key.has_value()) << key.error().message.c_str();
        EXPECT_EQ(*key, kRfcKey);
    }

    TEST(WebSocketHandshakeTest, HeaderNamesAndTokensAreCaseInsensitive) {
        Request request;
        request.Remove("Host").Remove("Upgrade").Remove("Connection");
        request.Add("hOST: LocalHost:8765").Add("UPGRADE: WebSocket").Add("connection: keep-alive,  upgrade");
        EXPECT_TRUE(Parse(request).has_value());
    }

    TEST(WebSocketHandshakeTest, BareNewlinesEndTheLinesToo) {
        const std::string text = "GET / HTTP/1.1\nHost: localhost\nUpgrade: websocket\nConnection: Upgrade\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\nSec-WebSocket-Version: 13\n\n";
        EXPECT_TRUE(ParseHandshakeRequest(View(text)).has_value());
    }

    TEST(WebSocketHandshakeTest, RepeatedListHeadersAreJoined) {
        Request request;
        request.Set("Connection", "keep-alive").Add("Connection: Upgrade");
        EXPECT_TRUE(Parse(request).has_value());
    }

    TEST(WebSocketHandshakeTest, LoopbackHostsAreAccepted) {
        for (const char* host : {"127.0.0.1", "127.0.0.1:8765", "localhost", "LOCALHOST:3000", "[::1]", "[::1]:8765"}) {
            EXPECT_TRUE(Parse(Request().Set("Host", host)).has_value()) << host;
        }
    }

    TEST(WebSocketHandshakeTest, OtherHostsAreForbidden) {
        // A page of another site that resolves its name to 127.0.0.1 (DNS rebinding) sends its own name as the Host
        for (const char* host : {"example.com", "example.com:8765", "127.0.0.2", "127.0.0.1.evil.com", "localhost.evil.com", "evil.com:8765", "127.0.0.1:abc", "127.0.0.1:123456", "127.0.0.1:", "[::1",
                                 "[::2]", "[::1]x", "::1", ""}) {
            SCOPED_TRACE(host);
            ExpectRefused(Parse(Request().Set("Host", host)), 403);
        }
    }

    TEST(WebSocketHandshakeTest, MissingHostIsABadRequest) {
        ExpectRefused(Parse(Request().Remove("Host")), 400);
    }

    TEST(WebSocketHandshakeTest, NoOriginIsFineForProgramsAndLoopbackPagesAreFine) {
        EXPECT_TRUE(Parse(Request()).has_value());
        for (const char* origin : {"http://127.0.0.1:3000", "http://localhost", "https://localhost:8443", "http://[::1]:5000", "http://localhost/index.html"}) {
            EXPECT_TRUE(Parse(Request().Set("Origin", origin)).has_value()) << origin;
        }
    }

    TEST(WebSocketHandshakeTest, OriginOfAnotherPageIsForbidden) {
        for (const char* origin : {"http://evil.example", "https://evil.example:8765", "http://localhost.evil.example", "http://127.0.0.1.evil.example", "http://localhost:80@evil.example", "null",
                                   "file://", "ftp://localhost", "localhost", ""}) {
            SCOPED_TRACE(origin);
            ExpectRefused(Parse(Request().Set("Origin", origin)), 403);
        }
    }

    TEST(WebSocketHandshakeTest, UpgradeHeadersAreRequired) {
        ExpectRefused(Parse(Request().Remove("Upgrade")), 400);
        ExpectRefused(Parse(Request().Set("Upgrade", "h2c")), 400);
        ExpectRefused(Parse(Request().Remove("Connection")), 400);
        ExpectRefused(Parse(Request().Set("Connection", "keep-alive")), 400);
    }

    TEST(WebSocketHandshakeTest, OtherVersionsAreAskedToUseThirteen) {
        ExpectRefused(Parse(Request().Set("Sec-WebSocket-Version", "8")), 426);
        ExpectRefused(Parse(Request().Set("Sec-WebSocket-Version", "")), 426);
        ExpectRefused(Parse(Request().Remove("Sec-WebSocket-Version")), 426);
    }

    TEST(WebSocketHandshakeTest, KeyHasToBeSixteenBytesInBase64) {
        ExpectRefused(Parse(Request().Remove("Sec-WebSocket-Key")), 400);
        ExpectRefused(Parse(Request().Set("Sec-WebSocket-Key", "")), 400);
        ExpectRefused(Parse(Request().Set("Sec-WebSocket-Key", "dGhlIHNhbXBsZQ==")), 400);           // 10 bytes
        ExpectRefused(Parse(Request().Set("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ")), 400);     // no padding
        ExpectRefused(Parse(Request().Set("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ===")), 400);  // too long
        ExpectRefused(Parse(Request().Set("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25j!Q==")), 400);   // not base64
    }

    TEST(WebSocketHandshakeTest, RequestLineHasToBeGetOverHttp11) {
        for (const char* line : {"POST /control HTTP/1.1", "GET /control HTTP/1.0", "GET /control", "GET  HTTP/1.1", "get /control HTTP/1.1", ""}) {
            Request request;
            request.requestLine = line;
            SCOPED_TRACE(line);
            ExpectRefused(Parse(request), 400);
        }
    }

    TEST(WebSocketHandshakeTest, AnyTargetIsAccepted) {
        for (const char* line : {"GET / HTTP/1.1", "GET /control?x=1 HTTP/1.1", "GET /a/b/c HTTP/1.1"}) {
            Request request;
            request.requestLine = line;
            EXPECT_TRUE(Parse(request).has_value()) << line;
        }
    }

    TEST(WebSocketHandshakeTest, RepeatedSingleValueHeadersAreRefused) {
        ExpectRefused(Parse(Request().Add("Host: localhost")), 400);
        ExpectRefused(Parse(Request().Add("Origin: http://localhost").Add("Origin: http://localhost")), 400);
        ExpectRefused(Parse(Request().Add("Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==")), 400);
        ExpectRefused(Parse(Request().Add("Sec-WebSocket-Version: 13")), 400);
    }

    TEST(WebSocketHandshakeTest, MalformedHeaderLinesAreRefused) {
        ExpectRefused(Parse(Request().Add("this line has no colon")), 400);
        ExpectRefused(Parse(Request().Add(": no name")), 400);
        ExpectRefused(Parse(Request().Add(" folded: line")), 400);
    }

    TEST(WebSocketHandshakeTest, HeadOverTheLimitIsRefused) {
        const std::string text = Request().Add("X-Padding: " + std::string(kMaxHandshakeBytes, 'x')).Text();
        ExpectRefused(ParseHandshakeRequest(View(text)), 431);
    }

    TEST(WebSocketHandshakeTest, AcceptResponseCompletesTheHandshake) {
        const std::string response = BuildAcceptResponse(kRfcKey).c_str();
        EXPECT_TRUE(response.starts_with("HTTP/1.1 101 Switching Protocols\r\n"));
        EXPECT_NE(response.find("Upgrade: websocket\r\n"), std::string::npos);
        EXPECT_NE(response.find("Connection: Upgrade\r\n"), std::string::npos);
        EXPECT_NE(response.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"), std::string::npos);
        EXPECT_TRUE(response.ends_with("\r\n\r\n"));
    }

    TEST(WebSocketHandshakeTest, RejectResponseCarriesTheStatusAndTheMessage) {
        const std::string response = BuildRejectResponse(HandshakeError{403, "no way"}).c_str();
        EXPECT_TRUE(response.starts_with("HTTP/1.1 403 Forbidden\r\n"));
        EXPECT_NE(response.find("Connection: close\r\n"), std::string::npos);
        EXPECT_NE(response.find("Content-Length: 6\r\n"), std::string::npos);
        EXPECT_TRUE(response.ends_with("\r\n\r\nno way"));
        EXPECT_EQ(response.find("Sec-WebSocket-Version"), std::string::npos);
    }

    TEST(WebSocketHandshakeTest, UpgradeRequiredNamesTheVersion) {
        const std::string response = BuildRejectResponse(HandshakeError{426, "use 13"}).c_str();
        EXPECT_TRUE(response.starts_with("HTTP/1.1 426 Upgrade Required\r\n"));
        EXPECT_NE(response.find("Sec-WebSocket-Version: 13\r\n"), std::string::npos);
    }

    TEST(WebSocketHandshakeTest, EveryRefusalStatusHasItsReasonPhrase) {
        EXPECT_TRUE(std::string(BuildRejectResponse(HandshakeError{400, "x"}).c_str()).starts_with("HTTP/1.1 400 Bad Request\r\n"));
        EXPECT_TRUE(std::string(BuildRejectResponse(HandshakeError{431, "x"}).c_str()).starts_with("HTTP/1.1 431 Request Header Fields Too Large\r\n"));
        EXPECT_TRUE(std::string(BuildRejectResponse(HandshakeError{503, "x"}).c_str()).starts_with("HTTP/1.1 503 Service Unavailable\r\n"));
    }

}  // namespace BECore::Tests
