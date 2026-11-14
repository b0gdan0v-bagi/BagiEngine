#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Widgets/EditorLayout/LayoutStore.h>

namespace BECore::Tests {

    namespace {

        class LayoutStoreTest : public ::testing::Test {
        protected:
            void SetUp() override {
                _directory = TestTempDirectory("LayoutStore");
                std::error_code ec;
                std::filesystem::remove_all(_directory, ec);
                std::filesystem::create_directories(_directory, ec);
            }

            void TearDown() override {
                std::error_code ec;
                std::filesystem::remove_all(_directory, ec);
            }

            void WriteFile(const std::filesystem::path& path, const std::string& text) const {
                std::filesystem::create_directories(path.parent_path());
                std::ofstream(path, std::ios::binary) << text;
            }

            std::filesystem::path _directory;
        };

        String Joined(const eastl::vector<String>& names) {
            String text;
            for (const String& name : names) {
                if (!text.empty()) {
                    text += "|";
                }
                text += name;
            }
            return text;
        }

    }  // namespace

    TEST_F(LayoutStoreTest, ValidNames) {
        EXPECT_TRUE(IsValidLayoutName("Wide"));
        EXPECT_TRUE(IsValidLayoutName("my layout-2_b"));
        EXPECT_TRUE(IsValidLayoutName("A"));
        EXPECT_TRUE(IsValidLayoutName(String(kMaxLayoutNameLength, 'x')));
        EXPECT_TRUE(IsValidLayoutName("CONSOLE"));
        EXPECT_TRUE(IsValidLayoutName("COM10"));
        EXPECT_TRUE(IsValidLayoutName("COM"));
    }

    TEST_F(LayoutStoreTest, BadNames) {
        const char* bad[] = {"", " ", " a", "a ", "..", ".", "a/b", "a\\b", "a.b", "a.ini", "a\tb", "naïve", "a:b", "a*b", "a\"b", "CON", "con", "Prn", "AUX", "nul", "COM1", "com9", "LPT1", "lpt5"};
        for (const char* name : bad) {
            EXPECT_FALSE(IsValidLayoutName(name)) << name;
        }
        EXPECT_FALSE(IsValidLayoutName(String(kMaxLayoutNameLength + 1, 'x')));
    }

    TEST_F(LayoutStoreTest, LooksLikeLayoutTextNeedsTheDockTree) {
        EXPECT_TRUE(LooksLikeLayoutText("[Window][Viewport]\nPos=1,2\n\n[Docking][Data]\nDockSpace ID=0x1\n"));
        EXPECT_FALSE(LooksLikeLayoutText(""));
        EXPECT_FALSE(LooksLikeLayoutText("garbage"));
        EXPECT_FALSE(LooksLikeLayoutText("[Window][Viewport]\nPos=1,2\n"));
    }

    TEST_F(LayoutStoreTest, SaveAndLoadRoundTrip) {
        LayoutStore store(_directory);
        const String text = "[Window][Viewport]\nPos=1,2\nSize=300,200\n\n[Docking][Data]\n";
        ASSERT_TRUE(store.Save("Wide", text).has_value());
        EXPECT_TRUE(std::filesystem::exists(_directory / "layouts" / "Wide.ini"));
        const auto loaded = store.Load("Wide");
        ASSERT_TRUE(loaded.has_value());
        EXPECT_EQ(*loaded, text);
        EXPECT_TRUE(store.Exists("Wide"));
    }

    TEST_F(LayoutStoreTest, SaveOverwritesAndLeavesNoTempFile) {
        LayoutStore store(_directory);
        ASSERT_TRUE(store.Save("a", "first").has_value());
        ASSERT_TRUE(store.Save("a", "second").has_value());
        EXPECT_EQ(*store.Load("a"), "second");
        EXPECT_FALSE(std::filesystem::exists(_directory / "layouts" / "a.ini.tmp"));
        EXPECT_EQ(Joined(store.List()), "a");
    }

    TEST_F(LayoutStoreTest, SaveRefusesABadNameAndAnEmptyText) {
        LayoutStore store(_directory);
        EXPECT_EQ(store.Save("..", "x").error().kind, LayoutErrorKind::InvalidName);
        EXPECT_EQ(store.Save("a/b", "x").error().kind, LayoutErrorKind::InvalidName);
        EXPECT_EQ(store.Save("CON", "x").error().kind, LayoutErrorKind::InvalidName);
        const auto empty = store.Save("a", "");
        ASSERT_FALSE(empty.has_value());
        EXPECT_EQ(empty.error().kind, LayoutErrorKind::EmptyText);
        EXPECT_FALSE(empty.error().detail.empty());
        EXPECT_FALSE(store.Exists("a"));
        EXPECT_TRUE(store.List().empty());
        EXPECT_FALSE(std::filesystem::exists(_directory / "layouts"));
    }

    TEST_F(LayoutStoreTest, AMissingLayoutIsNotFound) {
        LayoutStore store(_directory);
        const auto loaded = store.Load("nope");
        ASSERT_FALSE(loaded.has_value());
        EXPECT_EQ(loaded.error().kind, LayoutErrorKind::NotFound);
        EXPECT_EQ(store.Delete("nope").error().kind, LayoutErrorKind::NotFound);
        EXPECT_EQ(store.Load("..").error().kind, LayoutErrorKind::InvalidName);
        EXPECT_EQ(store.Delete("a/b").error().kind, LayoutErrorKind::InvalidName);
        EXPECT_FALSE(store.Exists(".."));
    }

    TEST_F(LayoutStoreTest, DeleteRemovesTheLayout) {
        LayoutStore store(_directory);
        ASSERT_TRUE(store.Save("a", "x").has_value());
        ASSERT_TRUE(store.Save("b", "y").has_value());
        ASSERT_TRUE(store.Delete("a").has_value());
        EXPECT_FALSE(store.Exists("a"));
        EXPECT_EQ(Joined(store.List()), "b");
        EXPECT_EQ(store.Load("a").error().kind, LayoutErrorKind::NotFound);
    }

    TEST_F(LayoutStoreTest, ListIsSortedAndIgnoresOtherFiles) {
        LayoutStore store(_directory);
        ASSERT_TRUE(store.Save("zeta", "x").has_value());
        ASSERT_TRUE(store.Save("alpha", "x").has_value());
        ASSERT_TRUE(store.Save("mid dle", "x").has_value());
        const std::filesystem::path folder = _directory / "layouts";
        WriteFile(folder / "leftover.ini.tmp", "x");
        WriteFile(folder / "readme.txt", "x");
        WriteFile(folder / "bad.name.ini", "x");
        WriteFile(folder / "CON.ini", "x");
        WriteFile(folder / "sub" / "inner.ini", "x");
        std::filesystem::create_directories(folder / "folder.ini");
        EXPECT_EQ(Joined(store.List()), "alpha|mid dle|zeta");
    }

    TEST_F(LayoutStoreTest, ListOfAMissingFolderIsEmpty) {
        const LayoutStore store(_directory / "nowhere");
        EXPECT_TRUE(store.List().empty());
    }

}  // namespace BECore::Tests
