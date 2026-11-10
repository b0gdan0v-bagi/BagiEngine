#pragma once

#include <Events/EventBase.h>
#include <Widgets/EditorLayout/KeyChord.h>

namespace BECore {

    /**
     * @brief One command of the editor: what the command palette lists and what a hotkey runs.
     *
     * The table (GetEditorCommands) is static, so it does not depend on which panels are alive (they die and are
     * re-created at a mode switch). A panel acts on the commands it owns by subscribing to EditorCommandEvent.
     */
    struct EditorCommand {
        PoolString id;             // "area.action", the key in hotkeys.json and in EditorCommandEvent
        eastl::string_view title;  // shown in the palette and in the hotkey table
        std::optional<KeyChord> defaultChord;
        bool rebindable = true;  // false: the chord is fixed and not in the map's reach (mode.toggle)
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
        /// Switches Game and Editor. Application reads Ctrl+Shift+E from the raw SDL event (RunMode.h), before ImGui, so
        /// this command is fixed: the map lists it, the palette runs it through WidgetManager::RequestToggle().
        inline constexpr auto kModeToggle = "mode.toggle"_intern;
    }  // namespace EditorCommandIds

    /// A command was asked for, by the palette, a menu item or a hotkey. Emitted, not enqueued: it runs inside a frame.
    struct EditorCommandEvent : public EventBase<EditorCommandEvent> {
        BE_EVENT(EditorCommandEvent)
        BE_REFLECT_FIELD PoolString commandId;

        EditorCommandEvent() = default;
        explicit EditorCommandEvent(PoolString id) : commandId(id) {}
    };

}  // namespace BECore
