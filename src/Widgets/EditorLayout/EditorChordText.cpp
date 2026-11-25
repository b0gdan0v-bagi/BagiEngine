#include "EditorChordText.h"

#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/IEditorLayoutHost.h>
#include <Widgets/EditorLayout/KeyChord.h>

namespace BECore {

    EditorChordText::EditorChordText() {
        PanelChordText::Install(this);
    }

    EditorChordText::~EditorChordText() {
        PanelChordText::Uninstall(this);
    }

    StringView EditorChordText::ChordOf(StringView command) {
        IEditorLayoutHost* host = EditorLayoutHostRegistry::GetInstance().Get();
        const EditorSettings* settings = host != nullptr ? host->GetSettings() : nullptr;
        if (settings == nullptr) {
            return {};
        }
        const HotkeyMap& hotkeys = settings->GetHotkeys();
        if (_map != &hotkeys || _revision != hotkeys.Revision()) {
            _entries.clear();
            _map = &hotkeys;
            _revision = hotkeys.Revision();
        }
        for (const Entry& entry : _entries) {
            if (entry.command == command) {
                return entry.text;
            }
        }
        Entry& added = _entries.emplace_back();
        added.command = String(command);
        if (const EditorCommand* known = FindEditorCommand(command)) {
            if (const Optional<KeyChord> chord = hotkeys.GetChord(known->id)) {
                added.text = ToString(*chord);
            }
        }
        return added.text;
    }

}  // namespace BECore
