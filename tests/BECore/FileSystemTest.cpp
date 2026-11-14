#include <BECore/FileSystem/FileSystem.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>

namespace BECore::Tests {

    namespace {

        void WriteFile(const std::filesystem::path& path) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream(path) << "x";
        }

    }  // namespace

    /// Two content roots on disk, like an engine checkout and a game on top of it.
    class FileSystemTest : public ::testing::Test {
    protected:
        void SetUp() override {
            // One directory per test: ctest runs every test in its own process
            _base = TestTempDirectory("BagiFileSystemTest");
            std::filesystem::remove_all(_base);
            _engine = _base / "engine";
            _game = _base / "game";
            std::filesystem::create_directories(_engine);
            std::filesystem::create_directories(_game);
        }

        void TearDown() override {
            std::error_code ec;
            std::filesystem::remove_all(_base, ec);
        }

        std::filesystem::path _base;
        std::filesystem::path _engine;
        std::filesystem::path _game;
    };

    TEST_F(FileSystemTest, UpperRootOverridesLowerRoot) {
        WriteFile(_engine / "config" / "A.xml");
        WriteFile(_game / "config" / "A.xml");

        FileSystem fs;
        fs.AddContentRoot(_engine);
        fs.AddContentRoot(_game);
        fs.Initialize();

        EXPECT_EQ(fs.ResolvePath("config/A.xml"), (_game / "config" / "A.xml").lexically_normal());
    }

    TEST_F(FileSystemTest, FallsBackToLowerRoot) {
        WriteFile(_engine / "config" / "OnlyEngine.xml");
        WriteFile(_game / "config" / "OnlyGame.xml");

        FileSystem fs;
        fs.AddContentRoot(_engine);
        fs.AddContentRoot(_game);
        fs.Initialize();

        EXPECT_EQ(fs.ResolvePath("config/OnlyEngine.xml"), (_engine / "config" / "OnlyEngine.xml").lexically_normal());
        EXPECT_EQ(fs.ResolvePath("config/OnlyGame.xml"), (_game / "config" / "OnlyGame.xml").lexically_normal());
    }

    TEST_F(FileSystemTest, MissingFileResolvesToEmptyPath) {
        WriteFile(_engine / "config" / "A.xml");

        FileSystem fs;
        fs.AddContentRoot(_engine);
        fs.Initialize();

        EXPECT_TRUE(fs.ResolvePath("config/Missing.xml").empty());
        EXPECT_FALSE(fs.Exists("config/Missing.xml"));
        EXPECT_TRUE(fs.Exists("config/A.xml"));
    }

    TEST_F(FileSystemTest, MountLayersAreOrderedLowestPriorityFirst) {
        WriteFile(_engine / "config" / "A.xml");
        WriteFile(_game / "config" / "B.xml");

        FileSystem fs;
        fs.AddContentRoot(_engine);
        fs.AddContentRoot(_game);
        fs.Initialize();

        const auto layers = fs.GetMountLayers("config"_intern);
        ASSERT_EQ(layers.size(), 2u);
        EXPECT_EQ(layers[0], (_engine / "config").lexically_normal());
        EXPECT_EQ(layers[1], (_game / "config").lexically_normal());
        EXPECT_EQ(fs.GetMountedPath("config"_intern), (_game / "config").lexically_normal());
        EXPECT_EQ(fs.GetRootPath(), _game.lexically_normal());
    }

    TEST_F(FileSystemTest, RootWithoutDirectoryAddsNoLayer) {
        WriteFile(_engine / "assets" / "a.png");
        std::filesystem::create_directories(_game / "config");

        FileSystem fs;
        fs.AddContentRoot(_engine);
        fs.AddContentRoot(_game);
        fs.Initialize();

        EXPECT_EQ(fs.GetMountLayers("assets"_intern).size(), 1u);
        EXPECT_EQ(fs.GetMountLayers("config"_intern).size(), 1u);
    }

    TEST_F(FileSystemTest, AddingTheSameRootTwiceIsNoOp) {
        WriteFile(_engine / "config" / "A.xml");

        FileSystem fs;
        fs.AddContentRoot(_engine);
        fs.AddContentRoot(_engine);
        fs.Initialize();

        EXPECT_EQ(fs.GetMountLayers("config"_intern).size(), 1u);
    }

    TEST_F(FileSystemTest, SetRootPathReplacesAllRoots) {
        WriteFile(_engine / "config" / "A.xml");
        WriteFile(_game / "config" / "B.xml");

        FileSystem fs;
        fs.AddContentRoot(_engine);
        fs.SetRootPath(_game);
        fs.Initialize();

        const auto layers = fs.GetMountLayers("config"_intern);
        ASSERT_EQ(layers.size(), 1u);
        EXPECT_EQ(layers[0], (_game / "config").lexically_normal());
    }

    TEST_F(FileSystemTest, EnumerateFilesMergesLayersAndListsOverriddenFileOnce) {
        WriteFile(_engine / "assets" / "a.png");
        WriteFile(_engine / "assets" / "b.png");
        WriteFile(_engine / "assets" / "ignored.txt");
        WriteFile(_game / "assets" / "b.png");
        WriteFile(_game / "assets" / "sub" / "c.PNG");

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

    TEST_F(FileSystemTest, WithoutContentRootsTheProjectRootIsFoundFromTheWorkingDirectory) {
        FileSystem fs;
        fs.Initialize();

        // The test runs inside the repository, which has a config/ folder
        EXPECT_FALSE(fs.GetRootPath().empty());
        EXPECT_FALSE(fs.GetMountLayers("config"_intern).empty());
    }

}  // namespace BECore::Tests
