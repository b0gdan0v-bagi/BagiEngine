#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Widgets/PanelAssets/PanelElements.h>
#include <Widgets/EditorLayout/EditorSettings.h>
#include <Widgets/EditorLayout/KeyChord.h>
#include <cstdint>

namespace BECore {

    /// The popup that rebinds one command: reading the next chord, or asking what to do about a conflict
    CORE_ENUM(RebindStage, uint8_t, Closed, Capturing, Conflict)

    /// What a captured chord did to the dialog
    CORE_ENUM(CaptureOutcome, uint8_t, Cancelled, Assigned, Conflict, Refused)

    /// A row of the Hotkeys table: one command of the table, as text and enabled flags
    struct HotkeyRow {
        BE_CLASS(HotkeyRow)

        BE_REFLECT_FIELD String id;
        BE_REFLECT_FIELD String title;
        /// The chord, `none` when unbound; a fixed command adds ` (fixed)`
        BE_REFLECT_FIELD String chordText;
        BE_REFLECT_FIELD PanelTextStyle chordStyle = PanelTextStyle::Normal;
        BE_REFLECT_FIELD bool canRebind = false;
        BE_REFLECT_FIELD bool canClear = false;
        BE_REFLECT_FIELD bool canReset = false;
    };

    /**
     * @brief The state of the Hotkeys tab: the rows, the rebind dialog's stage and texts, and the changes to the `HotkeyMap`, with no ImGui
     *
     * Owned by `PreferencesPanelModel`. Every change goes through `HotkeyMap` (`Assign`, `AssignSwapping`, `Unbind`, `ResetOne`, `ResetAll`), which stays the
     * only place of the rules. A command id comes from a button's argument, so it is looked up as text and an unknown or fixed one is ignored. The rows
     * and the texts are made again only when something changed (`Rebuild` and each change), never per frame. A change is saved at once; a save that fails
     * leaves `Not saved: <error>` as the message.
     */
    class PreferencesHotkeys {
    public:
        /// Makes the rows from @p hotkeys: one per command of the table that the editor's host offers
        void Rebuild(const HotkeyMap& hotkeys);

        /// Forgets the dialog and the message, as when the panel opens
        void Reset();

        [[nodiscard]] const Vector<HotkeyRow>& GetRows() const {
            return _rows;
        }
        [[nodiscard]] RebindStage GetStage() const {
            return _stage;
        }
        /// The title of the command being rebound
        [[nodiscard]] const String& GetTitle() const {
            return _title;
        }
        /// What the Capturing stage asks of the user
        [[nodiscard]] const String& GetPrompt() const {
            return _prompt;
        }
        /// Why the last chord was refused (Capturing); empty when it was not
        [[nodiscard]] const String& GetRefusal() const {
            return _refusal;
        }
        /// The Conflict stage: which command holds the chord and what Swap does
        [[nodiscard]] const String& GetConflictText() const {
            return _conflictText;
        }
        /// The last failed save, or why a Reset did nothing; empty when none
        [[nodiscard]] const String& GetMessage() const {
            return _message;
        }

        /// Opens the dialog for @p commandId. False (nothing changes) for an unknown or fixed command
        bool StartRebind(StringView commandId);

        /// Takes @p chord in the Capturing stage: Esc with no modifier cancels; a chord that `Assign` takes is saved and closes the dialog; a conflict goes to
        /// the Conflict stage; a refusal shows its reason. Ignored (Cancelled) in another stage
        CaptureOutcome Capture(EditorSettings& settings, const KeyChord& chord);

        /// The Conflict stage's Swap: takes the chord from the other command, saves and closes. Ignored in another stage
        void Swap(EditorSettings& settings);

        /// Closes the dialog without a change
        void Cancel();

        /// The buttons of a row. Each ignores an unknown or fixed command and one the button would have been disabled for
        void Clear(EditorSettings& settings, StringView commandId);
        void ResetCommand(EditorSettings& settings, StringView commandId);
        void ResetAll(EditorSettings& settings);

    private:
        void Save(EditorSettings& settings);
        void Close();

        Vector<HotkeyRow> _rows;
        RebindStage _stage = RebindStage::Closed;
        PoolString _rebindCommand;    // the command being rebound
        KeyChord _pendingChord;       // Conflict: the chord that another command holds
        PoolString _conflictCommand;  // Conflict: that command
        String _title;
        String _prompt;
        String _refusal;
        String _conflictText;
        String _message;
    };

}  // namespace BECore
