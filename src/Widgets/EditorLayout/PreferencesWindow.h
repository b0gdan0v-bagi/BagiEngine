#pragma once

#include <Widgets/EditorLayout/EditorSettings.h>

namespace BECore {

    /// The popup that rebinds one command: reading the next chord, or asking what to do about a conflict
    CORE_ENUM(RebindStage, uint8_t, Closed, Capturing, Conflict)

    /**
     * @brief The Preferences window: the General tab (font scale, viewport grid) and the Hotkeys tab (rebinding).
     *
     * Owned by EditorLayoutWidget, which calls Open() on view.preferences and Draw() once per frame. A change is applied to
     * EditorSettings at once and saved at once (a slider is saved when the user lets go of it); a failed save is shown as a line
     * in the window. Draw returns true when the preferences changed, so the caller applies the font scale and tells the panels.
     * Rebinding runs in a modal popup that reads the next chord; while it is open the hotkey dispatch is off (any popup stops it).
     */
    class PreferencesWindow {
    public:
        void Open() {
            _isOpen = true;
            _focusRequested = true;
        }

        /// Draws the window when it is open; true when the preferences were changed this frame
        [[nodiscard]] bool Draw(EditorSettings& settings);

    private:
        bool DrawGeneralTab(EditorSettings& settings);
        void DrawHotkeysTab(EditorSettings& settings);
        void DrawRebindPopup(EditorSettings& settings);
        void StartRebind(PoolString commandId);
        void SaveHotkeys(EditorSettings& settings);

        bool _isOpen = false;
        bool _focusRequested = false;

        RebindStage _stage = RebindStage::Closed;
        bool _openRebindPopup = false;
        PoolString _rebindCommand;    // the command being rebound
        KeyChord _pendingChord;       // Conflict: the chord that another command holds
        PoolString _conflictCommand;  // Conflict: that command
        eastl::string _refusal;       // Capturing: why the last chord was refused

        eastl::string _preferencesError;  // the last failed save of each file, empty when it worked
        eastl::string _hotkeysMessage;    // the last failed save or reset of the hotkeys
    };

}  // namespace BECore
