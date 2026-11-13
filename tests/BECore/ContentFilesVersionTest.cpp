#include <Common/ContentFilesCheck.h>
#include <Common/TempPath.h>

namespace BECore::Tests {

    namespace {

        std::filesystem::path EngineRoot() {
            return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
        }

        void WriteFile(const std::filesystem::path& path, const std::string& text) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream(path) << text;
        }

    }  // namespace

    /// The content files the engine ships are at the version its loaders read; an engine change that needs a step bumps
    /// kContentVersion and runs tools/migrate/migrate.py over them (tools/migrate/CLAUDE.md).
    TEST(ContentFilesVersionTest, EngineConfigIsCurrent) {
        ExpectContentFilesAtCurrentVersion(EngineRoot() / "config");
    }

    TEST(ContentFilesVersionTest, Match3ConfigIsCurrent) {
        ExpectContentFilesAtCurrentVersion(EngineRoot() / "games" / "Match3" / "config");
    }

    TEST(ContentFilesVersionTest, StressConfigIsCurrent) {
        ExpectContentFilesAtCurrentVersion(EngineRoot() / "tools" / "stress" / "config");
    }

    /// The helper a game's own test calls, on a folder with one file of every kind
    TEST(ContentFilesVersionTest, FindsOnlyStaleContentFiles) {
        const std::filesystem::path dir = TestTempDirectory("BagiContentFilesCheck");
        std::filesystem::remove_all(dir);
        WriteFile(dir / "SceneConfig.xml", Format("<SceneManager contentVersion=\"{}\"/>", kContentVersion).c_str());
        WriteFile(dir / "nodes" / "Old.xml", "<node name=\"Old\" contentVersion=\"0\"/>");
        WriteFile(dir / "nodes" / "New.xml", Format("<node name=\"New\" contentVersion=\"{}\"/>", kContentVersion + 1).c_str());
        WriteFile(dir / "nodes" / "Bad.xml", "<node name=\"Bad\" contentVersion=\"x\"/>");
        WriteFile(dir / "nodes" / "Broken.xml", "<node name=\"Broken\"");
        WriteFile(dir / "UIConfig.xml", "<UIConfig contentVersion=\"0\"/>");
        WriteFile(dir / "notes.txt", "<node/>");

        const std::vector<std::string> stale = FindStaleContentFiles(dir);
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);

        ASSERT_EQ(stale.size(), 3u);
        const std::string base = dir.generic_string() + "/nodes/";
        EXPECT_TRUE(stale[0].starts_with(base + "Bad.xml: ")) << stale[0];
        EXPECT_NE(stale[0].find("is not a whole number"), std::string::npos) << stale[0];
        EXPECT_TRUE(stale[1].starts_with(base + "New.xml: ")) << stale[1];
        EXPECT_NE(stale[1].find("newer than this engine reads"), std::string::npos) << stale[1];
        EXPECT_TRUE(stale[2].starts_with(base + "Old.xml: ")) << stale[2];
        EXPECT_NE(stale[2].find("content version 0"), std::string::npos) << stale[2];
        EXPECT_NE(stale[2].find("python3 engine/tools/migrate/migrate.py"), std::string::npos) << stale[2];
    }

}  // namespace BECore::Tests
