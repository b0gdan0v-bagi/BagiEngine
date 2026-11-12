#include <Common/Printers.h>
#include <Widgets/EditorLayout/StatusBarModel.h>
#include <limits>

namespace BECore::Tests {

    namespace {

        FrameStats Frame(uint64_t number, double frameMs) {
            FrameStats stats;
            stats.frame = number;
            stats.frameMs = frameMs;
            return stats;
        }

    }  // namespace

    TEST(StatusBarModelTest, ClassifyFrameByTheBudget) {
        EXPECT_EQ(ClassifyFrame(5.0, kFrameBudgetMs), FrameLevel::Within);
        EXPECT_EQ(ClassifyFrame(kFrameBudgetMs, kFrameBudgetMs), FrameLevel::Within);
        EXPECT_EQ(ClassifyFrame(kFrameBudgetMs + 0.01, kFrameBudgetMs), FrameLevel::Over);
        EXPECT_EQ(ClassifyFrame(2.0 * kFrameBudgetMs, kFrameBudgetMs), FrameLevel::Over);
        EXPECT_EQ(ClassifyFrame(2.0 * kFrameBudgetMs + 0.01, kFrameBudgetMs), FrameLevel::WayOver);
        EXPECT_EQ(ClassifyFrame(500.0, kFrameBudgetMs), FrameLevel::WayOver);
    }

    TEST(StatusBarModelTest, ClassifyFrameTakesBadTimesAsWithin) {
        EXPECT_EQ(ClassifyFrame(0.0, kFrameBudgetMs), FrameLevel::Within);
        EXPECT_EQ(ClassifyFrame(-3.0, kFrameBudgetMs), FrameLevel::Within);
        EXPECT_EQ(ClassifyFrame(std::numeric_limits<double>::quiet_NaN(), kFrameBudgetMs), FrameLevel::Within);
    }

    TEST(StatusBarModelTest, ClassifyAllocs) {
        EXPECT_EQ(ClassifyAllocs(0), AllocLevel::Zero);
        EXPECT_EQ(ClassifyAllocs(1), AllocLevel::Some);
        EXPECT_EQ(ClassifyAllocs(100000), AllocLevel::Some);
    }

    TEST(StatusBarModelTest, MeterIsZeroBeforeAFrame) {
        const FrameMeter meter;
        EXPECT_EQ(meter.GetFps(), 0u);
        EXPECT_EQ(meter.GetFrameMs(), 0.0);
        EXPECT_EQ(meter.Version(), 0u);
        EXPECT_EQ(meter.GetLevel(), FrameLevel::Within);
    }

    TEST(StatusBarModelTest, MeterRoundsTheMean) {
        FrameMeter meter;
        EXPECT_TRUE(meter.Feed(Frame(1, 16.0)));
        EXPECT_EQ(meter.GetFps(), 63u);
        EXPECT_DOUBLE_EQ(meter.GetFrameMs(), 16.0);

        EXPECT_TRUE(meter.Feed(Frame(2, 17.0)));  // mean 16.5 ms, 60.6 fps
        EXPECT_EQ(meter.GetFps(), 61u);
        EXPECT_DOUBLE_EQ(meter.GetFrameMs(), 16.5);
    }

    TEST(StatusBarModelTest, MeterKeepsTheLastWindowOnly) {
        FrameMeter meter;
        for (uint64_t i = 1; i <= kMeterWindow; ++i) {
            meter.Feed(Frame(i, 100.0));
        }
        EXPECT_EQ(meter.GetFps(), 10u);
        EXPECT_DOUBLE_EQ(meter.GetFrameMs(), 100.0);

        // A window of 10 ms frames pushes the 100 ms ones out
        for (uint64_t i = 1; i <= kMeterWindow; ++i) {
            meter.Feed(Frame(kMeterWindow + i, 10.0));
        }
        EXPECT_EQ(meter.GetFps(), 100u);
        EXPECT_DOUBLE_EQ(meter.GetFrameMs(), 10.0);
    }

    TEST(StatusBarModelTest, MeterMeanOverAPartialWindow) {
        FrameMeter meter;
        meter.Feed(Frame(1, 10.0));
        meter.Feed(Frame(2, 20.0));
        meter.Feed(Frame(3, 30.0));
        EXPECT_DOUBLE_EQ(meter.GetFrameMs(), 20.0);
        EXPECT_EQ(meter.GetFps(), 50u);
    }

    TEST(StatusBarModelTest, MeterCountsAFrameOnce) {
        FrameMeter meter;
        EXPECT_TRUE(meter.Feed(Frame(1, 10.0)));
        EXPECT_TRUE(meter.Feed(Frame(2, 30.0)));
        const uint32_t version = meter.Version();

        EXPECT_FALSE(meter.Feed(Frame(2, 500.0)));  // the same frame again
        EXPECT_FALSE(meter.Feed(Frame(1, 500.0)));  // an older one
        EXPECT_DOUBLE_EQ(meter.GetFrameMs(), 20.0);
        EXPECT_EQ(meter.Version(), version);
    }

