#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/StaticPoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/UI/UIPreview.h>
#include <Events/EventBase.h>
#include <Widgets/EditorLayout/KeyChord.h>
#include <Widgets/Viewport/ViewportInput.h>
#include <cstdint>

namespace BECore {

    /**
     * @brief One command of the editor: what the command palette lists and what a hotkey runs.
     *
     * The table (GetEditorCommands) is static, so it does not depend on which panels are alive (they die and are
     * re-created at a mode switch). A panel acts on the commands it owns by subscribing to EditorCommandEvent.
     */
    struct EditorCommand {
        PoolString id;     // "area.action", the key in hotkeys.json and in EditorCommandEvent
        StringView title;  // shown in the palette and in the hotkey table
        Optional<KeyChord> defaultChord;
        bool rebindable = true;  // false: the chord is fixed and not in the map's reach (mode.toggle)
        bool repeats = false;    // the hotkey fires again while the chord is held (game.step)
    };

    /// Every command of the editor in the order the palette lists them when the query is empty
    [[nodiscard]] Span<const EditorCommand> GetEditorCommands();

    /// The command with @p id, or nullptr
    [[nodiscard]] const EditorCommand* FindEditorCommand(PoolString id);

    /// The command whose id reads @p id. Never interns: a panel asset names its commands in a file, and the validator asks with a name that may not exist
    [[nodiscard]] const EditorCommand* FindEditorCommand(StringView id);

