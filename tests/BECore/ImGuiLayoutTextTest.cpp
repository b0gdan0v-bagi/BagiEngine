#include <Common/Printers.h>
#include <Widgets/ImGuiLayoutText.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kStaleDockTree = R"([Docking][Data]
DockSpace       ID=0x283A2638 Window=0xE704AE68 Pos=0,64 Size=1920,982 Split=X
  DockNode      ID=0x00000001 Parent=0x283A2638 SizeRef=958,622 Split=X
    DockNode    ID=0x00000003 Parent=0x00000001 SizeRef=210,622 Split=Y
      DockNode  ID=0x00000005 Parent=0x00000003 SizeRef=210,434 Selected=0x0FE433CA
      DockNode  ID=0x00000006 Parent=0x00000003 SizeRef=210,186 Selected=0x7E645E5C
    DockNode    ID=0x00000004 Parent=0x00000001 SizeRef=746,622 CentralNode=1 Selected=0xC450F867
  DockNode      ID=0x00000002 Parent=0x283A2638 SizeRef=320,622 Selected=0x198662CE
)";

        constexpr StringView kSceneEditor = R"([Window][Scene Editor]
Pos=0,64
Size=210,982
Collapsed=0
DockId=0x00000005,0

)";

        constexpr StringView kLegacyConfigEditor = R"([Window][Config Editor]
Pos=0,500
Size=210,186
Collapsed=0
DockId=0x00000006,0

)";

        constexpr StringView kRenamedConfigEditor = R"([Window][ConfigEditor]
Pos=0,500
Size=210,186
Collapsed=0
DockId=0x00000006,0

)";

        constexpr StringView kFloatingConfigEditor = R"([Window][ConfigEditor]
Pos=60,60
Size=900,550
Collapsed=0

)";

        constexpr StringView kViewport = R"([Window][Viewport]
Pos=212,64
Size=1386,982
Collapsed=0
DockId=0x00000004,0

)";

        String Joined(std::initializer_list<StringView> parts) {
            String text;
            for (const StringView part : parts) {
                text.append(part.data(), part.size());
            }
            return text;
        }

        String WithCrLf(StringView text) {
            String result;
            for (const char c : text) {
                if (c == '\n') {
                    result += '\r';
                }
                result += c;
            }
            return result;
        }

    }  // namespace

    TEST(ImGuiLayoutTextTest, StaleIniGetsTheDockedWindowUnderTheCurrentName) {
        const String stale = Joined({kSceneEditor, kLegacyConfigEditor, kViewport, kFloatingConfigEditor, kStaleDockTree});
        const String expected = Joined({kSceneEditor, kRenamedConfigEditor, kViewport, kStaleDockTree});
        EXPECT_EQ(MigrateLegacyWindowNames(stale), expected);
    }

    TEST(ImGuiLayoutTextTest, CurrentNameAndEmptyTextComeBackEqual) {
        const String fresh = Joined({kSceneEditor, kRenamedConfigEditor, kViewport, kStaleDockTree});
        EXPECT_EQ(MigrateLegacyWindowNames(fresh), fresh);
        EXPECT_EQ(MigrateLegacyWindowNames(""), String());
        EXPECT_EQ(MigrateLegacyWindowNames("not an ini at all\nConfig Editor"), String("not an ini at all\nConfig Editor"));
    }

    TEST(ImGuiLayoutTextTest, LegacyWithoutDockIdNextToCurrentIsLeftAlone) {
        constexpr StringView legacyFloating = "[Window][Config Editor]\nPos=5,5\nSize=100,100\n\n";
        const String text = Joined({legacyFloating, kFloatingConfigEditor, kStaleDockTree});
        EXPECT_EQ(MigrateLegacyWindowNames(text), text);
    }

    TEST(ImGuiLayoutTextTest, LegacyWithoutDockIdAndWithoutCurrentKeepsItsPlace) {
        constexpr StringView legacyFloating = "[Window][Config Editor]\nPos=5,5\nSize=100,100\n\n";
        constexpr StringView renamedFloating = "[Window][ConfigEditor]\nPos=5,5\nSize=100,100\n\n";
        EXPECT_EQ(MigrateLegacyWindowNames(Joined({legacyFloating, kStaleDockTree})), Joined({renamedFloating, kStaleDockTree}));
    }

    TEST(ImGuiLayoutTextTest, LineEndsAreKept) {
        const String stale = WithCrLf(Joined({kLegacyConfigEditor, kFloatingConfigEditor, kStaleDockTree}));
        const String expected = WithCrLf(Joined({kRenamedConfigEditor, kStaleDockTree}));
        EXPECT_EQ(MigrateLegacyWindowNames(stale), expected);
    }

    TEST(ImGuiLayoutTextTest, OnlyTheWholeNameOfAWindowSectionChanges) {
        const String text = Joined(
            {"[Window][Config Editor 2]\nDockId=0x1,0\n\n", "[Window][config editor]\nDockId=0x2,0\n\n", "[Docking][Config Editor]\nDockId=0x3,0\n\n", "[Window][Config Editor2]\nDockId=0x4,0\n"});
        EXPECT_EQ(MigrateLegacyWindowNames(text), text);
    }

    TEST(ImGuiLayoutTextTest, LegacySectionAtTheEndWithoutNewlineIsRenamed) {
        EXPECT_EQ(MigrateLegacyWindowNames("[Window][Config Editor]\nDockId=0x6,0"), String("[Window][ConfigEditor]\nDockId=0x6,0"));
    }

}  // namespace BECore::Tests
