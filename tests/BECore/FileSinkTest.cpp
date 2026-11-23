#include <BECore/File/File.h>
#include <BECore/File/TextReader.h>
#include <BECore/Logger/FileSink.h>
#include <BECore/Logger/LoggerManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/AllocCheck.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>

namespace BECore::Tests {

    namespace {

        String ReadFirstLine(const std::filesystem::path& file) {
            const auto text = ReadTextFile(FromStdString(file.generic_string()));
            if (!text) {
                ADD_FAILURE() << text.error();
                return {};
            }
            TextReader reader{StringView{text->data(), text->size()}};
            const Optional<StringView> line = reader.ReadLine();
            return line ? String(*line) : String{};
        }

        String ReadAll(const String& file) {
            const auto text = ReadTextFile(file);
            if (!text) {
                ADD_FAILURE() << text.error();
                return {};
            }
            return *text;
        }

        StringView ViewOf(const String& text) {
            return StringView{text.data(), text.size()};
        }

        /// A manager with one FileSink on @p file (every level), initialized
        IntrusivePtr<LoggerManager> MakeManager(const String& file, bool append) {
            const String xml = Format("<root><sinks><sink type=\"FileSink\" enabled=\"true\" minLevel=\"Debug\" filename=\"{}\" append=\"{}\"/></sinks></root>", file, append);
            XmlDeserializer deserializer;
            EXPECT_TRUE(deserializer.LoadFromString(xml));
            auto manager = New<LoggerManager>();
            manager->Deserialize(deserializer);
            EXPECT_FALSE(deserializer.HasErrors());
            FileSink* sink = manager->GetSink<FileSink>();
            EXPECT_NE(sink, nullptr);
            if (sink != nullptr) {
                sink->Initialize();
            }
            return manager;
        }

    }  // namespace

    /// The text of a log line in a file is "[Level] [date time.ms] message": the layer's Format must write it so.
    TEST(FileSinkTest, WritesTheLevelTheTimeAndTheMessage) {
        const std::filesystem::path dir = TestTempDirectory("BagiFileSinkTest");
        MakeEmptyDirectory(dir);
        const std::filesystem::path file = dir / "sink.log";

        const String xml = Format("<root><sinks><sink type=\"FileSink\" enabled=\"true\" minLevel=\"Warning\" filename=\"{}\" append=\"false\"/></sinks></root>", file.generic_string().c_str());
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(xml));
        {
            const auto manager = New<LoggerManager>();
            manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            FileSink* sink = manager->GetSink<FileSink>();
            ASSERT_NE(sink, nullptr);
            sink->Initialize();
            ASSERT_TRUE(sink->IsOpen());
            sink->Write(LogLevel::Warning, "hello world");
        }

