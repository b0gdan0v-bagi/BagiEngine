#include <Common/Printers.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/KeyChord.h>

namespace BECore::Tests {

    namespace {

        KeyChord Chord(ImGuiKey key, uint8_t mods) {
            return KeyChord{key, mods};
        }

    }  // namespace

    TEST(KeyChordTest, ToStringUsesTheCanonicalOrder) {
        EXPECT_EQ(ToString(Chord(ImGuiKey_K, kChordModCtrl | kChordModShift)), "Ctrl+Shift+K");
        EXPECT_EQ(ToString(Chord(ImGuiKey_K, kChordModShift | kChordModAlt | kChordModCtrl)), "Ctrl+Alt+Shift+K");
        EXPECT_EQ(ToString(Chord(ImGuiKey_F5, 0)), "F5");
        EXPECT_EQ(ToString(Chord(ImGuiKey_Comma, kChordModCtrl)), "Ctrl+,");
        EXPECT_EQ(ToString(Chord(ImGuiKey_Escape, 0)), "Esc");
    }

    TEST(KeyChordTest, ParseReadsAnyCase) {
        const std::optional<KeyChord> chord = ParseKeyChord("cTRL+shift+k");
        ASSERT_TRUE(chord.has_value());
        EXPECT_EQ(*chord, Chord(ImGuiKey_K, kChordModCtrl | kChordModShift));

        EXPECT_EQ(ParseKeyChord(" Ctrl + S "), Chord(ImGuiKey_S, kChordModCtrl));
        EXPECT_EQ(ParseKeyChord("shift+ctrl+s"), Chord(ImGuiKey_S, kChordModCtrl | kChordModShift));
        EXPECT_EQ(ParseKeyChord("f12"), Chord(ImGuiKey_F12, 0));
    }

    TEST(KeyChordTest, ParseReadsAliases) {
        EXPECT_EQ(ParseKeyChord("Ctrl+Comma"), Chord(ImGuiKey_Comma, kChordModCtrl));
        EXPECT_EQ(ParseKeyChord("Escape"), Chord(ImGuiKey_Escape, 0));
        EXPECT_EQ(ParseKeyChord("Alt+Return"), Chord(ImGuiKey_Enter, kChordModAlt));
    }

    TEST(KeyChordTest, EveryNamedKeyRoundTrips) {
        const ImGuiKey keys[] = {ImGuiKey_A,      ImGuiKey_Z,        ImGuiKey_0,         ImGuiKey_9,          ImGuiKey_F1,      ImGuiKey_F12,       ImGuiKey_Enter, ImGuiKey_Space,
                                 ImGuiKey_Tab,    ImGuiKey_Escape,   ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_Home,  ImGuiKey_End,
                                 ImGuiKey_PageUp, ImGuiKey_PageDown, ImGuiKey_Delete,    ImGuiKey_Backspace,  ImGuiKey_Comma,   ImGuiKey_Period,    ImGuiKey_Slash};
        for (const ImGuiKey key : keys) {
            for (uint8_t mods = 0; mods <= (kChordModCtrl | kChordModAlt | kChordModShift); ++mods) {
                const KeyChord chord = Chord(key, mods);
                const eastl::string text = ToString(chord);
                ASSERT_FALSE(text.empty()) << static_cast<int>(key);
                EXPECT_EQ(ParseKeyChord(text), chord) << text.c_str();
            }
        }
    }

    TEST(KeyChordTest, EveryChordOfTheCommandTableRoundTrips) {
        for (const EditorCommand& command : GetEditorCommands()) {
            if (!command.defaultChord) {
                continue;
            }
            const eastl::string text = ToString(*command.defaultChord);
            EXPECT_EQ(ParseKeyChord(text), command.defaultChord) << command.id.CStr();
        }
    }

    TEST(KeyChordTest, ParseRefusesBadText) {
        const char* bad[] = {"", "+", "Ctrl", "Ctrl+", "+K", "Ctrl++K", "Ctrl+Ctrl+K", "K+Ctrl", "A+B", "Ctrl+Nope", "F0", "F13", "F1x", "Ctrl+Shift", "Ctrl+Alt+Shift", "Super+K"};
        for (const char* text : bad) {
            EXPECT_FALSE(ParseKeyChord(text).has_value()) << text;
        }
    }