    /// Ids of the commands (a panel compares the id of an event with the ones it owns)
    namespace EditorCommandIds {
        inline constexpr auto kFileQuit = "file.quit"_intern;
        inline constexpr auto kViewCommandPalette = "view.commandPalette"_intern;
        inline constexpr auto kViewPreferences = "view.preferences"_intern;
        inline constexpr auto kViewResetLayout = "view.resetLayout"_intern;
        inline constexpr auto kViewSaveLayout = "view.saveLayout"_intern;
        inline constexpr auto kSceneSave = "scene.save"_intern;
        /// The inspector's Add Component button: `SceneEditorWidget` asks for the Add Component dialog of the selected node (a locked one gets none)
        inline constexpr auto kSceneEditorAddComponent = "sceneEditor.addComponent"_intern;
        inline constexpr auto kConfigSave = "config.save"_intern;
        inline constexpr auto kTextureSave = "texture.save"_intern;
        /// Play / Stop of the main toolbar: `EditorPlayModel::PressPlay`, then the pause of the model is written to the engine (state only until `editor-play-in-editor`)
        inline constexpr auto kGamePlay = "game.play"_intern;
        /// Pauses or resumes the game through the engine's GamePause (the same state as the toolbar's Pause button and the control API)
        inline constexpr auto kGamePause = "game.pause"_intern;
        /// One stepped frame while the game is paused; repeats while its hotkey is held
        inline constexpr auto kGameStep = "game.step"_intern;
        /// Flips what a press in the viewport is for, Play (the game gets it) or Edit (the editor does). ViewportToolbarModel owns the action.
        inline constexpr auto kViewportInputMode = "viewport.inputMode"_intern;
        /// The gizmo tool, the World / Local space of the gizmo and the snap switches (ViewportToolbarModel owns them), and the camera frame of the viewport (ViewportWidget). No default chord:
        /// a bare letter is refused as a hotkey, the keys W / E / R / X / F are read by the viewport itself while it has the keys
        inline constexpr auto kViewportToolTranslate = "viewport.tool.translate"_intern;
        inline constexpr auto kViewportToolRotate = "viewport.tool.rotate"_intern;
        inline constexpr auto kViewportToolScale = "viewport.tool.scale"_intern;
        inline constexpr auto kViewportGizmoSpace = "viewport.gizmoSpace"_intern;
        inline constexpr auto kViewportSnap = "viewport.snap"_intern;
        inline constexpr auto kViewportFrameSelected = "viewport.frameSelected"_intern;
        /// The Reset button of the viewport's toolbar: the editor camera back to the whole image. ViewportWidget owns the camera, so it runs the command
        inline constexpr auto kViewportCameraReset = "viewport.cameraReset"_intern;
        /// Switches Game and Editor. Application reads Ctrl+Shift+E from the raw SDL event (RunMode.h), before ImGui, so
        /// this command is fixed: the map lists it, the palette runs it through WidgetManager::RequestToggle().
        inline constexpr auto kModeToggle = "mode.toggle"_intern;
        /// Shows and hides the Profiler panel. Fixed like mode.toggle: ProfilerPanelModel reads Ctrl+Shift+P from the raw SDL event
        /// (Profiler/ProfilerToggle.h) and sends this command, which the asset's `command` turns into the open flag
        inline constexpr auto kViewProfiler = "view.profiler"_intern;
        /// The Profiler panel's buttons (config/panels/ProfilerPanel.xml names them). ProfilerPanelModel owns them and runs them only while its panel is open;
        /// trace takes the literal argument record, restart, write or writeFrames and does nothing for any other text, the rest take none
        inline constexpr auto kProfilerTrace = "profiler.trace"_intern;
        inline constexpr auto kProfilerSpikeLive = "profiler.spikeLive"_intern;
        inline constexpr auto kProfilerSpikeClear = "profiler.spikeClear"_intern;
        inline constexpr auto kProfilerResetZoneAllocs = "profiler.resetZoneAllocs"_intern;
        /// Shows and hides the Console panel (the log). The asset's `command` (config/panels/ConsolePanel.xml) owns the action
        inline constexpr auto kViewConsole = "view.console"_intern;
        /// The Console panel's buttons (config/panels/ConsolePanel.xml names them). ConsolePanelModel owns them and runs them only while its panel is open; none takes an argument
        inline constexpr auto kConsolePause = "console.pause"_intern;
        inline constexpr auto kConsoleClear = "console.clear"_intern;
        inline constexpr auto kConsoleClearOutput = "console.clearOutput"_intern;
        /// Shows and hides the Game panel (the active game's state, actions, fields and methods): the `command` of config/panels/GamePanel.xml
        inline constexpr auto kViewGame = "view.game"_intern;
        /// Shows and hides the Generate panel (art generation): the `command` of config/panels/GeneratePanel.xml
        inline constexpr auto kViewGenerate = "view.generate"_intern;
        /// The Generate panel's buttons (config/panels/GeneratePanel.xml names them). ArtGenPanelModel owns them and runs them only while its panel is open.
        /// Each takes a literal argument that names the action (ArtGenPanelArgs in ArtGenPanelModel.h) and does nothing for any other text, except listen
        /// (play or stop the processed preview) and saveAsOpened (the open command of the Save-as dropdown, which carries none)
        inline constexpr auto kArtGenService = "artgen.service"_intern;
        inline constexpr auto kArtGenSetup = "artgen.setup"_intern;
        inline constexpr auto kArtGenPreset = "artgen.preset"_intern;
        inline constexpr auto kArtGenSaveAsOpened = "artgen.saveAsOpened"_intern;
        inline constexpr auto kArtGenReference = "artgen.reference"_intern;
        inline constexpr auto kArtGenRun = "artgen.run"_intern;
        inline constexpr auto kArtGenAccept = "artgen.accept"_intern;
        inline constexpr auto kArtGenListen = "artgen.listen"_intern;
        /// Show and hide the always-present panels; each panel owns its command. No default chord
        inline constexpr auto kViewSceneEditor = "view.sceneEditor"_intern;
        inline constexpr auto kViewConfigEditor = "view.configEditor"_intern;
        inline constexpr auto kViewFileChanges = "view.fileChanges"_intern;
        inline constexpr auto kViewSpriteLibrary = "view.spriteLibrary"_intern;
        inline constexpr auto kViewViewport = "view.viewport"_intern;
        /// Show and hide the Asset Browser (the `command` of its asset); hidden at start, no default chord
        inline constexpr auto kViewAssetBrowser = "view.assetBrowser"_intern;
        /// The File changes panel's buttons (config/panels/FileChangesPanel.xml names them). FileChangesPanelModel owns them and runs them only while its panel is open;
        /// fileChanges.apply takes the virtual path of a pending row as the argument and does nothing without one
        inline constexpr auto kFileChangesRescan = "fileChanges.rescan"_intern;
        inline constexpr auto kFileChangesApplyAll = "fileChanges.applyAll"_intern;
        inline constexpr auto kFileChangesApply = "fileChanges.apply"_intern;
        /// The Asset Browser's Rescan button: AssetBrowserPanelModel lists the content files again, only while its panel is open
        inline constexpr auto kAssetBrowserRescan = "assetBrowser.rescan"_intern;
        /// Delete and rename of the selected asset: the first two open the dialog that names its users, confirm does it, cancel closes (AssetBrowserPanelModel, only
        /// while its panel is open)
        inline constexpr auto kAssetBrowserDelete = "assetBrowser.delete"_intern;
        inline constexpr auto kAssetBrowserRename = "assetBrowser.rename"_intern;
        inline constexpr auto kAssetBrowserConfirm = "assetBrowser.confirm"_intern;
        inline constexpr auto kAssetBrowserCancel = "assetBrowser.cancel"_intern;
        /// Show and hide the Export window (the `command` of its asset); hidden at start, no default chord
        inline constexpr auto kViewExport = "view.export"_intern;
        /// The Export window's buttons. ExportPanelModel owns them and runs them only while its panel is open; none takes an argument. openFolder and run do
        /// nothing before an export succeeded; run of a web package serves it until stop; check lists the missing and unused assets
        inline constexpr auto kExportStart = "export.start"_intern;
        inline constexpr auto kExportCancel = "export.cancel"_intern;
        inline constexpr auto kExportOpenFolder = "export.openFolder"_intern;
        inline constexpr auto kExportRun = "export.run"_intern;
        inline constexpr auto kExportStop = "export.stop"_intern;
        inline constexpr auto kExportCheck = "export.check"_intern;
        /// Show and hide the Activity panel (the `command` of its asset); hidden at start, no default chord
        inline constexpr auto kViewActivity = "view.activity"_intern;
        /// The Activity panel's Clear button: ActivityPanelModel empties the control journal, only while its panel is open
        inline constexpr auto kActivityClear = "activity.clear"_intern;
        /// The Game panel's buttons (config/panels/GamePanel.xml names them). GamePanelAssetModel owns them and runs them only while its panel is open;
        /// perform and call take the index of a row of the actions or the methods as the argument, copyState and reset take none
        inline constexpr auto kGamePanelReset = "gamePanel.reset"_intern;
        inline constexpr auto kGamePanelPerform = "gamePanel.perform"_intern;
        inline constexpr auto kGamePanelCall = "gamePanel.call"_intern;
        inline constexpr auto kGamePanelCopyState = "gamePanel.copyState"_intern;
        /// The Config Editor's buttons and popups (config/panels/ConfigEditorPanel.xml names them). ConfigEditorPanelModel owns them and runs them only while its
        /// panel is open; select takes the name of a loaded config as the argument and does nothing for any other text, the rest take none
        inline constexpr auto kConfigEditorSelect = "configEditor.select"_intern;
        inline constexpr auto kConfigEditorReload = "configEditor.reload"_intern;
        inline constexpr auto kConfigEditorReloadDiscard = "configEditor.reloadDiscard"_intern;
        inline constexpr auto kConfigEditorReloadCancel = "configEditor.reloadCancel"_intern;
        inline constexpr auto kConfigEditorSwitchDiscard = "configEditor.switchDiscard"_intern;
        inline constexpr auto kConfigEditorSwitchCancel = "configEditor.switchCancel"_intern;
        /// The Sprite Library's buttons and fields (config/panels/SpriteLibraryPanel.xml names them). SpriteLibraryPanelModel owns them and runs them only while its
        /// panel is open; none takes an argument. rename is the name field's `commitCommand`, dropAccept and dropCancel the buttons of the "Add sprite" dialog,
        /// sliceFindIslands and sliceAdd the buttons of the Slice sheet section, atlasName the group field's `commitCommand`, pack and unpack the buttons of the
        /// Atlas section; `texture.save` is not one of them (the model takes it while closed too)
        inline constexpr auto kSpriteLibraryRename = "spriteLibrary.rename"_intern;
        inline constexpr auto kSpriteLibraryRescan = "spriteLibrary.rescan"_intern;
        inline constexpr auto kSpriteLibraryAdd = "spriteLibrary.add"_intern;
        inline constexpr auto kSpriteLibraryDelete = "spriteLibrary.delete"_intern;
        inline constexpr auto kSpriteLibraryDropAccept = "spriteLibrary.dropAccept"_intern;
        inline constexpr auto kSpriteLibraryDropCancel = "spriteLibrary.dropCancel"_intern;
        inline constexpr auto kSpriteLibrarySliceFindIslands = "spriteLibrary.sliceFindIslands"_intern;
        inline constexpr auto kSpriteLibrarySliceAdd = "spriteLibrary.sliceAdd"_intern;
        inline constexpr auto kSpriteLibraryAtlasName = "spriteLibrary.atlasName"_intern;
        inline constexpr auto kSpriteLibraryPack = "spriteLibrary.pack"_intern;
        inline constexpr auto kSpriteLibraryUnpack = "spriteLibrary.unpack"_intern;
        /// The Preferences window's "save now": the `commitCommand` of its controls (config/panels/PreferencesPanel.xml). PreferencesPanelModel owns it and runs it
        /// only while its panel is open; it takes no argument
        inline constexpr auto kPreferencesCommit = "preferences.commit"_intern;
        /// The Preferences window's Hotkeys tab (config/panels/PreferencesPanel.xml names them). PreferencesPanelModel owns them and runs them only while its panel
        /// is open; rebind, clear and reset take the id of a command of the table as the argument and ignore an unknown or fixed one, the rest take none.
        /// cancelRebind is also the `closeCommand` of the Rebind Shortcut popup
        inline constexpr auto kPreferencesRebind = "preferences.rebind"_intern;
        inline constexpr auto kPreferencesClear = "preferences.clear"_intern;
        inline constexpr auto kPreferencesReset = "preferences.reset"_intern;
        inline constexpr auto kPreferencesResetAll = "preferences.resetAll"_intern;
        inline constexpr auto kPreferencesSwap = "preferences.swap"_intern;
        inline constexpr auto kPreferencesCancelRebind = "preferences.cancelRebind"_intern;
        /// The Save Layout dialog (config/panels/EditorDialogsPanel.xml names them). EditorDialogsModel owns them and `view.saveLayout` opens it; none takes an argument.
        /// save is both the Save and the Overwrite button, enter the name field's `submitCommand` (saves a new name only), cancel also the popup's `closeCommand`
        inline constexpr auto kLayoutsSaveDialogSave = "layouts.saveDialog.save"_intern;
        inline constexpr auto kLayoutsSaveDialogEnter = "layouts.saveDialog.enter"_intern;
        inline constexpr auto kLayoutsSaveDialogCancel = "layouts.saveDialog.cancel"_intern;
        /// The Scene Editor's Add Node and Add Component dialogs (config/panels/SceneDialogsPanel.xml names them). SceneDialogsModel owns them. addNode is the OK button
        /// and the name field's `submitCommand`, addComponent takes the class name of a row as the argument and does nothing for an unknown one, cancel is both buttons
        /// and both popups' `closeCommand`
        inline constexpr auto kSceneDialogsAddNode = "sceneDialogs.addNode"_intern;
        inline constexpr auto kSceneDialogsAddComponent = "sceneDialogs.addComponent"_intern;
        inline constexpr auto kSceneDialogsCancel = "sceneDialogs.cancel"_intern;
        /// The Layouts and Delete Layout menus of the menu bar (config/panels/MainMenuPanel.xml names them). MainMenuModel owns them. menuOpened is the menus' `openCommand`
        /// (the folder is read again); load and delete take the name of a saved layout as the argument and do nothing for an empty or unknown one
        inline constexpr auto kLayoutsMenuOpened = "layouts.menuOpened"_intern;
        inline constexpr auto kLayoutsLoad = "layouts.load"_intern;
        inline constexpr auto kLayoutsDelete = "layouts.delete"_intern;
    }  // namespace EditorCommandIds

