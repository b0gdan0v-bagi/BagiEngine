#include <BECore/Profiler/FrameStats/FrameStatsText.h>
#include <Common/AllocCheck.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr ProfileSite kRenderSite{"Render", "Test", "Render.cpp", 1, 0};
        constexpr ProfileSite kUpdateSite{"Update", "Test", "Update.cpp", 2, 0};
        constexpr ProfileSite kPhysicsSite{"Physics", "Test", "Physics.cpp", 3, 0};
        constexpr ProfileSite kOverlaySite{"Profiler overlay", "Test", "Overlay.cpp", 4, 0};
        constexpr ProfileSite kLongSite{
            "A zone with a very long name that goes on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on "
            "and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and on and end",
            "Test", "Long.cpp", 5, 0};

        ZoneStatRow Row(const ProfileSite& site, double selfMs, uint64_t calls) {
            ZoneStatRow row;
            row.site = &site;
            row.selfMs = selfMs;
            row.calls = calls;
            return row;
        }

        Vector<String> TopLines(const Vector<ZoneStatRow>& rows, uint32_t windowFrames, size_t count, StringView skipName) {
            Vector<String> lines;
            FormatTopZoneLinesTo(rows, windowFrames, count, skipName, [&lines](StringView line) { lines.push_back(String(line.data(), line.size())); });
            return lines;
        }

    }  // namespace

    TEST(FrameStatsTextTest, TopZonesComeLargestSelfTimeFirstPerFrame) {
        const Vector<ZoneStatRow> rows = {Row(kUpdateSite, 20.0, 60), Row(kRenderSite, 120.0, 120), Row(kPhysicsSite, 60.0, 30)};
        const Vector<String> lines = TopLines(rows, 60, 6, "");
        ASSERT_EQ(lines.size(), 3u);
        EXPECT_STREQ(lines[0].c_str(), "Render  2.00 ms  2.0x");
        EXPECT_STREQ(lines[1].c_str(), "Physics  1.00 ms  0.5x");
        EXPECT_STREQ(lines[2].c_str(), "Update  0.33 ms  1.0x");
    }

    TEST(FrameStatsTextTest, CountCutsTheListAndTheSkippedZoneTakesNoPlace) {
        const Vector<ZoneStatRow> rows = {Row(kOverlaySite, 500.0, 60), Row(kRenderSite, 120.0, 60), Row(kUpdateSite, 60.0, 60), Row(kPhysicsSite, 30.0, 60)};
        const Vector<String> lines = TopLines(rows, 60, 2, "Profiler overlay");
        ASSERT_EQ(lines.size(), 2u);
        EXPECT_EQ(StringView(lines[0].c_str()).substr(0, 6), "Render");
        EXPECT_EQ(StringView(lines[1].c_str()).substr(0, 6), "Update");
        EXPECT_EQ(TopLines(rows, 60, 10, "Profiler overlay").size(), 3u);
    }

    TEST(FrameStatsTextTest, EqualSelfTimesAreOrderedByNameLikeTheTable) {
        const Vector<ZoneStatRow> rows = {Row(kUpdateSite, 6.0, 6), Row(kRenderSite, 6.0, 6), Row(kPhysicsSite, 6.0, 6)};
        const Vector<String> lines = TopLines(rows, 6, 3, "");
        ASSERT_EQ(lines.size(), 3u);
        EXPECT_EQ(StringView(lines[0].c_str()).substr(0, 7), "Physics");
        EXPECT_EQ(StringView(lines[1].c_str()).substr(0, 6), "Render");
        EXPECT_EQ(StringView(lines[2].c_str()).substr(0, 6), "Update");
    }

    TEST(FrameStatsTextTest, NothingIsEmittedForNoRowsNoCountOrNoFrames) {
        const Vector<ZoneStatRow> rows = {Row(kRenderSite, 6.0, 6)};
        EXPECT_TRUE(TopLines({}, 60, 6, "").empty());
        EXPECT_TRUE(TopLines(rows, 60, 0, "").empty());
        EXPECT_TRUE(TopLines(rows, 0, 6, "").empty());
        EXPECT_TRUE(TopLines(rows, 60, 6, "Render").empty());
    }

    TEST(FrameStatsTextTest, TheTopZoneLinesToAVisitorAllocateNothing) {
        const Vector<ZoneStatRow> rows = {Row(kUpdateSite, 20.0, 60), Row(kRenderSite, 120.0, 120), Row(kPhysicsSite, 60.0, 30)};
        size_t emitted = 0;
        ExpectNoAllocations([&] { FormatTopZoneLinesTo(rows, 60, 6, "Update", [&emitted](StringView) { ++emitted; }); });
        EXPECT_EQ(emitted, 2u);
    }

    TEST(FrameStatsTextTest, ALongZoneNameIsCutAtTheLineCapacity) {
        const Vector<ZoneStatRow> rows = {Row(kLongSite, 6.0, 6)};
        const Vector<String> lines = TopLines(rows, 6, 1, "");
        ASSERT_EQ(lines.size(), 1u);
        EXPECT_EQ(lines[0].size(), kOverlayLineCapacity);
    }

}  // namespace BECore::Tests
