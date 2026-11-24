#include <Common/ContentFilesCheck.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>

namespace BECore::Tests {

    namespace {

        Path EngineRoot() {
            return Path(__FILE__).Parent().Parent().Parent();
        }

    }  // namespace

    /// The content files the engine ships are at the version its loaders read; an engine change that needs a step bumps
    /// kContentVersion and runs tools/migrate/migrate.py over them (tools/migrate/CLAUDE.md).
    TEST(ContentFilesVersionTest, EngineConfigIsCurrent) {
        ExpectContentFilesAtCurrentVersion(EngineRoot() / "config");
    }

    TEST(ContentFilesVersionTest, StressConfigIsCurrent) {
        ExpectContentFilesAtCurrentVersion(EngineRoot() / "tools" / "stress" / "config");
    }

    /// The helper a game's own test calls, on a folder with one file of every kind
    TEST(ContentFilesVersionTest, FindsOnlyStaleContentFiles) {
        const Path dir = TestTempDirectory("BagiContentFilesCheck");
        RemoveDirectory(dir);
        WriteFile(dir / "SceneConfig.xml", Format("<SceneManager contentVersion=\"{}\"/>", kContentVersion).c_str());
        WriteFile(dir / "nodes" / "Old.xml", "<node name=\"Old\" contentVersion=\"0\"/>");
        WriteFile(dir / "nodes" / "New.xml", Format("<node name=\"New\" contentVersion=\"{}\"/>", kContentVersion + 1).c_str());
        WriteFile(dir / "nodes" / "Bad.xml", "<node name=\"Bad\" contentVersion=\"x\"/>");
        WriteFile(dir / "nodes" / "Broken.xml", "<node name=\"Broken\"");
        WriteFile(dir / "UIConfig.xml", "<UIConfig contentVersion=\"0\"/>");
        WriteFile(dir / "notes.txt", "<node/>");

        const Vector<String> stale = FindStaleContentFiles(dir);
        RemoveDirectory(dir);

        ASSERT_EQ(stale.size(), 3u);
        const String base = Format("{}/nodes/", dir.View());
        EXPECT_TRUE(StringView(stale[0]).starts_with(Format("{}Bad.xml: ", base))) << stale[0];
        EXPECT_NE(stale[0].find("is not a whole number"), String::npos) << stale[0];
        EXPECT_TRUE(StringView(stale[1]).starts_with(Format("{}New.xml: ", base))) << stale[1];
        EXPECT_NE(stale[1].find("newer than this engine reads"), String::npos) << stale[1];
        EXPECT_TRUE(StringView(stale[2]).starts_with(Format("{}Old.xml: ", base))) << stale[2];
        EXPECT_NE(stale[2].find("content version 0"), String::npos) << stale[2];
        EXPECT_NE(stale[2].find("python3 engine/tools/migrate/migrate.py"), String::npos) << stale[2];
    }

}  // namespace BECore::Tests
