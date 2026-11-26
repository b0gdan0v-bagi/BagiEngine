#include <BECore/FileSystem/FileSystem.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/AssetBrowser/AssetCatalog.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// A catalog over two content roots on disk, the second on top of the first (an engine and a game)
    class AssetCatalogTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _dir = TestTempDirectory("assetcatalog");
            MakeEmptyDirectory(_dir);
        }

        void TearDown() override {
            RemoveDirectory(_dir);
        }

        void Engine(StringView file, StringView text = "x") {
            WriteFile(_dir / "engine" / file, text);
        }

        void Game(StringView file, StringView text = "x") {
            WriteFile(_dir / "game" / file, text);
        }

        void Rebuild() {
            _fileSystem.AddContentRoot(_dir / "engine");
            _fileSystem.AddContentRoot(_dir / "game");
            _fileSystem.Initialize();
            _catalog.Rebuild(_fileSystem);
        }

        [[nodiscard]] AssetKind KindOf(StringView path) const {
            const AssetFile* file = _catalog.Find(path);
            return file != nullptr ? file->kind : AssetKind::None;
        }

        [[nodiscard]] Vector<String> Shown(StringView folder, StringView search = {}, AssetTypeFilter filter = AssetTypeFilter::All) const {
            Vector<uint32_t> indices;
            _catalog.CollectShown(folder, search, filter, indices);
            Vector<String> paths;
            for (const uint32_t index : indices) {
                paths.push_back(String(At(_catalog.GetFiles(), index).path.ToStringView()));
            }
            return paths;
        }

        Path _dir;
        FileSystem _fileSystem;
        AssetCatalog _catalog;
    };

    TEST_F(AssetCatalogTest, TheNameOfAFileSaysItsKind) {
        for (const char* name : {"assets/a.png", "assets/b.JPG", "assets/c.jpeg", "assets/d.bmp", "assets/e.ttf", "assets/f.otf", "assets/g.wav", "assets/h.ogg", "assets/i.flac", "assets/hud.ui.xml",
                                 "assets/page.ui.html", "assets/data.xml", "assets/page.css", "assets/page.rcss", "assets/page.rml", "assets/sheet.json"}) {
            Engine(name);
        }
        Engine("config/menu.ui.xml");

        Rebuild();

        EXPECT_EQ(KindOf("assets/a.png"), AssetKind::Texture);
        EXPECT_EQ(KindOf("assets/b.JPG"), AssetKind::Texture);
        EXPECT_EQ(KindOf("assets/c.jpeg"), AssetKind::Texture);
        EXPECT_EQ(KindOf("assets/d.bmp"), AssetKind::Texture);
        EXPECT_EQ(KindOf("assets/e.ttf"), AssetKind::Font);
        EXPECT_EQ(KindOf("assets/f.otf"), AssetKind::Font);
        EXPECT_EQ(KindOf("assets/g.wav"), AssetKind::Sound);
        EXPECT_EQ(KindOf("assets/h.ogg"), AssetKind::Sound);
        EXPECT_EQ(KindOf("assets/i.flac"), AssetKind::Sound);
        EXPECT_EQ(KindOf("assets/hud.ui.xml"), AssetKind::UiDocument);
        EXPECT_EQ(KindOf("config/menu.ui.xml"), AssetKind::UiDocument);
        EXPECT_EQ(KindOf("assets/page.ui.html"), AssetKind::UiDocument);
        EXPECT_EQ(KindOf("assets/data.xml"), AssetKind::Other);
        EXPECT_EQ(KindOf("assets/page.css"), AssetKind::Other);
        EXPECT_EQ(KindOf("assets/page.rcss"), AssetKind::Other);
        EXPECT_EQ(KindOf("assets/page.rml"), AssetKind::Other);
        EXPECT_EQ(KindOf("assets/sheet.json"), AssetKind::Other);
    }

    TEST_F(AssetCatalogTest, AnXmlUnderConfigIsToldByItsContent) {
        Engine("config/SceneConfig.xml", "<SceneManager/>");
        Engine("config/nodes/Hud.xml", "<?xml version=\"1.0\"?>\n<!-- a comment -->\n<node name=\"Hud\"/>");
        Engine("config/LoggerConfig.xml", "<LoggerManager><node/></LoggerManager>");
        Engine("config/broken.xml", "<node name=");

        Rebuild();

        EXPECT_EQ(KindOf("config/SceneConfig.xml"), AssetKind::Scene);
        EXPECT_EQ(KindOf("config/nodes/Hud.xml"), AssetKind::Node);
        EXPECT_EQ(KindOf("config/LoggerConfig.xml"), AssetKind::Config);
        EXPECT_EQ(KindOf("config/broken.xml"), AssetKind::Config);
    }

    TEST_F(AssetCatalogTest, OtherFilesAreNotListedAndMakeNoFolder) {
        Engine("config/imgui.ini");
        Engine("config/nodes/Hud.bin");
        Engine("assets/fonts/OFL.txt");
        Engine("assets/art/rook.png.artgen.json");
        Engine("assets/art/rook.png.tmp");

        Rebuild();

        EXPECT_TRUE(_catalog.GetFiles().empty());
        EXPECT_TRUE(_catalog.GetFolders().empty());
    }

    TEST_F(AssetCatalogTest, AFileOfTwoLayersIsListedOnceWithTheKindOfTheTopOne) {
        Engine("config/shared.xml", "<Settings/>");
        Game("config/shared.xml", "<node name=\"Shared\"/>");
        Engine("assets/only_engine.png");
        Game("assets/only_game.png");

        Rebuild();

        EXPECT_EQ(Shown("config"), Vector<String>{"config/shared.xml"});
        EXPECT_EQ(KindOf("config/shared.xml"), AssetKind::Node);
        EXPECT_EQ(Shown("assets"), (Vector<String>{"assets/only_engine.png", "assets/only_game.png"}));
    }

    TEST_F(AssetCatalogTest, TheTreeListsAParentBeforeItsChildren) {
        Engine("assets/ui/icons/star.png");
        Engine("assets/ui-old/a.png");
        Engine("assets/z.png");
        Game("assets/sounds/click.wav");
        Game("config/LoggerConfig.xml", "<LoggerManager/>");

        Rebuild();

        Vector<String> folders;
        Vector<uint32_t> depths;
        for (const AssetFolder& folder : _catalog.GetFolders()) {
            folders.push_back(String(folder.path.ToStringView()));
            depths.push_back(folder.depth);
        }
        EXPECT_EQ(folders, (Vector<String>{"assets", "assets/sounds", "assets/ui", "assets/ui/icons", "assets/ui-old", "config"}));
        EXPECT_EQ(depths, (Vector<uint32_t>{0, 1, 1, 2, 1, 0}));
        EXPECT_TRUE(_catalog.HasFolder("assets/ui"));
        EXPECT_FALSE(_catalog.HasFolder("assets/u"));
    }

    TEST_F(AssetCatalogTest, WithoutSearchOrFilterAFolderShowsOnlyItsOwnFiles) {
        Engine("assets/board.png");
        Engine("assets/pieces/rook.png");

        Rebuild();

        EXPECT_EQ(Shown("assets"), Vector<String>{"assets/board.png"});
        EXPECT_EQ(Shown("assets/pieces"), Vector<String>{"assets/pieces/rook.png"});
    }

    TEST_F(AssetCatalogTest, ASearchLooksBelowTheFolderAtTheFileNameIgnoringCase) {
        Engine("assets/Rook.png");
        Engine("assets/pieces/white_rook.png");
        Engine("assets/rooks/bishop.png");
        Engine("config/rook.xml", "<RookConfig/>");

        Rebuild();

        EXPECT_EQ(Shown("assets", "rOoK"), (Vector<String>{"assets/Rook.png", "assets/pieces/white_rook.png"}));
        EXPECT_EQ(Shown("assets/pieces", "rook"), Vector<String>{"assets/pieces/white_rook.png"});
    }

    TEST_F(AssetCatalogTest, AFilterLooksBelowTheFolderForOneKindAndCombinesWithTheSearch) {
        Engine("assets/board.png");
        Engine("assets/sounds/board.wav");
        Engine("assets/sounds/click.wav");

        Rebuild();

        EXPECT_EQ(Shown("assets", {}, AssetTypeFilter::Sound), (Vector<String>{"assets/sounds/board.wav", "assets/sounds/click.wav"}));
        EXPECT_EQ(Shown("assets", "board", AssetTypeFilter::Sound), Vector<String>{"assets/sounds/board.wav"});
        EXPECT_TRUE(Shown("assets", {}, AssetTypeFilter::Font).empty());
    }

    TEST_F(AssetCatalogTest, TheEmptyFolderHoldsNoFilesAndSearchesEveryMount) {
        Engine("assets/hud.png");
        Engine("config/hud.xml", "<HudConfig/>");

        Rebuild();

        EXPECT_TRUE(Shown("").empty());
        EXPECT_EQ(Shown("", "hud"), (Vector<String>{"assets/hud.png", "config/hud.xml"}));
        EXPECT_EQ(Shown("", {}, AssetTypeFilter::Config), Vector<String>{"config/hud.xml"});
    }

}  // namespace BECore::Tests
