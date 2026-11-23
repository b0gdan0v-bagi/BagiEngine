#include <BECore/File/FilePath.h>
#include <BECore/File/TextFileWriter.h>
#include <Common/AllocCheck.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>

namespace BECore::Tests {

    namespace {

        constexpr size_t kSmall = 16;

        String ReadAll(const String& path) {
            const auto text = ReadTextFile(path);
            EXPECT_TRUE(text.has_value()) << (text ? "" : text.error().c_str());
            return text ? *text : String{};
        }

    }  // namespace

    class TextFileWriterTest : public ::testing::Test {
    protected:
        TestTempFolder _folder{"BagiTextFileWriterTest"};
    };

    TEST_F(TextFileWriterTest, LinesLongerThanTheBufferAreAllWrittenInOrder) {
        const String path = _folder.PathOf("lines.txt");
        String expected;
        {
            TextFileWriter<kSmall> writer;
            ASSERT_TRUE(writer.Open(path, FileMode::Write).has_value());
            for (int i = 0; i < 20; ++i) {
                writer.AppendFormat("line {}\n", i);
                expected += Format("line {}\n", i);
            }
            ASSERT_FALSE(writer.HasError());
        }

        EXPECT_EQ(ReadAll(path), expected);
    }

    TEST_F(TextFileWriterTest, FlushMakesTheTextVisibleBeforeCloseAndCloseWritesTheRest) {
        const String path = _folder.PathOf("flush.txt");
        TextFileWriter<kSmall> writer;
        ASSERT_TRUE(writer.Open(path, FileMode::Write).has_value());

        writer.Append("abc");
        writer.Flush();
        EXPECT_EQ(ReadAll(path), "abc");

        writer.Append("def");
        ASSERT_TRUE(writer.Close().has_value());
        EXPECT_FALSE(writer.IsOpen());
        EXPECT_EQ(ReadAll(path), "abcdef");
    }

    TEST_F(TextFileWriterTest, DestructorWritesTheBuffer) {
        const String path = _folder.PathOf("destructor.txt");
        {
            TextFileWriter<kSmall> writer;
            ASSERT_TRUE(writer.Open(path, FileMode::Write).has_value());
            writer.Append("tail");
        }

        EXPECT_EQ(ReadAll(path), "tail");
    }

    TEST_F(TextFileWriterTest, AppendModeKeepsTheFileAndWriteModeTruncatesIt) {
        const String path = _folder.Write("existing.txt", "old|");
        {
            TextFileWriter<kSmall> writer;
            ASSERT_TRUE(writer.Open(path, FileMode::Append).has_value());
            writer.Append("new");
        }
        EXPECT_EQ(ReadAll(path), "old|new");
        {
            TextFileWriter<kSmall> writer;
            ASSERT_TRUE(writer.Open(path, FileMode::Write).has_value());
            writer.Append("x");
        }

        EXPECT_EQ(ReadAll(path), "x");
    }

    TEST_F(TextFileWriterTest, TextLongerThanTheBufferGoesStraightToTheFileAfterTheBufferedOne) {
        const String path = _folder.PathOf("long.txt");
        const String big(kSmall * 3, 'z');
        {
            TextFileWriter<kSmall> writer;
            ASSERT_TRUE(writer.Open(path, FileMode::Write).has_value());
            writer.Append("head|");
            writer.Append(big);
            writer.Append("|tail");
        }

        EXPECT_EQ(ReadAll(path), "head|" + big + "|tail");
    }

    TEST_F(TextFileWriterTest, FormattedLineLongerThanTheBufferIsCutAtItsSize) {
        const String path = _folder.PathOf("cut.txt");
        {
            TextFileWriter<kSmall> writer;
            ASSERT_TRUE(writer.Open(path, FileMode::Write).has_value());
            writer.Append("ab|");
            writer.AppendFormat("{}", String(kSmall * 2, 'q'));
            writer.Append("|z");
        }

        EXPECT_EQ(ReadAll(path), "ab|" + String(kSmall, 'q') + "|z");
    }

