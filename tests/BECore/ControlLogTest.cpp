#include <BECore/Control/ControlLog.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Threading/Thread.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        void Log(LogLevel level, StringView message) {
            LogEvent::Emit(level, message);
            LogEvent::Flush();
        }

        StringView MessageOf(const LogRecord& record) {
            return {record.message.data(), record.message.size()};
        }

        constexpr size_t kUnlimited = 100000;

    }  // namespace

    TEST(ControlLogTest, DebugAndInfoAreNeitherKeptNorCounted) {
        ControlLog log;
        log.Start();

        Log(LogLevel::Debug, "ControlLogTest debug");
        Log(LogLevel::Info, "ControlLogTest info");

        EXPECT_EQ(log.GetWarnings(), 0U);
        EXPECT_EQ(log.GetErrors(), 0U);
        EXPECT_EQ(log.History().Size(), 0U);
    }

    TEST(ControlLogTest, WarningsAndErrorsAreCounted) {
        ControlLog log;
        log.Start();

        Log(LogLevel::Warning, "ControlLogTest warning");
        Log(LogLevel::Error, "ControlLogTest error");
        Log(LogLevel::Fatal, "ControlLogTest fatal");

        EXPECT_EQ(log.GetWarnings(), 1U);
        EXPECT_EQ(log.GetErrors(), 2U);
        EXPECT_EQ(log.History().Size(), 3U);
        EXPECT_EQ(log.GetNextSequence(), 4U);
    }

    TEST(ControlLogTest, AMarkSplitsTheLines) {
        ControlLog log;
        log.Start();
        Log(LogLevel::Warning, "ControlLogTest before");
        const uint64_t mark = log.GetNextSequence();
        Log(LogLevel::Error, "ControlLogTest after");

        const ControlLog::Since since = log.ReadSince(mark, LogLevel::Warning, kUnlimited);

        ASSERT_EQ(since.lines.size(), 1U);
        EXPECT_EQ(MessageOf(*since.lines[0]), "ControlLogTest after");
        EXPECT_EQ(since.lines[0]->level, LogLevel::Error);
        EXPECT_EQ(since.warnings, 0U);
        EXPECT_EQ(since.errors, 1U);
        EXPECT_EQ(since.dropped, 0U);
        EXPECT_EQ(since.next, log.GetNextSequence());
        EXPECT_FALSE(since.truncated);
    }

    TEST(ControlLogTest, MarkZeroAndOneBothMeanSinceTheStart) {
        ControlLog log;
        log.Start();
        Log(LogLevel::Warning, "ControlLogTest first");
        Log(LogLevel::Warning, "ControlLogTest second");

        EXPECT_EQ(log.ReadSince(0, LogLevel::Warning, kUnlimited).lines.size(), 2U);
        EXPECT_EQ(log.ReadSince(1, LogLevel::Warning, kUnlimited).lines.size(), 2U);
        EXPECT_EQ(log.ReadSince(2, LogLevel::Warning, kUnlimited).lines.size(), 1U);
    }

    TEST(ControlLogTest, AMarkInTheFutureFindsNothing) {
        ControlLog log;
        log.Start();
        Log(LogLevel::Warning, "ControlLogTest line");

        const ControlLog::Since since = log.ReadSince(1000, LogLevel::Warning, kUnlimited);

        EXPECT_TRUE(since.lines.empty());
        EXPECT_EQ(since.dropped, 0U);
        EXPECT_EQ(since.warnings, 0U);
    }

    TEST(ControlLogTest, TheErrorFilterShortensTheLinesNotTheCounts) {
        ControlLog log;
        log.Start();
        Log(LogLevel::Warning, "ControlLogTest warning");
        Log(LogLevel::Error, "ControlLogTest error");

        const ControlLog::Since since = log.ReadSince(0, LogLevel::Error, kUnlimited);

        ASSERT_EQ(since.lines.size(), 1U);
        EXPECT_EQ(MessageOf(*since.lines[0]), "ControlLogTest error");
        EXPECT_EQ(since.warnings, 1U);
        EXPECT_EQ(since.errors, 1U);
    }

    TEST(ControlLogTest, TheLineCapKeepsTheNewestAndSaysSo) {
        ControlLog log;
        log.Start();
        Log(LogLevel::Warning, "ControlLogTest one");
        Log(LogLevel::Warning, "ControlLogTest two");
        Log(LogLevel::Warning, "ControlLogTest three");

        const ControlLog::Since since = log.ReadSince(0, LogLevel::Warning, 2);

        ASSERT_EQ(since.lines.size(), 2U);
        EXPECT_EQ(MessageOf(*since.lines[0]), "ControlLogTest two");
        EXPECT_EQ(MessageOf(*since.lines[1]), "ControlLogTest three");
        EXPECT_TRUE(since.truncated);
        EXPECT_EQ(since.warnings, 3U);
    }

    TEST(ControlLogTest, DroppedCountsTheLinesTheRingHasLost) {
        constexpr size_t kCapacity = 4;
        ControlLog log(kCapacity);
        log.Start();
        const uint64_t mark = log.GetNextSequence();
        for (int i = 0; i < 6; ++i) {
            Log(LogLevel::Warning, Format("ControlLogTest line {}", i));
        }

        const ControlLog::Since since = log.ReadSince(mark, LogLevel::Warning, kUnlimited);

        EXPECT_EQ(since.dropped, 2U);
        ASSERT_EQ(since.lines.size(), kCapacity);
        EXPECT_EQ(MessageOf(*since.lines.front()), "ControlLogTest line 2");
        EXPECT_EQ(MessageOf(*since.lines.back()), "ControlLogTest line 5");
        EXPECT_EQ(since.warnings, kCapacity);
        EXPECT_EQ(log.GetWarnings(), 6U);

        EXPECT_EQ(log.ReadSince(log.GetNextSequence() - 1, LogLevel::Warning, kUnlimited).dropped, 0U);
    }

    TEST(ControlLogTest, TheLinesAreOldestFirstWithTheClockText) {
        ControlLog log;
        log.Start();
        Log(LogLevel::Warning, "ControlLogTest a");
        Log(LogLevel::Error, "ControlLogTest b");

        const ControlLog::Since since = log.ReadSince(0, LogLevel::Warning, kUnlimited);

        ASSERT_EQ(since.lines.size(), 2U);
        EXPECT_LT(since.lines[0]->sequence, since.lines[1]->sequence);
        EXPECT_EQ(MessageOf(*since.lines[0]), "ControlLogTest a");
        const StringView time(since.lines[0]->time);
        ASSERT_EQ(time.size(), kLogTimeLength - 1);
        EXPECT_EQ(time[2], ':');
        EXPECT_EQ(time[5], ':');
        EXPECT_EQ(time[8], '.');
    }

    TEST(ControlLogTest, ALineFromAWorkerThreadAppearsAfterFlush) {
        if constexpr (!Thread::kAvailable) {
            GTEST_SKIP() << "no threads in this build";
        }
        ControlLog log;
        log.Start();

        Thread thread("ControlLogWriter", [] { LogEvent::Emit(LogLevel::Error, "ControlLogTest worker line"); });
        thread.join();
        EXPECT_EQ(log.GetErrors(), 0U);
        LogEvent::Flush();

        EXPECT_EQ(log.GetErrors(), 1U);
    }

    TEST(ControlLogTest, StartingTwiceSubscribesOnce) {
        ControlLog log;
        log.Start();
        log.Start();

        Log(LogLevel::Warning, "ControlLogTest once");

        EXPECT_EQ(log.GetWarnings(), 1U);
        EXPECT_EQ(log.History().Size(), 1U);
    }

    TEST(ControlLogTest, ALineBeforeStartIsNotCounted) {
        ControlLog log;
        Log(LogLevel::Warning, "ControlLogTest early");

        log.Start();

        EXPECT_EQ(log.GetWarnings(), 0U);
        EXPECT_EQ(log.History().Size(), 0U);
    }

    TEST(ControlLogTest, AServiceOwnsALogThatListensAfterStart) {
        ControlService service;
        Log(LogLevel::Warning, "ControlLogTest before service start");
        EXPECT_EQ(service.Log().GetWarnings(), 0U);

        service.Log().Start();
        Log(LogLevel::Warning, "ControlLogTest after service start");

        EXPECT_EQ(service.Log().GetWarnings(), 1U);
    }

}  // namespace BECore::Tests
