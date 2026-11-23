// The formatted LOG_X form: site, cut, raw single argument, no allocation. A wrong format string does not compile, and a probe over a
// requires-expression cannot see that (a consteval format string is not evaluated in an unevaluated operand), so the ctest
// BagiLogFormatCompileFail builds tests/CompileFail/LogFormat.cpp instead.
#include <BECore/Logger/LogEvent.h>
#include <BECore/Logger/LogRingBuffer.h>
#include <BECore/Logger/Logger.h>
#include <BECore/Threading/Thread.h>
#include <Common/AllocCheck.h>
#include <Common/LogRecorder.h>

namespace BECore::Tests {

    namespace {

        constexpr size_t kCellText = LogEntry::MaxMessageSize - 1;

        bool EndsWith(const char* text, StringView tail) {
            const StringView view(text);
            return view.size() >= tail.size() && view.substr(view.size() - tail.size()) == tail;
        }

    }  // namespace

    TEST(LoggerFormatTest, AFormattedLineKeepsTheSiteOfTheCall) {
        LogRecorder recorder;
        constexpr uint32_t kLine = __LINE__ + 1;
        LOG_ERROR("LoggerFormatTest site {} of {}", 3, "four");
        LogEvent::Flush();

        const LogRecorder::Entry* entry = recorder.Find(LogLevel::Error, {"LoggerFormatTest site 3 of four"});
        ASSERT_NE(entry, nullptr);
        ASSERT_NE(entry->site.file, nullptr);
        EXPECT_TRUE(EndsWith(entry->site.file, "LoggerFormatTest.cpp")) << entry->site.file;
        EXPECT_EQ(entry->site.line, kLine);
    }

    TEST(LoggerFormatTest, EveryLevelTakesTheFormattedForm) {
        LogRecorder recorder;
        LOG_DEBUG("LoggerFormatTest level {}", 1);
        LOG_INFO("LoggerFormatTest level {}", 2);
        LOG_WARNING("LoggerFormatTest level {}", 3);
        LOG_ERROR("LoggerFormatTest level {}", 4);
        LogEvent::Flush();

        EXPECT_NE(recorder.Find(LogLevel::Debug, {"LoggerFormatTest level 1"}), nullptr);
        EXPECT_NE(recorder.Find(LogLevel::Info, {"LoggerFormatTest level 2"}), nullptr);
        EXPECT_NE(recorder.Find(LogLevel::Warning, {"LoggerFormatTest level 3"}), nullptr);
        EXPECT_NE(recorder.Find(LogLevel::Error, {"LoggerFormatTest level 4"}), nullptr);
    }

    TEST(LoggerFormatTest, AFatalFormattedLineIsLoggedAtFatal) {
        LogRecorder recorder;
        LOG_FATAL("LoggerFormatTest fatal {}", 5);
        LogEvent::Flush();

        EXPECT_NE(recorder.Find(LogLevel::Fatal, {"LoggerFormatTest fatal 5"}), nullptr);
    }

    TEST(LoggerFormatTest, ASingleArgumentIsNotAFormatString) {
        LogRecorder recorder;
        const String owned = "LoggerFormatTest owned {} {";
        const StringView view = "LoggerFormatTest view {}";
        const char* cString = "LoggerFormatTest cstring }{";
        LOG_INFO("LoggerFormatTest literal {}");
        LOG_INFO(owned);
        LOG_INFO(view);
        LOG_INFO(cString);
        LogEvent::Flush();

        EXPECT_NE(recorder.Find(LogLevel::Info, {"LoggerFormatTest literal {}"}), nullptr);
        EXPECT_NE(recorder.Find(LogLevel::Info, {"LoggerFormatTest owned {} {"}), nullptr);
        EXPECT_NE(recorder.Find(LogLevel::Info, {"LoggerFormatTest view {}"}), nullptr);
        EXPECT_NE(recorder.Find(LogLevel::Info, {"LoggerFormatTest cstring }{"}), nullptr);
    }

    TEST(LoggerFormatTest, ALongFormattedLineIsCutLikeAPlainOne) {
        LogRecorder recorder;
        const String body(2000, 'x');
        LOG_INFO("LoggerFormatTest cut {}", body);
        String plainLine = "LoggerFormatTest plain ";
        plainLine += body;
        LOG_INFO(plainLine);
        LogEvent::Flush();

        const Vector<String> formatted = recorder.Messages(LogLevel::Info, {"LoggerFormatTest cut "});
        const Vector<String> plain = recorder.Messages(LogLevel::Info, {"LoggerFormatTest plain "});
        ASSERT_EQ(formatted.size(), 1U);
        ASSERT_EQ(plain.size(), 1U);
        EXPECT_EQ(formatted[0].size(), kCellText);
        EXPECT_EQ(plain[0].size(), kCellText);
        EXPECT_EQ(formatted[0].back(), 'x');
        EXPECT_EQ(plain[0].back(), 'x');
    }

    TEST(LoggerFormatTest, ALineThatFillsTheCellExactlyIsKept) {
        LogRecorder recorder;
        const String body(kCellText - StringView("LoggerFormatTest exact ").size(), 'y');
        LOG_INFO("LoggerFormatTest exact {}", body);
        LogEvent::Flush();

        const Vector<String> lines = recorder.Messages(LogLevel::Info, {"LoggerFormatTest exact "});
        ASSERT_EQ(lines.size(), 1U);
        EXPECT_EQ(lines[0].size(), kCellText);
    }

    namespace {

        /// The formatted lines of the allocation checks: an int, a String, a StringView and a named argument
        void LogFormattedLines(const String& text, StringView view) {
            LOG_INFO("LoggerFormatTest alloc {} {} {} {x}", 7, text, view, FormatArg("x", 1.5));
            LOG_WARNING("LoggerFormatTest alloc {}", 8);
        }

    }  // namespace

    TEST(LoggerFormatTest, AFormattedLineAllocatesNothingOnTheCallingThread) {
        const String text = "a String long enough not to sit in the small buffer of a string object";
        const StringView view = "a view";
        LogFormattedLines(text, view);  // warms the thread-local state of the logger
        LogEvent::Flush();

        ExpectNoAllocations([&] { LogFormattedLines(text, view); });
        LogEvent::Flush();
    }

    TEST(LoggerFormatTest, AFormattedLineAllocatesNothingOnAWorkerThread) {
        const String text = "a String long enough not to sit in the small buffer of a string object";
        const StringView view = "a view";
        LogEvent::Flush();

        uint64_t allocations = 0;
        Thread worker("LoggerFormatTest", [&] {
            LogFormattedLines(text, view);
            allocations = CountAllocations([&] { LogFormattedLines(text, view); }).count;
        });
        worker.join();
        LogEvent::Flush();

        if constexpr (AllocTracker::kEnabled) {
            EXPECT_EQ(allocations, 0U) << "allocations on the worker thread";
        } else {
            GTEST_SKIP() << kNoAllocTracker;
        }
    }

}  // namespace BECore::Tests
