#pragma once

#include <BECore/UI/UIPreview.h>
#include <Events/EventBase.h>
#include <Widgets/EditorLayout/KeyChord.h>
#include <Widgets/Viewport/ViewportInput.h>

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
    [[nodiscard]] eastl::span<const EditorCommand> GetEditorCommands();

    /// The command with @p id, or nullptr
    [[nodiscard]] const EditorCommand* FindEditorCommand(PoolString id);

    /// Ids of the commands (a panel compares the id of an event with the ones it owns)
    namespace EditorCommandIds {
        inline constexpr auto kFileQuit = "file.quit"_intern;
        inline constexpr auto kViewCommandPalette = "view.commandPalette"_intern;
        inline constexpr auto kViewPreferences = "view.preferences"_intern;
        inline constexpr auto kViewResetLayout = "view.resetLayout"_intern;
        inline constexpr auto kViewSaveLayout = "view.saveLayout"_intern;
        inline constexpr auto kSceneSave = "scene.save"_intern;
        inline constexpr auto kConfigSave = "config.save"_intern;
        inline constexpr auto kTextureSave = "texture.save"_intern;
        /// Pauses or resumes the game through the engine's GamePause (the same state as the toolbar's Pause button and the control API)
        inline constexpr auto kGamePause = "game.pause"_intern;
        /// One stepped frame while the game is paused; repeats while its hotkey is held
        inline constexpr auto kGameStep = "game.step"_intern;
        /// Flips what a press in the viewport is for, Play (the game gets it) or Edit (the editor does). ViewportWidget owns the action.
        inline constexpr auto kViewportInputMode = "viewport.inputMode"_intern;
        /// The gizmo tool, the World / Local space of the gizmo, the snap switches and the camera frame of the viewport. ViewportWidget owns them. No default chord:
        /// a bare letter is refused as a hotkey, the keys W / E / R / X / F are read by the viewport itself while it has the keys
        inline constexpr auto kViewportToolTranslate = "viewport.tool.translate"_intern;
        inline constexpr auto kViewportToolRotate = "viewport.tool.rotate"_intern;
        inline constexpr auto kViewportToolScale = "viewport.tool.scale"_intern;
        inline constexpr auto kViewportGizmoSpace = "viewport.gizmoSpace"_intern;
        inline constexpr auto kViewportSnap = "viewport.snap"_intern;
        inline constexpr auto kViewportFrameSelected = "viewport.frameSelected"_intern;
        /// Switches Game and Editor. Application reads Ctrl+Shift+E from the raw SDL event (RunMode.h), before ImGui, so
        /// this command is fixed: the map lists it, the palette runs it through WidgetManager::RequestToggle().
        inline constexpr auto kModeToggle = "mode.toggle"_intern;
        /// Shows and hides the Profiler panel. Fixed like mode.toggle: ProfilerWidget reads Ctrl+Shift+P from the raw SDL event
        /// (Profiler/ProfilerToggle.h), the palette runs it through this command
        inline constexpr auto kViewProfiler = "view.profiler"_intern;
    }  // namespace EditorCommandIds

    /// A command was asked for, by the palette, a menu item or a hotkey. Emitted, not enqueued: it runs inside a frame.
    struct EditorCommandEvent : public EventBase<EditorCommandEvent> {
        BE_EVENT(EditorCommandEvent)
        BE_REFLECT_FIELD PoolString commandId;

        EditorCommandEvent() = default;
        explicit EditorCommandEvent(PoolString id) : commandId(id) {}
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
        BE_REFLECT_FIELD ViewportInputMode viewportInputMode = ViewportInputMode::Play;

        EditorPreferencesChangedEvent() = default;
        EditorPreferencesChangedEvent(bool grid, int32_t step, const PreviewSettings& preview, ViewportInputMode inputMode)
            : viewportGrid(grid), viewportGridStep(step), previewAspect(preview.shape.aspect), previewDevice(preview.shape.device), previewCustom(preview.shape.custom),
              previewWidth(preview.shape.customWidth), previewHeight(preview.shape.customHeight), previewRotated(preview.shape.rotated), previewSafeArea(preview.safeArea),
              overlaySafeArea(preview.overlays.safeArea), overlayViewBounds(preview.overlays.viewBounds), overlayCanvasBounds(preview.overlays.canvasBounds),
              overlayTouchTargets(preview.overlays.touchTargets), overlayRegions(preview.overlays.regions), viewportInputMode(inputMode) {}

        /// The viewport preview choice carried by the event
        [[nodiscard]] PreviewSettings GetPreview() const {
            return {{previewAspect, previewDevice, previewCustom, previewWidth, previewHeight, previewRotated},
                    previewSafeArea,
                    {overlaySafeArea, overlayViewBounds, overlayCanvasBounds, overlayTouchTargets, overlayRegions}};
        }
    };

    /// The viewport's preview choice (size, rotation, safe area, overlays) changed: sent by ViewportWidget only on a real change (never every
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

        ViewportPreviewChangedEvent() = default;
        explicit ViewportPreviewChangedEvent(const PreviewSettings& preview)
            : previewAspect(preview.shape.aspect), previewDevice(preview.shape.device), previewCustom(preview.shape.custom), previewWidth(preview.shape.customWidth),
              previewHeight(preview.shape.customHeight), previewRotated(preview.shape.rotated), previewSafeArea(preview.safeArea), overlaySafeArea(preview.overlays.safeArea),
              overlayViewBounds(preview.overlays.viewBounds), overlayCanvasBounds(preview.overlays.canvasBounds), overlayTouchTargets(preview.overlays.touchTargets),
              overlayRegions(preview.overlays.regions) {}

        [[nodiscard]] PreviewSettings GetPreview() const {
            return {{previewAspect, previewDevice, previewCustom, previewWidth, previewHeight, previewRotated},
                    previewSafeArea,
                    {overlaySafeArea, overlayViewBounds, overlayCanvasBounds, overlayTouchTargets, overlayRegions}};
        }
    };

    /// The viewport's input mode changed (the toolbar toggle or the command): sent by ViewportWidget only on a real change, not for what it
    /// just applied from EditorPreferencesChangedEvent. EditorLayoutWidget stores and saves it and does not answer with another
    /// EditorPreferencesChangedEvent.
    struct ViewportInputModeChangedEvent : public EventBase<ViewportInputModeChangedEvent> {
        BE_EVENT(ViewportInputModeChangedEvent)
        BE_REFLECT_FIELD ViewportInputMode mode = ViewportInputMode::Play;

        ViewportInputModeChangedEvent() = default;
        explicit ViewportInputModeChangedEvent(ViewportInputMode inputMode) : mode(inputMode) {}
    };

}  // namespace BECore
