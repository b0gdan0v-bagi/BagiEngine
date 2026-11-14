#include <BECore/MainWindow/WindowTitle.h>
#include <Common/Printers.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kDash = "\xE2\x80\x94";  // U+2014 in UTF-8

        String Expect(const char* game) {
            return String(game) + " " + kDash + " BagiEngine Editor";
        }

    }  // namespace

    TEST(WindowTitleTest, EditorWithAGameNamesTheGameAndTheEditor) {
        EXPECT_EQ(ComposeWindowTitle("", "Mahjong", true), Expect("Mahjong"));
    }

    TEST(WindowTitleTest, EditorIgnoresTheConfigTitle) {
        EXPECT_EQ(ComposeWindowTitle("Own Title", "Mahjong", true), Expect("Mahjong"));
    }

    TEST(WindowTitleTest, EditorWithoutAGameIsJustTheEditor) {
        EXPECT_EQ(ComposeWindowTitle("", "", true), "BagiEngine Editor");
        EXPECT_EQ(ComposeWindowTitle("Own Title", "", true), "BagiEngine Editor");
    }

    TEST(WindowTitleTest, GameModeTakesTheConfigTitleFirst) {
        EXPECT_EQ(ComposeWindowTitle("Own Title", "Mahjong", false), "Own Title");
        EXPECT_EQ(ComposeWindowTitle("Own Title", "", false), "Own Title");
    }

    TEST(WindowTitleTest, GameModeFallsBackToTheGameName) {
        EXPECT_EQ(ComposeWindowTitle("", "Mahjong", false), "Mahjong");
    }

    TEST(WindowTitleTest, GameModeWithNothingIsTheEngineName) {
        EXPECT_EQ(ComposeWindowTitle("", "", false), "BagiEngine");
    }

    TEST(WindowTitleTest, TheEmDashHasSpacesAroundIt) {
        const String title = ComposeWindowTitle("", "G", true);
        EXPECT_NE(title.find(" \xE2\x80\x94 "), String::npos);
    }

}  // namespace BECore::Tests
