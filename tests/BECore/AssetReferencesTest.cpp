#include <BECore/File/File.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/Format/Format.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/AssetBrowser/AssetReferences.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// The finder over two content roots on disk, the second on top of the first (an engine and a game)
    class AssetReferencesTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _dir = TestTempDirectory("assetreferences");
            MakeEmptyDirectory(_dir);
            MakeDirectory(_dir / "engine" / "config");
            MakeDirectory(_dir / "game" / "config");
            MakeDirectory(_dir / "game" / "assets");
        }

        void TearDown() override {
            RemoveDirectory(_dir);
        }

        void Engine(StringView file, StringView text) {
            WriteFile(_dir / "engine" / file, text);
        }

        void Game(StringView file, StringView text) {
            WriteFile(_dir / "game" / file, text);
        }

        [[nodiscard]] AssetReferences Find(StringView asset) {
            FileSystem fileSystem;
            fileSystem.AddContentRoot(_dir / "engine");
            fileSystem.AddContentRoot(_dir / "game");
            fileSystem.Initialize();
            return FindAssetReferences(fileSystem, asset);
        }

        /// The uses of @p asset as "file:line"
        [[nodiscard]] Vector<String> Uses(StringView asset) {
            Vector<String> uses;
            for (const AssetUse& use : Find(asset).uses) {
                uses.push_back(Format("{}:{}", use.file, use.line));
            }
            return uses;
        }

        Path _dir;
    };

    TEST_F(AssetReferencesTest, AnAttributeAndAnElementTextOfXmlNameAFile) {
        Game("config/TextConfig.xml", "<TextConfig\n"
                                      "  defaultFont=\"assets/fonts/a.ttf\">\n"
                                      "  <face path='assets/fonts/a.ttf' other=\"assets/fonts/a.ttf\"/>\n"
                                      "  <font>assets/fonts/a.ttf</font>\n"
                                      "  <data><![CDATA[assets/fonts/a.ttf]]></data>\n"
                                      "</TextConfig>\n");
        Game("assets/hud.ui.xml", "<ui>\n  <element sprite=\"assets/fonts/a.ttf\"/>\n</ui>\n");

        EXPECT_EQ(Uses("assets/fonts/a.ttf"), (Vector<String>{"assets/hud.ui.xml:2", "config/TextConfig.xml:2", "config/TextConfig.xml:3", "config/TextConfig.xml:4", "config/TextConfig.xml:5"}));
    }

    TEST_F(AssetReferencesTest, ALongerPathIsNotAUse) {
        Game("config/A.xml", "<a x=\"assets/icon.png.bak\"\n"
                             "   y=\"assets/icon.pngx\"\n"
                             "   z=\"game/assets/icon.png\"\n"
                             "   w=\"myassets/icon.png\"\n"
                             "   v=\"assets/icon.png/more\"\n"
                             "   u=\"assets/big-icon.png\"\n"
                             "   t=\"icon.png\"/>\n");

        EXPECT_EQ(Uses("assets/icon.png"), Vector<String>{});
    }

    TEST_F(AssetReferencesTest, ASentenceThatEndsInThePathIsAUse) {
        Game("config/A.xml", "<a tooltip=\"Plays assets/sounds/move.wav. Then stops\"/>\n");

        EXPECT_EQ(Uses("assets/sounds/move.wav"), (Vector<String>{"config/A.xml:1"}));
    }

    TEST_F(AssetReferencesTest, ACommentIsNoUse) {
        Game("config/A.xml", "<!-- assets/icon.png -->\n"
                             "<a>\n"
                             "  <!--\n"
                             "    <b x=\"assets/icon.png\"/>\n"
                             "  -->\n"
                             "  <b x=\"<!--\" y=\"assets/icon.png\" z=\"-->\"/>\n"
                             "</a>\n");
        Game("assets/a.css", "/* url(assets/icon.png) */\n"
                             "a { content: \"/*\"; background: url(assets/icon.png); }\n"
                             "/* */\n");

        EXPECT_EQ(Uses("assets/icon.png"), (Vector<String>{"assets/a.css:2", "config/A.xml:6"}));
    }

    TEST_F(AssetReferencesTest, ACharacterReferenceOfXmlAndAnEscapeOfJsonAreRead) {
        Game("config/A.xml", "<a x=\"assets/Tom &amp; Jerry&#x2e;png\"/>\n");
        Game("assets/a.json", "{\n  \"x\": \"assets\\/Tom & Jerry\\u002Epng\"\n}\n");

        EXPECT_EQ(Uses("assets/Tom & Jerry.png"), (Vector<String>{"assets/a.json:2", "config/A.xml:1"}));
    }

    TEST_F(AssetReferencesTest, AnotherCaseOrSlashNamesTheSameFile) {
        Game("config/A.xml", "<a x=\"Assets\\Icons\\Star.PNG\"/>\n");

        EXPECT_EQ(Uses("assets/icons/star.png"), (Vector<String>{"config/A.xml:1"}));
    }

    TEST_F(AssetReferencesTest, ALinkOfAStyleSheetStartsAtItsFolder) {
        Game("assets/ui/skin/a.rcss", "a { decorator: image(star.png); }\n"
                                      "b { background: url('../skin/star.png'); }\n"
                                      "@spritesheet s { src: ./star.png; }\n"
                                      "c { background: url(star.png.bak); }\n"
                                      "d { background: url(other/star.png); }\n");
        Game("assets/ui/b.css", "a { background: url(\"skin/star.png\"); }\n");

        EXPECT_EQ(Uses("assets/ui/skin/star.png"), (Vector<String>{"assets/ui/b.css:1", "assets/ui/skin/a.rcss:1", "assets/ui/skin/a.rcss:2", "assets/ui/skin/a.rcss:3"}));
    }

    TEST_F(AssetReferencesTest, ALinkOfADocumentStartsAtItsFolderOrAtTheVirtualRoot) {
        Game("assets/ui/menu.rml", "<rml><head>\n"
                                   "  <link type=\"text/rcss\" href=\"menu.rcss\"/>\n"
                                   "</head><body>\n"
                                   "  <img src=\"/assets/ui/menu.rcss\"/>\n"
                                   "</body></rml>\n");
        Game("assets/page.ui.html", "<html><link rel=\"stylesheet\" href=\"./ui/menu.rcss?v=2\">\n"
                                    "<p>it's</p><a href='ui/menu.rcss#top'></a>\n"
                                    "</html>\n");
        Game("assets/ui/x/deep.html", "<link href=\"menu.rcss\">\n");

        EXPECT_EQ(Uses("assets/ui/menu.rcss"), (Vector<String>{"assets/page.ui.html:1", "assets/page.ui.html:2", "assets/ui/menu.rml:2", "assets/ui/menu.rml:4"}));
    }

    TEST_F(AssetReferencesTest, ALinkWithASpaceIsReadBetweenItsQuotes) {
        Game("assets/ui/a.html", "<p>don't</p><img src=\"big star.png\">\n");

        EXPECT_EQ(Uses("assets/ui/big star.png"), (Vector<String>{"assets/ui/a.html:1"}));
    }

    TEST_F(AssetReferencesTest, AnXmlFileHasNoLinksFromItsFolder) {
        Game("config/panels/a.xml", "<a x=\"star.png\"/>\n");

        EXPECT_EQ(Uses("config/panels/star.png"), Vector<String>{});
    }

    TEST_F(AssetReferencesTest, ASceneNamesANodeFileByItsReference) {
        Game("config/SceneConfig.xml", "<SceneManager><scenes><scene><nodes>\n"
                                       "  <node ref=\"nodes/Hud\"/>\n"
                                       "  <node ref =\n 'nodes/Hud' name=\"Second\"/>\n"
                                       "  <node ref=\"nodes/HudLarge\"/>\n"
                                       "  <node ref=\"nodes/Hud/Inner\"/>\n"
                                       "  <node ref=\"old/nodes/Hud\"/>\n"
                                       "</nodes></scene></scenes></SceneManager>\n");
        Game("config/Spawner.xml", "<a prefab=\"nodes/Hud\"/>\n");
        // The node file is no user of itself
        Game("config/nodes/Hud.xml", "<node name=\"nodes/Hud\"/>\n");

        EXPECT_EQ(Uses("config/nodes/Hud.xml"), (Vector<String>{"config/SceneConfig.xml:2", "config/SceneConfig.xml:4", "config/Spawner.xml:1"}));
    }

    TEST_F(AssetReferencesTest, ANodeFileWithNoFolderIsNamedOnlyByARef) {
        Game("config/SceneConfig.xml", "<nodes>\n"
                                       "  <node name=\"Hud\" href=\"Hud\"/>\n"
                                       "  <node ref=\"Hud\"/>\n"
                                       "</nodes>\n");
        Game("config/Hud.xml", "<node name=\"Hud\"/>\n");

        EXPECT_EQ(Uses("config/Hud.xml"), (Vector<String>{"config/SceneConfig.xml:3"}));
    }

    TEST_F(AssetReferencesTest, OnlyTheTopLayerOfAFileIsRead) {
        Engine("config/A.xml", "<a x=\"assets/icon.png\"/>\n");
        Engine("config/B.xml", "<b x=\"assets/icon.png\"/>\n");
        Game("config/A.xml", "<a/>\n");

        EXPECT_EQ(Uses("assets/icon.png"), (Vector<String>{"config/B.xml:1"}));
    }

    TEST_F(AssetReferencesTest, ThePresetsAndSetsOfTheGameNamePictures) {
        Game("artgen/presets/rook.json", "{\"reference\": {\"mode\": \"init\", \"image\": \"assets/artgen/rook.png\"}}\n");
        Game("artgen/out/set/manifest.json", "{\n\"reference\": {\"image\": \"./assets/artgen/rook.png\"}\n}\n");
        Game("artgen/out/set/rook/rook_1.artgen.json", "{\"image\": \"assets/artgen/rook.png\"}\n");
        Game("assets/artgen/rook.artgen.json", "{\"clip\": \"assets/artgen/rook.png\"}\n");
        Engine("artgen/presets/engine.json", "{\"image\": \"assets/artgen/rook.png\"}\n");

        EXPECT_EQ(Uses("assets/artgen/rook.png"), (Vector<String>{"artgen/out/set/manifest.json:2", "artgen/presets/rook.json:1"}));
    }

    TEST_F(AssetReferencesTest, AFileThatCannotBeReadIsReportedAndNotTakenAsUnused) {
        Game("config/Huge.xml", "<a/>");
        ResizeFile(_dir / "game" / "config/Huge.xml", kDefaultMaxFileBytes + 1);
        // UTF-16, which the engine reads and the search cannot
        Game("config/Wide.xml", StringView("\xff\xfe<\0a\0/\0>\0", 10));
        Game("config/Fine.xml", "<a x=\"assets/icon.png\"/>");

        const AssetReferences references = Find("assets/icon.png");

        EXPECT_EQ(references.unchecked, (Vector<String>{"config/Huge.xml", "config/Wide.xml"}));
        ASSERT_EQ(references.uses.size(), 1u);
        EXPECT_EQ(references.uses.front().file, "config/Fine.xml");
    }

    TEST_F(AssetReferencesTest, ALinkedFolderIsReportedSinceNoListingEntersIt) {
        WriteFile(_dir / "elsewhere" / "a.xml", "<a x=\"assets/icon.png\"/>");
        CreateSymlink(_dir / "game" / "config" / "linked", _dir / "elsewhere");

        const AssetReferences references = Find("assets/icon.png");

        EXPECT_EQ(references.unchecked, (Vector<String>{"config/linked/"}));
    }

}  // namespace BECore::Tests
