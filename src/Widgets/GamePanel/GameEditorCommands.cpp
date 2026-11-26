#include "GameEditorCommands.h"

#include <Games/GameEditorTools.h>

namespace BECore {

    void GameEditorCommands::Sync(const GameEditorTools* tools) {
        const uint64_t toolsVersion = tools != nullptr ? tools->GetVersion() : 0;
        if (tools == _tools && toolsVersion == _toolsVersion) {
            return;
        }
        _tools = tools;
        _toolsVersion = toolsVersion;
        ++_version;
        _titles.clear();
        _rows.clear();
        if (tools == nullptr) {
            return;
        }

        Vector<PoolString> ids;
        for (const IntrusivePtr<GameToolCommandEntry>& entry : tools->GetCommands()) {
            const StringView method(entry->method.data(), entry->method.size());
            if (FindEditorCommand(method) != nullptr) {
                LOG_WARNING("[GameEditorCommands] '{}' is a command of the editor, so the palette does not list the game's", entry->method);
                continue;
            }
            ids.push_back(PoolString::Intern(method));
            _titles.push_back(Format("{}: {}", tools->GetGameName().ToStringView(), entry->command.title));
        }
        if (ids.empty()) {
            return;
        }

        const Span<const EditorCommand> table = GetEditorCommands();
        _rows.reserve(table.size() + ids.size());
        _rows.insert(_rows.end(), table.begin(), table.end());
        // Only now: a row views its title, and a growing _titles would move the text
        for (size_t i = 0; i < ids.size(); ++i) {
            _rows.push_back(EditorCommand{ids[i], StringView(_titles[i].data(), _titles[i].size()), std::nullopt, false, false});
        }
    }

    Span<const EditorCommand> GameEditorCommands::GetCommands() const {
        if (_rows.empty()) {
            return GetEditorCommands();
        }
        return Span<const EditorCommand>(_rows.data(), _rows.size());
    }

}  // namespace BECore
