#include <Common/Printers.h>
#include <RemoteControl/WebSocketUrl.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        WebSocketUrl MustParse(eastl::string_view text) {
            auto url = ParseWebSocketUrl(text);
            EXPECT_TRUE(url.has_value()) << std::string(text.data(), text.size()) << ": " << (url ? "" : url.error().c_str());
            return url ? *url : WebSocketUrl{};
        }

        void ExpectRefused(eastl::string_view text, std::string_view messagePart = {}) {
            const auto url = ParseWebSocketUrl(text);
            ASSERT_FALSE(url.has_value()) << std::string(text.data(), text.size());
            if (!messagePart.empty()) {
                EXPECT_NE(std::string(url.error().c_str()).find(messagePart), std::string::npos) << url.error().c_str();
            }
        }

    }  // namespace

    TEST(WebSocketUrlTest, PlainUrlWithAPortAndAPath) {
        const WebSocketUrl url = MustParse("ws://127.0.0.1:8080/rooms/play");
        EXPECT_FALSE(url.secure);
        EXPECT_EQ(url.host, "127.0.0.1");
        EXPECT_EQ(url.port, 8080);
        EXPECT_EQ(url.pathAndQuery, "/rooms/play");
    }

    TEST(WebSocketUrlTest, DefaultsToTheSchemePortAndTheRootPath) {
        const WebSocketUrl plain = MustParse("ws://example.com");
        EXPECT_EQ(plain.port, 80);
        EXPECT_EQ(plain.pathAndQuery, "/");
        const WebSocketUrl secure = MustParse("wss://example.com/");
        EXPECT_TRUE(secure.secure);
        EXPECT_EQ(secure.port, 443);
        EXPECT_EQ(secure.pathAndQuery, "/");
    }

    TEST(WebSocketUrlTest, QueryIsKept) {
        EXPECT_EQ(MustParse("ws://a.com/play?room=7&x=1").pathAndQuery, "/play?room=7&x=1");
        EXPECT_EQ(MustParse("ws://a.com?room=7").pathAndQuery, "/?room=7");
        EXPECT_EQ(MustParse("ws://a.com:81?room=7").port, 81);
    }

    TEST(WebSocketUrlTest, SchemeAndHostAreCaseInsensitiveAndTheHostIsLowered) {
        const WebSocketUrl url = MustParse("WSS://Example.COM:9443/Path");
        EXPECT_TRUE(url.secure);
        EXPECT_EQ(url.host, "example.com");
        EXPECT_EQ(url.port, 9443);
        EXPECT_EQ(url.pathAndQuery, "/Path");  // the path is the server's business
    }

    TEST(WebSocketUrlTest, PortBounds) {
        EXPECT_EQ(MustParse("ws://a.com:1").port, 1);
        EXPECT_EQ(MustParse("ws://a.com:65535").port, 65535);
        ExpectRefused("ws://a.com:0");
        ExpectRefused("ws://a.com:65536");
        ExpectRefused("ws://a.com:99999");
        ExpectRefused("ws://a.com:123456");
        ExpectRefused("ws://a.com:");
        ExpectRefused("ws://a.com:80a");
        ExpectRefused("ws://a.com:-1");
    }

    TEST(WebSocketUrlTest, RefusesAnotherSchemeOrNone) {
        ExpectRefused("http://a.com");
        ExpectRefused("https://a.com");
        ExpectRefused("wsss://a.com");
        ExpectRefused("a.com");
        ExpectRefused("a.com:80/x");
        ExpectRefused("");
    }

    TEST(WebSocketUrlTest, RefusesAHostThatIsNotOne) {
        ExpectRefused("ws://", "no host");
        ExpectRefused("ws:///path", "no host");
        ExpectRefused("ws://:80/", "no host");
        ExpectRefused("ws://a.com%2Fevil.org/");
        ExpectRefused("ws://a b.com/");
    }

    TEST(WebSocketUrlTest, RefusesIPv6) {
        ExpectRefused("ws://[::1]:8080/", "IPv6");
        ExpectRefused("wss://[2001:db8::1]/", "IPv6");
    }

    TEST(WebSocketUrlTest, RefusesFragmentUserinfoAndLookAlikes) {
        ExpectRefused("ws://a.com/x#frag", "fragment");
        ExpectRefused("ws://a.com#frag", "fragment");
        ExpectRefused("ws://a.com@evil.org", "userinfo");
        ExpectRefused("ws://user:pass@a.com/", "userinfo");
        ExpectRefused("ws://a.com:80@evil.org/", "userinfo");
    }

    TEST(WebSocketUrlTest, RefusesSpacesControlCharactersAndNonAscii) {
        ExpectRefused("ws://a.com/a b");
        ExpectRefused(" ws://a.com/");
        ExpectRefused("ws://a.com/\tx");
        ExpectRefused("ws://a.com/\r\nHost: evil");
        ExpectRefused(eastl::string_view("ws://a.com/\0x", 13));
        ExpectRefused("ws://a.com/\x7F");
        ExpectRefused("ws://\xD0\xBF.com/");
        ExpectRefused("ws://a.com/\xD0\xBF");
    }

    TEST(WebSocketUrlTest, RefusesAUrlOverTheLimit) {
        const std::string longPath = "ws://a.com/" + std::string(kMaxWebSocketUrlBytes, 'a');
        ExpectRefused(eastl::string_view(longPath.data(), longPath.size()));
        std::string atLimit = "ws://a.com/";
        atLimit.append(kMaxWebSocketUrlBytes - atLimit.size(), 'a');
        EXPECT_EQ(MustParse(eastl::string_view(atLimit.data(), atLimit.size())).pathAndQuery.size(), kMaxWebSocketUrlBytes - 10);
    }

    TEST(WebSocketUrlTest, RefusesAHostLongerThanADnsName) {
        const std::string atLimit = "ws://" + std::string(kMaxWebSocketHostBytes, 'a') + "/x";
        EXPECT_EQ(MustParse(eastl::string_view(atLimit.data(), atLimit.size())).host.size(), kMaxWebSocketHostBytes);
        const std::string overLimit = "ws://" + std::string(kMaxWebSocketHostBytes + 1, 'a') + ":9/x";
        ExpectRefused(eastl::string_view(overLimit.data(), overLimit.size()), "host is longer");
    }

    TEST(WebSocketUrlTest, HostHeaderLeavesOutTheDefaultPortOfTheScheme) {
        EXPECT_EQ(GetHostHeader(MustParse("ws://a.com")), "a.com");
        EXPECT_EQ(GetHostHeader(MustParse("ws://a.com:80/x")), "a.com");
        EXPECT_EQ(GetHostHeader(MustParse("ws://a.com:8080")), "a.com:8080");
        EXPECT_EQ(GetHostHeader(MustParse("wss://a.com")), "a.com");
        EXPECT_EQ(GetHostHeader(MustParse("wss://a.com:443")), "a.com");
        EXPECT_EQ(GetHostHeader(MustParse("wss://a.com:80")), "a.com:80");  // 80 is not the default of wss
        EXPECT_EQ(GetHostHeader(MustParse("ws://a.com:443")), "a.com:443");
        EXPECT_EQ(GetHostHeader(MustParse("ws://127.0.0.1:9000")), "127.0.0.1:9000");
    }

}  // namespace BECore::Tests
