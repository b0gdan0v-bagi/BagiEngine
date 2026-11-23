#include <BECore/File/File.h>
#include <BECore/File/Path.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <EASTL/sort.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kNonAsciiFolder = "Папка_é";
        constexpr StringView kNonAsciiFile = "данные_é.bin";

        /// One line per entry, "F name" or "D name" or "O name", sorted: the order of WalkDirectory is the OS's
        Vector<String> Collect(StringView folder, WalkMode mode) {
            Vector<String> entries;
            const auto walked = WalkDirectory(folder, mode, [&](StringView relative, FileKind kind) {
                const char* letter = kind == FileKind::File ? "F" : (kind == FileKind::Directory ? "D" : "O");
                entries.push_back(Format("{} {}", letter, relative));
                return true;
            });
            EXPECT_TRUE(walked.has_value()) << (walked ? String{} : walked.error());
            eastl::sort(entries.begin(), entries.end());
            return entries;
        }

#ifndef _WIN32
        /// The layer has no link of its own; the tests that need one make it through std
        void MakeLink(StringView target, StringView link, bool folder) {
            std::error_code error;  //-NoStd: creates a symbolic link, which the file layer has no call for
            if (folder) {
                std::filesystem::create_directory_symlink(Path(target), Path(link), error);  //-NoStd: same
            } else {
                std::filesystem::create_symlink(Path(target), Path(link), error);  //-NoStd: same
            }
            EXPECT_FALSE(error) << error.message().c_str();  //-NoStd: same
        }
#endif

    }  // namespace

    class FileDirectoryTest : public ::testing::Test {
    protected:
        /// a.txt, sub/b.txt, sub/deeper/c.txt and the empty folder empty/
        void MakeTree() {
            _folder.Write("tree/a.txt", "a");
            _folder.Write("tree/sub/b.txt", "bb");
            _folder.Write("tree/sub/deeper/c.txt", "ccc");
            ASSERT_TRUE(CreateDirectories(_folder.PathOf("tree/empty")).has_value());
        }

        /// The temp folder without the trailing separator that PathOf("") has
        String Root() const {
            return String(Path(_folder.PathOf("")).Parent().View());
        }

        TestTempFolder _folder{"BagiFileDirectoryTest"};
    };

    TEST_F(FileDirectoryTest, FolderModeListsOnlyTheEntriesOfTheFolderWithTheirKind) {
        MakeTree();

        EXPECT_EQ(Collect(_folder.PathOf("tree"), WalkMode::Folder), (Vector<String>{"D empty", "D sub", "F a.txt"}));
    }

    TEST_F(FileDirectoryTest, TreeModeListsEveryEntryBelowTheFolderWithRelativePaths) {
        MakeTree();

        EXPECT_EQ(Collect(_folder.PathOf("tree"), WalkMode::Tree), (Vector<String>{"D empty", "D sub", "D sub/deeper", "F a.txt", "F sub/b.txt", "F sub/deeper/c.txt"}));
    }

    TEST_F(FileDirectoryTest, TrailingSeparatorOfTheFolderChangesNothing) {
        MakeTree();

        EXPECT_EQ(Collect(Format("{}/", _folder.PathOf("tree")), WalkMode::Tree), Collect(_folder.PathOf("tree"), WalkMode::Tree));
    }

    TEST_F(FileDirectoryTest, EmptyFolderVisitsNothing) {
        ASSERT_TRUE(CreateDirectories(_folder.PathOf("nothing")).has_value());

        EXPECT_TRUE(Collect(_folder.PathOf("nothing"), WalkMode::Tree).empty());
    }

    TEST_F(FileDirectoryTest, MissingFolderIsAnErrorThatNamesIt) {
        const String missing = _folder.PathOf("missing");

        const auto walked = WalkDirectory(missing, WalkMode::Tree, [](StringView, FileKind) { return true; });

        ASSERT_FALSE(walked.has_value());
        EXPECT_NE(walked.error().find(missing), String::npos) << walked.error();
    }

    TEST_F(FileDirectoryTest, FileGivenAsTheFolderIsAnError) {
        const String file = _folder.Write("plain.txt", "x");
        size_t visited = 0;

        const auto walked = WalkDirectory(file, WalkMode::Folder, [&](StringView, FileKind) {
            ++visited;
            return true;
        });

        EXPECT_FALSE(walked.has_value());
        EXPECT_EQ(visited, 0u);
    }

    TEST_F(FileDirectoryTest, CallbackReturningFalseStopsTheWholeWalkAsASuccess) {
        MakeTree();
        size_t visits = 0;
        bool stopped = false;
        bool visitedAfterStop = false;

        // The stop comes from inside a subfolder: the folders still to be listed above it are not listed either
        const auto walked = WalkDirectory(_folder.PathOf("tree"), WalkMode::Tree, [&](StringView relative, FileKind) {
            visitedAfterStop = visitedAfterStop || stopped;
            ++visits;
            stopped = relative == "sub/deeper/c.txt";
            return !stopped;
        });

        EXPECT_TRUE(walked.has_value());
        EXPECT_FALSE(visitedAfterStop);
        EXPECT_GE(visits, 1u);

        size_t onlyOne = 0;
        EXPECT_TRUE(WalkDirectory(_folder.PathOf("tree"), WalkMode::Tree, [&](StringView, FileKind) {
                        ++onlyOne;
                        return false;
                    }).has_value());
        EXPECT_EQ(onlyOne, 1u);
    }

    TEST_F(FileDirectoryTest, NonAsciiNamesAreListedAsUtf8) {
        _folder.Write(Format("tree/{}/{}", kNonAsciiFolder, kNonAsciiFile).c_str(), "x");

        EXPECT_EQ(Collect(Format("{}/tree", Root()), WalkMode::Tree), (Vector<String>{Format("D {}", kNonAsciiFolder), Format("F {}/{}", kNonAsciiFolder, kNonAsciiFile)}));
    }

#ifndef _WIN32
    TEST_F(FileDirectoryTest, SymbolicLinkToFolderIsReportedAsAFolderButNotEntered) {
        MakeTree();
        MakeLink(_folder.PathOf("tree/sub"), _folder.PathOf("tree/link"), true);

        const Vector<String> entries = Collect(_folder.PathOf("tree"), WalkMode::Tree);

        EXPECT_NE(eastl::find(entries.begin(), entries.end(), String("D link")), entries.end());
        EXPECT_EQ(eastl::find(entries.begin(), entries.end(), String("F link/b.txt")), entries.end());
    }

    TEST_F(FileDirectoryTest, SymbolicLinkLoopEndsTheWalk) {
        MakeTree();
        MakeLink(_folder.PathOf("tree"), _folder.PathOf("tree/sub/back"), true);

        EXPECT_EQ(Collect(_folder.PathOf("tree"), WalkMode::Tree).size(), 7u);
    }

    TEST_F(FileDirectoryTest, SymbolicLinkToNowhereIsOther) {
        ASSERT_TRUE(CreateDirectories(_folder.PathOf("tree")).has_value());
        MakeLink(_folder.PathOf("gone"), _folder.PathOf("tree/dangling"), false);

        EXPECT_EQ(Collect(_folder.PathOf("tree"), WalkMode::Folder), (Vector<String>{"O dangling"}));
    }
#endif

}  // namespace BECore::Tests
