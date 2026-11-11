#include "HotkeyMap.h"

namespace BECore {

    namespace {

        constexpr KeyChord kReservedChords[] = {
            {ImGuiKey_E, kChordModCtrl | kChordModShift},
            {ImGuiKey_P, kChordModCtrl | kChordModShift},
            {ImGuiKey_A, kChordModCtrl},
            {ImGuiKey_C, kChordModCtrl},
            {ImGuiKey_V, kChordModCtrl},
            {ImGuiKey_X, kChordModCtrl},
            {ImGuiKey_Y, kChordModCtrl},
            {ImGuiKey_Z, kChordModCtrl},
            {ImGuiKey_Tab, kChordModCtrl},
        };

        AssignResult Refused(AssignRefusal refusal) {
            AssignResult result;
            result.status = AssignStatus::NotAllowed;
            result.refusal = refusal;
            return result;
        }

    }  // namespace

    bool IsReservedChord(const KeyChord& chord) {
        return std::ranges::find(kReservedChords, chord) != std::end(kReservedChords);
    }

    HotkeyMap::HotkeyMap() {
        for (const EditorCommand& command : GetEditorCommands()) {
            _bindings.push_back({command.id, command.defaultChord});
        }
    }

    HotkeyMap::Binding* HotkeyMap::FindBinding(PoolString commandId) {
        for (Binding& binding : _bindings) {
            if (binding.id == commandId) {
                return &binding;
            }
        }
        return nullptr;
    }

    const HotkeyMap::Binding* HotkeyMap::FindBinding(PoolString commandId) const {
        for (const Binding& binding : _bindings) {
            if (binding.id == commandId) {
                return &binding;
            }
        }
        return nullptr;
    }

    std::optional<KeyChord> HotkeyMap::GetChord(PoolString commandId) const {
        const Binding* binding = FindBinding(commandId);
        return binding != nullptr ? binding->chord : std::nullopt;
    }

    PoolString HotkeyMap::Find(const KeyChord& chord) const {
        for (const Binding& binding : _bindings) {
            if (binding.chord == chord) {
                return binding.id;
            }
        }
        return {};
    }

    bool HotkeyMap::IsDefault(PoolString commandId) const {
        const Binding* binding = FindBinding(commandId);
        const EditorCommand* command = FindEditorCommand(commandId);
        return binding != nullptr && command != nullptr && binding->chord == command->defaultChord;
    }

    AssignResult HotkeyMap::Place(PoolString commandId, const KeyChord& chord, bool takeFromOther) {
        Binding* binding = FindBinding(commandId);
        const EditorCommand* command = FindEditorCommand(commandId);
        if (binding == nullptr || command == nullptr) {
            return Refused(AssignRefusal::UnknownCommand);
        }
        if (!command->rebindable) {
            return Refused(AssignRefusal::FixedCommand);
        }
        if (!IsAllowedAsHotkey(chord)) {
            return Refused(AssignRefusal::NeedsModifier);
        }
        if (IsReservedChord(chord)) {
            return Refused(AssignRefusal::Reserved);
        }

        // A chord belongs to one command at most, so there is one holder to find, and it is not the command itself
        for (Binding& other : _bindings) {
            if (other.chord == chord && other.id != commandId) {
                if (!takeFromOther) {
                    AssignResult conflict;
                    conflict.status = AssignStatus::Conflict;
                    conflict.otherCommandId = other.id;
                    return conflict;
                }
                other.chord.reset();
            }
        }
        binding->chord = chord;
        return {};
    }

    AssignResult HotkeyMap::Assign(PoolString commandId, const KeyChord& chord) {
        return Place(commandId, chord, false);
    }

    AssignResult HotkeyMap::AssignSwapping(PoolString commandId, const KeyChord& chord) {
        return Place(commandId, chord, true);
    }

    bool HotkeyMap::Unbind(PoolString commandId) {
        Binding* binding = FindBinding(commandId);
        const EditorCommand* command = FindEditorCommand(commandId);
        if (binding == nullptr || command == nullptr || !command->rebindable) {
            return false;
        }
        binding->chord.reset();
        return true;
    }

    AssignResult HotkeyMap::ResetOne(PoolString commandId) {
        Binding* binding = FindBinding(commandId);
        const EditorCommand* command = FindEditorCommand(commandId);
        if (binding == nullptr || command == nullptr) {
            return Refused(AssignRefusal::UnknownCommand);
        }
        if (!command->rebindable) {
            return Refused(AssignRefusal::FixedCommand);
        }
        if (!command->defaultChord) {
            binding->chord.reset();
            return {};
        }
        return Place(commandId, *command->defaultChord, false);
    }

    void HotkeyMap::ResetAll() {
        for (Binding& binding : _bindings) {
            const EditorCommand* command = FindEditorCommand(binding.id);
            binding.chord = command != nullptr ? command->defaultChord : std::nullopt;
        }
    }

}  // namespace BECore
