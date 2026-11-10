#pragma once

#include <Widgets/EditorLayout/EditorCommands.h>

namespace BECore {

    CORE_ENUM(AssignStatus, uint8_t, Assigned, Conflict, NotAllowed)

    /// Why an assignment is refused (AssignStatus::NotAllowed); the preferences window shows it
    CORE_ENUM(AssignRefusal, uint8_t, None, UnknownCommand, FixedCommand, NeedsModifier, Reserved)

    struct AssignResult {
        AssignStatus status = AssignStatus::Assigned;
        PoolString otherCommandId;                    // Conflict: the command that holds the chord
        AssignRefusal refusal = AssignRefusal::None;  // NotAllowed: the reason
    };

    /// A chord that no command may take: Ctrl+Shift+E (Application reads it before ImGui), ImGui's text editing chords
    /// (Ctrl+A, C, V, X, Y, Z) and Ctrl+Tab (window switching)
    [[nodiscard]] bool IsReservedChord(const KeyChord& chord);

    /**
     * @brief The chord of every command of the editor: the defaults of the command table plus the user's changes.
     *
     * Pure data, no ImGui context. A chord belongs to one command at most (a command may have none). A refused
     * assignment changes nothing. The fixed command (mode.toggle) shows its chord in the map, so Find sees it and
     * nobody can take it, but it cannot be changed.
     */
    class HotkeyMap {
    public:
        HotkeyMap();

        /// The chord of @p commandId; empty when it is unbound or unknown
        [[nodiscard]] std::optional<KeyChord> GetChord(PoolString commandId) const;

        /// The command that holds @p chord, or an empty PoolString
        [[nodiscard]] PoolString Find(const KeyChord& chord) const;

        /// True when the chord of @p commandId is the one of the command table (an unknown command is not)
        [[nodiscard]] bool IsDefault(PoolString commandId) const;

        /// Gives @p chord to @p commandId. NotAllowed: an unknown or fixed command, a chord that needs Ctrl or Alt
        /// (IsAllowedAsHotkey), a reserved chord. Conflict: another command holds the chord.
        AssignResult Assign(PoolString commandId, const KeyChord& chord);

        /// Like Assign, but a conflict is settled by taking the chord from the other command, which becomes unbound
        AssignResult AssignSwapping(PoolString commandId, const KeyChord& chord);

        /// Leaves @p commandId without a chord. False for an unknown or fixed command.
        bool Unbind(PoolString commandId);

        /// Puts the default chord of @p commandId back (none when the table has none). Conflict when another command
        /// holds that chord now; NotAllowed for an unknown or fixed command.
        AssignResult ResetOne(PoolString commandId);

        /// Puts every default back
        void ResetAll();

    private:
        struct Binding {
            PoolString id;
            std::optional<KeyChord> chord;
        };

        Binding* FindBinding(PoolString commandId);
        const Binding* FindBinding(PoolString commandId) const;
        AssignResult Place(PoolString commandId, const KeyChord& chord, bool takeFromOther);

        eastl::vector<Binding> _bindings;
    };

}  // namespace BECore