    /// A command was asked for, by the palette, a menu item or a hotkey. Emitted, not enqueued: it runs inside a frame.
    /// A button of an asset panel passes a value of its row as the argument (a String: it is read from a file, so no pool entry);
    /// the palette, a menu item and a hotkey leave it empty.
    struct EditorCommandEvent : public EventBase<EditorCommandEvent> {
        BE_EVENT(EditorCommandEvent)
        BE_REFLECT_FIELD PoolString commandId;
        BE_REFLECT_FIELD String argument;

        EditorCommandEvent() = default;
        explicit EditorCommandEvent(PoolString id) : commandId(id) {}
        EditorCommandEvent(PoolString id, String commandArgument) : commandId(id), argument(std::move(commandArgument)) {}
    };

    /// The preferences that other panels read changed (or the editor just started): sent by EditorLayoutWidget, which owns the
    /// settings, so a panel never reads the files itself. Emitted on the first frame of the widget, so a panel that subscribes in
    /// Initialize gets the stored values too.
    struct EditorPreferencesChangedEvent : public EventBase<EditorPreferencesChangedEvent> {
        BE_EVENT(EditorPreferencesChangedEvent)
        BE_REFLECT_FIELD bool viewportGrid = false;
        BE_REFLECT_FIELD int32_t viewportGridStep = 0;
        // The viewport preview choice (PreviewSettings, flat); the device is its index in UIPreview::GetDevices()
        BE_REFLECT_FIELD ViewportAspect previewAspect = ViewportAspect::Free;
        BE_REFLECT_FIELD int32_t previewDevice = -1;
        BE_REFLECT_FIELD bool previewCustom = false;
        BE_REFLECT_FIELD uint32_t previewWidth = PreviewShape{}.customWidth;
        BE_REFLECT_FIELD uint32_t previewHeight = PreviewShape{}.customHeight;
        BE_REFLECT_FIELD bool previewRotated = false;
        BE_REFLECT_FIELD SafeAreaSimulation previewSafeArea = SafeAreaSimulation::None;
        BE_REFLECT_FIELD bool overlaySafeArea = true;
        BE_REFLECT_FIELD bool overlayViewBounds = false;
        BE_REFLECT_FIELD bool overlayCanvasBounds = false;
        BE_REFLECT_FIELD bool overlayTouchTargets = false;
        BE_REFLECT_FIELD bool overlayRegions = false;
        BE_REFLECT_FIELD bool overlayElementRects = false;
        BE_REFLECT_FIELD bool overlayAnchors = false;
        BE_REFLECT_FIELD bool overlayDesignRect = false;
        BE_REFLECT_FIELD bool overlayFindings = false;
        BE_REFLECT_FIELD ViewportInputMode viewportInputMode = ViewportInputMode::Play;
        BE_REFLECT_FIELD String externalEditor;
        BE_REFLECT_FIELD Vector<String> consoleHistory;

