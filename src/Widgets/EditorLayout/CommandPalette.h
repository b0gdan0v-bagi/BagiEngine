#pragma once

#include <BECore/PoolString/PoolString.h>
#include <Widgets/EditorLayout/CommandFilter.h>
#include <Widgets/EditorLayout/HotkeyMap.h>
#include <Widgets/Feedback/UIMotion.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /// Longest query the palette field takes
    inline constexpr size_t kMaxPaletteQueryLength = 64;

    /**
     * @brief The command palette: a centered popup with a text field and the commands that match it.
     *
     * Owned by EditorLayoutWidget, which calls Open() on view.commandPalette and Draw() once per frame inside its window.
     * It fades in as it opens. Up and Down move the selection, Enter or a click chooses, Esc or a click outside closes. Draw returns the chosen
     * command after the popup is closed; the caller emits EditorCommandEvent, so a command that opens a popup of its own
     * does not do it from inside this one.
     */
    class CommandPalette {
    public:
        void Open() {
            _openRequested = true;
            _matches.Invalidate();
        }

        /// Draws the popup over the editor's table; returns the id of the chosen command, or an empty PoolString
        PoolString Draw(const HotkeyMap& hotkeys) {
            return Draw(hotkeys, GetEditorCommands(), 0);
        }

        /// Draws the popup over @p commands. @p version has to move whenever they are another list, also in the same memory: the palette holds
        /// pointers into it and drops them then
        PoolString Draw(const HotkeyMap& hotkeys, Span<const EditorCommand> commands, uint64_t version);

    private:
        char _query[kMaxPaletteQueryLength + 1] = {};
        int _selected = 0;
        bool _openRequested = false;
        bool _focusQuery = false;
        Fade _fade;
        // The filtered rows and, parallel to them, the chord text of each; both change only with the query and the list
        CommandMatchList _matches;
        Vector<String> _chordTexts;
        uint64_t _commandsVersion = 0;
    };

}  // namespace BECore
