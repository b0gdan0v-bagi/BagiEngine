#include "EditorCommands.h"

#include <Generated/EditorCommands.gen.hpp>

namespace BECore {

    namespace {

        constexpr KeyChord kCtrlP{ImGuiKey_P, kChordModCtrl};
        constexpr KeyChord kCtrlComma{ImGuiKey_Comma, kChordModCtrl};
        constexpr KeyChord kCtrlS{ImGuiKey_S, kChordModCtrl};
        constexpr KeyChord kCtrlShiftS{ImGuiKey_S, kChordModCtrl | kChordModShift};
        constexpr KeyChord kCtrlShiftE{ImGuiKey_E, kChordModCtrl | kChordModShift};
        constexpr KeyChord kCtrlShiftP{ImGuiKey_P, kChordModCtrl | kChordModShift};
        constexpr KeyChord kF6{ImGuiKey_F6, 0};
        constexpr KeyChord kF7{ImGuiKey_F7, 0};
        constexpr KeyChord kF8{ImGuiKey_F8, 0};

        const eastl::vector<EditorCommand>& CommandTable() {
            using namespace EditorCommandIds;
            static const eastl::vector<EditorCommand> table = {
                {kFileQuit, "Quit", std::nullopt, true},
                {kViewCommandPalette, "Command Palette", kCtrlP, true},
                {kViewPreferences, "Preferences", kCtrlComma, true},
                {kViewResetLayout, "Reset Layout", std::nullopt, true},
                {kViewSaveLayout, "Save Layout As", std::nullopt, true},
                {kSceneSave, "Save Scene", kCtrlS, true},
                {kConfigSave, "Save Config", kCtrlShiftS, true},
                {kTextureSave, "Save Texture", std::nullopt, true},
                {kGamePause, "Pause / Resume Game", kF6, true},
                {kGameStep, "Step One Frame", kF7, true, true},
                {kViewportInputMode, "Toggle Viewport Input: Edit / Play", kF8, true},
                {kModeToggle, "Switch Game and Editor", kCtrlShiftE, false},
                {kViewProfiler, "Profiler", kCtrlShiftP, false},
            };
            return table;
        }

    }  // namespace

    eastl::span<const EditorCommand> GetEditorCommands() {
        const eastl::vector<EditorCommand>& table = CommandTable();
        return {table.data(), table.size()};
    }

    const EditorCommand* FindEditorCommand(PoolString id) {
        for (const EditorCommand& command : CommandTable()) {
            if (command.id == id) {
                return &command;
            }
        }
        return nullptr;
    }

}  // namespace BECore
