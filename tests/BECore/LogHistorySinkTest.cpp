#include <BECore/Logger/LogEvent.h>
#include <BECore/Logger/LogHistorySink.h>
#include <BECore/Logger/LoggerManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/Printers.h>

namespace BECore::Tests {

    namespace {

        /// A history sink alone: nothing is written to a file, and the other sinks of the shipped config are not built
        IntrusivePtrAtomic<LoggerManager> MakeManager(StringView attributes) {
            const String xml = Format("<root><sinks><sink type=\"LogHistorySink\" enabled=\"true\" {}/></sinks></root>", attributes);
            XmlDeserializer deserializer;
            EXPECT_TRUE(deserializer.LoadFromString(xml));
            auto manager = New<LoggerManager>();
            manager->Deserialize(deserializer);
            EXPECT_FALSE(deserializer.HasErrors());
            return manager;
        }

        size_t CountOf(const LogHistory& history, StringView message) {
            size_t count = 0;
            for (size_t i = 0; i < history.Size(); ++i) {
                count += StringView(history.At(i).message.data(), history.At(i).message.size()) == message ? 1 : 0;
            }
            return count;
        }

        const LogRecord* Find(const LogHistory& history, StringView message) {
            for (size_t i = 0; i < history.Size(); ++i) {
                if (StringView(history.At(i).message.data(), history.At(i).message.size()) == message) {
                    return &history.At(i);
                }
            }
            return nullptr;
        }

    }  // namespace

    TEST(LogHistorySinkTest, TheManagerFindsTheSink) {
        const auto manager = MakeManager("");
        LogHistorySink* sink = manager->GetSink<LogHistorySink>();
        ASSERT_NE(sink, nullptr);
        manager->Initialize();
        ASSERT_NE(sink->GetHistory(), nullptr);
        EXPECT_EQ(sink->GetHistory()->GetCapacity(), static_cast<size_t>(LogHistorySink::kDefaultCapacity));
    }

    TEST(LogHistorySinkTest, ALineFromAWorkerThreadIsInTheHistoryAfterFlush) {
        if constexpr (!Thread::kAvailable) {
            GTEST_SKIP() << "no threads in this build";
        }
        const auto manager = MakeManager("");
        manager->Initialize();
        const LogHistory& history = *manager->GetSink<LogHistorySink>()->GetHistory();

        Thread thread("LogHistorySinkWriter", [] { LOG_ERROR("LogHistorySinkTest worker line"); });
        thread.join();
        EXPECT_EQ(CountOf(history, "LogHistorySinkTest worker line"), 0U);
        LogEvent::Flush();

        const LogRecord* record = Find(history, "LogHistorySinkTest worker line");
        ASSERT_NE(record, nullptr);
        EXPECT_EQ(record->level, LogLevel::Error);
        ASSERT_NE(record->site.file, nullptr);
        EXPECT_NE(record->site.line, 0U);

        // HH:MM:SS.mmm
        const StringView time(record->time);
        ASSERT_EQ(time.size(), kLogTimeLength - 1);
        EXPECT_EQ(time[2], ':');
        EXPECT_EQ(time[5], ':');
        EXPECT_EQ(time[8], '.');
    }

    TEST(LogHistorySinkTest, TheCategoryComesFromTheSite) {
        const auto manager = MakeManager("");
        manager->Initialize();
        const LogHistory& history = *manager->GetSink<LogHistorySink>()->GetHistory();

        LogEvent::Emit(LogLevel::Info, "LogHistorySinkTest category", LogSite{"x/src/Chess/Game/Y.cpp", 7});
        LogEvent::Flush();

        const LogRecord* record = Find(history, "LogHistorySinkTest category");
        ASSERT_NE(record, nullptr);
        EXPECT_EQ(record->site.line, 7U);
        EXPECT_EQ(history.GetCategoryName(record->category), "Chess");
    }

    TEST(LogHistorySinkTest, WriteRecordsALineWithoutASite) {
        const auto manager = MakeManager("");
        manager->Initialize();
        LogHistorySink& sink = *manager->GetSink<LogHistorySink>();

        sink.Write(LogLevel::Warning, "LogHistorySinkTest direct write");
        const LogRecord* record = Find(*sink.GetHistory(), "LogHistorySinkTest direct write");
        ASSERT_NE(record, nullptr);
        EXPECT_EQ(record->site.file, nullptr);
        EXPECT_EQ(record->category, kGeneralLogCategory);
    }

    TEST(LogHistorySinkTest, MinLevelFilters) {
        const auto manager = MakeManager("minLevel=\"Warning\"");
        manager->Initialize();
        const LogHistory& history = *manager->GetSink<LogHistorySink>()->GetHistory();

        LOG_INFO("LogHistorySinkTest filtered info");
        LOG_WARNING("LogHistorySinkTest filtered warning");
        LogEvent::Flush();

        EXPECT_EQ(CountOf(history, "LogHistorySinkTest filtered info"), 0U);
        EXPECT_EQ(CountOf(history, "LogHistorySinkTest filtered warning"), 1U);
    }

    TEST(LogHistorySinkTest, TheCapacityIsClamped) {
        const auto small = MakeManager("capacity=\"1\"");
        small->Initialize();
        EXPECT_EQ(small->GetSink<LogHistorySink>()->GetHistory()->GetCapacity(), size_t{LogHistorySink::kMinCapacity});

        const auto large = MakeManager("capacity=\"999999999\"");
        large->Initialize();
        EXPECT_EQ(large->GetSink<LogHistorySink>()->GetHistory()->GetCapacity(), size_t{LogHistorySink::kMaxCapacity});

        const auto exact = MakeManager("capacity=\"300\"");
        exact->Initialize();
        EXPECT_EQ(exact->GetSink<LogHistorySink>()->GetHistory()->GetCapacity(), 300U);
    }

    TEST(LogHistorySinkTest, InitializeTwiceSubscribesOnce) {
        const auto manager = MakeManager("");
        LogHistorySink& sink = *manager->GetSink<LogHistorySink>();
        sink.Initialize();
        sink.Initialize();
        manager->Initialize();

        LOG_INFO("LogHistorySinkTest once");
        LogEvent::Flush();
        EXPECT_EQ(CountOf(*sink.GetHistory(), "LogHistorySinkTest once"), 1U);
    }

}  // namespace BECore::Tests
