#include <BECore/Logger/LogTime.h>
#include <BECore/Logger/OutputSink.h>
#include <Common/AllocCheck.h>

namespace BECore::Tests {

    namespace {

        using Clock = std::chrono::system_clock;

        std::tm ZoneFieldsOf(std::time_t seconds, bool local) {
            std::tm fields{};
#ifdef _WIN32
            local ? localtime_s(&fields, &seconds) : gmtime_s(&fields, &seconds);
#else
            local ? localtime_r(&seconds, &fields) : gmtime_r(&seconds, &fields);
#endif
            return fields;
        }

        bool SameFields(const std::tm& a, const std::tm& b) {
            return a.tm_year == b.tm_year && a.tm_mon == b.tm_mon && a.tm_mday == b.tm_mday && a.tm_hour == b.tm_hour && a.tm_min == b.tm_min && a.tm_sec == b.tm_sec;
        }

        void ExpectFieldsOfShiftedInstant(std::time_t seconds, std::chrono::seconds offset, int millisecond) {
            const Clock::time_point moment = Clock::from_time_t(seconds) + std::chrono::milliseconds{millisecond};
            const LocalLogTime made = MakeLocalLogTime(moment, offset);
            const std::tm expected = ZoneFieldsOf(seconds + static_cast<std::time_t>(offset.count()), false);
            EXPECT_TRUE(SameFields(made.tm, expected)) << "seconds " << seconds << " offset " << offset.count();
            EXPECT_EQ(made.millisecond, millisecond);
        }

    }  // namespace

    // The civil-date arithmetic has to agree with the C library on the days that break a hand-made calendar.
    TEST(LogTimeTest, FieldsEqualTheLibraryForCalendarEdges) {
        constexpr std::time_t kEpoch = 0;
        constexpr std::time_t kLeapDay2024 = 1709210096;    // 2024-02-29 12:34:56 UTC
        constexpr std::time_t kYearEnd2023 = 1704067199;    // 2023-12-31 23:59:59 UTC
        constexpr std::time_t kCenturyLeapDay = 951782400;  // 2000-02-29 00:00:00 UTC
        constexpr std::time_t kBeforeEpoch = -1;
        constexpr std::time_t kFarPast = -2208988800;   // 1900-01-01 00:00:00 UTC
        constexpr std::time_t kFarFuture = 4102444799;  // 2099-12-31 23:59:59 UTC
        for (const std::time_t seconds : {kEpoch, kLeapDay2024, kYearEnd2023, kCenturyLeapDay, kBeforeEpoch, kFarPast, kFarFuture}) {
            ExpectFieldsOfShiftedInstant(seconds, std::chrono::seconds{0}, 789);
        }
    }

    TEST(LogTimeTest, OffsetShiftsTheDayAcrossMidnight) {
        constexpr std::time_t kYearEnd2023 = 1704067199;
        constexpr std::chrono::seconds kIndiaOffset{5 * 3600 + 1800};
        constexpr std::chrono::seconds kPacificOffset{-8 * 3600};
        ExpectFieldsOfShiftedInstant(kYearEnd2023, kIndiaOffset, 0);
        ExpectFieldsOfShiftedInstant(kYearEnd2023, kPacificOffset, 999);
        ExpectFieldsOfShiftedInstant(0, kPacificOffset, 1);
    }

    TEST(LogTimeTest, MillisecondOfAMomentBeforeTheEpochCountsForward) {
        const LocalLogTime made = MakeLocalLogTime(Clock::time_point{} - std::chrono::milliseconds{1}, std::chrono::seconds{0});
        EXPECT_EQ(made.tm.tm_sec, 59);
        EXPECT_EQ(made.millisecond, 999);
    }

    // The lookup of the zone is done once; what a line reads afterwards is the zone of that moment.
    TEST(LogTimeTest, LocalTimeNowIsTheLocalTimeOfThisInstant) {
        const std::time_t before = Clock::to_time_t(Clock::now());
        const LocalLogTime now = GetLocalLogTime();
        const std::time_t after = Clock::to_time_t(Clock::now());

        EXPECT_TRUE(SameFields(now.tm, ZoneFieldsOf(before, true)) || SameFields(now.tm, ZoneFieldsOf(after, true)));
        EXPECT_GE(now.millisecond, 0);
        EXPECT_LE(now.millisecond, 999);
    }

    TEST(LogTimeTest, LocalTimeAllocatesNothingAfterTheInit) {
        InitLogClock();
        ExpectNoAllocations([] { (void)GetLocalLogTime(); });
    }

    TEST(OutputLineTest, TextIsLevelTimeMessageAndNewline) {
        char buffer[kOutputLineSize];
        const StringView line = FormatOutputLine(buffer, LogLevel::Warning, "12:34:56.789", "hello");
        EXPECT_EQ(line, "[Warning] [12:34:56.789] hello\n");
        EXPECT_EQ(line.data()[line.size()], '\0');
    }

    TEST(OutputLineTest, LongMessageIsCutAndStillTerminated) {
        char buffer[kOutputLineSize];
        const String longMessage(kOutputLineSize * 2, 'x');
        const StringView line = FormatOutputLine(buffer, LogLevel::Info, "00:00:00.000", longMessage);
        EXPECT_EQ(line.size(), kOutputLineSize - 1);
        EXPECT_EQ(line.data()[line.size()], '\0');
        EXPECT_EQ(line.substr(0, 8), "[Info] [");
    }

    // A whole log message (the ring cell size) with the longest head fits, so the newline is not lost.
    TEST(OutputLineTest, FullSizeMessageKeepsItsNewline) {
        char buffer[kOutputLineSize];
        const String message(LogEntry::MaxMessageSize - 1, 'm');
        const StringView line = FormatOutputLine(buffer, LogLevel::Warning, "23:59:59.999", message);
        EXPECT_EQ(line.back(), '\n');
    }

}  // namespace BECore::Tests
