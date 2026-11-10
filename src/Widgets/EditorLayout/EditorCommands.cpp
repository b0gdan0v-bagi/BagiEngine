#include "EditorCommands.h"

#include <Generated/EditorCommands.gen.hpp>

namespace BECore {

    namespace {

        constexpr KeyChord kCtrlP{ImGuiKey_P, kChordModCtrl};
        constexpr KeyChord kCtrlComma{ImGuiKey_Comma, kChordModCtrl};
        constexpr KeyChord kCtrlS{ImGuiKey_S, kChordModCtrl};
        constexpr KeyChord kCtrlShiftS{ImGuiKey_S, kChordModCtrl | kChordModShift};
        constexpr KeyChord kCtrlShiftE{ImGuiKey_E, kChordModCtrl | kChordModShift};

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
                {kModeToggle, "Switch Game and Editor", kCtrlShiftE, false},
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
