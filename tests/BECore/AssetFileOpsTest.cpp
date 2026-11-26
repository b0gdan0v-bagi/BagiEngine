#include <BECore/FileSystem/FileSystem.h>
#include <BECore/Format/Format.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/AssetBrowser/AssetFileOps.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// The file operations over temporary roots laid out as a game is: the engine's checkout lies inside the game's folder, which is on top of it
    class AssetFileOpsTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _dir = TestTempDirectory("assetfileops");
            MakeEmptyDirectory(_dir);
            MakeDirectory(_dir / "game" / "assets");
            MakeDirectory(_dir / "game" / "config");
            MakeDirectory(_dir / "game" / "engine" / "assets");
            MakeDirectory(_dir / "game" / "engine" / "config");
        }

        void TearDown() override {
            RemoveDirectory(_dir);
        }

        [[nodiscard]] Path InGame(StringView file) const {
            return _dir / "game" / file;
        }

        [[nodiscard]] Path InEngine(StringView file) const {
            return _dir / "game" / "engine" / file;
        }

        void Game(StringView file, StringView text = "x") {
            WriteFile(InGame(file), text);
        }

        void Engine(StringView file, StringView text = "x") {
            WriteFile(InEngine(file), text);
        }

        /// Made after the files are laid out: a folder that appears later is in no layer
        const FileSystem& Files() {
            if (!_made) {
                _fileSystem.AddContentRoot(_dir / "game" / "engine");
                _fileSystem.AddContentRoot(_dir / "game");
                _fileSystem.Initialize();
                _made = true;
            }
            return _fileSystem;
        }

        /// Neither a delete nor a rename touches @p path, and the file @p onDisk keeps its text
        void ExpectRefused(StringView path, const Path& onDisk, StringView text = "x") {
            const auto deleted = DeleteAsset(Files(), path);
            EXPECT_FALSE(deleted.has_value()) << String(path).c_str() << " was deleted";
            const auto renamed = RenameAsset(Files(), path, "renamed");
            EXPECT_FALSE(renamed.has_value()) << String(path).c_str() << " was renamed";
            ASSERT_TRUE(IsFile(onDisk)) << onDisk.CStr();
            EXPECT_EQ(ReadFile(onDisk), String(text));
        }

        void ExpectRenameRefused(StringView path, StringView newStem) {
            const auto renamed = RenameAsset(Files(), path, newStem);
            EXPECT_FALSE(renamed.has_value()) << String(path).c_str() << " was renamed to " << String(newStem).c_str();
            EXPECT_TRUE(IsFile(InGame(path))) << String(path).c_str();
        }

        Path _dir;
        FileSystem _fileSystem;
        bool _made = false;
    };

    TEST_F(AssetFileOpsTest, ADeleteRemovesTheFileOfTheGame) {
        Game("assets/icons/star.png");
        Game("assets/icons/moon.png");

        const auto deleted = DeleteAsset(Files(), "assets/icons/star.png");

        ASSERT_TRUE(deleted.has_value()) << deleted.error().c_str();
        EXPECT_TRUE(deleted->newPath.empty());
        EXPECT_FALSE(deleted->lowerLayerShows);
        EXPECT_FALSE(PathExists(InGame("assets/icons/star.png")));
        EXPECT_TRUE(IsFile(InGame("assets/icons/moon.png")));
    }

    TEST_F(AssetFileOpsTest, ARenameKeepsTheFolderAndTheExtension) {
        Game("assets/ui/menu.ui.xml", "menu");
        Game("assets/ui/data.v2.json", "data");
        Game("config/nodes/Hud.xml", "<node name=\"Hud\"/>");

        const auto document = RenameAsset(Files(), "assets/ui/menu.ui.xml", "main.menu");
        const auto data = RenameAsset(Files(), "assets/ui/data.v2.json", "data");
        const auto node = RenameAsset(Files(), "config/nodes/Hud.xml", "Score_2");

        ASSERT_TRUE(document.has_value()) << document.error().c_str();
        ASSERT_TRUE(data.has_value()) << data.error().c_str();
        ASSERT_TRUE(node.has_value()) << node.error().c_str();
        EXPECT_EQ(document->newPath, "assets/ui/main.menu.ui.xml");
        EXPECT_EQ(data->newPath, "assets/ui/data.json");
        EXPECT_EQ(node->newPath, "config/nodes/Score_2.xml");
        EXPECT_EQ(ReadFile(InGame("assets/ui/main.menu.ui.xml")), "menu");
        EXPECT_EQ(ReadFile(InGame("assets/ui/data.json")), "data");
        EXPECT_TRUE(IsFile(InGame("config/nodes/Score_2.xml")));
        EXPECT_FALSE(PathExists(InGame("assets/ui/menu.ui.xml")));
    }

    TEST_F(AssetFileOpsTest, AnEnginesFileSeenFromTheGameIsRefused) {
        // The engine's checkout is below the game's root, and its file is still not the game's
        Engine("assets/engine.png");

        ExpectRefused("assets/engine.png", InEngine("assets/engine.png"));
    }

    TEST_F(AssetFileOpsTest, ConfigsScenesAndUnlistedFilesAreRefused) {
        Game("config/TextConfig.xml", "<TextConfig/>");
        Game("config/SceneConfig.xml", "<SceneManager/>");
        Game("config/panels/APanel.xml", "<panel/>");
        Game("config/broken.xml", "<oops");
        Game("config/imgui.ini");
        Game("assets/a.artgen.json");

        ExpectRefused("config/TextConfig.xml", InGame("config/TextConfig.xml"), "<TextConfig/>");
        ExpectRefused("config/SceneConfig.xml", InGame("config/SceneConfig.xml"), "<SceneManager/>");
        ExpectRefused("config/panels/APanel.xml", InGame("config/panels/APanel.xml"), "<panel/>");
        ExpectRefused("config/broken.xml", InGame("config/broken.xml"), "<oops");
        ExpectRefused("config/imgui.ini", InGame("config/imgui.ini"));
        ExpectRefused("assets/a.artgen.json", InGame("assets/a.artgen.json"));
    }

    TEST_F(AssetFileOpsTest, APathThatIsNoContentFileIsRefused) {
        Game("assets/a.png");
        Game("loose.png");
        WriteFile(_dir / "outside.png", "x");

        ExpectRefused("loose.png", InGame("loose.png"));
        ExpectRefused("assets/../loose.png", InGame("loose.png"));
        ExpectRefused("assets/../../outside.png", _dir / "outside.png");
        ExpectRefused(InGame("assets/a.png").View(), InGame("assets/a.png"));
        EXPECT_FALSE(DeleteAsset(Files(), "assets/missing.png").has_value());
        EXPECT_FALSE(DeleteAsset(Files(), "assets").has_value());
    }

    TEST_F(AssetFileOpsTest, WithAContentOverlayNothingChanges) {
        Game("assets/a.png");
        const auto overlay = _fileSystem.SetContentOverlay(_dir / "overlay");
        ASSERT_TRUE(overlay.has_value()) << overlay.error().c_str();

        ExpectRefused("assets/a.png", InGame("assets/a.png"));
    }

    TEST_F(AssetFileOpsTest, ASymbolicLinkIsRefusedAndSoIsAFileBehindOne) {
        Game("assets/real.png");
        WriteFile(_dir / "outside" / "far.png", "x");
        CreateSymlink(InGame("assets/link.png"), InGame("assets/real.png"));
        CreateSymlink(InGame("assets/shared"), _dir / "outside");

        ExpectRefused("assets/link.png", InGame("assets/real.png"));
        ExpectRefused("assets/shared/far.png", _dir / "outside" / "far.png");
        EXPECT_TRUE(PathExists(InGame("assets/link.png")));
    }

    TEST_F(AssetFileOpsTest, ANameThatIsNotAllowedIsRefused) {
        Game("assets/a.png");
        Game("assets/data.xml");
        Game("assets/s.json");
        Game("config/nodes/Hud.xml", "<node name=\"Hud\"/>");
        Game("config/my nodes/Hud.xml", "<node name=\"Hud\"/>");

        for (const char* stem : {"", "a b", "../b", "sub/b", "sub\\b", ".b", "b.", "b..c", "b:c", "\xd0\xb1"}) {
            ExpectRenameRefused("assets/a.png", stem);
        }
        // A node's name is a part of its reference, which has no dot
        ExpectRenameRefused("config/nodes/Hud.xml", "Hud.big");
        ExpectRenameRefused("config/my nodes/Hud.xml", "Score");
        // The new name would make another kind of file of it
        ExpectRenameRefused("assets/data.xml", "data.ui");
        ExpectRenameRefused("assets/s.json", "s.artgen");
    }

    TEST_F(AssetFileOpsTest, ARenameOntoAnExistingNameOfAnyLayerOrCaseIsRefused) {
        Game("assets/a.png", "a");
        Game("assets/b.png", "b");
        Game("assets/Capital.png", "c");
        Engine("assets/engine.png", "e");

        ExpectRenameRefused("assets/a.png", "b");
        ExpectRenameRefused("assets/a.png", "capital");
        ExpectRenameRefused("assets/a.png", "engine");
        ExpectRenameRefused("assets/a.png", "a");
        ExpectRenameRefused("assets/a.png", "A");
        EXPECT_EQ(ReadFile(InGame("assets/a.png")), "a");
        EXPECT_EQ(ReadFile(InGame("assets/b.png")), "b");
        EXPECT_EQ(ReadFile(InGame("assets/Capital.png")), "c");
        EXPECT_FALSE(PathExists(InGame("assets/engine.png")));
    }

    TEST_F(AssetFileOpsTest, ANodeIsNotRenamedOntoACookedNode) {
        Game("config/nodes/Hud.xml", "<node name=\"Hud\"/>");
        Engine("config/nodes/Score.bin");

        ExpectRenameRefused("config/nodes/Hud.xml", "Score");
    }

    TEST_F(AssetFileOpsTest, TheCallerLearnsThatALowerLayerShowsThrough) {
        Game("assets/a.png", "game");
        Engine("assets/a.png", "engine");
        Game("assets/b.png", "game");
        Engine("assets/b.png", "engine");
        Game("config/nodes/Hud.xml", "<node name=\"Hud\"/>");
        Engine("config/nodes/Hud.bin");

        const auto deleted = DeleteAsset(Files(), "assets/a.png");
        const auto renamed = RenameAsset(Files(), "assets/b.png", "c");
        const auto node = CheckAssetChange(Files(), "config/nodes/Hud.xml");

        ASSERT_TRUE(deleted.has_value()) << deleted.error().c_str();
        ASSERT_TRUE(renamed.has_value()) << renamed.error().c_str();
        EXPECT_TRUE(deleted->lowerLayerShows);
        EXPECT_TRUE(renamed->lowerLayerShows);
        EXPECT_TRUE(Require(node).lowerLayerShows);
        EXPECT_EQ(ReadFile(Files().ResolvePath("assets/a.png")), "engine");
        EXPECT_EQ(ReadFile(Files().ResolvePath("assets/b.png")), "engine");
    }

}  // namespace BECore::Tests
