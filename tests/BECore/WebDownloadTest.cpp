#include <Common/Printers.h>
#include <CoreSDL/SDLWebDownload.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    TEST(WebDownloadTest, ANamePassesOnlyAsAPlainFileName) {
        EXPECT_TRUE(IsWebDownloadNameValid("profiler-trace.json"));
        EXPECT_TRUE(IsWebDownloadNameValid("a"));
        EXPECT_TRUE(IsWebDownloadNameValid(String(kWebDownloadMaxNameBytes, 'a')));

        EXPECT_FALSE(IsWebDownloadNameValid(""));
        EXPECT_FALSE(IsWebDownloadNameValid(String(kWebDownloadMaxNameBytes + 1, 'a')));
        EXPECT_FALSE(IsWebDownloadNameValid("dir/file.json"));
        EXPECT_FALSE(IsWebDownloadNameValid("dir\\file.json"));
        EXPECT_FALSE(IsWebDownloadNameValid(".."));
        EXPECT_FALSE(IsWebDownloadNameValid("a..b"));
        EXPECT_FALSE(IsWebDownloadNameValid("a b"));
        EXPECT_FALSE(IsWebDownloadNameValid("a\nb"));
        EXPECT_FALSE(IsWebDownloadNameValid(StringView("a\0b", 3)));
        EXPECT_FALSE(IsWebDownloadNameValid("\xc3\xa9.json"));
    }

    TEST(WebDownloadTest, ARefusalNamesItsCauseAndTheDesktopCannotDownload) {
        EXPECT_EQ(WebDownloadStart("../x", "{}", "application/json").error(), "bad name");
        EXPECT_EQ(WebDownloadStart("x.json", "{}", "text/\xc3\xa9").error(), "bad mime type");
        EXPECT_EQ(WebDownloadStart("x.json", "{}", "text/a\nb").error(), "bad mime type");
        EXPECT_EQ(WebDownloadStart("x.json", "{}", "application/json").error(), "not in the browser");
        EXPECT_EQ(WebDownloadStart("x.json", "", "").error(), "not in the browser");
    }

}  // namespace BECore::Tests