    TEST(StatusBarModelTest, MeterVersionMovesOnlyWithTheRoundedValues) {
        FrameMeter meter;
        for (uint64_t i = 1; i <= kMeterWindow; ++i) {
            meter.Feed(Frame(i, 20.0));
        }
        const uint32_t version = meter.Version();

        // 19.99 / 20.01 around 20.0: the mean stays 20.0 after rounding
        meter.Feed(Frame(kMeterWindow + 1, 20.01));
        meter.Feed(Frame(kMeterWindow + 2, 19.99));
        EXPECT_EQ(meter.GetFps(), 50u);
        EXPECT_DOUBLE_EQ(meter.GetFrameMs(), 20.0);
        EXPECT_EQ(meter.Version(), version);

        for (uint64_t i = 3; i < 3 + kMeterWindow; ++i) {
            meter.Feed(Frame(kMeterWindow + i, 40.0));
        }
        EXPECT_GT(meter.Version(), version);
    }

    TEST(StatusBarModelTest, MeterSkipsUnusableTimes) {
        FrameMeter meter;
        meter.Feed(Frame(1, 10.0));
        const uint32_t version = meter.Version();

        EXPECT_TRUE(meter.Feed(Frame(2, 0.0)));
        EXPECT_TRUE(meter.Feed(Frame(3, -5.0)));
        EXPECT_TRUE(meter.Feed(Frame(4, std::numeric_limits<double>::quiet_NaN())));
        EXPECT_TRUE(meter.Feed(Frame(5, std::numeric_limits<double>::infinity())));
        EXPECT_DOUBLE_EQ(meter.GetFrameMs(), 10.0);
        EXPECT_EQ(meter.GetFps(), 100u);
        EXPECT_EQ(meter.Version(), version);
    }

    TEST(StatusBarModelTest, MeterCapsTheFps) {
        FrameMeter meter;
        meter.Feed(Frame(1, 1e-9));
        EXPECT_EQ(meter.GetFps(), kMaxMeterFps);
    }

    TEST(StatusBarModelTest, MeterLevelFollowsTheMean) {
        FrameMeter meter;
        meter.Feed(Frame(1, 10.0));
        EXPECT_EQ(meter.GetLevel(), FrameLevel::Within);
        meter.Feed(Frame(2, 30.0));  // mean 20 ms
        EXPECT_EQ(meter.GetLevel(), FrameLevel::Over);
        meter.Feed(Frame(3, 200.0));  // mean 80 ms
        EXPECT_EQ(meter.GetLevel(), FrameLevel::WayOver);
    }

    TEST(StatusBarModelTest, FormatFpsAndFrameMs) {
        char buffer[32];
        FormatFps(60, buffer);
        EXPECT_STREQ(buffer, "60 fps");
        FormatFps(0, buffer);
        EXPECT_STREQ(buffer, "0 fps");
        FormatFrameMs(16.6, buffer);
        EXPECT_STREQ(buffer, "16.6 ms");
        FormatFrameMs(0.0, buffer);
        EXPECT_STREQ(buffer, "0.0 ms");
    }

    TEST(StatusBarModelTest, FormatAllocsHasASingular) {
        char buffer[32];
        FormatAllocs(0, buffer);
        EXPECT_STREQ(buffer, "0 allocs");
        FormatAllocs(1, buffer);
        EXPECT_STREQ(buffer, "1 alloc");
        FormatAllocs(1234, buffer);
        EXPECT_STREQ(buffer, "1234 allocs");
    }

    TEST(StatusBarModelTest, FormatControlStatus) {
        char buffer[32];
        FormatControlStatus(false, 8080, 3, buffer);
        EXPECT_STREQ(buffer, "off");
        FormatControlStatus(true, 8080, 0, buffer);
        EXPECT_STREQ(buffer, ":8080  0 clients");
        FormatControlStatus(true, 8080, 1, buffer);
        EXPECT_STREQ(buffer, ":8080  1 client");
        FormatControlStatus(true, 8080, 2, buffer);
        EXPECT_STREQ(buffer, ":8080  2 clients");
        FormatControlStatus(true, 0, 0, buffer);
        EXPECT_STREQ(buffer, ":0  0 clients");
        FormatControlStatus(true, 65535, 12, buffer);
        EXPECT_STREQ(buffer, ":65535  12 clients");
    }

    TEST(StatusBarModelTest, FormattersCutAndTerminateInASmallBuffer) {
        char buffer[6];
        FormatControlStatus(true, 65535, 12, buffer);
        EXPECT_STREQ(buffer, ":6553");
        FormatFrameMs(16.66, buffer);
        EXPECT_STREQ(buffer, "16.7 ");
        FormatAllocs(123456, buffer);
        EXPECT_STREQ(buffer, "12345");

        char one[1] = {'x'};
        FormatFps(60, one);
        EXPECT_EQ(one[0], '\0');

        FormatFps(60, eastl::span<char>());  // an empty buffer is left alone
    }

}  // namespace BECore::Tests
