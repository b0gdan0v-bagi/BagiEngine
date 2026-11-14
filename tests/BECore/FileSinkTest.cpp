#include <BECore/Logger/FileSink.h>
#include <BECore/Logger/LoggerManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>

namespace BECore::Tests {

    /// The text of a log line in a file is "[Level] [date time.ms] message": the layer's Format must write it as before.
    TEST(FileSinkTest, WritesTheLevelTheTimeAndTheMessage) {
        const std::filesystem::path dir = TestTempDirectory("BagiFileSinkTest");
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
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

        std::ifstream stream(file);
        //-NoStd: std::getline reads into std::string
        std::string line;
        std::getline(stream, line);
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

}  // namespace BECore::Tests
