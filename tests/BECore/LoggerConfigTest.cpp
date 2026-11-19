#include <BECore/Config/ConfigFactory.h>
#include <BECore/Logger/ConsoleSink.h>
#include <BECore/Logger/LoggerManager.h>
#include <BECore/Logger/OutputSink.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/LogRecorder.h>
#include <Common/Printers.h>
#include <Common/TestText.h>

namespace BECore::Tests {

    namespace {

        // minLevel="Fatal" keeps the sinks quiet while they are subscribed to LogEvent
        constexpr StringView kUnknownTypeXml = "<root><sinks>"
                                               "<sink type=\"Console\" enabled=\"true\" minLevel=\"Fatal\"/>"
                                               "<sink type=\"ConsoleSink\" enabled=\"true\" minLevel=\"Fatal\" colorEnabled=\"false\"/>"
                                               "</sinks></root>";

        constexpr StringView kValidTypesXml = "<root><sinks>"
                                              "<sink type=\"OutputSink\" enabled=\"true\" minLevel=\"Fatal\"/>"
                                              "<sink type=\"ConsoleSink\" enabled=\"true\" minLevel=\"Fatal\" colorEnabled=\"false\"/>"
                                              "</sinks></root>";

    }  // namespace

    TEST(LoggerConfigTest, AnUnknownSinkTypeIsReportedAndDropped) {
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(kUnknownTypeXml));

        const auto manager = New<LoggerManager>();
        manager->Deserialize(deserializer);

        ASSERT_TRUE(deserializer.HasErrors());
        ASSERT_EQ(deserializer.GetErrors().size(), 1u);
        const StringView message = deserializer.GetErrors()[0].errorMessage;
        EXPECT_TRUE(Contains(message, "Unknown factory type")) << message.data();
        EXPECT_TRUE(Contains(message, "'Console'")) << message.data();

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