        EditorPreferencesChangedEvent() = default;
        EditorPreferencesChangedEvent(bool grid, int32_t step, const PreviewSettings& preview, ViewportInputMode inputMode, String editor, Vector<String> history)
            : viewportGrid(grid), viewportGridStep(step), previewAspect(preview.shape.aspect), previewDevice(preview.shape.device), previewCustom(preview.shape.custom),
              previewWidth(preview.shape.customWidth), previewHeight(preview.shape.customHeight), previewRotated(preview.shape.rotated), previewSafeArea(preview.safeArea),
              overlaySafeArea(preview.overlays.safeArea), overlayViewBounds(preview.overlays.viewBounds), overlayCanvasBounds(preview.overlays.canvasBounds),
              overlayTouchTargets(preview.overlays.touchTargets), overlayRegions(preview.overlays.regions), overlayElementRects(preview.overlays.elementRects),
              overlayAnchors(preview.overlays.anchors), overlayDesignRect(preview.overlays.designRect), overlayFindings(preview.overlays.findings), viewportInputMode(inputMode),
              externalEditor(std::move(editor)), consoleHistory(std::move(history)) {}

        /// The viewport preview choice carried by the event
        [[nodiscard]] PreviewSettings GetPreview() const {
            return {{previewAspect, previewDevice, previewCustom, previewWidth, previewHeight, previewRotated},
                    previewSafeArea,
                    {overlaySafeArea, overlayViewBounds, overlayCanvasBounds, overlayTouchTargets, overlayRegions, overlayElementRects, overlayAnchors, overlayDesignRect, overlayFindings}};
        }
    };

