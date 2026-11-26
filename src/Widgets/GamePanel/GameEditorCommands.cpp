#include "GameEditorCommands.h"

#include <Games/GameEditorTools.h>

namespace BECore {

    namespace {

        String PanelViewCommand(StringView prefix, StringView panelId) {
            return Format("{}.view.{}", prefix, panelId);
        }

    }  // namespace

    IntrusivePtr<GameToolPanelEntry> FindPanelOfViewCommand(const GameEditorTools& tools, StringView id) {
        const String& prefix = tools.GetPrefix();
        for (const IntrusivePtr<GameToolPanelEntry>& entry : tools.GetPanels()) {
            const String view = PanelViewCommand(StringView(prefix.data(), prefix.size()), StringView(entry->panel.id.data(), entry->panel.id.size()));
            if (StringView(view.data(), view.size()) == id) {
                return entry;
            }
        }
        return {};
    }

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
        const auto add = [this, &ids](const String& id, String title) {
            const StringView text(id.data(), id.size());
            if (FindEditorCommand(text) != nullptr) {
                LOG_WARNING("[GameEditorCommands] '{}' is a command of the editor, so the palette does not list the game's", id);
                return;
            }
            ids.push_back(PoolString::Intern(text));
            _titles.push_back(std::move(title));
        };
        const StringView game = tools->GetGameName().ToStringView();
        for (const IntrusivePtr<GameToolCommandEntry>& entry : tools->GetCommands()) {
            add(entry->method, Format("{}: {}", game, entry->command.title));
        }
        const StringView prefix(tools->GetPrefix().data(), tools->GetPrefix().size());
        for (const IntrusivePtr<GameToolPanelEntry>& entry : tools->GetPanels()) {
            add(PanelViewCommand(prefix, StringView(entry->panel.id.data(), entry->panel.id.size())), Format("{}: Panel {}", game, entry->panel.title));
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
