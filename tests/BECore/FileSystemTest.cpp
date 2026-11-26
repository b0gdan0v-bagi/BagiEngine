#include <BECore/FileSystem/FileSystem.h>
#include <Common/FreshText.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>

namespace BECore::Tests {

    namespace {

        /// A file whose content no test reads: only where it lies matters
        void Touch(const Path& path) {
            WriteFile(path, "x");
        }

        /// A host path as `Path` in the absolute and normal form that `FileSystem` keeps
        Path Normal(const Path& path) {
            return path.LexicallyNormal();
        }

    }  // namespace

    /// Two content roots on disk, like an engine checkout and a game on top of it.
    class FileSystemTest : public ::testing::Test {
    protected:
        void SetUp() override {
            // One directory per test and per process: ctest runs the tests in parallel processes, two checkouts may run the same one (TempPath.h)
            _base = TestTempDirectory("BagiFileSystemTest");
            RemoveDirectory(_base);
            _engine = _base / "engine";
            _game = _base / "game";
            MakeDirectory(_engine);
            MakeDirectory(_game);
        }

        void TearDown() override {
            RemoveDirectory(_base);
        }

        Path _base;
        Path _engine;
        Path _game;
    };

    TEST_F(FileSystemTest, UpperRootOverridesLowerRoot) {
        Touch(_engine / "config" / "A.xml");
        Touch(_game / "config" / "A.xml");

        FileSystem fs;
        fs.AddContentRoot(_engine);
        fs.AddContentRoot(_game);
        fs.Initialize();

        EXPECT_EQ(fs.ResolvePath("config/A.xml"), Normal(_game / "config" / "A.xml"));
    }

    TEST_F(FileSystemTest, FallsBackToLowerRoot) {
        Touch(_engine / "config" / "OnlyEngine.xml");
        Touch(_game / "config" / "OnlyGame.xml");

        FileSystem fs;
        fs.AddContentRoot(_engine);
        fs.AddContentRoot(_game);
        fs.Initialize();

        EXPECT_EQ(fs.ResolvePath("config/OnlyEngine.xml"), Normal(_engine / "config" / "OnlyEngine.xml"));
        EXPECT_EQ(fs.ResolvePath("config/OnlyGame.xml"), Normal(_game / "config" / "OnlyGame.xml"));
    }

    TEST_F(FileSystemTest, MissingFileResolvesToEmptyPath) {
        Touch(_engine / "config" / "A.xml");

        FileSystem fs;
        fs.AddContentRoot(_engine);
        fs.Initialize();

        EXPECT_TRUE(fs.ResolvePath("config/Missing.xml").Empty());
        EXPECT_FALSE(fs.Exists("config/Missing.xml"));
        EXPECT_TRUE(fs.Exists("config/A.xml"));
    }

    TEST_F(FileSystemTest, PathInTopLayerNamesThePlaceInTheTopLayerAndMakesNothing) {
        Touch(_engine / "config" / "A.xml");
        MakeDirectory(_game / "config");

        FileSystem fs;
        fs.AddContentRoot(_engine);
        fs.AddContentRoot(_game);
        fs.Initialize();

        // The top layer even for a file that a lower layer has
        EXPECT_EQ(fs.PathInTopLayer("config/A.xml"), Normal(_game / "config" / "A.xml"));
        EXPECT_EQ(fs.PathInTopLayer("config/made/deep"), Normal(_game / "config" / "made" / "deep"));
        EXPECT_EQ(fs.PathInTopLayer("config"), Normal(_game / "config"));
        EXPECT_EQ(fs.PathInTopLayer("config/"), Normal(_game / "config"));
        EXPECT_EQ(fs.PathInTopLayer("config/./A.xml").LexicallyNormal(), Normal(_game / "config" / "A.xml"));

        EXPECT_TRUE(fs.PathInTopLayer("nowhere/A.xml").Empty());
        EXPECT_TRUE(fs.PathInTopLayer((_engine / "config" / "A.xml").View()).Empty());
        const StringView leaving[] = {"config/../x", "config//A.xml", "config/a//b", "config\\..", StringView("config/a\0b", 10)};
        for (const StringView path : leaving) {
            EXPECT_TRUE(fs.PathInTopLayer(path).Empty()) << String(path).c_str();
        }

        EXPECT_FALSE(PathExists(_game / "config" / "A.xml"));
        EXPECT_FALSE(PathExists(_game / "config" / "made"));
    }