    /// The viewport's preview choice (size, rotation, safe area, overlays) changed: sent by ViewportToolbarModel only on a real change (never every
    /// frame, and not for what it just applied from EditorPreferencesChangedEvent). EditorLayoutWidget stores and saves it and does not
    /// answer with another EditorPreferencesChangedEvent.
    struct ViewportPreviewChangedEvent : public EventBase<ViewportPreviewChangedEvent> {
        BE_EVENT(ViewportPreviewChangedEvent)
        BE_REFLECT_FIELD ViewportAspect previewAspect = ViewportAspect::Free;
        BE_REFLECT_FIELD int32_t previewDevice = -1;
        BE_REFLECT_FIELD bool previewCustom = false;
        BE_REFLECT_FIELD uint32_t previewWidth = PreviewShape{}.customWidth;
        BE_REFLECT_FIELD uint32_t previewHeight = PreviewShape{}.customHeight;
        BE_REFLECT_FIELD bool previewRotated = false;
        BE_REFLECT_FIELD SafeAreaSimulation previewSafeArea = SafeAreaSimulation::None;
        BE_REFLECT_FIELD bool overlaySafeArea = true;
        BE_REFLECT_FIELD bool overlayViewBounds = false;
        BE_REFLECT_FIELD bool overlayCanvasBounds = false;
        BE_REFLECT_FIELD bool overlayTouchTargets = false;
        BE_REFLECT_FIELD bool overlayRegions = false;
        BE_REFLECT_FIELD bool overlayElementRects = false;
        BE_REFLECT_FIELD bool overlayAnchors = false;
        BE_REFLECT_FIELD bool overlayDesignRect = false;
        BE_REFLECT_FIELD bool overlayFindings = false;

