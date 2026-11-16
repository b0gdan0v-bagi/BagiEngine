#include <BECore/Assert/AssertEvent.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Logger/Logger.h>
#include <BECore/Threading/Thread.h>
#include <Common/AssertBridge.h>
#include <Common/EngineEnvironment.h>
#include <Common/LogRecorder.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kExpression = "LoggerFatalTest expression";
        constexpr StringView kFatalText = "LoggerFatalTest fatal from a worker";

        bool EndsWith(const char* text, StringView tail) {
            const StringView view(text);
            return view.size() >= tail.size() && view.substr(view.size() - tail.size()) == tail;
        }

    }  // namespace

    /// The engine's own LogHandler (in the test environment, with the debug break off) turns an AssertEvent into a log line
    class LoggerFatalTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }
    };

    TEST_F(LoggerFatalTest, AnAssertLineHasItsSite) {
        LogRecorder recorder;
        constexpr int kLine = __LINE__ + 2;
        AssertBridge::Capture capture;
        AssertEvent::Emit(__FILE__, kLine, kExpression, nullptr, AssertType::Assert);
        LogEvent::Flush();

        const LogRecorder::Entry* entry = recorder.Find(LogLevel::Error, {"ASSERT failed", kExpression});
        ASSERT_NE(entry, nullptr);
        ASSERT_NE(entry->site.file, nullptr);
        EXPECT_TRUE(EndsWith(entry->site.file, "LoggerFatalTest.cpp")) << entry->site.file;
        EXPECT_EQ(entry->site.line, static_cast<uint32_t>(kLine));
    }

    TEST_F(LoggerFatalTest, AnAssertIsFlushedOnTheMainThread) {
        LogRecorder recorder;
        AssertBridge::Capture capture;
        AssertEvent::Emit(__FILE__, __LINE__, kExpression, nullptr, AssertType::Assert);
        AssertEvent::Emit(__FILE__, __LINE__, nullptr, "LoggerFatalTest fatal error", AssertType::FatalError);

        // No LogEvent::Flush(): the handler delivered the lines itself
        EXPECT_NE(recorder.Find(LogLevel::Error, {"ASSERT failed", kExpression}), nullptr);
        EXPECT_NE(recorder.Find(LogLevel::Fatal, {"FATAL", "LoggerFatalTest fatal error"}), nullptr);
    }

    TEST_F(LoggerFatalTest, AnExpectIsNotFlushedAtOnce) {
        LogRecorder recorder;
        AssertBridge::Capture capture;
        AssertEvent::Emit(__FILE__, __LINE__, kExpression, nullptr, AssertType::Expect);
        EXPECT_EQ(recorder.Count(LogLevel::Warning, {"EXPECT failed", kExpression}), 0U);

        LogEvent::Flush();
        EXPECT_EQ(recorder.Count(LogLevel::Warning, {"EXPECT failed", kExpression}), 1U);
    }

    TEST_F(LoggerFatalTest, FatalFromAWorkerWaitsForTheFrameFlush) {
        if constexpr (!Thread::kAvailable) {
            GTEST_SKIP() << "no threads in this build";
        }
        LogRecorder recorder;
        bool flushedOnTheWorker = true;
        Thread thread("LoggerFatalTestWorker", [&flushedOnTheWorker] {
            LOG_FATAL(kFatalText);
            flushedOnTheWorker = Logger::FlushOnMainThread();
        });
        thread.join();

        EXPECT_FALSE(flushedOnTheWorker);
        EXPECT_EQ(recorder.Count(LogLevel::Fatal, {kFatalText}), 0U);

        LogEvent::Flush();
        EXPECT_EQ(recorder.Count(LogLevel::Fatal, {kFatalText}), 1U);
    }

    TEST_F(LoggerFatalTest, FatalOnTheMainThreadIsDeliveredBeforeItReturns) {
        LogRecorder recorder;
        LOG_FATAL("LoggerFatalTest fatal on the main thread");

        EXPECT_EQ(recorder.Count(LogLevel::Fatal, {"LoggerFatalTest fatal on the main thread"}), 1U);
    }

}  // namespace BECore::Tests