    TEST_F(TextFileWriterTest, OpenErrorNamesThePathAndLeavesTheWriterClosed) {
        const String path = _folder.PathOf("missing-folder/file.txt");
        TextFileWriter<kSmall> writer;

        const auto opened = writer.Open(path, FileMode::Write);

        ASSERT_FALSE(opened.has_value());
        EXPECT_NE(opened.error().find("missing-folder"), String::npos) << opened.error();
        EXPECT_FALSE(writer.IsOpen());
        writer.Append("ignored");
        EXPECT_FALSE(writer.HasError());
    }

    TEST_F(TextFileWriterTest, FirstWriteErrorIsKeptAndLaterCallsDoNothing) {
        const String path = _folder.Write("readonly.txt", "keep");
        auto readOnly = File::Open(path, FileMode::Read);
        ASSERT_TRUE(readOnly.has_value());
        TextFileWriter<kSmall> writer;
        writer.Attach(std::move(*readOnly));

        writer.Append("this does not fit the buffer at all");
        ASSERT_TRUE(writer.HasError());
        writer.Append("more");
        writer.AppendFormat("{}", 1);
        writer.Flush();

        const auto error = writer.TakeError();
        ASSERT_TRUE(error.has_value());
        EXPECT_FALSE(error->empty());
        EXPECT_FALSE(writer.HasError());
        EXPECT_FALSE(writer.TakeError().has_value());
    }

    TEST_F(TextFileWriterTest, CloseReportsTheErrorTheDestructorCannot) {
        const String path = _folder.Write("readonly.txt", "keep");
        auto readOnly = File::Open(path, FileMode::Read);
        ASSERT_TRUE(readOnly.has_value());
        TextFileWriter<kSmall> writer;
        writer.Attach(std::move(*readOnly));
        writer.Append("x");

        const auto closed = writer.Close();

        EXPECT_FALSE(closed.has_value());
        EXPECT_FALSE(writer.IsOpen());
        EXPECT_EQ(ReadAll(path), "keep");
    }

    TEST_F(TextFileWriterTest, MovedWriterCarriesTheBufferedText) {
        const String path = _folder.PathOf("moved.txt");
        {
            TextFileWriter<kSmall> first;
            ASSERT_TRUE(first.Open(path, FileMode::Write).has_value());
            first.Append("moved");
            TextFileWriter<kSmall> second(std::move(first));
            EXPECT_FALSE(first.IsOpen());
            second.Append("+more");
        }

        EXPECT_EQ(ReadAll(path), "moved+more");
    }

    TEST_F(TextFileWriterTest, AppendAndFormatAllocateNothingAfterOpen) {
        const String path = _folder.PathOf("alloc.txt");
        TextFileWriter<64> writer;
        ASSERT_TRUE(writer.Open(path, FileMode::Write).has_value());
        writer.AppendFormat("warm {}\n", 1);
        writer.Flush();

        ExpectNoAllocations([&] {
            for (int i = 0; i < 50; ++i) {
                writer.AppendFormat("[{:>5}] {} {}\n", i, "message", 1.5);
                writer.Append("plain\n");
            }
            writer.Flush();
        });
        EXPECT_FALSE(writer.HasError());
    }

    TEST(PathToUtf8Test, NonAsciiPathKeepsItsUtf8BytesAndOpensAsAFile) {
        const TestTempFolder folder{"BagiPathToUtf8Test"};
        const std::filesystem::path path = std::filesystem::path(ToStd(folder.PathOf("").c_str())) / u8"файл.txt";

        const String utf8 = PathToUtf8(path);

        EXPECT_NE(utf8.find("\xD1\x84\xD0\xB0\xD0\xB9\xD0\xBB.txt"), String::npos) << utf8;
        TextFileWriter<kSmall> writer;
        ASSERT_TRUE(writer.Open(utf8, FileMode::Write).has_value());
        writer.Append("ok");
        ASSERT_TRUE(writer.Close().has_value());
        EXPECT_TRUE(GetFileInfo(utf8).has_value());
    }

}  // namespace BECore::Tests
