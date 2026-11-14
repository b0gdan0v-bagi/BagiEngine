#include <Common/Printers.h>
#include <CoreSDL/SDLWebPost.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kGoodPath = "/matches/0123456789abcdef0123456789abcdef/0";

    }  // namespace

    TEST(WebPostTest, TheDesktopCannotPost) {
        EXPECT_FALSE(WebPostIsAvailable());
        const auto started = WebPostStart(kGoodPath, "{\"event\":\"start\"}\n", "application/x-ndjson");
        ASSERT_FALSE(started.has_value());
        EXPECT_EQ(started.error(), "unavailable");
    }

    TEST(WebPostTest, ARefusalNamesItsCauseOnEveryPlatform) {
        EXPECT_EQ(WebPostStart("matches/x", "", "").error(), "bad path");
        const eastl::string tooLarge(kWebPostMaxBodyBytes + 1, 'x');
        EXPECT_EQ(WebPostStart(kGoodPath, tooLarge, "").error(), "body too large");
        const eastl::string longType(kWebPostMaxContentTypeBytes + 1, 't');
        EXPECT_EQ(WebPostStart(kGoodPath, "", longType).error(), "bad content type");
        // the largest body and an empty one pass the checks and meet only the platform
        const eastl::string largest(kWebPostMaxBodyBytes, 'x');
        EXPECT_EQ(WebPostStart(kGoodPath, largest, "").error(), "unavailable");
        EXPECT_EQ(WebPostStart(kGoodPath, "", "").error(), "unavailable");
    }

    TEST(WebPostTest, ThePathRuleAcceptsAPathOfThisOrigin) {
        EXPECT_TRUE(IsWebPostPathValid(kGoodPath));
        EXPECT_TRUE(IsWebPostPathValid("/"));
        EXPECT_TRUE(IsWebPostPathValid("/a.b/c?d=1"));
        EXPECT_TRUE(IsWebPostPathValid(eastl::string("/") + eastl::string(kWebPostMaxPathBytes - 1, 'a')));
    }

    TEST(WebPostTest, ThePathRuleRefusesAnythingElse) {
        EXPECT_FALSE(IsWebPostPathValid(""));
        EXPECT_FALSE(IsWebPostPathValid("matches/x"));
        EXPECT_FALSE(IsWebPostPathValid("//host/x"));
        EXPECT_FALSE(IsWebPostPathValid("http://x/y"));
        EXPECT_FALSE(IsWebPostPathValid("javascript:alert(1)"));
        EXPECT_FALSE(IsWebPostPathValid("/a b"));
        EXPECT_FALSE(IsWebPostPathValid("/a\tb"));
        EXPECT_FALSE(IsWebPostPathValid("/a\nb"));
        EXPECT_FALSE(IsWebPostPathValid(StringView("/a\0b", 4)));
        EXPECT_FALSE(IsWebPostPathValid("/a\x7f"));
        EXPECT_FALSE(IsWebPostPathValid("/a\\b"));
        EXPECT_FALSE(IsWebPostPathValid("/a/../b"));
        EXPECT_FALSE(IsWebPostPathValid("/.."));
        EXPECT_FALSE(IsWebPostPathValid("/\xc3\xa9"));
        EXPECT_FALSE(IsWebPostPathValid(eastl::string("/") + eastl::string(kWebPostMaxPathBytes, 'a')));
    }

    TEST(WebPostTest, PollingNothingOrAnUnknownIdIsFailedAndDoesNotCrash) {
        for (const WebPostId id : {WebPostId{0}, WebPostId{1}, WebPostId{4242}, WebPostId{0xFFFFFFFFu}}) {
            const WebPostResult result = WebPostPoll(id);
            EXPECT_EQ(result.status, WebPostStatus::Failed) << id;
            EXPECT_EQ(result.httpStatus, 0) << id;
        }
    }

    TEST(WebPostTest, ADefaultResultIsUnavailable) {
        const WebPostResult result;
        EXPECT_EQ(result.status, WebPostStatus::Unavailable);
        EXPECT_EQ(result.httpStatus, 0);
    }

}  // namespace BECore::Tests
