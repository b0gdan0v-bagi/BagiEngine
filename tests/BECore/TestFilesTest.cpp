#include <BECore/File/File.h>
#include <BECore/File/Path.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>

namespace BECore::Tests {

    namespace {

        constexpr int64_t kHourNs = int64_t{3600} * 1000 * 1000 * 1000;

        int64_t WriteTimeNs(const Path& path) {
            const Optional<FileInfo> info = GetFileInfo(path);
            return info ? info->modifyTimeNs : 0;
        }

    }  // namespace

    TEST(TestFilesTest, SetWriteTimeIsSeenByTheLayerInTheRightDirection) {
        const TestTempFolder folder("TestFilesTime");
        const Path file(folder.Write("a.txt", "x"));
        const int64_t written = WriteTimeNs(file);

        SetWriteTime(file, std::chrono::hours(1));
        const int64_t future = WriteTimeNs(file);
        EXPECT_GT(future, written + kHourNs / 2);

        SetWriteTime(file, -std::chrono::hours(1));
        EXPECT_LT(WriteTimeNs(file), written - kHourNs / 2);
    }

    TEST(TestFilesTest, CreateSymlinkMakesALinkToAFileAndToAFolder) {
        const TestTempFolder folder("TestFilesLink");
        const Path file(folder.Write("a.txt", "x"));
        const Path sub = Path(folder.PathOf("sub"));
        MakeDirectory(sub);

        CreateSymlink(Path(folder.PathOf("file_link")), file);
        CreateSymlink(Path(folder.PathOf("folder_link")), sub);

        EXPECT_TRUE(IsSymbolicLink(folder.PathOf("file_link")));
        EXPECT_TRUE(IsSymbolicLink(folder.PathOf("folder_link")));
        EXPECT_TRUE(IsFile(Path(folder.PathOf("file_link"))));
        EXPECT_TRUE(IsFolder(Path(folder.PathOf("folder_link"))));
    }

    TEST(TestFilesTest, ResizeFileGivesTheSize) {
        const TestTempFolder folder("TestFilesResize");
        const Path file(folder.Write("a.bin", "abc"));
        constexpr uint64_t kSize = 5'000'000;

        ResizeFile(file, kSize);

        const Optional<FileInfo> info = GetFileInfo(file);
        ASSERT_TRUE(info.has_value());
        EXPECT_EQ(info->size, kSize);
    }

    TEST(TestFilesTest, ScopedCurrentDirectoryEntersAndRestores) {
        const TestTempFolder folder("TestFilesCwd");
        const String before = OsCurrentDirectory();
        const String expected = OsCanonical(Path(folder.PathOf("")));
        {
            const ScopedCurrentDirectory scoped{Path(folder.PathOf(""))};
            EXPECT_EQ(OsCurrentDirectory(), expected);
        }
        EXPECT_EQ(OsCurrentDirectory(), before);
    }

    TEST(TestFilesTest, RemoveFileIfPresentIsQuietOnAMissingFileAndRemovesAnExistingOne) {
        const TestTempFolder folder("TestFilesRemove");
        const Path missing(folder.PathOf("missing.txt"));
        RemoveFileIfPresent(missing);

        const Path file(folder.Write("a.txt", "x"));
        ASSERT_TRUE(IsFile(file));
        RemoveFileIfPresent(file);
        EXPECT_FALSE(PathExists(file));
    }

    TEST(TestFilesTest, ANonAsciiNameGoesThroughWriteAndRead) {
        const TestTempFolder folder("TestFilesUtf8");
        const Path file = Path(folder.PathOf("")) / "\xD0\xB8\xD0\xB3\xD1\x80\xD0\xB0_\xE2\x9C\x93.txt";

        WriteFile(file, "text");

        EXPECT_TRUE(IsFile(file));
        EXPECT_EQ(ReadFile(file), "text");
    }

    TEST(TestFilesTest, MakeEmptyDirectoryClearsWhatWasThere) {
        const TestTempFolder folder("TestFilesEmpty");
        const Path sub = Path(folder.PathOf("sub"));
        WriteFile(sub / "old.txt", "x");

        MakeEmptyDirectory(sub);

        EXPECT_TRUE(IsFolder(sub));
        EXPECT_FALSE(PathExists(sub / "old.txt"));
    }

}  // namespace BECore::Tests
