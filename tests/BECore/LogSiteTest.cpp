#include <BECore/Logger/LogEvent.h>
#include <BECore/Logger/LogRingBuffer.h>
#include <BECore/Logger/Logger.h>
#include <BECore/Threading/Thread.h>
#include <Common/LogRecorder.h>
#include <Common/Printers.h>

namespace BECore::Tests {

    namespace {

        bool EndsWith(const char* text, StringView tail) {
            const StringView view(text);
            return view.size() >= tail.size() && view.substr(view.size() - tail.size()) == tail;
        }

    }  // namespace

    TEST(LogSiteTest, AMacroPassesTheFileAndTheLineOfTheCall) {
        LogRecorder recorder;
        constexpr uint32_t kLine = __LINE__ + 1;
        LOG_WARNING("LogSiteTest macro site");
        LogEvent::Flush();

        const LogRecorder::Entry* entry = recorder.Find(LogLevel::Warning, {"LogSiteTest macro site"});
        ASSERT_NE(entry, nullptr);
        ASSERT_NE(entry->site.file, nullptr);
        EXPECT_TRUE(EndsWith(entry->site.file, "LogSiteTest.cpp")) << entry->site.file;
        EXPECT_EQ(entry->site.line, kLine);
    }

    TEST(LogSiteTest, EveryLevelOfTheMacrosHasASite) {
        LogRecorder recorder;
        LOG_DEBUG("LogSiteTest every level");
        LOG_INFO("LogSiteTest every level");
        LOG_ERROR("LogSiteTest every level");
        LogEvent::Flush();

        for (const LogLevel level : {LogLevel::Debug, LogLevel::Info, LogLevel::Error}) {
            const LogRecorder::Entry* entry = recorder.Find(level, {"LogSiteTest every level"});
            ASSERT_NE(entry, nullptr);
            EXPECT_NE(entry->site.file, nullptr);
            EXPECT_NE(entry->site.line, 0U);
        }
    }

    TEST(LogSiteTest, AnEmitWithoutASiteHasNone) {
        LogRecorder recorder;
        LogEvent::Emit(LogLevel::Info, "LogSiteTest no site");
        Logger::Warning("LogSiteTest no site either");
        LogEvent::Flush();

        const LogRecorder::Entry* emitted = recorder.Find(LogLevel::Info, {"LogSiteTest no site"});
        ASSERT_NE(emitted, nullptr);
        EXPECT_EQ(emitted->site.file, nullptr);
        EXPECT_EQ(emitted->site.line, 0U);

        const LogRecorder::Entry* direct = recorder.Find(LogLevel::Warning, {"LogSiteTest no site either"});
        ASSERT_NE(direct, nullptr);
        EXPECT_EQ(direct->site.file, nullptr);
    }

    TEST(LogSiteTest, ATruncatedMessageKeepsItsSite) {
        LogRecorder recorder;
        const String text = Format("LogSiteTest long {}", String(600, 'x'));
        constexpr uint32_t kLine = __LINE__ + 1;
        LOG_ERROR(StringView(text.data(), text.size()));
        LogEvent::Flush();

        const LogRecorder::Entry* entry = recorder.Find(LogLevel::Error, {"LogSiteTest long"});
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->message.size(), LogEntry::MaxMessageSize - 1);
        EXPECT_EQ(entry->site.line, kLine);
        ASSERT_NE(entry->site.file, nullptr);
    }

    TEST(LogSiteTest, ALineFromAnotherThreadArrivesWithItsSite) {
        if constexpr (!Thread::kAvailable) {
            GTEST_SKIP() << "no threads in this build";
        }
        LogRecorder recorder;
        uint32_t line = 0;
        Thread thread("LogSiteTestWriter", [&line] {
            line = __LINE__ + 1;
            LOG_INFO("LogSiteTest from a thread");
        });
        thread.join();
        LogEvent::Flush();

        const LogRecorder::Entry* entry = recorder.Find(LogLevel::Info, {"LogSiteTest from a thread"});
        ASSERT_NE(entry, nullptr);
        ASSERT_NE(entry->site.file, nullptr);
        EXPECT_TRUE(EndsWith(entry->site.file, "LogSiteTest.cpp")) << entry->site.file;
        EXPECT_EQ(entry->site.line, line);
    }

}  // namespace BECore::Tests
