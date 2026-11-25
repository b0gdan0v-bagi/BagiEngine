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
        constexpr KeyChord kCtrlShiftL{ImGuiKey_L, kChordModCtrl | kChordModShift};
        constexpr KeyChord kCtrlShiftG{ImGuiKey_G, kChordModCtrl | kChordModShift};
        constexpr KeyChord kF6{ImGuiKey_F6, 0};
        constexpr KeyChord kF7{ImGuiKey_F7, 0};
        constexpr KeyChord kF8{ImGuiKey_F8, 0};

        const Vector<EditorCommand>& CommandTable() {
            using namespace EditorCommandIds;
            static const Vector<EditorCommand> table = {
                {kFileQuit, "Quit", std::nullopt, true},
                {kViewCommandPalette, "Command Palette", kCtrlP, true},
                {kViewPreferences, "Preferences", kCtrlComma, true},
                {kViewResetLayout, "Reset Layout", std::nullopt, true},
                {kViewSaveLayout, "Save Layout As", std::nullopt, true},
                {kSceneSave, "Save Scene", kCtrlS, true},
                {kConfigSave, "Save Config", kCtrlShiftS, true},
                {kTextureSave, "Save Texture", std::nullopt, true},
                {kGamePlay, "Play / Stop Game", std::nullopt, true},
                {kGamePause, "Pause / Resume Game", kF6, true},
                {kGameStep, "Step One Frame", kF7, true, true},
                {kViewportInputMode, "Toggle Viewport Input: Edit / Play", kF8, true},
                {kViewportToolTranslate, "Viewport Tool: Move", std::nullopt, true},
                {kViewportToolRotate, "Viewport Tool: Rotate", std::nullopt, true},
                {kViewportToolScale, "Viewport Tool: Scale", std::nullopt, true},
                {kViewportGizmoSpace, "Viewport Gizmo: World / Local", std::nullopt, true},
                {kViewportSnap, "Viewport Snap: On / Off", std::nullopt, true},
                {kViewportFrameSelected, "Viewport: Frame Selection", std::nullopt, true},
                {kViewportCameraReset, "Viewport: Reset Camera", std::nullopt, true},
                {kModeToggle, "Switch Game and Editor", kCtrlShiftE, false},
                {kViewProfiler, "Profiler", kCtrlShiftP, false},
                {kProfilerTrace, "Profiler: Chrome Trace (record, restart, write)", std::nullopt, true},
                {kProfilerSpikeLive, "Profiler: Back To The Live Frame", std::nullopt, true},
                {kProfilerSpikeClear, "Profiler: Clear Spikes", std::nullopt, true},
                {kProfilerResetZoneAllocs, "Profiler: Reset Allocations Per Zone", std::nullopt, true},
                {kViewConsole, "Console", kCtrlShiftL, true},
                {kViewGame, "Game Panel", kCtrlShiftG, true},
                {kViewSceneEditor, "Scene Editor", std::nullopt, true},
                {kViewConfigEditor, "Config Editor", std::nullopt, true},
                {kViewFileChanges, "File Changes", std::nullopt, true},
                {kViewSpriteLibrary, "Sprite Library", std::nullopt, true},
                {kViewViewport, "Viewport", std::nullopt, true},
                {kViewGenerate, "Generate Art", std::nullopt, true},
                {kFileChangesRescan, "File Changes: Rescan", std::nullopt, true},
                {kFileChangesApplyAll, "File Changes: Apply All", std::nullopt, true},
                {kFileChangesApply, "File Changes: Apply One", std::nullopt, true},
                {kConsolePause, "Console: Pause / Resume", std::nullopt, true},
                {kConsoleClear, "Console: Clear Log", std::nullopt, true},
                {kConsoleClearOutput, "Console: Clear Output", std::nullopt, true},
                {kGamePanelReset, "Game Panel: Reset", std::nullopt, true},
                {kGamePanelPerform, "Game Panel: Perform", std::nullopt, true},
                {kGamePanelCall, "Game Panel: Call", std::nullopt, true},
                {kGamePanelCopyState, "Game Panel: Copy State", std::nullopt, true},
                {kConfigEditorSelect, "Config Editor: Select", std::nullopt, false},
                {kConfigEditorReload, "Config Editor: Reload", std::nullopt, false},
                {kConfigEditorReloadDiscard, "Config Editor: Discard And Reload", std::nullopt, false},
                {kConfigEditorReloadCancel, "Config Editor: Cancel Reload", std::nullopt, false},
                {kConfigEditorSwitchDiscard, "Config Editor: Discard And Switch", std::nullopt, false},
                {kConfigEditorSwitchCancel, "Config Editor: Cancel Switch", std::nullopt, false},
                {kPreferencesCommit, "Preferences: Save", std::nullopt, false},
                {kPreferencesRebind, "Preferences: Rebind Shortcut", std::nullopt, false},
                {kPreferencesClear, "Preferences: Clear Shortcut", std::nullopt, false},
                {kPreferencesReset, "Preferences: Reset Shortcut", std::nullopt, false},
                {kPreferencesResetAll, "Preferences: Reset All Shortcuts", std::nullopt, false},
                {kPreferencesSwap, "Preferences: Swap Shortcut", std::nullopt, false},
                {kPreferencesCancelRebind, "Preferences: Cancel Rebind", std::nullopt, false},
                {kLayoutsMenuOpened, "Rescan Saved Layouts", std::nullopt, false},
                {kLayoutsLoad, "Load Saved Layout", std::nullopt, false},
                {kLayoutsDelete, "Delete Saved Layout", std::nullopt, false},
                {kLayoutsSaveDialogSave, "Save Layout Dialog: Save", std::nullopt, false},
                {kLayoutsSaveDialogEnter, "Save Layout Dialog: Enter", std::nullopt, false},
                {kLayoutsSaveDialogCancel, "Save Layout Dialog: Cancel", std::nullopt, false},
                {kSceneDialogsAddNode, "Add Node Dialog: OK", std::nullopt, false},
                {kSceneDialogsAddComponent, "Add Component Dialog: Add", std::nullopt, false},
                {kSceneDialogsCancel, "Scene Dialogs: Cancel", std::nullopt, false},
            };
            return table;
        }

    }  // namespace

    Span<const EditorCommand> GetEditorCommands() {
        const Vector<EditorCommand>& table = CommandTable();
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

    const EditorCommand* FindEditorCommand(StringView id) {
        for (const EditorCommand& command : CommandTable()) {
            if (command.id.ToStringView() == id) {
                return &command;
            }
        }
        return nullptr;
    }

}  // namespace BECore