        const String line = ReadFirstLine(file);
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);

        // "[Warning] [YYYY-MM-DD HH:MM:SS.mmm] hello world"
        ASSERT_EQ(line.size(), 47U) << line;
        const StringView text{line.data(), line.size()};
        EXPECT_EQ(text.substr(0, 11), "[Warning] [");
        EXPECT_EQ(text.substr(15, 1), "-");
        EXPECT_EQ(text.substr(18, 1), "-");
        EXPECT_EQ(text.substr(21, 1), " ");
        EXPECT_EQ(text.substr(24, 1), ":");
        EXPECT_EQ(text.substr(27, 1), ":");
        EXPECT_EQ(text.substr(30, 1), ".");
        EXPECT_EQ(text.substr(34), "] hello world");
    }

    /// The sink prints the view it is given, not the characters up to a terminator that may lie past the view.
    TEST(FileSinkTest, WritesOnlyTheViewItIsGiven) {
        const std::filesystem::path dir = TestTempDirectory("BagiFileSinkViewTest");
        MakeEmptyDirectory(dir);
        const std::filesystem::path file = dir / "sink.log";

        const String xml = Format("<root><sinks><sink type=\"FileSink\" enabled=\"true\" minLevel=\"Warning\" filename=\"{}\" append=\"false\"/></sinks></root>", file.generic_string().c_str());
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(xml));
        {
            const auto manager = New<LoggerManager>();
            manager->Deserialize(deserializer);
            FileSink* sink = manager->GetSink<FileSink>();
            ASSERT_NE(sink, nullptr);
            sink->Initialize();
            ASSERT_TRUE(sink->IsOpen());
            sink->Write(LogLevel::Warning, StringView("abcdef", 3));
        }

        const String line = ReadFirstLine(file);
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);

        ASSERT_GE(line.size(), 5U) << line;
        EXPECT_EQ(StringView(line.data(), line.size()).substr(line.size() - 5), "] abc");
    }

    class FileSinkFileTest : public ::testing::Test {
    protected:
        TestTempFolder _folder{"BagiFileSinkFileTest"};
    };

    TEST_F(FileSinkFileTest, LineIsInTheFileAfterTheFlushEvent) {
        const String file = _folder.PathOf("sink.log");
        const auto manager = MakeManager(file, false);
        manager->GetSink<FileSink>()->Write(LogLevel::Info, "flushed line");

        FlushLogsEvent::Emit();

        EXPECT_TRUE(ViewOf(ReadAll(file)).ends_with("] flushed line\n"));
    }

    TEST_F(FileSinkFileTest, LineIsNotLostWhenTheSinkIsDestroyed) {
        const String file = _folder.PathOf("sink.log");
        {
            const auto manager = MakeManager(file, false);
            manager->GetSink<FileSink>()->Write(LogLevel::Info, "last line");
        }

        EXPECT_TRUE(ViewOf(ReadAll(file)).ends_with("] last line\n"));
    }

    TEST_F(FileSinkFileTest, AppendKeepsTheOldContentAndNoAppendTruncates) {
        const String file = _folder.Write("sink.log", "old\n");
        {
            const auto manager = MakeManager(file, true);
            manager->GetSink<FileSink>()->Write(LogLevel::Info, "appended");
        }
        const String appended = ReadAll(file);
        EXPECT_TRUE(ViewOf(appended).starts_with("old\n[Info] ")) << appended;
        EXPECT_TRUE(ViewOf(appended).ends_with("] appended\n")) << appended;

        {
            const auto manager = MakeManager(file, false);
            manager->GetSink<FileSink>()->Write(LogLevel::Info, "fresh");
        }
        const String truncated = ReadAll(file);
        EXPECT_TRUE(ViewOf(truncated).starts_with("[Info] ")) << truncated;
        EXPECT_EQ(truncated.find("old"), String::npos);
    }

    // More text than the sink's buffer holds, so the buffer is written out in between, and every line comes in order and whole.
    TEST_F(FileSinkFileTest, ManyLinesComeInOrder) {
        const String file = _folder.PathOf("sink.log");
        constexpr int kLines = 40;
        const String padding(100, 'p');
        {
            const auto manager = MakeManager(file, false);
            for (int i = 0; i < kLines; ++i) {
                manager->GetSink<FileSink>()->Write(LogLevel::Info, Format("line {:02} {}", i, padding));
            }
        }

        const String text = ReadAll(file);
        TextReader reader{StringView{text.data(), text.size()}};
        for (int i = 0; i < kLines; ++i) {
            const Optional<StringView> line = reader.ReadLine();
            ASSERT_TRUE(line.has_value()) << "line " << i;
            EXPECT_TRUE(line->ends_with(ViewOf(Format("] line {:02} {}", i, padding)))) << String(*line);
        }
        EXPECT_TRUE(reader.AtEnd());
    }

    TEST_F(FileSinkFileTest, WholeMessageOfTheRingCellKeepsItsLine) {
        const String file = _folder.PathOf("sink.log");
        const String message(LogEntry::MaxMessageSize - 1, 'm');
        {
            const auto manager = MakeManager(file, false);
            manager->GetSink<FileSink>()->Write(LogLevel::Warning, message);
        }

        EXPECT_TRUE(ViewOf(ReadAll(file)).ends_with(ViewOf(Format("] {}\n", message))));
    }

    TEST_F(FileSinkFileTest, FileInAMissingFolderLeavesTheSinkClosedAndWritesHarmless) {
        const auto manager = MakeManager(_folder.PathOf("missing/sink.log"), false);
        FileSink* sink = manager->GetSink<FileSink>();
        ASSERT_NE(sink, nullptr);

        EXPECT_FALSE(sink->IsOpen());
        sink->Write(LogLevel::Error, "nowhere to go");
        FlushLogsEvent::Emit();
    }

    TEST_F(FileSinkFileTest, ALineAllocatesNothingOnceTheFirstIsWritten) {
        const String file = _folder.PathOf("sink.log");
        const auto manager = MakeManager(file, false);
        FileSink* sink = manager->GetSink<FileSink>();
        const String padding(150, 'p');
        sink->Write(LogLevel::Info, padding);

        // Enough lines to pass the buffer more than once, so the write path is in the count
        ExpectNoAllocations([&] {
            for (int i = 0; i < 20; ++i) {
                sink->Write(LogLevel::Info, padding);
            }
            FlushLogsEvent::Emit();
        });
    }

}  // namespace BECore::Tests