    TEST_F(FileSystemTest, ResolvePathDoesNotFollowAVirtualPathOutOfWhereItLooks) {
        // Beside the content root: reachable from a layer, a root and the working directory by climbing only
        const Path outside = _base / "outside.txt";
        Touch(outside);
        MakeDirectory(_engine / "config");
        MakeDirectory(_engine / "unmounted");
        MakeDirectory(_base / "cwd" / "work");
        const ScopedCurrentDirectory enter(_base / "cwd");

        FileSystem fs;
        fs.AddContentRoot(_engine);
        fs.Initialize();

        // The OS would find the file through each of these joins
        ASSERT_TRUE(PathExists(_engine / "config" / "../../outside.txt"));
        ASSERT_TRUE(PathExists(_engine / "unmounted/../../outside.txt"));
        ASSERT_TRUE(PathExists(Path("work/../../outside.txt")));

        EXPECT_TRUE(fs.ResolvePath("config/../../outside.txt").Empty()) << "mount layer";
        EXPECT_TRUE(fs.ResolvePath(Format("config/{}", outside.View())).Empty()) << "an absolute remainder";
        EXPECT_TRUE(fs.ResolvePath("unmounted/../../outside.txt").Empty()) << "root";
        EXPECT_TRUE(fs.ResolvePath("work/../../outside.txt").Empty()) << "working directory";
        EXPECT_FALSE(fs.Exists("config/../../outside.txt"));

        // A path that is absolute as a whole is the caller naming a real file
        EXPECT_EQ(fs.ResolvePath(outside.View()), outside);
    }

    TEST_F(FileSystemTest, ResolvePathFindsNamesThatOnlyContainDots) {
        Touch(_engine / "config" / "a..b");
        Touch(_engine / "config" / "..hidden");

        FileSystem fs;
        fs.AddContentRoot(_engine);
        fs.Initialize();

        EXPECT_EQ(fs.ResolvePath("config/a..b"), Normal(_engine / "config" / "a..b"));
        EXPECT_EQ(fs.ResolvePath("config/..hidden"), Normal(_engine / "config" / "..hidden"));
    }

    TEST_F(FileSystemTest, MountLayersAreOrderedLowestPriorityFirst) {
        Touch(_engine / "config" / "A.xml");
        Touch(_game / "config" / "B.xml");

        FileSystem fs;
        fs.AddContentRoot(_engine);
        fs.AddContentRoot(_game);
        fs.Initialize();

        const auto layers = fs.GetMountLayers("config"_intern);
        ASSERT_EQ(layers.size(), 2u);
        EXPECT_EQ(layers[0], Normal(_engine / "config"));
        EXPECT_EQ(layers[1], Normal(_game / "config"));
        EXPECT_EQ(fs.GetMountedPath("config"_intern), Normal(_game / "config"));
        EXPECT_EQ(fs.GetRootPath(), Normal(_game));
    }

    TEST_F(FileSystemTest, RootWithoutDirectoryAddsNoLayer) {
        Touch(_engine / "assets" / "a.png");
        MakeDirectory(_game / "config");

        FileSystem fs;
        fs.AddContentRoot(_engine);
        fs.AddContentRoot(_game);
        fs.Initialize();

        EXPECT_EQ(fs.GetMountLayers("assets"_intern).size(), 1u);
        EXPECT_EQ(fs.GetMountLayers("config"_intern).size(), 1u);
    }

    TEST_F(FileSystemTest, AddingTheSameRootTwiceIsNoOp) {
        Touch(_engine / "config" / "A.xml");

        FileSystem fs;
        fs.AddContentRoot(_engine);
        fs.AddContentRoot(_engine);
        fs.Initialize();

        EXPECT_EQ(fs.GetMountLayers("config"_intern).size(), 1u);
    }

    TEST_F(FileSystemTest, SetRootPathReplacesAllRoots) {
        Touch(_engine / "config" / "A.xml");
        Touch(_game / "config" / "B.xml");

        FileSystem fs;
        fs.AddContentRoot(_engine);
        fs.SetRootPath(_game);
        fs.Initialize();

        const auto layers = fs.GetMountLayers("config"_intern);
        ASSERT_EQ(layers.size(), 1u);
        EXPECT_EQ(layers[0], Normal(_game / "config"));
    }

