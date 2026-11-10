#pragma once

#include <Widgets/EditorLayout/HotkeyMap.h>

namespace BECore {

    /// Longest query the palette field takes
    inline constexpr size_t kMaxPaletteQueryLength = 64;

    /**
     * @brief The command palette: a centered popup with a text field and the commands that match it.
     *
     * Owned by EditorLayoutWidget, which calls Open() on view.commandPalette and Draw() once per frame inside its window.
     * Up and Down move the selection, Enter or a click chooses, Esc or a click outside closes. Draw returns the chosen
     * command after the popup is closed; the caller emits EditorCommandEvent, so a command that opens a popup of its own
     * does not do it from inside this one.
     */
    class CommandPalette {
    public:
        void Open() {
            _openRequested = true;
        }

        /// Draws the popup; returns the id of the chosen command, or an empty PoolString
        PoolString Draw(const HotkeyMap& hotkeys);

    private:
        char _query[kMaxPaletteQueryLength + 1] = {};
        int _selected = 0;
        bool _openRequested = false;
        bool _focusQuery = false;
    };

}  // namespace BECore
