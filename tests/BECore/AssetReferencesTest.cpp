#include <BECore/File/File.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/Format/Format.h>
#include <Common/EngineEnvironment.h>
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

        [[nodiscard]] ContentAssetCheck Check() {
            FileSystem fileSystem;
            fileSystem.AddContentRoot(_dir / "engine");
            fileSystem.AddContentRoot(_dir / "game");
            fileSystem.Initialize();
            return CheckContentAssets(fileSystem);
        }

        /// The missing names as "file:line name"
        [[nodiscard]] Vector<String> Missing() {
            Vector<String> missing;
            for (const MissingAsset& asset : Check().missing) {
                missing.push_back(Format("{}:{} {}", asset.use.file, asset.use.line, asset.name));
            }
            return missing;
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

    TEST_F(AssetReferencesTest, APathThatNoLayerHasIsMissingAtItsLine) {
        Game("config/A.xml", "<a x=\"assets/gone.png\"\n"
                             "   y='Assets\\Gone Too.ttf'>\n"
                             "  <font> assets/fonts/a b.otf </font>\n"
                             "  <b tooltip=\"Plays assets/gone.wav. Then config/gone.xml!\"/>\n"
                             "</a>\n");
        Game("assets/ui/a.css", "a { background: url(assets/gone.jpg?v=2); }\n");
        Game("assets/ui/b.html", "<img src=\"assets/gone.bmp#top\">\n");
        Engine("assets/a.json", "{\n  \"x\": \"assets\\/gone.ogg\",\n  \"y\": \"assets/gone.ogg\" }\n");

        EXPECT_EQ(Missing(), (Vector<String>{"assets/a.json:2 assets/gone.ogg", "assets/a.json:3 assets/gone.ogg", "assets/ui/a.css:1 assets/gone.jpg", "assets/ui/b.html:1 assets/gone.bmp",
                                             "config/A.xml:1 assets/gone.png", "config/A.xml:2 Assets/Gone Too.ttf", "config/A.xml:3 assets/fonts/a b.otf", "config/A.xml:4 assets/gone.wav",
                                             "config/A.xml:4 config/gone.xml"}));
    }

    TEST_F(AssetReferencesTest, APathThatSomeLayerHasIsNotMissing) {
        Engine("assets/engine.png", "");
        Game("assets/Big Star.png", "");
        Game("assets/icon copy.png", "");
        Game("assets/c#.png", "");
        Game("config/nodes/Cooked.bin", "");
        WriteFile(_dir / "elsewhere" / "linked.png", "");
        CreateSymlink(_dir / "game" / "assets" / "linked", _dir / "elsewhere");
        Game("config/A.xml", "<a b=\"assets/engine.png\" c=\"ASSETS/Engine.PNG\" d=\"assets/./engine.png\"\n"
                             "   e=\"assets/Big Star.png\" f=\"assets/icon copy.png\" g=\"assets/c#.png\"\n"
                             "   h=\"config/nodes/Cooked.xml\" i=\"assets/linked/linked.png\"/>\n");

        EXPECT_EQ(Missing(), Vector<String>{});
    }

    TEST_F(AssetReferencesTest, OnlyAWholeListedPathInAMountedFileCanBeMissing) {
        Game("config/A.xml", "<!-- assets/comment.png -->\n"
                             "<a longer=\"old/assets/a.png\" rooted=\"/assets/a.png\" relative=\"../assets/a.png\" up=\"assets/../../a.png\"\n"
                             "   url=\"https://example.com/assets/a.png\" scheme=\"res:assets/a.png\" word=\"myassets/a.png\"\n"
                             "   folder=\"assets/icons\" slash=\"config/panels/\" ini=\"config/imgui.ini\" backup=\"assets/a.png.bak\"\n"
                             "   pattern=\"assets/*.png\" format=\"assets/%s.png\" template=\"assets/&lt;name&gt;.wav\"\n"
                             "   prose=\"assets/ holds what config/ names, as a.png\" record=\"assets/a.artgen.json\" link=\"icons/a.png\"/>\n");
        Game("assets/ui/a.css", "/* url(assets/comment.png) */\n");
        Game("assets/ui/b.rml", "<rml><style>\n  /* url(assets/comment.png) */\n</style></rml>\n");
        Game("artgen/presets/a.json", "{\"image\": \"assets/a.png\"}\n");

        EXPECT_EQ(Missing(), Vector<String>{});
    }

    TEST_F(AssetReferencesTest, ANodeReferenceOfASceneOrANodeFileNeedsItsFile) {
        Engine("config/nodes/Engine.xml", "<node/>\n");
        Game("config/nodes/Cooked.bin", "");
        Game("config/SceneConfig.xml", "\xEF\xBB\xBF<SceneManager><scenes><scene><nodes>\n"
                                       "  <node ref=\"nodes/Engine\"/>\n"
                                       "  <node ref=\"nodes/Cooked\"/>\n"
                                       "  <!-- <node ref=\"nodes/Commented\"/> -->\n"
                                       "  <node ref=\"nodes/Gone\"/>\n"
                                       "  <node ref=\"../Bad\"/>\n"
                                       "</nodes></scene></scenes></SceneManager>\n");
        Game("config/nodes/Broken.xml", "<node>\n  <children><node ref=\"nodes/Inner\"/></children>\n</node>\n");
        // In any other config a `ref` is that format's own
        Game("config/Other.xml", "<a><node ref=\"nodes/NoFile\"/></a>\n");

        EXPECT_EQ(Missing(), (Vector<String>{"config/SceneConfig.xml:5 nodes/Gone", "config/SceneConfig.xml:6 ../Bad", "config/nodes/Broken.xml:2 nodes/Inner"}));
    }

    TEST_F(AssetReferencesTest, AFileOfTheContentRootThatNothingNamesIsUnused) {
        Game("assets/by_xml.png", "");
        Game("assets/ui/by_css.png", "");
        Game("assets/by_artgen.png", "");
        Game("config/nodes/ByRef.xml", "<node/>\n");
        Game("config/nodes/Lost.xml", "<node name=\"nodes/Lost\"/>\n");
        Game("assets/lost.png", "");
        Game("assets/ui/lost.css", "a { background: url(by_css.png); }\n");
        Game("config/SceneConfig.xml", "<nodes><node ref=\"nodes/ByRef\"/></nodes>\n");
        Game("config/Settings.xml", "<a icon=\"assets/by_xml.png\"/>\n");
        Game("artgen/presets/a.json", "{\"image\": \"assets/by_artgen.png\"}\n");
        Game("assets/readme.txt", "");

        EXPECT_EQ(Check().unused, (Vector<String>{"assets/lost.png", "assets/ui/lost.css", "config/nodes/Lost.xml"}));
    }

    TEST_F(AssetReferencesTest, AFileOfALowerLayerIsNeverUnused) {
        Engine("assets/engine.png", "");
        Engine("assets/replaced.png", "");
        Game("assets/replaced.png", "");

        EXPECT_EQ(Check().unused, Vector<String>{"assets/replaced.png"});
    }

    TEST_F(AssetReferencesTest, TheCheckSaysWhatItCouldNotRead) {
        Game("config/Wide.xml", StringView("\xff\xfe<\0a\0/\0>\0", 10));
        Game("config/SceneConfig.xml", "<SceneManager><scenes>\n");

        EXPECT_EQ(Check().unchecked, (Vector<String>{"config/SceneConfig.xml", "config/Wide.xml"}));
    }

    TEST(AssetReferencesShippedTest, TheContentOfTheEngineAndOfItsStressGameHasNoMissingFile) {
        const Path engine = StringView(BagiContentRoots::kEngine);
        for (const bool withGame : {false, true}) {
            FileSystem fileSystem;
            fileSystem.AddContentRoot(engine);
            if (withGame) {
                fileSystem.AddContentRoot(engine / "tools" / "stress");
            }
            fileSystem.Initialize();
            const ContentAssetCheck check = CheckContentAssets(fileSystem);
            for (const MissingAsset& asset : check.missing) {
                ADD_FAILURE() << asset.use.file.c_str() << ":" << asset.use.line << " names " << asset.name.c_str();
            }
            EXPECT_EQ(check.unchecked, Vector<String>{});
        }
    }

}  // namespace BECore::Tests