    TEST_F(FileSystemTest, EnumerateFilesMergesLayersAndListsOverriddenFileOnce) {
        Touch(_engine / "assets" / "a.png");
        Touch(_engine / "assets" / "b.png");
        Touch(_engine / "assets" / "ignored.txt");
        Touch(_game / "assets" / "b.png");
        Touch(_game / "assets" / "sub" / "c.PNG");

        FileSystem fs;
        fs.AddContentRoot(_engine);
        fs.AddContentRoot(_game);
        fs.Initialize();

        const StringView extensions[] = {".png"};
        const auto files = fs.EnumerateFiles("assets"_intern, Span<const StringView>(extensions));

        ASSERT_EQ(files.size(), 3u);
        EXPECT_EQ(files[0].ToStringView(), "assets/a.png");
        EXPECT_EQ(files[1].ToStringView(), "assets/b.png");
        EXPECT_EQ(files[2].ToStringView(), "assets/sub/c.PNG");
    }

    TEST_F(FileSystemTest, EnumerateFilesInternsTheWholeListWithOneBatch) {
        // Names no other case of this process interned: the pool is shared and cannot forget them
        constexpr size_t kFiles = 40;
        const String stem = FreshText("enumbatch");
        Vector<String> expected;
        for (size_t i = 0; i < kFiles; ++i) {
            const String name = Format("{}_{:03}.png", stem, kFiles - 1 - i);
            Touch(_engine / "assets" / "enumbatch" / name);
            if (i % 2 == 0) {
                Touch(_game / "assets" / "enumbatch" / name);
            }
        }
        for (size_t i = 0; i < kFiles; ++i) {
            expected.push_back(Format("assets/enumbatch/{}_{:03}.png", stem, i));
        }

        FileSystem fs;
        fs.AddContentRoot(_engine);
        fs.AddContentRoot(_game);
        fs.Initialize();
        const PoolString assets = "assets"_intern;
        const StringView extensions[] = {".png"};

        const PoolStringStats before = PoolString::GetStats();
        const auto files = fs.EnumerateFiles(assets, Span<const StringView>(extensions));
        const PoolStringStats after = PoolString::GetStats();

        ASSERT_EQ(files.size(), expected.size());
        for (size_t i = 0; i < files.size(); ++i) {
            EXPECT_EQ(files[i].ToStringView(), StringView(expected[i])) << i;
        }
        EXPECT_EQ(after.batchCalls - before.batchCalls, 1u);
        EXPECT_EQ(after.batchStrings - before.batchStrings, kFiles);
        EXPECT_EQ(after.writeLocks - before.writeLocks, 1u);
    }

    TEST_F(FileSystemTest, EnumerateFilesOrdersByPathComponentsNotByText) {
        // As text "a.txt" comes before "a/b.txt" ('.' is below '/'); as a path the component "a" is below "a-b.txt" and "a.txt", so the folder's files come first
        Touch(_engine / "assets" / "a.txt");
        Touch(_engine / "assets" / "a" / "b.txt");
        Touch(_game / "assets" / "a" / "a.txt");
        Touch(_game / "assets" / "a-b.txt");

        FileSystem fs;
        fs.AddContentRoot(_engine);
        fs.AddContentRoot(_game);
        fs.Initialize();

        const StringView extensions[] = {".txt"};
        const auto files = fs.EnumerateFiles("assets"_intern, Span<const StringView>(extensions));

        ASSERT_EQ(files.size(), 4u);
        EXPECT_EQ(files[0].ToStringView(), "assets/a/a.txt");
        EXPECT_EQ(files[1].ToStringView(), "assets/a/b.txt");
        EXPECT_EQ(files[2].ToStringView(), "assets/a-b.txt");
        EXPECT_EQ(files[3].ToStringView(), "assets/a.txt");
    }

