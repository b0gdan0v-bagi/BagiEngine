#include <Common/Printers.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/HotkeyMap.h>

namespace BECore::Tests {

    namespace {

        using namespace EditorCommandIds;

        constexpr KeyChord kCtrlK{ImGuiKey_K, kChordModCtrl};
        constexpr KeyChord kCtrlP{ImGuiKey_P, kChordModCtrl};
        constexpr KeyChord kCtrlS{ImGuiKey_S, kChordModCtrl};
        constexpr KeyChord kCtrlShiftE{ImGuiKey_E, kChordModCtrl | kChordModShift};

    }  // namespace

    TEST(HotkeyMapTest, StartsWithTheDefaults) {
        const HotkeyMap map;
        for (const EditorCommand& command : GetEditorCommands()) {
            EXPECT_EQ(map.GetChord(command.id), command.defaultChord) << command.id.CStr();
            EXPECT_TRUE(map.IsDefault(command.id)) << command.id.CStr();
        }
        EXPECT_EQ(map.Find(kCtrlP), PoolString(kViewCommandPalette));
        EXPECT_TRUE(map.Find(kCtrlK).Empty());
    }

    TEST(HotkeyMapTest, TheGameCommandsAreListedWithAllowedUniqueChords) {
        const HotkeyMap map;
        for (const PoolString id : {PoolString(kGamePause), PoolString(kGameStep)}) {
            const EditorCommand* command = FindEditorCommand(id);
            ASSERT_NE(command, nullptr) << id.CStr();
            EXPECT_TRUE(command->rebindable) << id.CStr();
            ASSERT_TRUE(command->defaultChord.has_value()) << id.CStr();
            EXPECT_TRUE(IsAllowedAsHotkey(*command->defaultChord)) << id.CStr();
            EXPECT_FALSE(IsReservedChord(*command->defaultChord)) << id.CStr();
            // The map finds the command by its chord, so no other command holds the same one
            EXPECT_EQ(map.Find(*command->defaultChord), id);
        }
        // Only the step repeats while held
        EXPECT_FALSE(FindEditorCommand(kGamePause)->repeats);
        EXPECT_TRUE(FindEditorCommand(kGameStep)->repeats);
    }

    TEST(HotkeyMapTest, TheViewportInputModeCommandIsRebindableOnF8) {
        const HotkeyMap map;
        const EditorCommand* command = FindEditorCommand(kViewportInputMode);
        ASSERT_NE(command, nullptr);
        EXPECT_TRUE(command->rebindable);
        EXPECT_FALSE(command->repeats);
        ASSERT_TRUE(command->defaultChord.has_value());
        EXPECT_EQ(*command->defaultChord, (KeyChord{ImGuiKey_F8, 0}));
        EXPECT_TRUE(IsAllowedAsHotkey(*command->defaultChord));
        EXPECT_FALSE(IsReservedChord(*command->defaultChord));
        EXPECT_EQ(map.Find(*command->defaultChord), PoolString(kViewportInputMode));
    }

    TEST(HotkeyMapTest, NoTwoCommandsShareADefaultChord) {
        const Span<const EditorCommand> commands = GetEditorCommands();
        for (size_t i = 0; i < commands.size(); ++i) {
            for (size_t j = i + 1; j < commands.size(); ++j) {
                if (commands[i].defaultChord && commands[j].defaultChord) {
                    EXPECT_NE(*commands[i].defaultChord, *commands[j].defaultChord) << commands[i].id.CStr() << " and " << commands[j].id.CStr();
                }
            }
        }
    }

    TEST(HotkeyMapTest, AssignChangesTheChord) {
        HotkeyMap map;
        const AssignResult result = map.Assign(kSceneSave, kCtrlK);
        EXPECT_EQ(result.status, AssignStatus::Assigned);
        EXPECT_EQ(map.GetChord(kSceneSave), kCtrlK);
        EXPECT_EQ(map.Find(kCtrlK), PoolString(kSceneSave));
        EXPECT_TRUE(map.Find(kCtrlS).Empty());
        EXPECT_FALSE(map.IsDefault(kSceneSave));
    }

    TEST(HotkeyMapTest, AssigningTheSameChordAgainIsFine) {
        HotkeyMap map;
        EXPECT_EQ(map.Assign(kSceneSave, kCtrlS).status, AssignStatus::Assigned);
        EXPECT_TRUE(map.IsDefault(kSceneSave));
    }

    TEST(HotkeyMapTest, ConflictNamesTheOtherCommandAndChangesNothing) {
        HotkeyMap map;
        const AssignResult result = map.Assign(kSceneSave, kCtrlP);
        EXPECT_EQ(result.status, AssignStatus::Conflict);
        EXPECT_EQ(result.otherCommandId, PoolString(kViewCommandPalette));
        EXPECT_EQ(map.GetChord(kSceneSave), kCtrlS);
        EXPECT_EQ(map.GetChord(kViewCommandPalette), kCtrlP);
    }

    TEST(HotkeyMapTest, SwapTakesTheChordFromTheOtherCommand) {
        HotkeyMap map;
        const AssignResult result = map.AssignSwapping(kSceneSave, kCtrlP);
        EXPECT_EQ(result.status, AssignStatus::Assigned);
        EXPECT_EQ(map.GetChord(kSceneSave), kCtrlP);
        EXPECT_FALSE(map.GetChord(kViewCommandPalette).has_value());
        EXPECT_EQ(map.Find(kCtrlP), PoolString(kSceneSave));
        EXPECT_FALSE(map.IsDefault(kViewCommandPalette));
    }

