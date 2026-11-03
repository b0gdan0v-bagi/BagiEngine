#include <BECore/Config/ConfigFactory.h>
#include <BECore/Logger/ConsoleSink.h>
#include <BECore/Logger/LoggerManager.h>
#include <BECore/Logger/OutputSink.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/Printers.h>

namespace BECore::Tests {

    namespace {

        // minLevel="Fatal" keeps the sinks quiet while they are subscribed to LogEvent
        constexpr eastl::string_view kUnknownTypeXml = "<root><sinks>"
                                                       "<sink type=\"Console\" enabled=\"true\" minLevel=\"Fatal\"/>"
                                                       "<sink type=\"ConsoleSink\" enabled=\"true\" minLevel=\"Fatal\" colorEnabled=\"false\"/>"
                                                       "</sinks></root>";

        constexpr eastl::string_view kValidTypesXml = "<root><sinks>"
                                                      "<sink type=\"OutputSink\" enabled=\"true\" minLevel=\"Fatal\"/>"
                                                      "<sink type=\"ConsoleSink\" enabled=\"true\" minLevel=\"Fatal\" colorEnabled=\"false\"/>"
                                                      "</sinks></root>";

        bool Contains(eastl::string_view text, eastl::string_view part) {
            return text.find(part) != eastl::string_view::npos;
        }

        /// Collects the log messages that LogEvent::Flush() delivers. It copies them: the string_view of a LogEvent
        /// points into the ring buffer. Unsubscribes when it goes out of scope.
        class LogRecorder : public SubscriptionHolder {
        public:
            struct Entry {
                LogLevel level = LogLevel::Debug;
                eastl::string message;
            };

            LogRecorder() {
                Subscribe<LogEvent, &LogRecorder::OnLog>(this);
            }

            /// The first recorded message of @p level that contains every part of @p parts, or nullptr.
            const Entry* Find(LogLevel level, std::initializer_list<eastl::string_view> parts) const {
                for (const Entry& entry : _entries) {
                    if (entry.level != level) {
                        continue;
                    }
                    const eastl::string_view message(entry.message.data(), entry.message.size());
                    bool all = true;
                    for (const eastl::string_view part : parts) {
                        all = all && Contains(message, part);
                    }
                    if (all) {
                        return &entry;
                    }
                }
                return nullptr;
            }

        private:
            void OnLog(const LogEvent& event) {
                _entries.push_back({event.level, eastl::string(event.message.data(), event.message.size())});
            }

            eastl::vector<Entry> _entries;
        };

    }  // namespace

    TEST(LoggerConfigTest, AnUnknownSinkTypeIsReportedAndDropped) {
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(kUnknownTypeXml));

        const auto manager = New<LoggerManager>();
        manager->Deserialize(deserializer);

        ASSERT_TRUE(deserializer.HasErrors());
        ASSERT_EQ(deserializer.GetErrors().size(), 1u);
        const eastl::string_view message = deserializer.GetErrors()[0].errorMessage.ToStringView();
        EXPECT_TRUE(Contains(message, "Unknown factory type")) << message.data();
        EXPECT_TRUE(Contains(message, "'Console'")) << message.data();

        // The unknown entry is gone, the valid one after it stays
        ASSERT_EQ(manager->GetSinks().size(), 1u);
        EXPECT_NE(manager->GetSink<ConsoleSink>(), nullptr);
    }

    TEST(LoggerConfigTest, KnownSinkTypesLoadWithoutErrors) {
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(kValidTypesXml));

        const auto manager = New<LoggerManager>();
        manager->Deserialize(deserializer);

        EXPECT_FALSE(deserializer.HasErrors());
        EXPECT_EQ(manager->GetSinks().size(), 2u);
        EXPECT_NE(manager->GetSink<OutputSink>(), nullptr);
        EXPECT_NE(manager->GetSink<ConsoleSink>(), nullptr);
    }

    TEST(LoggerConfigTest, LogDeserializeErrorsWritesAnErrorWithTheConfigThePathAndTheType) {
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(kUnknownTypeXml));
        {
            const auto manager = New<LoggerManager>();
            manager->Deserialize(deserializer);
        }
        ASSERT_TRUE(deserializer.HasErrors());

        LogRecorder recorder;
        LogDeserializeErrors("LoggerConfig", deserializer);
        LogEvent::Flush();

        const LogRecorder::Entry* entry = recorder.Find(LogLevel::Error, {"Config 'LoggerConfig'"});
        ASSERT_NE(entry, nullptr);
        // The config, the path of the entry in the document and the unknown type
        EXPECT_EQ(entry->message, "Config 'LoggerConfig': sinks[0].type: Unknown factory type: 'Console'");
    }

    TEST(LoggerConfigTest, AnErrorWithoutAPathOmitsIt) {
        XmlDeserializer deserializer;
        ASSERT_FALSE(deserializer.LoadFromString("<root><sinks>"));
        ASSERT_TRUE(deserializer.HasErrors());

        LogRecorder recorder;
        LogDeserializeErrors("LoggerConfigBroken", deserializer);
        LogEvent::Flush();

        const LogRecorder::Entry* entry = recorder.Find(LogLevel::Error, {"Config 'LoggerConfigBroken'"});
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->message, "Config 'LoggerConfigBroken': Failed to parse XML string");
    }

    TEST(LoggerConfigTest, ACleanDeserializerLogsNothing) {
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(kValidTypesXml));
        {
            const auto manager = New<LoggerManager>();
            manager->Deserialize(deserializer);
        }
        ASSERT_FALSE(deserializer.HasErrors());

        LogRecorder recorder;
        LogDeserializeErrors("LoggerConfigClean", deserializer);
        LogEvent::Flush();

        EXPECT_EQ(recorder.Find(LogLevel::Error, {"LoggerConfigClean"}), nullptr);
    }

    TEST(LoggerConfigTest, NoSinksFallBackToAConsoleSinkAndWarn) {
        LogRecorder recorder;
        {
            const auto manager = New<LoggerManager>();
            manager->Initialize();

            ASSERT_EQ(manager->GetSinks().size(), 1u);
            ConsoleSink* console = manager->GetSink<ConsoleSink>();
            ASSERT_NE(console, nullptr);
            console->SetMinLevel(LogLevel::Fatal);  // the warning below is not printed to the test output

            LogEvent::Flush();
        }

        EXPECT_NE(recorder.Find(LogLevel::Warning, {"falling back"}), nullptr);
    }

}  // namespace BECore::Tests