        ViewportPreviewChangedEvent() = default;
        explicit ViewportPreviewChangedEvent(const PreviewSettings& preview)
            : previewAspect(preview.shape.aspect), previewDevice(preview.shape.device), previewCustom(preview.shape.custom), previewWidth(preview.shape.customWidth),
              previewHeight(preview.shape.customHeight), previewRotated(preview.shape.rotated), previewSafeArea(preview.safeArea), overlaySafeArea(preview.overlays.safeArea),
              overlayViewBounds(preview.overlays.viewBounds), overlayCanvasBounds(preview.overlays.canvasBounds), overlayTouchTargets(preview.overlays.touchTargets),
              overlayRegions(preview.overlays.regions), overlayElementRects(preview.overlays.elementRects), overlayAnchors(preview.overlays.anchors), overlayDesignRect(preview.overlays.designRect),
              overlayFindings(preview.overlays.findings) {}

        [[nodiscard]] PreviewSettings GetPreview() const {
            return {{previewAspect, previewDevice, previewCustom, previewWidth, previewHeight, previewRotated},
                    previewSafeArea,
                    {overlaySafeArea, overlayViewBounds, overlayCanvasBounds, overlayTouchTargets, overlayRegions, overlayElementRects, overlayAnchors, overlayDesignRect, overlayFindings}};
        }
    };

    /// The viewport's input mode changed (the toolbar toggle or the command): sent by ViewportToolbarModel only on a real change, not for what it
    /// just applied from EditorPreferencesChangedEvent. EditorLayoutWidget stores and saves it and does not answer with another
    /// EditorPreferencesChangedEvent.
    struct ViewportInputModeChangedEvent : public EventBase<ViewportInputModeChangedEvent> {
        BE_EVENT(ViewportInputModeChangedEvent)
        BE_REFLECT_FIELD ViewportInputMode mode = ViewportInputMode::Play;

        ViewportInputModeChangedEvent() = default;
        explicit ViewportInputModeChangedEvent(ViewportInputMode inputMode) : mode(inputMode) {}
    };

    /// The Console ran a command line that parsed: sent by the Console's command line slot. EditorLayoutWidget keeps it in the saved history and does not
    /// answer with an EditorPreferencesChangedEvent (the Console already holds its own history).
    struct ConsoleCommandSubmittedEvent : public EventBase<ConsoleCommandSubmittedEvent> {
        BE_EVENT(ConsoleCommandSubmittedEvent)
        BE_REFLECT_FIELD String command;

        ConsoleCommandSubmittedEvent() = default;
        explicit ConsoleCommandSubmittedEvent(String text) : command(std::move(text)) {}
    };

}  // namespace BECore
