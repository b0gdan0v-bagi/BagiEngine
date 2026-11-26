#include "PreferencesHotkeys.h"

#include <Generated/PreferencesHotkeys.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/HotkeyMap.h>

namespace BECore {

    namespace {

        constexpr StringView kNoChordText = "none";
        constexpr StringView kFixedSuffix = " (fixed)";

        String TitleOf(PoolString commandId) {
            const EditorCommand* command = FindEditorCommand(commandId);
            return command != nullptr ? String(command->title.data(), command->title.size()) : String(commandId.ToStringView());
        }

        const char* RefusalText(AssignRefusal refusal) {
            switch (refusal) {
                case AssignRefusal::UnknownCommand:
                    return "This command does not exist";
                case AssignRefusal::FixedCommand:
                    return "The shortcut of this command is fixed";
                case AssignRefusal::NeedsModifier:
                    return "Use Ctrl or Alt with a key, or an F key";
                case AssignRefusal::Reserved:
                    return "The editor keeps this shortcut for itself";
                case AssignRefusal::None:
                    break;
            }
            return "This shortcut cannot be used";
        }

        /// The command a button's argument names when it may be changed from the Hotkeys tab
        const EditorCommand* FindChangeable(StringView commandId) {
            const EditorCommand* command = FindEditorCommand(commandId);
            return command != nullptr && command->rebindable ? command : nullptr;
        }

    }  // namespace

    void PreferencesHotkeys::Rebuild(const HotkeyMap& hotkeys) {
        _rows.clear();
        for (const EditorCommand& command : GetEditorCommands()) {
            const Optional<KeyChord> chord = hotkeys.GetChord(command.id);
            HotkeyRow row;
            row.id = String(command.id.ToStringView());
            row.title = String(command.title.data(), command.title.size());
            row.chordText = chord ? ToString(*chord) : String(kNoChordText);
            row.chordStyle = chord ? PanelTextStyle::Normal : PanelTextStyle::Disabled;
            if (!command.rebindable) {
                row.chordText.append(kFixedSuffix.data(), kFixedSuffix.size());
            } else {
                row.canRebind = true;
                row.canClear = chord.has_value();
                row.canReset = !hotkeys.IsDefault(command.id);
            }
            _rows.push_back(std::move(row));
        }
    }

    void PreferencesHotkeys::Reset() {
        Close();
        _message.clear();
    }

    void PreferencesHotkeys::Close() {
        _stage = RebindStage::Closed;
        _rebindCommand = PoolString();
        _conflictCommand = PoolString();
        _title.clear();
        _prompt.clear();
        _refusal.clear();
        _conflictText.clear();
    }

    bool PreferencesHotkeys::StartRebind(StringView commandId) {
        const EditorCommand* command = FindChangeable(commandId);
        if (command == nullptr) {
            return false;
        }
        Close();
        _rebindCommand = command->id;
        _stage = RebindStage::Capturing;
        _title = TitleOf(command->id);
        _prompt = Format("Press the new shortcut for {}. Esc cancels.", _title);
        _message.clear();
        return true;
    }

    CaptureOutcome PreferencesHotkeys::Capture(EditorSettings& settings, const KeyChord& chord) {
        if (_stage != RebindStage::Capturing) {
            return CaptureOutcome::Cancelled;
        }
        if (chord.key == ImGuiKey_Escape && chord.mods == 0) {
            Close();
            return CaptureOutcome::Cancelled;
        }

        HotkeyMap& hotkeys = settings.GetHotkeys();
        const AssignResult result = hotkeys.Assign(_rebindCommand, chord);
        if (result.status == AssignStatus::Assigned) {
            _message.clear();
            Save(settings);
            Close();
            Rebuild(hotkeys);
            return CaptureOutcome::Assigned;
        }
        if (result.status == AssignStatus::Conflict) {
            _pendingChord = chord;
            _conflictCommand = result.otherCommandId;
            _conflictText = Format("{} is already used by {}. Swap takes it from there and gives it to {}.", ToString(chord), TitleOf(_conflictCommand), _title);
            _refusal.clear();
            _stage = RebindStage::Conflict;
            return CaptureOutcome::Conflict;
        }
        _refusal = RefusalText(result.refusal);
        return CaptureOutcome::Refused;
    }

    void PreferencesHotkeys::Swap(EditorSettings& settings) {
        if (_stage != RebindStage::Conflict) {
            return;
        }
        HotkeyMap& hotkeys = settings.GetHotkeys();
        hotkeys.AssignSwapping(_rebindCommand, _pendingChord);
        _message.clear();
        Save(settings);
        Close();
        Rebuild(hotkeys);
    }

    void PreferencesHotkeys::Cancel() {
        Close();
    }

    void PreferencesHotkeys::Clear(EditorSettings& settings, StringView commandId) {
        const EditorCommand* command = FindChangeable(commandId);
        HotkeyMap& hotkeys = settings.GetHotkeys();
        if (command == nullptr || !hotkeys.GetChord(command->id)) {
            return;
        }
        hotkeys.Unbind(command->id);
        _message.clear();
        Save(settings);
        Rebuild(hotkeys);
    }

    void PreferencesHotkeys::ResetCommand(EditorSettings& settings, StringView commandId) {
        const EditorCommand* command = FindChangeable(commandId);
        HotkeyMap& hotkeys = settings.GetHotkeys();
        if (command == nullptr || hotkeys.IsDefault(command->id)) {
            return;
        }
        _message.clear();
        const AssignResult result = hotkeys.ResetOne(command->id);
        if (result.status == AssignStatus::Conflict) {
            _message = Format("The default shortcut is used by {}", TitleOf(result.otherCommandId));
        } else {
            Save(settings);
        }
        Rebuild(hotkeys);
    }

    void PreferencesHotkeys::ResetAll(EditorSettings& settings) {
        settings.GetHotkeys().ResetAll();
        _message.clear();
        Save(settings);
        Rebuild(settings.GetHotkeys());
    }

    void PreferencesHotkeys::Save(EditorSettings& settings) {
        if (!settings.SaveHotkeys()) {
            _message = Format("Not saved: {}", settings.GetLastSaveError());
        }
    }

}  // namespace BECore
