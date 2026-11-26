#include <BECore/File/File.h>
#include <Common/AllocCheck.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kRawBytes{"a\0b\r\n\xFF\r", 7};

        /// Opens @p path for writing and writes @p content; a failure fails the test
        void PutFile(const String& path, StringView content, FileMode mode = FileMode::Write) {
            auto file = File::Open(path, mode);
            ASSERT_TRUE(file.has_value()) << file.error();
            ASSERT_TRUE(file->Write(content).has_value());
            ASSERT_TRUE(file->Close().has_value());
        }

        /// The whole content of @p path read through File::Read in small pieces, so a short read in the middle would show
        String GetFile(const String& path) {
            auto file = File::Open(path, FileMode::Read);
            if (!file) {
                ADD_FAILURE() << file.error();
                return {};
            }
            String content;
            char piece[3];
            for (;;) {
                const auto count = file->Read(piece, sizeof(piece));
                if (!count) {
                    ADD_FAILURE() << count.error();
                    break;
                }
                content.append(piece, *count);
                if (*count < sizeof(piece)) {
                    break;
                }
            }
            return content;
        }

    }  // namespace

    class FileTest : public ::testing::Test {
    protected:
        TestTempFolder _folder{"BagiFileTest"};
    };

    TEST_F(FileTest, OpenOfMissingFileForReadNamesThePath) {
        const String path = _folder.PathOf("missing.bin");

        const auto file = File::Open(path, FileMode::Read);

        ASSERT_FALSE(file.has_value());
        EXPECT_NE(file.error().find(path.c_str()), String::npos) << file.error();
    }

    TEST_F(FileTest, BytesRoundTripWithoutLineEndTranslation) {
        const String path = _folder.PathOf("raw.bin");

        PutFile(path, kRawBytes);

        EXPECT_EQ(GetFile(path), String(kRawBytes));
        const auto info = GetFileInfo(path);
        ASSERT_TRUE(info.has_value());
        EXPECT_EQ(info->size, kRawBytes.size());
    }

    TEST_F(FileTest, AppendAddsAndWriteTruncates) {
        const String path = _folder.PathOf("log.txt");

        PutFile(path, "one;", FileMode::Append);
        PutFile(path, "two;", FileMode::Append);
        EXPECT_EQ(GetFile(path), "one;two;");

        PutFile(path, "x", FileMode::Write);
        EXPECT_EQ(GetFile(path), "x");
    }

    TEST_F(FileTest, CreateNewFailsOnAnExistingFileOnly) {
        const String path = _folder.PathOf("once.txt");

        PutFile(path, "first", FileMode::CreateNew);
        const auto second = File::Open(path, FileMode::CreateNew);

        EXPECT_FALSE(second.has_value());
        EXPECT_EQ(GetFile(path), "first");
    }

    TEST_F(FileTest, EmptyFileHasSizeZeroAndReadsNothing) {
        const String path = _folder.PathOf("empty.bin");
        PutFile(path, "");

        auto file = File::Open(path, FileMode::Read);
        ASSERT_TRUE(file.has_value());
        const auto size = file->Size();
        ASSERT_TRUE(size.has_value());
        EXPECT_EQ(*size, 0u);
        char byte = 0;
        const auto count = file->Read(&byte, 1);
        ASSERT_TRUE(count.has_value());
        EXPECT_EQ(*count, 0u);
    }

    TEST_F(FileTest, ReadIsShortOnlyAtTheEnd) {
        const String path = _folder.Write("five.bin", "12345");

        auto file = File::Open(path, FileMode::Read);
        ASSERT_TRUE(file.has_value());
        EXPECT_EQ(file->Size().value_or(0), 5u);
        char buffer[8] = {};
        const auto first = file->Read(buffer, 3);
        ASSERT_TRUE(first.has_value());
        EXPECT_EQ(*first, 3u);
        const auto second = file->Read(buffer, 8);
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(*second, 2u);
        const auto third = file->Read(buffer, 8);
        ASSERT_TRUE(third.has_value());
        EXPECT_EQ(*third, 0u);
    }

    TEST_F(FileTest, AReadAfterTheEndSeesWhatWasAppended) {
        const String path = _folder.PathOf("growing.log");
        PutFile(path, "one");
        auto reader = File::Open(path, FileMode::Read);
        ASSERT_TRUE(reader.has_value()) << reader.error();
        char piece[16];
        EXPECT_EQ(reader->Read(piece, sizeof(piece)).value_or(0), 3u);
        EXPECT_EQ(reader->Read(piece, sizeof(piece)).value_or(1), 0u);

        PutFile(path, "two", FileMode::Append);
        EXPECT_EQ(reader->Read(piece, sizeof(piece)).value_or(0), 3u);
        EXPECT_EQ(StringView(piece, 3), "two");
    }

    TEST_F(FileTest, ADirectoryCannotBeOpenedInAnyMode) {
        const String path = _folder.PathOf("folder");
        ASSERT_TRUE(CreateDirectories(path).has_value());

        for (const FileMode mode : {FileMode::Read, FileMode::Write, FileMode::Append, FileMode::CreateNew}) {
            EXPECT_FALSE(File::Open(path, mode).has_value()) << static_cast<int>(+mode);
        }
    }

    TEST_F(FileTest, MovedFileKeepsTheStreamAndTheSourceIsClosed) {
        const String path = _folder.PathOf("moved.txt");
        auto opened = File::Open(path, FileMode::Write);
        ASSERT_TRUE(opened.has_value());

        File moved = std::move(*opened);

        EXPECT_FALSE(opened->IsOpen());  // NOLINT(bugprone-use-after-move): the moved-from state is the contract
        ASSERT_TRUE(moved.IsOpen());
        EXPECT_TRUE(moved.Write("kept").has_value());
        EXPECT_TRUE(moved.Close().has_value());
        EXPECT_FALSE(moved.IsOpen());
        EXPECT_EQ(GetFile(path), "kept");
    }

    TEST_F(FileTest, DestructorFlushesWhatWasWritten) {
        const String path = _folder.PathOf("scoped.txt");
        {
            auto file = File::Open(path, FileMode::Write);
            ASSERT_TRUE(file.has_value());
            ASSERT_TRUE(file->Write("text").has_value());
        }

        EXPECT_EQ(GetFile(path), "text");
    }

    TEST_F(FileTest, GetFileInfoTellsFileFolderAndMissingApart) {
        const String filePath = _folder.Write("a.txt", "abc");
        const String folderPath = _folder.PathOf("dir");
        ASSERT_TRUE(CreateDirectories(folderPath).has_value());

        const auto file = GetFileInfo(filePath);
        const auto folder = GetFileInfo(folderPath);

        ASSERT_TRUE(file.has_value());
        EXPECT_EQ(file->kind, FileKind::File);
        EXPECT_EQ(file->size, 3u);
        EXPECT_GT(file->modifyTimeNs, 0);
        ASSERT_TRUE(folder.has_value());
        EXPECT_EQ(folder->kind, FileKind::Directory);
        EXPECT_FALSE(GetFileInfo(_folder.PathOf("nothing")).has_value());
    }

    TEST_F(FileTest, GetFileInfoAllocatesNothing) {
#ifdef _WIN32
        GTEST_SKIP() << "SDL converts the path to UTF-16 with SDL_malloc on Windows";
#endif
        const String path = _folder.Write("a.txt", "abc");

        ExpectNoAllocations([&] {
            const auto info = GetFileInfo(path);
            EXPECT_TRUE(info.has_value());
        });
    }

    TEST_F(FileTest, CreateDirectoriesMakesNestedFoldersAndIsIdempotent) {
        const String path = Format("{}/b/c", _folder.PathOf("a"));

        EXPECT_TRUE(CreateDirectories(path).has_value());
        EXPECT_TRUE(CreateDirectories(path).has_value());

        const auto info = GetFileInfo(path);
        ASSERT_TRUE(info.has_value());
        EXPECT_EQ(info->kind, FileKind::Directory);
    }

    TEST_F(FileTest, RemoveFileRemovesAFileAndRefusesAMissingPathAndAFolder) {
        const String path = _folder.Write("gone.txt", "x");
        const String folderPath = _folder.PathOf("dir");
        ASSERT_TRUE(CreateDirectories(folderPath).has_value());

        EXPECT_TRUE(RemoveFile(path).has_value());
        EXPECT_FALSE(GetFileInfo(path).has_value());
        const auto again = RemoveFile(path);
        ASSERT_FALSE(again.has_value());
        EXPECT_NE(again.error().find(path.c_str()), String::npos) << again.error();
        EXPECT_FALSE(RemoveFile(folderPath).has_value());
        EXPECT_TRUE(GetFileInfo(folderPath).has_value());
    }

    TEST_F(FileTest, RenameReplacesAFileAndFailsOverAFolder) {
        const String from = _folder.Write("from.txt", "new");
        const String to = _folder.Write("to.txt", "old");
        const String folderPath = _folder.PathOf("dir");
        ASSERT_TRUE(CreateDirectories(folderPath).has_value());

        EXPECT_TRUE(RenameFile(from, to).has_value());
        EXPECT_EQ(GetFile(to), "new");
        EXPECT_FALSE(GetFileInfo(from).has_value());

        const String other = _folder.Write("other.txt", "x");
        EXPECT_FALSE(RenameFile(other, folderPath).has_value());
        EXPECT_EQ(GetFile(other), "x");
    }

    // The names are UTF-8 literals appended to an ASCII base: generic_string() would be the ANSI code page on Windows, where the layer must still read them as UTF-8
    TEST_F(FileTest, PathWithNonAsciiCharactersWorksForEveryCall) {
        const String folderPath = Format("{}/{}", _folder.PathOf("base"), "Файл_é");
        const String filePath = Format("{}/{}", folderPath, "данные_é.bin");
        const String renamed = Format("{}/{}", folderPath, "ещё_é.bin");

        ASSERT_TRUE(CreateDirectories(folderPath).has_value());
        PutFile(filePath, kRawBytes);
        EXPECT_EQ(GetFile(filePath), String(kRawBytes));
        ASSERT_TRUE(RenameFile(filePath, renamed).has_value());
        EXPECT_TRUE(GetFileInfo(renamed).has_value());
        EXPECT_TRUE(RemoveFile(renamed).has_value());
        EXPECT_FALSE(GetFileInfo(renamed).has_value());
    }

    TEST_F(FileTest, PathLongerThanTheInlineBufferWorks) {
        String folderPath = _folder.PathOf("deep");
        while (folderPath.size() < 700) {
            folderPath = Format("{}/{}", folderPath, "nested_folder_name_of_some_length");
        }
        const String filePath = Format("{}/file.txt", folderPath);

        ASSERT_TRUE(CreateDirectories(folderPath).has_value());
        PutFile(filePath, "deep");

        EXPECT_EQ(GetFile(filePath), "deep");
        EXPECT_TRUE(GetFileInfo(filePath).has_value());
    }

}  // namespace BECore::Tests
