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

        String Read(StringView path) {
            auto text = ReadTextFile(path);
            if (!text) {
                ADD_FAILURE() << text.error();
                return {};
            }
            return *text;
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

    TEST_F(FileDirectoryTest, RemoveTreeRemovesAFileAnEmptyFolderAndAFullOne) {
        MakeTree();
        const String file = _folder.PathOf("tree/a.txt");
        const String empty = _folder.PathOf("tree/empty");

        EXPECT_TRUE(RemoveTree(file).has_value());
        EXPECT_FALSE(GetFileInfo(file).has_value());
        EXPECT_TRUE(RemoveTree(empty).has_value());
        EXPECT_FALSE(GetFileInfo(empty).has_value());
        EXPECT_TRUE(RemoveTree(_folder.PathOf("tree")).has_value());
        EXPECT_FALSE(GetFileInfo(_folder.PathOf("tree")).has_value());
        EXPECT_TRUE(GetFileInfo(Root()).has_value());
    }

    TEST_F(FileDirectoryTest, RemoveTreeOfAMissingPathIsAnError) {
        const String missing = _folder.PathOf("missing");

        const auto removed = RemoveTree(missing);

        ASSERT_FALSE(removed.has_value());
        EXPECT_NE(removed.error().find(missing), String::npos) << removed.error();
    }

    TEST_F(FileDirectoryTest, RemoveTreeWithNonAsciiNames) {
        _folder.Write(Format("{}/{}", kNonAsciiFolder, kNonAsciiFile).c_str(), "x");

        EXPECT_TRUE(RemoveTree(Format("{}/{}", Root(), kNonAsciiFolder)).has_value());
        EXPECT_FALSE(GetFileInfo(Format("{}/{}", Root(), kNonAsciiFolder)).has_value());
    }

#ifndef _WIN32
    TEST_F(FileDirectoryTest, RemoveTreeRemovesASymbolicLinkWithoutTouchingItsTarget) {
        MakeTree();
        MakeLink(_folder.PathOf("tree/sub"), _folder.PathOf("link"), true);

        EXPECT_TRUE(RemoveTree(_folder.PathOf("link")).has_value());
        EXPECT_FALSE(GetFileInfo(_folder.PathOf("link")).has_value());
        EXPECT_EQ(Read(_folder.PathOf("tree/sub/b.txt")), "bb");

        // A link inside the removed folder is removed as a link too
        ASSERT_TRUE(CreateDirectories(_folder.PathOf("other")).has_value());
        MakeLink(_folder.PathOf("tree/sub"), _folder.PathOf("other/link"), true);
        EXPECT_TRUE(RemoveTree(_folder.PathOf("other")).has_value());
        EXPECT_EQ(Read(_folder.PathOf("tree/sub/b.txt")), "bb");
    }
#endif

    TEST_F(FileDirectoryTest, CopyMakesAnotherFileWithTheSameBytes) {
        const String bytes("a\0b\r\n\xFF", 6);
        const String from = _folder.Write("from.bin", bytes);
        const String to = _folder.PathOf("to.bin");

        ASSERT_TRUE(CopyFileTo(from, to).has_value());

        EXPECT_EQ(Read(to), bytes);
        EXPECT_EQ(Read(from), bytes);
    }

    TEST_F(FileDirectoryTest, CopyOverAnExistingFileFailsInFailModeAndReplacesInOverwriteMode) {
        const String from = _folder.Write("from.txt", "new");
        const String to = _folder.Write("to.txt", "old content that is longer");

        const auto refused = CopyFileTo(from, to, CopyMode::Fail);
        ASSERT_FALSE(refused.has_value());
        EXPECT_NE(refused.error().find(to), String::npos) << refused.error();
        EXPECT_EQ(Read(to), "old content that is longer");

        ASSERT_TRUE(CopyFileTo(from, to, CopyMode::Overwrite).has_value());
        EXPECT_EQ(Read(to), "new");
    }

    TEST_F(FileDirectoryTest, CopyRefusesAFolderOnEitherSide) {
        const String file = _folder.Write("file.txt", "x");
        ASSERT_TRUE(CreateDirectories(_folder.PathOf("folder")).has_value());

        EXPECT_FALSE(CopyFileTo(_folder.PathOf("folder"), _folder.PathOf("copy"), CopyMode::Overwrite).has_value());
        EXPECT_FALSE(GetFileInfo(_folder.PathOf("copy")).has_value());
        EXPECT_FALSE(CopyFileTo(file, _folder.PathOf("folder"), CopyMode::Overwrite).has_value());
        EXPECT_EQ(GetFileInfo(_folder.PathOf("folder"))->kind, FileKind::Directory);
    }

    TEST_F(FileDirectoryTest, CopyOfAMissingFileAndIntoAMissingFolderFail) {
        const String file = _folder.Write("file.txt", "x");

        EXPECT_FALSE(CopyFileTo(_folder.PathOf("missing"), _folder.PathOf("copy")).has_value());
        EXPECT_FALSE(CopyFileTo(file, _folder.PathOf("no/such/folder/copy")).has_value());
        EXPECT_FALSE(GetFileInfo(_folder.PathOf("no")).has_value());
    }

    TEST_F(FileDirectoryTest, CopyOntoTheSameFileIsRefusedAndKeepsItsContent) {
        const String file = _folder.Write("file.txt", "keep me");

        const auto copied = CopyFileTo(file, Format("{}/./file.txt", Root()), CopyMode::Overwrite);

        EXPECT_FALSE(copied.has_value());
        EXPECT_EQ(Read(file), "keep me");
    }

    TEST_F(FileDirectoryTest, CopyWithNonAsciiNames) {
        const String from = _folder.Write(Format("{}/{}", kNonAsciiFolder, kNonAsciiFile).c_str(), "x");
        const String to = Format("{}/копия_é.bin", Root());

        ASSERT_TRUE(CopyFileTo(from, to).has_value());
        EXPECT_EQ(Read(to), "x");
    }

#ifndef _WIN32
    TEST_F(FileDirectoryTest, CurrentDirectoryIsTheWorkingFolderWithoutATrailingSeparator) {
        const auto current = CurrentDirectory();

        ASSERT_TRUE(current.has_value());
        EXPECT_EQ(*current, String(std::filesystem::current_path().generic_string().c_str()));  //-NoStd: the oracle
    }
#endif

    TEST_F(FileDirectoryTest, AbsolutePathOfAnAbsolutePathIsItsNormalForm) {
        const String base = Root();

        const auto absolute = AbsolutePath(Format("{}/a/./b/../c/", base));

        ASSERT_TRUE(absolute.has_value());
        EXPECT_EQ(*absolute, Format("{}/a/c/", base));
    }

    TEST_F(FileDirectoryTest, AbsolutePathOfARelativePathStartsAtTheWorkingFolder) {
        const auto current = CurrentDirectory();
        ASSERT_TRUE(current.has_value());

        const auto absolute = AbsolutePath(Format("x/../{}", kNonAsciiFile));

        ASSERT_TRUE(absolute.has_value());
        EXPECT_EQ(*absolute, String((Path(*current) / kNonAsciiFile).LexicallyNormal().View()));
        EXPECT_TRUE(PathIsAbsolute(*absolute));
    }

    TEST_F(FileDirectoryTest, AbsolutePathOfAnEmptyPathIsAnError) {
        EXPECT_FALSE(AbsolutePath("").has_value());
    }

    TEST_F(FileDirectoryTest, CanonicalPathOfAnExistingPathIsTheOneTheSystemKnowsIt) {
        _folder.Write("real/file.txt", "x");
        const String spelled = Format("{}/real/../real/./file.txt", Root());

        const auto canonical = CanonicalPath(spelled);

        ASSERT_TRUE(canonical.has_value()) << canonical.error();
        // The temp folder itself can be a link (/var on macOS): the oracle resolves it the same way
        EXPECT_EQ(*canonical, String(std::filesystem::canonical(Path(spelled)).generic_string().c_str()));  //-NoStd: the oracle
        const auto folder = CanonicalPath(_folder.PathOf("real"));
        ASSERT_TRUE(folder.has_value());
        EXPECT_EQ(*folder, String(Path(*canonical).Parent().View()));
    }

    TEST_F(FileDirectoryTest, CanonicalPathOfAMissingPathIsAnErrorThatNamesIt) {
        const String missing = _folder.PathOf("missing/file.txt");

        const auto canonical = CanonicalPath(missing);

        ASSERT_FALSE(canonical.has_value());
        EXPECT_NE(canonical.error().find(missing), String::npos) << canonical.error();
    }

#ifndef _WIN32
    TEST_F(FileDirectoryTest, CanonicalPathFollowsASymbolicLink) {
        _folder.Write("real/file.txt", "x");
        MakeLink(_folder.PathOf("real"), _folder.PathOf("link"), true);

        const auto viaLink = CanonicalPath(_folder.PathOf("link/file.txt"));
        const auto direct = CanonicalPath(_folder.PathOf("real/file.txt"));

        ASSERT_TRUE(viaLink.has_value());
        ASSERT_TRUE(direct.has_value());
        EXPECT_EQ(*viaLink, *direct);
    }

    TEST_F(FileDirectoryTest, WeaklyCanonicalPathResolvesWhatExistsAndCleansUpTheRest) {
        _folder.Write("real/file.txt", "x");
        MakeLink(_folder.PathOf("real"), _folder.PathOf("link"), true);
        const String spelled = Format("{}/link/not/./there/../here.txt", Root());

        const auto weak = WeaklyCanonicalPath(spelled);

        ASSERT_TRUE(weak.has_value()) << weak.error();
        EXPECT_EQ(*weak, String(std::filesystem::weakly_canonical(Path(spelled)).generic_string().c_str()));  //-NoStd: the oracle
        const auto real = CanonicalPath(_folder.PathOf("real"));
        ASSERT_TRUE(real.has_value());
        EXPECT_EQ(*weak, Format("{}/not/here.txt", *real));
    }

    TEST_F(FileDirectoryTest, WeaklyCanonicalPathOfAnExistingPathIsItsCanonicalPath) {
        _folder.Write("real/file.txt", "x");
        const String file = _folder.PathOf("real/file.txt");

        const auto weak = WeaklyCanonicalPath(file);
        const auto canonical = CanonicalPath(file);

        ASSERT_TRUE(weak.has_value());
        ASSERT_TRUE(canonical.has_value());
        EXPECT_EQ(*weak, *canonical);
    }

    TEST_F(FileDirectoryTest, SameFileThroughALinkAndThroughDotDot) {
        const String file = _folder.Write("real/file.txt", "x");
        _folder.Write("real/other.txt", "x");
        MakeLink(_folder.PathOf("real"), _folder.PathOf("link"), true);

        EXPECT_TRUE(IsSameFile(file, _folder.PathOf("link/file.txt")).value_or(false));
        EXPECT_TRUE(IsSameFile(file, Format("{}/real/../real/file.txt", Root())).value_or(false));
        const auto other = IsSameFile(file, _folder.PathOf("real/other.txt"));
        ASSERT_TRUE(other.has_value());
        EXPECT_FALSE(*other);
    }
#endif

    TEST_F(FileDirectoryTest, SameFileNeedsBothPathsToExist) {
        const String file = _folder.Write("file.txt", "x");

        EXPECT_TRUE(IsSameFile(file, file).value_or(false));
        EXPECT_FALSE(IsSameFile(file, _folder.PathOf("missing")).has_value());
        EXPECT_FALSE(IsSameFile(_folder.PathOf("missing"), file).has_value());
    }

}  // namespace BECore::Tests
