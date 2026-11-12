#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Widgets/EditorLayout/GitBranch.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kSha1 = "0123456789abcdef0123456789abcdef01234567";
        constexpr const char* kSha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

        class GitBranchTest : public ::testing::Test {
        protected:
            void SetUp() override {
                _root = TestTempDirectory("GitBranch");
                std::error_code ec;
                std::filesystem::remove_all(_root, ec);
                std::filesystem::create_directories(_root, ec);
            }

            void TearDown() override {
                std::error_code ec;
                std::filesystem::remove_all(_root, ec);
            }

            static void WriteFile(const std::filesystem::path& path, const std::string& text) {
                std::error_code ec;
                std::filesystem::create_directories(path.parent_path(), ec);
                std::ofstream(path, std::ios::binary) << text;
            }

            std::filesystem::path _root;
        };

    }  // namespace

    TEST_F(GitBranchTest, ParseABranch) {
        EXPECT_EQ(ParseGitHead("ref: refs/heads/main"), "main");
        EXPECT_EQ(ParseGitHead("ref: refs/heads/games/mahjong"), "games/mahjong");
    }

    TEST_F(GitBranchTest, ParseABranchWithSlashes) {
        EXPECT_EQ(ParseGitHead("ref: refs/heads/todo/editor-look-2"), "todo/editor-look-2");
    }

    TEST_F(GitBranchTest, ParseIgnoresLineEndsAndSpaces) {
        EXPECT_EQ(ParseGitHead("ref: refs/heads/main\n"), "main");
        EXPECT_EQ(ParseGitHead("ref: refs/heads/main\r\n"), "main");
        EXPECT_EQ(ParseGitHead("  ref: refs/heads/main \n\n"), "main");
        EXPECT_EQ(ParseGitHead((std::string(kSha1) + "\r\n").c_str()), "0123456");
    }

    TEST_F(GitBranchTest, ParseADetachedHead) {
        EXPECT_EQ(ParseGitHead(kSha1), "0123456");
        EXPECT_EQ(ParseGitHead(kSha256), "0123456");
        EXPECT_EQ(ParseGitHead("ABCDEF0123456789ABCDEF0123456789ABCDEF01"), "ABCDEF0");
    }

    TEST_F(GitBranchTest, ParseGivesEmptyForAnythingElse) {
        EXPECT_EQ(ParseGitHead(""), "");
        EXPECT_EQ(ParseGitHead("\n"), "");
        EXPECT_EQ(ParseGitHead("garbage"), "");
        EXPECT_EQ(ParseGitHead("ref: refs/heads/"), "");
        EXPECT_EQ(ParseGitHead("ref: refs/tags/v1"), "");
        EXPECT_EQ(ParseGitHead("0123456789abcdef"), "");                           // a short id is not a HEAD
        EXPECT_EQ(ParseGitHead("0123456789abcdef0123456789abcdef0123456g"), "");   // 40 characters, one not hex
        EXPECT_EQ(ParseGitHead("0123456789abcdef0123456789abcdef012345678"), "");  // 41
        EXPECT_EQ(ParseGitHead("gitdir: ../x"), "");
    }

    TEST_F(GitBranchTest, ReadFromAGitDirectory) {
        WriteFile(_root / ".git" / "HEAD", "ref: refs/heads/todo/x\n");
        EXPECT_EQ(ReadGitBranch(_root), "todo/x");
    }

    TEST_F(GitBranchTest, ReadFromSeveralLevelsBelowTheRoot) {
        WriteFile(_root / ".git" / "HEAD", "ref: refs/heads/main\n");
        const std::filesystem::path deep = _root / "a" / "b" / "c";
        std::error_code ec;
        std::filesystem::create_directories(deep, ec);
        EXPECT_EQ(ReadGitBranch(deep), "main");
    }

    TEST_F(GitBranchTest, ReadFromADetachedHead) {
        WriteFile(_root / ".git" / "HEAD", std::string(kSha1) + "\n");
        EXPECT_EQ(ReadGitBranch(_root), "0123456");
    }

    TEST_F(GitBranchTest, ReadFollowsAnAbsoluteGitdirFile) {
        WriteFile(_root / "elsewhere" / "HEAD", "ref: refs/heads/games/mahjong\n");
        WriteFile(_root / "project" / ".git", "gitdir: " + (_root / "elsewhere").string() + "\n");
        EXPECT_EQ(ReadGitBranch(_root / "project"), "games/mahjong");
    }

    TEST_F(GitBranchTest, ReadFollowsARelativeGitdirFile) {
        // The submodule shape: engine/.git is a file that points into the superproject's .git/modules
        WriteFile(_root / ".git" / "modules" / "engine" / "HEAD", "ref: refs/heads/todo/y\n");
        WriteFile(_root / "engine" / ".git", "gitdir: ../.git/modules/engine\n");
        const std::filesystem::path below = _root / "engine" / "config";
        std::error_code ec;
        std::filesystem::create_directories(below, ec);
        EXPECT_EQ(ReadGitBranch(below), "todo/y");
    }

    TEST_F(GitBranchTest, ReadStopsAtTheNearestGitEntry) {
        WriteFile(_root / ".git" / "HEAD", "ref: refs/heads/outer\n");
        WriteFile(_root / "inner" / ".git" / "HEAD", "ref: refs/heads/inner\n");
        EXPECT_EQ(ReadGitBranch(_root / "inner"), "inner");
    }

    TEST_F(GitBranchTest, ReadGivesEmptyWithoutARepository) {
        // Assumes the system temp folder has no .git above it
        std::error_code ec;
        std::filesystem::create_directories(_root / "plain" / "dir", ec);
        EXPECT_EQ(ReadGitBranch(_root / "plain" / "dir"), "");
    }

    TEST_F(GitBranchTest, ReadGivesEmptyForAMissingHead) {
        std::error_code ec;
        std::filesystem::create_directories(_root / ".git", ec);
        EXPECT_EQ(ReadGitBranch(_root), "");
    }

    TEST_F(GitBranchTest, ReadGivesEmptyForABrokenGitdirFile) {
        WriteFile(_root / "a" / ".git", "not a pointer\n");
        EXPECT_EQ(ReadGitBranch(_root / "a"), "");
        WriteFile(_root / "b" / ".git", "gitdir: ../nowhere\n");
        EXPECT_EQ(ReadGitBranch(_root / "b"), "");
    }

}  // namespace BECore::Tests