    TEST(HotkeyMapTest, ReservedChordsAreRefused) {
        HotkeyMap map;
        const KeyChord reserved[] = {kCtrlShiftE,
                                     {ImGuiKey_A, kChordModCtrl},
                                     {ImGuiKey_C, kChordModCtrl},
                                     {ImGuiKey_V, kChordModCtrl},
                                     {ImGuiKey_X, kChordModCtrl},
                                     {ImGuiKey_Y, kChordModCtrl},
                                     {ImGuiKey_Z, kChordModCtrl},
                                     {ImGuiKey_Tab, kChordModCtrl}};
        for (const KeyChord& chord : reserved) {
            EXPECT_TRUE(IsReservedChord(chord)) << ToString(chord).c_str();
            const AssignResult result = map.Assign(kSceneSave, chord);
            EXPECT_EQ(result.status, AssignStatus::NotAllowed) << ToString(chord).c_str();
            EXPECT_EQ(result.refusal, AssignRefusal::Reserved);
            EXPECT_EQ(map.AssignSwapping(kSceneSave, chord).status, AssignStatus::NotAllowed);
        }
        EXPECT_EQ(map.GetChord(kSceneSave), kCtrlS);
        EXPECT_EQ(map.GetChord(kModeToggle), kCtrlShiftE);
        EXPECT_FALSE(IsReservedChord(kCtrlK));
    }

    TEST(HotkeyMapTest, ChordWithoutCtrlOrAltIsRefused) {
        HotkeyMap map;
        const AssignResult result = map.Assign(kSceneSave, KeyChord{ImGuiKey_K, 0});
        EXPECT_EQ(result.status, AssignStatus::NotAllowed);
        EXPECT_EQ(result.refusal, AssignRefusal::NeedsModifier);
        EXPECT_EQ(map.GetChord(kSceneSave), kCtrlS);
        EXPECT_EQ(map.Assign(kSceneSave, KeyChord{ImGuiKey_F5, 0}).status, AssignStatus::Assigned);
    }

    TEST(HotkeyMapTest, FixedCommandCannotBeChangedAndItsChordCannotBeTaken) {
        HotkeyMap map;
        EXPECT_EQ(map.Assign(kModeToggle, kCtrlK).refusal, AssignRefusal::FixedCommand);
        EXPECT_EQ(map.AssignSwapping(kModeToggle, kCtrlK).refusal, AssignRefusal::FixedCommand);
        EXPECT_FALSE(map.Unbind(kModeToggle));
        EXPECT_EQ(map.ResetOne(kModeToggle).refusal, AssignRefusal::FixedCommand);
        EXPECT_EQ(map.GetChord(kModeToggle), kCtrlShiftE);
        EXPECT_EQ(map.Find(kCtrlShiftE), PoolString(kModeToggle));
    }

    TEST(HotkeyMapTest, UnknownCommandIsRefused) {
        HotkeyMap map;
        const PoolString unknown = PoolString::Intern("no.such");
        EXPECT_EQ(map.Assign(unknown, kCtrlK).refusal, AssignRefusal::UnknownCommand);
        EXPECT_EQ(map.ResetOne(unknown).refusal, AssignRefusal::UnknownCommand);
        EXPECT_FALSE(map.Unbind(unknown));
        EXPECT_FALSE(map.GetChord(unknown).has_value());
        EXPECT_FALSE(map.IsDefault(unknown));
        EXPECT_TRUE(map.Find(kCtrlK).Empty());
    }

    TEST(HotkeyMapTest, UnbindLeavesTheCommandWithoutAChordAndFreesIt) {
        HotkeyMap map;
        EXPECT_TRUE(map.Unbind(kSceneSave));
        EXPECT_FALSE(map.GetChord(kSceneSave).has_value());
        EXPECT_TRUE(map.Find(kCtrlS).Empty());
        EXPECT_EQ(map.Assign(kConfigSave, kCtrlS).status, AssignStatus::Assigned);
    }

    TEST(HotkeyMapTest, ResetOnePutsTheDefaultBack) {
        HotkeyMap map;
        map.Assign(kSceneSave, kCtrlK);
        EXPECT_EQ(map.ResetOne(kSceneSave).status, AssignStatus::Assigned);
        EXPECT_EQ(map.GetChord(kSceneSave), kCtrlS);
        EXPECT_TRUE(map.IsDefault(kSceneSave));

        // A command with no default is reset to nothing
        map.Assign(kFileQuit, kCtrlK);
        EXPECT_EQ(map.ResetOne(kFileQuit).status, AssignStatus::Assigned);
        EXPECT_FALSE(map.GetChord(kFileQuit).has_value());
    }

    TEST(HotkeyMapTest, ResetOneReportsAConflictWithTheHolderOfTheDefault) {
        HotkeyMap map;
        map.Assign(kSceneSave, kCtrlK);
        map.Assign(kConfigSave, kCtrlS);
        const AssignResult result = map.ResetOne(kSceneSave);
        EXPECT_EQ(result.status, AssignStatus::Conflict);
        EXPECT_EQ(result.otherCommandId, PoolString(kConfigSave));
        EXPECT_EQ(map.GetChord(kSceneSave), kCtrlK);
    }

    TEST(HotkeyMapTest, ResetAllPutsEveryDefaultBack) {
        HotkeyMap map;
        map.AssignSwapping(kSceneSave, kCtrlP);
        map.Unbind(kConfigSave);
        map.Assign(kFileQuit, kCtrlK);
        map.ResetAll();
        for (const EditorCommand& command : GetEditorCommands()) {
            EXPECT_EQ(map.GetChord(command.id), command.defaultChord) << command.id.CStr();
        }
    }

}  // namespace BECore::Tests