    TEST(KeyChordTest, ToStringOfAKeyWithoutNameIsEmpty) {
        EXPECT_TRUE(ToString(Chord(ImGuiKey_None, kChordModCtrl)).empty());
        EXPECT_TRUE(ToString(Chord(ImGuiKey_LeftCtrl, 0)).empty());
    }

    TEST(KeyChordTest, HotkeyNeedsCtrlOrAltOrAnFKey) {
        EXPECT_TRUE(IsAllowedAsHotkey(Chord(ImGuiKey_S, kChordModCtrl)));
        EXPECT_TRUE(IsAllowedAsHotkey(Chord(ImGuiKey_S, kChordModAlt)));
        EXPECT_TRUE(IsAllowedAsHotkey(Chord(ImGuiKey_S, kChordModCtrl | kChordModShift)));
        EXPECT_TRUE(IsAllowedAsHotkey(Chord(ImGuiKey_F5, 0)));
        EXPECT_TRUE(IsAllowedAsHotkey(Chord(ImGuiKey_F5, kChordModShift)));

        EXPECT_FALSE(IsAllowedAsHotkey(Chord(ImGuiKey_S, 0)));
        EXPECT_FALSE(IsAllowedAsHotkey(Chord(ImGuiKey_S, kChordModShift)));
        EXPECT_FALSE(IsAllowedAsHotkey(Chord(ImGuiKey_Enter, 0)));
        EXPECT_FALSE(IsAllowedAsHotkey(Chord(ImGuiKey_Space, 0)));
    }

    TEST(KeyChordTest, BareModifierIsNotAHotkey) {
        EXPECT_FALSE(IsAllowedAsHotkey(Chord(ImGuiKey_None, kChordModCtrl)));
        EXPECT_FALSE(IsAllowedAsHotkey(Chord(ImGuiKey_LeftCtrl, kChordModCtrl)));
        EXPECT_FALSE(IsAllowedAsHotkey(Chord(ImGuiKey_RightShift, 0)));
    }

    TEST(KeyChordTest, ToImGuiMergesTheModifiers) {
        EXPECT_EQ(Chord(ImGuiKey_S, kChordModCtrl | kChordModShift).ToImGui(), static_cast<int>(ImGuiKey_S) | ImGuiMod_Ctrl | ImGuiMod_Shift);
        EXPECT_EQ(Chord(ImGuiKey_F1, kChordModAlt).ToImGui(), static_cast<int>(ImGuiKey_F1) | ImGuiMod_Alt);
        EXPECT_EQ(Chord(ImGuiKey_F1, 0).ToImGui(), static_cast<int>(ImGuiKey_F1));
    }

    TEST(KeyChordTest, EveryCommandHasATitleAndAUniqueId) {
        const auto commands = GetEditorCommands();
        ASSERT_FALSE(commands.empty());
        for (size_t i = 0; i < commands.size(); ++i) {
            EXPECT_FALSE(commands[i].title.empty());
            EXPECT_FALSE(commands[i].id.Empty());
            for (size_t j = i + 1; j < commands.size(); ++j) {
                EXPECT_NE(commands[i].id, commands[j].id);
                if (commands[i].defaultChord && commands[j].defaultChord) {
                    EXPECT_NE(*commands[i].defaultChord, *commands[j].defaultChord) << commands[i].id.CStr() << " and " << commands[j].id.CStr();
                }
            }
        }
    }

    TEST(KeyChordTest, FindEditorCommandByIdOnly) {
        const EditorCommand* toggle = FindEditorCommand(EditorCommandIds::kModeToggle);
        ASSERT_NE(toggle, nullptr);
        EXPECT_FALSE(toggle->rebindable);
        EXPECT_EQ(FindEditorCommand(PoolString::Intern("no.such")), nullptr);
    }

}  // namespace BECore::Tests
