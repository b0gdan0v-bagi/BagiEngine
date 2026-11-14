#include <Common/Printers.h>
#include <RemoteControl/WebSocketHandshake.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kRfcKey = "dGhlIHNhbXBsZSBub25jZQ==";

        StringView View(const std::string& text) {
            return FromStd(text);
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

        HandshakePolicy PublicPolicy(std::initializer_list<const char*> origins = {}) {
            HandshakePolicy policy;
            policy.loopbackOnly = false;
            for (const char* origin : origins) {
                policy.allowedOrigins.push_back(origin);
            }
            return policy;
        }

        std::expected<eastl::string, HandshakeError> Parse(const Request& request, const HandshakePolicy& policy) {
            const std::string text = request.Text();
            return ParseHandshakeRequest(View(text), policy);
        }

        bool Allowed(const char* origin, const HandshakePolicy& policy) {
            return IsOriginAllowed(origin, policy);
        }

        void ExpectRefused(const std::expected<eastl::string, HandshakeError>& result, int status) {
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().httpStatus, status) << result.error().message.c_str();
            EXPECT_FALSE(result.error().message.empty());
        }

        HandshakePolicy HealthPolicy() {
            HandshakePolicy policy = PublicPolicy();
            policy.healthPath = "/healthz";
            return policy;
        }

        bool IsHealth(const std::string& text, const HandshakePolicy& policy) {
            return IsHealthCheckRequest(View(text), policy);
        }

        std::string Forwarded(const Request& request, size_t hops) {
            const std::string text = request.Text();
            const eastl::string address = ParseForwardedClientAddress(View(text), hops);
            return std::string(address.data(), address.size());
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

    // Public mode: the origin allow-list instead of the loopback checks

    TEST(WebSocketHandshakeTest, DefaultPolicyIsTheLoopbackPolicy) {
        const HandshakePolicy policy;
        EXPECT_TRUE(policy.loopbackOnly);
        EXPECT_TRUE(policy.allowedOrigins.empty());
        EXPECT_TRUE(policy.allowMissingOrigin);
        EXPECT_TRUE(Parse(Request(), policy).has_value());
        ExpectRefused(Parse(Request().Set("Host", "example.com"), policy), 403);
        ExpectRefused(Parse(Request().Set("Origin", "https://good.example"), policy), 403);
        EXPECT_TRUE(IsOriginAllowed("http://localhost:3000", policy));
        EXPECT_FALSE(IsOriginAllowed("https://good.example", policy));
    }

    TEST(WebSocketHandshakeTest, PublicPolicyAcceptsAListedOriginAndRefusesAForeignOne) {
        const HandshakePolicy policy = PublicPolicy({"https://good.example", "http://localhost:3000"});
        EXPECT_TRUE(Parse(Request().Set("Origin", "https://good.example"), policy).has_value());
        EXPECT_TRUE(Parse(Request().Set("Origin", "http://localhost:3000"), policy).has_value());
        ExpectRefused(Parse(Request().Set("Origin", "https://evil.example"), policy), 403);
        ExpectRefused(Parse(Request().Set("Origin", "http://localhost:3001"), policy), 403);
        // The loopback is not special in public mode: it has to be listed
        ExpectRefused(Parse(Request().Set("Origin", "http://127.0.0.1:3000"), policy), 403);
    }

    TEST(WebSocketHandshakeTest, PublicPolicyDoesNotCheckTheHostButNeedsOne) {
        const HandshakePolicy policy = PublicPolicy({"https://good.example"});
        EXPECT_TRUE(Parse(Request().Set("Host", "rooms.example.com"), policy).has_value());
        EXPECT_TRUE(Parse(Request().Set("Host", "10.0.0.7:8080"), policy).has_value());
        ExpectRefused(Parse(Request().Remove("Host"), policy), 400);
        ExpectRefused(Parse(Request().Set("Host", "rooms.example.com")), 403);  // the loopback mode refuses the same request
    }

    TEST(WebSocketHandshakeTest, PublicPolicyWithNoEntriesRefusesBrowsersAndAdmitsPrograms) {
        const HandshakePolicy policy = PublicPolicy();
        EXPECT_TRUE(Parse(Request(), policy).has_value());  // no Origin: a program
        ExpectRefused(Parse(Request().Set("Origin", "https://good.example"), policy), 403);
        ExpectRefused(Parse(Request().Set("Origin", "null"), policy), 403);
        ExpectRefused(Parse(Request().Set("Origin", ""), policy), 403);
    }

    TEST(WebSocketHandshakeTest, PublicPolicyCanRefuseProgramsToo) {
        HandshakePolicy policy = PublicPolicy({"https://good.example"});
        policy.allowMissingOrigin = false;
        ExpectRefused(Parse(Request(), policy), 403);
        EXPECT_TRUE(Parse(Request().Set("Origin", "https://good.example"), policy).has_value());
        // The flag belongs to the public mode: the loopback mode always admits programs
        HandshakePolicy loopback;
        loopback.allowMissingOrigin = false;
        EXPECT_TRUE(Parse(Request(), loopback).has_value());
    }

    TEST(WebSocketHandshakeTest, StarAllowsEveryOrigin) {
        const HandshakePolicy policy = PublicPolicy({"*"});
        for (const char* origin : {"https://anything.example", "http://127.0.0.1:1", "null", "garbage", ""}) {
            EXPECT_TRUE(Allowed(origin, policy)) << origin;
        }
        EXPECT_TRUE(Parse(Request().Set("Origin", "https://anything.example"), policy).has_value());
        EXPECT_TRUE(IsOriginAllowed("https://a.example", PublicPolicy({"https://b.example", "*"})));
    }

    TEST(WebSocketHandshakeTest, NullOriginNeedsItsOwnEntry) {
        EXPECT_FALSE(Allowed("null", PublicPolicy({"https://good.example"})));
        EXPECT_FALSE(Allowed("null", PublicPolicy()));
        EXPECT_TRUE(Allowed("null", PublicPolicy({"https://good.example", "null"})));
        EXPECT_TRUE(Parse(Request().Set("Origin", "null"), PublicPolicy({"null"})).has_value());
        // The entry `null` admits nothing but the null origin
        EXPECT_FALSE(Allowed("https://good.example", PublicPolicy({"null"})));
        EXPECT_FALSE(Allowed("https://null", PublicPolicy({"null"})));
    }

    TEST(WebSocketHandshakeTest, SchemeAndHostAreComparedInAnyCase) {
        const HandshakePolicy policy = PublicPolicy({"https://Good.Example"});
        EXPECT_TRUE(Allowed("https://good.example", policy));
        EXPECT_TRUE(Allowed("HTTPS://GOOD.EXAMPLE", policy));
        EXPECT_TRUE(Allowed("HtTpS://gOoD.eXaMpLe", PublicPolicy({"https://good.example"})));
    }

    TEST(WebSocketHandshakeTest, PortsAreComparedAsNumbersWithTheDefaultOfTheScheme) {
        EXPECT_TRUE(Allowed("https://a.example:443", PublicPolicy({"https://a.example"})));
        EXPECT_TRUE(Allowed("https://a.example", PublicPolicy({"https://a.example:443"})));
        EXPECT_TRUE(Allowed("http://a.example:80", PublicPolicy({"http://a.example"})));
        EXPECT_TRUE(Allowed("http://a.example", PublicPolicy({"http://a.example:80"})));
        EXPECT_TRUE(Allowed("http://a.example:8080", PublicPolicy({"http://a.example:8080"})));
        EXPECT_TRUE(Allowed("http://a.example:08080", PublicPolicy({"http://a.example:8080"})));

        EXPECT_FALSE(Allowed("https://a.example:8443", PublicPolicy({"https://a.example"})));
        EXPECT_FALSE(Allowed("https://a.example", PublicPolicy({"https://a.example:8443"})));
        EXPECT_FALSE(Allowed("https://a.example:80", PublicPolicy({"https://a.example"})));  // 80 is the default of http, not of https
        EXPECT_FALSE(Allowed("http://a.example:443", PublicPolicy({"http://a.example"})));
        EXPECT_FALSE(Allowed("http://a.example:8081", PublicPolicy({"http://a.example:8080"})));
    }

    TEST(WebSocketHandshakeTest, OtherSchemesHaveNoDefaultPort) {
        EXPECT_TRUE(Allowed("capacitor://localhost", PublicPolicy({"capacitor://localhost"})));
        EXPECT_FALSE(Allowed("capacitor://localhost:80", PublicPolicy({"capacitor://localhost"})));
        EXPECT_FALSE(Allowed("capacitor://localhost", PublicPolicy({"http://localhost"})));
    }

    TEST(WebSocketHandshakeTest, ADifferentSchemeIsAnotherOrigin) {
        EXPECT_FALSE(Allowed("http://good.example", PublicPolicy({"https://good.example"})));
        EXPECT_FALSE(Allowed("https://good.example", PublicPolicy({"http://good.example"})));
        EXPECT_FALSE(Allowed("http://good.example:443", PublicPolicy({"https://good.example"})));
    }

    TEST(WebSocketHandshakeTest, ATrailingSlashIsTheSameOriginButAPathIsNot) {
        EXPECT_TRUE(Allowed("https://good.example/", PublicPolicy({"https://good.example"})));
        EXPECT_TRUE(Allowed("https://good.example", PublicPolicy({"https://good.example/"})));
        EXPECT_FALSE(Allowed("https://good.example//", PublicPolicy({"https://good.example"})));
        EXPECT_FALSE(Allowed("https://good.example/index.html", PublicPolicy({"https://good.example"})));
        EXPECT_FALSE(Allowed("https://good.example/?x", PublicPolicy({"https://good.example"})));
        EXPECT_FALSE(Allowed("https://good.example#frag", PublicPolicy({"https://good.example"})));
        EXPECT_FALSE(Allowed("https://good.example:443/x", PublicPolicy({"https://good.example"})));
        // An entry with a path matches nothing
        EXPECT_FALSE(Allowed("https://good.example", PublicPolicy({"https://good.example/app"})));
        EXPECT_FALSE(Allowed("https://good.example/app", PublicPolicy({"https://good.example/app"})));
    }

    TEST(WebSocketHandshakeTest, UserinfoSpacesAndControlCharactersNeverMatch) {
        const HandshakePolicy policy = PublicPolicy({"https://good.example"});
        for (const char* origin : {"https://user@good.example",
                                   "https://good.example@evil.example",
                                   "https://evil.example@good.example",
                                   "https://user:pass@good.example",
                                   "https://good .example",
                                   "https://good.example\r",
                                   "https://good\x01.example",
                                   "https://good.example\\evil",
                                   "https://good.example:",
                                   "https://good.example:0",
                                   "https://good.example:65536",
                                   "https://good.example:443x",
                                   "https://good.example:4:43",
                                   "https://",
                                   "://good.example",
                                   "good.example",
                                   "https:good.example",
                                   "https:/good.example",
                                   "https://good_.example%00",
                                   "https://good.example."}) {
            SCOPED_TRACE(origin);
            EXPECT_FALSE(Allowed(origin, policy));
            ExpectRefused(Parse(Request().Set("Origin", origin), policy), 403);
        }
        // The header line is trimmed of spaces and tabs around the value (HTTP), the function itself does not trim
        for (const char* origin : {"https://good.example ", " https://good.example", "https://good.example\t"}) {
            SCOPED_TRACE(origin);
            EXPECT_FALSE(Allowed(origin, policy));
        }
        // An entry that carries one of these matches nothing, not even itself
        for (const char* entry : {"https://user@good.example", "https://good.example ", "https://good.example:", "https://"}) {
            SCOPED_TRACE(entry);
            EXPECT_FALSE(Allowed(entry, PublicPolicy({entry})));
        }
    }

    TEST(WebSocketHandshakeTest, LookAlikesAreNotMatched) {
        const HandshakePolicy policy = PublicPolicy({"https://good.com"});
        for (const char* origin :
             {"https://good.com.evil.org", "https://evil.org/https://good.com", "https://evil.org?https://good.com", "https://notgood.com", "https://good.comx", "https://sub.good.com",
              "https://good.co", "https://good.com:443.evil.org", "https://evil.org#@good.com", "https://good.com@evil.org", "https://xgood.com", "https://good-.com"}) {
            SCOPED_TRACE(origin);
            EXPECT_FALSE(Allowed(origin, policy));
        }
        EXPECT_FALSE(Allowed("https://good.com", PublicPolicy({"https://sub.good.com"})));
        EXPECT_FALSE(Allowed("https://good.com", PublicPolicy({"https://*.good.com"})));  // no wildcard inside an entry
        EXPECT_FALSE(Allowed("https://a.good.com", PublicPolicy({"https://*.good.com"})));
        EXPECT_FALSE(Allowed("https://good.com", PublicPolicy({"good.com"})));
    }

    TEST(WebSocketHandshakeTest, IpAddressesAndBracketedHostsAreOrigins) {
        EXPECT_TRUE(Allowed("http://192.168.1.5:8080", PublicPolicy({"http://192.168.1.5:8080"})));
        EXPECT_FALSE(Allowed("http://192.168.1.6:8080", PublicPolicy({"http://192.168.1.5:8080"})));
        EXPECT_TRUE(Allowed("http://[::1]:5000", PublicPolicy({"http://[::1]:5000"})));
        EXPECT_TRUE(Allowed("http://[::1]", PublicPolicy({"http://[::1]:80"})));
        EXPECT_FALSE(Allowed("http://[::1", PublicPolicy({"http://[::1"})));
        EXPECT_FALSE(Allowed("http://[]", PublicPolicy({"http://[]"})));
        EXPECT_FALSE(Allowed("http://[::1]x", PublicPolicy({"http://[::1]"})));
        EXPECT_FALSE(Allowed("http://[::1]:", PublicPolicy({"http://[::1]"})));
    }

    TEST(WebSocketHandshakeTest, TheFirstMatchingEntryIsEnoughAndAnUnparsableEntryDoesNotStopTheSearch) {
        const HandshakePolicy policy = PublicPolicy({"nonsense", "https://x.example/path", "https://good.example"});
        EXPECT_TRUE(Allowed("https://good.example", policy));
        EXPECT_FALSE(Allowed("https://other.example", policy));
    }

    TEST(WebSocketHandshakeTest, PublicRefusalNamesTheOriginCutToOneHundredBytes) {
        const HandshakePolicy policy = PublicPolicy({"https://good.example"});
        const auto refused = Parse(Request().Set("Origin", "https://evil.example"), policy);
        ExpectRefused(refused, 403);
        EXPECT_NE(refused.error().message.find("https://evil.example"), eastl::string::npos);

        const std::string huge = "https://" + std::string(6000, 'a') + ".example";
        const auto cut = Parse(Request().Set("Origin", huge), policy);
        ExpectRefused(cut, 403);
        EXPECT_LT(cut.error().message.size(), 200u);
        EXPECT_NE(cut.error().message.find(std::string(90, 'a').c_str()), eastl::string::npos);
        EXPECT_EQ(cut.error().message.find(std::string(150, 'a').c_str()), eastl::string::npos);

        // Bytes that are not printable ASCII never reach the response
        const auto odd = Parse(Request().Set("Origin", "https://\x01\xC3\xA9.example"), policy);
        ExpectRefused(odd, 403);
        for (const char letter : odd.error().message) {
            EXPECT_TRUE(letter >= ' ' && letter <= '~');
        }
    }

    TEST(WebSocketHandshakeTest, AnOriginThatFillsTheWholeHeadIsRefusedAndOneOverTheLimitIs431) {
        const HandshakePolicy policy = PublicPolicy({"https://good.example"});
        const std::string almost = "https://" + std::string(kMaxHandshakeBytes - 400, 'a');
        ExpectRefused(Parse(Request().Set("Origin", almost), policy), 403);
        const std::string tooMuch = "https://" + std::string(kMaxHandshakeBytes, 'a');
        ExpectRefused(Parse(Request().Set("Origin", tooMuch), policy), 431);
    }

    TEST(WebSocketHandshakeTest, PublicPolicyKeepsEveryOtherCheckOfTheHandshake) {
        const HandshakePolicy policy = PublicPolicy({"https://good.example"});
        ExpectRefused(Parse(Request().Set("Sec-WebSocket-Version", "8"), policy), 426);
        ExpectRefused(Parse(Request().Remove("Upgrade"), policy), 400);
        ExpectRefused(Parse(Request().Remove("Connection"), policy), 400);
        ExpectRefused(Parse(Request().Set("Sec-WebSocket-Key", "short"), policy), 400);
        ExpectRefused(Parse(Request().Add("Origin: https://good.example").Add("Origin: https://good.example"), policy), 400);
        Request post;
        post.requestLine = "POST /x HTTP/1.1";
        ExpectRefused(Parse(post, policy), 400);
        const std::string text = Request().Add("X-Padding: " + std::string(kMaxHandshakeBytes, 'x')).Text();
        ExpectRefused(ParseHandshakeRequest(View(text), policy), 431);
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

    TEST(WebSocketHandshakeTest, HealthCheckIsAPlainGetOfTheHealthPath) {
        const HandshakePolicy policy = HealthPolicy();
        EXPECT_TRUE(IsHealth("GET /healthz HTTP/1.1\r\nHost: game.example.net\r\n\r\n", policy));
        EXPECT_TRUE(IsHealth("GET /healthz HTTP/1.1\r\n\r\n", policy)) << "a probe may send no Host";
        EXPECT_TRUE(IsHealth("GET /healthz HTTP/1.1\nHost: a\n\n", policy));
        EXPECT_TRUE(IsHealth("GET /healthz HTTP/1.1\r\nHost: a\r\nOrigin: https://evil.example\r\n\r\n", policy)) << "Origin is not checked";
    }

    TEST(WebSocketHandshakeTest, HealthCheckIgnoresTheQueryAndAcceptsHttp10) {
        const HandshakePolicy policy = HealthPolicy();
        EXPECT_TRUE(IsHealth("GET /healthz?probe=1 HTTP/1.1\r\nHost: a\r\n\r\n", policy));
        EXPECT_TRUE(IsHealth("GET /healthz? HTTP/1.1\r\nHost: a\r\n\r\n", policy));
        EXPECT_TRUE(IsHealth("GET /healthz HTTP/1.0\r\n\r\n", policy));
    }

    TEST(WebSocketHandshakeTest, HealthCheckNeedsTheExactPathAndMethod) {
        const HandshakePolicy policy = HealthPolicy();
        for (const char* line : {"GET / HTTP/1.1", "GET /healthz/ HTTP/1.1", "GET /health HTTP/1.1", "GET /healthzz HTTP/1.1", "GET /HEALTHZ HTTP/1.1", "GET /control HTTP/1.1",
                                 "POST /healthz HTTP/1.1", "HEAD /healthz HTTP/1.1", "GET /healthz HTTP/2", "GET /healthz", "GET  HTTP/1.1", ""}) {
            EXPECT_FALSE(IsHealth(std::string(line) + "\r\nHost: a\r\n\r\n", policy)) << line;
        }
    }

    TEST(WebSocketHandshakeTest, HealthCheckWithUpgradeIsAHandshakeLikeAnyOther) {
        const HandshakePolicy policy = HealthPolicy();
        Request request;
        request.requestLine = "GET /healthz HTTP/1.1";
        request.Set("Host", "game.example.net");
        EXPECT_FALSE(IsHealth(request.Text(), policy));
        EXPECT_TRUE(Parse(request, policy).has_value()) << "any path opens the same endpoint";
        EXPECT_FALSE(IsHealth("GET /healthz HTTP/1.1\r\nHost: a\r\nUpgrade: h2c\r\n\r\n", policy));
    }

    TEST(WebSocketHandshakeTest, HealthCheckIsOffForLoopbackAndForAnEmptyPath) {
        const std::string request = "GET /healthz HTTP/1.1\r\nHost: a\r\n\r\n";
        HandshakePolicy loopback = HealthPolicy();
        loopback.loopbackOnly = true;
        EXPECT_FALSE(IsHealth(request, loopback));
        EXPECT_FALSE(IsHealth(request, HandshakePolicy{}));
        EXPECT_FALSE(IsHealth(request, PublicPolicy()));
        EXPECT_FALSE(IsHealth("GET  HTTP/1.1\r\n\r\n", PublicPolicy())) << "an empty path is not a health path";
    }

    TEST(WebSocketHandshakeTest, HealthCheckWithAMalformedHeadIsLeftToTheHandshake) {
        const HandshakePolicy policy = HealthPolicy();
        EXPECT_FALSE(IsHealth("GET /healthz HTTP/1.1\r\nHost: a\r\n folded\r\n\r\n", policy));
        EXPECT_FALSE(IsHealth("GET /healthz HTTP/1.1\r\nno colon here\r\n\r\n", policy));
        EXPECT_FALSE(IsHealth("GET /healthz HTTP/1.1\r\nHost: a\r\n\r\n" + std::string(kMaxHandshakeBytes, 'x'), policy));
    }

    TEST(WebSocketHandshakeTest, HealthResponseIsAnOkThatClosesTheConnection) {
        const std::string response = BuildHealthResponse().c_str();
        EXPECT_TRUE(response.starts_with("HTTP/1.1 200 OK\r\n"));
        EXPECT_NE(response.find("Content-Type: text/plain\r\n"), std::string::npos);
        EXPECT_NE(response.find("Cache-Control: no-store\r\n"), std::string::npos);
        EXPECT_NE(response.find("Connection: close\r\n"), std::string::npos);
        EXPECT_NE(response.find("Content-Length: 2\r\n"), std::string::npos);
        EXPECT_TRUE(response.ends_with("\r\n\r\nok"));
    }

    // What a head of the largest allowed size costs: heads built to hit a scan that is more than linear

    namespace {

        /// Longest time a head of kMaxHandshakeBytes of one repeated token may take, a Debug build on a loaded machine included (a linear scan needs a few milliseconds)
        constexpr double kScanLimitMilliseconds = 250.0;

        template <typename TFunction>
        double ElapsedMilliseconds(TFunction&& work) {
            const auto begin = std::chrono::steady_clock::now();
            work();
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        }

        /// A request head of about kMaxHandshakeBytes: the valid lines, then @p padding repeated up to the limit
        std::string FullHead(const std::string& padding, const std::string& fixedLinesBefore = {}) {
            std::string text = "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n" + fixedLinesBefore;
            const std::string tail = "Connection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
            while (text.size() + padding.size() + tail.size() <= kMaxHandshakeBytes) {
                text += padding;
            }
            return text + tail;
        }

    }  // namespace

    TEST(WebSocketHandshakeTest, AFullHeadOfRepeatedTokensIsCheapToRead) {
        const std::vector<std::string> heads = {
            FullHead("X-A: b\r\n"),                                        // as many header lines as fit
            FullHead("Upgrade: a\r\n"),                                    // a list header joined over and over
            FullHead("Connection: ,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,\r\n"),  // long lists of empty tokens
            FullHead("a:\r\n"),                                            // the shortest valid line
        };
        for (const std::string& head : heads) {
            ASSERT_LE(head.size(), kMaxHandshakeBytes);
            const HandshakePolicy policy = PublicPolicy();
            EXPECT_LT(ElapsedMilliseconds([&] { (void)ParseHandshakeRequest(View(head), HandshakePolicy{}); }), kScanLimitMilliseconds);
            EXPECT_LT(ElapsedMilliseconds([&] { (void)ParseHandshakeRequest(View(head), policy); }), kScanLimitMilliseconds);
            EXPECT_LT(ElapsedMilliseconds([&] { (void)IsHealthCheckRequest(View(head), HealthPolicy()); }), kScanLimitMilliseconds);
        }
        // One header line that is a single list: the token scan is the cost
        const std::string oneLongList = "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: " + std::string(kMaxHandshakeBytes - 200, ',') + "\r\n\r\n";
        EXPECT_LT(ElapsedMilliseconds([&] { (void)ParseHandshakeRequest(View(oneLongList), HandshakePolicy{}); }), kScanLimitMilliseconds);
    }

    TEST(WebSocketHandshakeTest, TokenSearchDoesNotAllocatePerToken) {
        const std::string commas(kMaxHandshakeBytes, ',');
        EXPECT_LT(ElapsedMilliseconds([&] { EXPECT_FALSE(HeaderHasToken(View(commas), "upgrade")); }), kScanLimitMilliseconds);
        EXPECT_TRUE(HeaderHasToken(View(commas + " Upgrade"), "upgrade"));
        EXPECT_TRUE(EqualsIgnoreCase("WebSocket", "websocket"));
        EXPECT_FALSE(EqualsIgnoreCase("websockets", "websocket"));
        EXPECT_FALSE(EqualsIgnoreCase("websocke!", "websocket"));
    }

    TEST(WebSocketHandshakeTest, RefusalMessagesQuoteWhatThePeerSent) {
        const std::string longHost = std::string(1000, 'h') + "\x01";
        const auto refusedHost = Parse(Request().Set("Host", longHost));
        ExpectRefused(refusedHost, 403);
        EXPECT_LT(refusedHost.error().message.size(), 200u) << "a loopback refusal repeats the Host cut like a public one repeats the Origin";

        const auto refusedOrigin = Parse(Request().Set("Origin", "http://evil.example\x01\x7F\xC3"));
        ExpectRefused(refusedOrigin, 403);
        for (const char letter : refusedOrigin.error().message) {
            EXPECT_TRUE(letter >= ' ' && letter <= '~') << "only printable ASCII is repeated";
        }
    }

    TEST(WebSocketHandshakeTest, QuoteForMessageCutsAndMasks) {
        EXPECT_EQ(QuoteForMessage("plain"), "plain");
        EXPECT_EQ(QuoteForMessage("a\r\nb\x7F\xC3"), "a??b??");
        const eastl::string cut = QuoteForMessage(View(std::string(500, 'x')));
        EXPECT_EQ(cut.size(), kMaxQuotedBytes + 3);
        EXPECT_TRUE(StringView(cut).ends_with("..."));
        EXPECT_EQ(QuoteForMessage(View(std::string(kMaxQuotedBytes, 'x'))).size(), kMaxQuotedBytes);
    }

    TEST(WebSocketHandshakeTest, ForwardedClientIsTheEntryFromTheRight) {
        const Request one = Request().Add("X-Forwarded-For: 203.0.113.9");
        EXPECT_EQ(Forwarded(one, 1), "203.0.113.9");

        const Request chain = Request().Add("X-Forwarded-For: 198.51.100.7, 203.0.113.9, 10.0.0.2");
        EXPECT_EQ(Forwarded(chain, 1), "10.0.0.2");
        EXPECT_EQ(Forwarded(chain, 2), "203.0.113.9");
        EXPECT_EQ(Forwarded(chain, 3), "198.51.100.7");
        EXPECT_EQ(Forwarded(chain, 4), "") << "fewer entries than hops";
    }

    TEST(WebSocketHandshakeTest, ForgedEntriesOnTheLeftAreNeverRead) {
        const Request forged = Request().Add("X-Forwarded-For: 1.1.1.1, 203.0.113.9");
        EXPECT_EQ(Forwarded(forged, 1), "203.0.113.9");
        const Request padded = Request().Add("X-Forwarded-For: 1.1.1.1, 2.2.2.2, 3.3.3.3, 203.0.113.9");
        EXPECT_EQ(Forwarded(padded, 1), "203.0.113.9");
        EXPECT_EQ(Forwarded(padded, 2), "3.3.3.3");
    }

    TEST(WebSocketHandshakeTest, ForwardedClientIsOffWithoutHopsOrHeader) {
        const Request with = Request().Add("X-Forwarded-For: 203.0.113.9");
        EXPECT_EQ(Forwarded(with, 0), "");
        EXPECT_EQ(Forwarded(Request(), 1), "");
        EXPECT_EQ(Forwarded(Request().Add("X-Forwarded-For:"), 1), "");
    }

    TEST(WebSocketHandshakeTest, ForwardedHeaderNameIsCaseInsensitiveAndSeveralHeadersAreOneList) {
        EXPECT_EQ(Forwarded(Request().Add("x-FORWARDED-for:   203.0.113.9 \t"), 1), "203.0.113.9");

        const Request two = Request().Add("X-Forwarded-For: 1.1.1.1, 2.2.2.2").Add("x-forwarded-for: 203.0.113.9");
        EXPECT_EQ(Forwarded(two, 1), "203.0.113.9");
        EXPECT_EQ(Forwarded(two, 2), "2.2.2.2");
        EXPECT_EQ(Forwarded(two, 3), "1.1.1.1");
        EXPECT_EQ(Forwarded(two, 4), "");
    }

    TEST(WebSocketHandshakeTest, ForwardedEntryThatIsNotAnAddressIsIgnored) {
        for (const char* entry : {"unknown", "1.2.3", "1.2.3.4.5", "256.1.1.1", "1.2.3.-4", "1.2.3.4:8080", "01234.1.1.1", "1..3.4", "12:34", "gggg::1", "a:b:c:d:e:f:1:2:3", "[::1]", "1.2.3.4 5",
                                  "<script>", "203.0.113.9\x01"}) {
            EXPECT_EQ(Forwarded(Request().Add(std::string("X-Forwarded-For: 203.0.113.1, ") + entry), 1), "") << entry;
        }
        EXPECT_EQ(Forwarded(Request().Add("X-Forwarded-For: 203.0.113.1,"), 1), "") << "an empty entry is not an address";
        EXPECT_EQ(Forwarded(Request().Add("X-Forwarded-For: 203.0.113.1,,203.0.113.2"), 2), "") << "the empty entry counts as an entry";
        EXPECT_EQ(Forwarded(Request().Add("X-Forwarded-For: " + std::string(1000, '1')), 1), "");
        EXPECT_EQ(Forwarded(Request().Add("X-Forwarded-For: " + std::string(kMaxForwardedAddressBytes + 1, 'a')), 1), "");
    }

    TEST(WebSocketHandshakeTest, ForwardedClientTakesIpv6AndLowersIt) {
        EXPECT_EQ(Forwarded(Request().Add("X-Forwarded-For: 2001:DB8::1"), 1), "2001:db8::1");
        EXPECT_EQ(Forwarded(Request().Add("X-Forwarded-For: ::ffff:192.0.2.1"), 1), "::ffff:192.0.2.1");
        EXPECT_EQ(Forwarded(Request().Add("X-Forwarded-For: ::1"), 1), "::1");
        const std::string longest = "0000:0000:0000:0000:0000:ffff:192.168.100.228";
        ASSERT_EQ(longest.size(), kMaxForwardedAddressBytes);
        EXPECT_EQ(Forwarded(Request().Add("X-Forwarded-For: " + longest), 1), longest) << "the longest literal fits";
    }

    TEST(WebSocketHandshakeTest, ForwardedClientOfAMalformedHeadIsEmptyAndTheHandshakeDoesNotNeedIt) {
        EXPECT_EQ(ParseForwardedClientAddress("", 1), "");
        EXPECT_EQ(ParseForwardedClientAddress("GET / HTTP/1.1", 1), "");
        EXPECT_EQ(ParseForwardedClientAddress("GET / HTTP/1.1\r\nno colon\r\n\r\n", 1), "");
        const std::string huge = std::string(kMaxHandshakeBytes + 1, 'x');
        EXPECT_EQ(ParseForwardedClientAddress(View(huge), 1), "");

        // The header does not change what the handshake accepts or refuses
        EXPECT_TRUE(Parse(Request().Add("X-Forwarded-For: garbage"), PublicPolicy()).has_value());
    }

}  // namespace BECore::Tests