    TEST_F(FileSystemTest, EnumerateFilesListsNonAsciiNamesInPathOrderAndAnUppercaseExtension) {
        // Bytes decide, per component: 'z' < 0xC3 (the folder and the names starting with "ü") < 0xD0 ("Ж"); in "ünï/", "ünï-x" and "ünï.png" the shorter component first, then '-' < '.'
        const Path engineAssets = Path(_engine) / "assets";
        Touch(engineAssets / "Ж.png");
        Touch(engineAssets / "ünï.png");
        Touch(engineAssets / "ünï-x.png");
        Touch(engineAssets / "z.png");
        Touch(Path(_game) / "assets" / "ünï" / "b.PNG");
        Touch(Path(_game) / "assets" / "ünï.png");

        FileSystem fs;
        fs.AddContentRoot(_engine);
        fs.AddContentRoot(_game);
        fs.Initialize();

        const StringView extensions[] = {".png"};
        const auto files = fs.EnumerateFiles("assets"_intern, Span<const StringView>(extensions));

        ASSERT_EQ(files.size(), 5u);
        EXPECT_EQ(files[0].ToStringView(), "assets/z.png");
        EXPECT_EQ(files[1].ToStringView(), "assets/ünï/b.PNG");
        EXPECT_EQ(files[2].ToStringView(), "assets/ünï-x.png");
        EXPECT_EQ(files[3].ToStringView(), "assets/ünï.png");  // in both layers, listed once
        EXPECT_EQ(files[4].ToStringView(), "assets/Ж.png");
    }

    TEST_F(FileSystemTest, EnumerateFilesSkipsALayerThatCannotBeRead) {
        Touch(_engine / "assets" / "a.png");
        Touch(_game / "assets" / "b.png");

        FileSystem fs;
        fs.AddContentRoot(_engine);
        fs.AddContentRoot(_game);
        fs.Initialize();
        RemoveDirectory(_game / "assets");

        const StringView extensions[] = {".png"};
        const auto files = fs.EnumerateFiles("assets"_intern, Span<const StringView>(extensions));

        ASSERT_EQ(files.size(), 1u);
        EXPECT_EQ(files[0].ToStringView(), "assets/a.png");
    }

    TEST_F(FileSystemTest, ResolvePathFindsAFileWithANonAsciiName) {
        const Path file = _engine / "config" / "héllo ж.xml";
        Touch(file);

        FileSystem fs;
        fs.AddContentRoot(_engine);
        fs.Initialize();

        EXPECT_EQ(fs.ResolvePath("config/héllo ж.xml"), Normal(file));
        EXPECT_TRUE(fs.Exists("config/héllo ж.xml"));
    }

    TEST_F(FileSystemTest, MountRefusesAFileAndStoresARelativeFolderAbsoluteAndNormal) {
        Touch(_engine / "data" / "file.txt");

        FileSystem fs;
        EXPECT_FALSE(fs.Mount("data"_intern, _engine / "data" / "file.txt"));
        EXPECT_FALSE(fs.Mount("data"_intern, _engine / "missing"));
        EXPECT_TRUE(fs.GetMountLayers("data"_intern).empty());

        const ScopedCurrentDirectory enter(_engine);
        const auto current = CurrentDirectory();
        ASSERT_TRUE(current.has_value()) << current.error().c_str();

        EXPECT_TRUE(fs.Mount("data"_intern, "./other/../data"));

        ASSERT_EQ(fs.GetMountLayers("data"_intern).size(), 1u);
        EXPECT_EQ(fs.GetMountedPath("data"_intern), Normal(Path(*current) / "data"));
    }

#ifndef _WIN32
    TEST_F(FileSystemTest, EnumerateFilesTurnsABackslashInAFileNameIntoASlash) {
        // A POSIX file name may hold a backslash; the virtual path has only slashes
        Touch(_engine / "assets" / "back\\slash.png");

        FileSystem fs;
        fs.AddContentRoot(_engine);
        fs.Initialize();

        const StringView extensions[] = {".png"};
        const auto files = fs.EnumerateFiles("assets"_intern, Span<const StringView>(extensions));

        ASSERT_EQ(files.size(), 1u);
        EXPECT_EQ(files[0].ToStringView(), "assets/back/slash.png");
    }
#endif

    TEST_F(FileSystemTest, WithoutContentRootsTheProjectRootIsFoundFromTheWorkingDirectory) {
        // A working directory two levels below a folder with config/, whatever folder the run started in
        MakeDirectory(_engine / "config");
        const Path deep = _engine / "a" / "b";
        MakeDirectory(deep);
        const ScopedCurrentDirectory enter(deep);

        FileSystem fs;
        fs.Initialize();

        const auto same = IsSameFile(fs.GetRootPath(), _engine);
        ASSERT_TRUE(same.has_value()) << same.error().c_str();
        EXPECT_TRUE(*same) << fs.GetRootPath().CStr() << " is not " << _engine.CStr();
        EXPECT_FALSE(fs.GetMountLayers("config"_intern).empty());
    }

}  // namespace BECore::Tests
